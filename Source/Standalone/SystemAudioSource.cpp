#include "SystemAudioSource.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <functiondiscoverykeys_devpkey.h>
 #include <ksmedia.h>
 #include <wrl/client.h>

namespace
{
using Microsoft::WRL::ComPtr;
struct ComScope
{
    HRESULT result = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED (result)) CoUninitialize(); }
};

float readSample (const BYTE* data, int bits, bool floating) noexcept
{
    if (floating)
    {
        float value;
        std::memcpy (&value, data, sizeof (value));
        return std::isfinite (value) ? value : 0.0f;
    }
    if (bits == 16)
    {
        juce::int16 value;
        std::memcpy (&value, data, sizeof (value));
        return static_cast<float> (value) / 32768.0f;
    }
    if (bits == 24)
    {
        auto value = static_cast<juce::int32> (data[0] | (data[1] << 8) | (data[2] << 16));
        if ((value & 0x800000) != 0) value -= 0x1000000;
        return static_cast<float> (value) / 8388608.0f;
    }
    juce::int32 value;
    std::memcpy (&value, data, sizeof (value));
    return static_cast<float> (static_cast<double> (value) / 2147483648.0);
}
}
#endif

SystemAudioSource::SystemAudioSource() : Thread ("System audio capture") {}
SystemAudioSource::~SystemAudioSource() { stopCapture(); }

std::vector<SystemAudioSource::Endpoint> SystemAudioSource::getEndpoints()
{
    std::vector<Endpoint> result;
   #if JUCE_WINDOWS
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&enumerator))))
        return result;
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED (enumerator->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &collection)))
        return result;
    UINT count = 0;
    collection->GetCount (&count);
    for (UINT i = 0; i < count; ++i)
    {
        ComPtr<IMMDevice> device;
        ComPtr<IPropertyStore> properties;
        LPWSTR id = nullptr;
        if (FAILED (collection->Item (i, &device)) || FAILED (device->GetId (&id)))
            continue;
        const juce::String deviceId (id);
        CoTaskMemFree (id);
        PROPVARIANT name;
        PropVariantInit (&name);
        if (SUCCEEDED (device->OpenPropertyStore (STGM_READ, &properties))
            && SUCCEEDED (properties->GetValue (PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR)
            result.push_back ({ deviceId, juce::String (name.pwszVal) });
        PropVariantClear (&name);
    }
   #endif
    return result;
}

juce::Result SystemAudioSource::startCapture (const juce::String& endpointId)
{
    stopCapture();
    endpoint = endpointId;
    setError ({});
    started.reset();
    ring.fifo.reset();
    ring.dropouts.store (0);
    primed = false;
    if (! startThread())
        return juce::Result::fail ("Could not start the capture worker.");
    if (! started.wait (5000))
    {
        stopCapture();
        return juce::Result::fail ("Timed out opening the selected system audio source.");
    }
    const auto failure = getError();
    return failure.isEmpty() ? juce::Result::ok() : juce::Result::fail (failure);
}

void SystemAudioSource::stopCapture()
{
    signalThreadShouldExit();
    waitForThreadToExit (-1);
}

void SystemAudioSource::setError (const juce::String& message)
{
    const juce::ScopedLock lock (errorLock);
    error = message;
}

juce::String SystemAudioSource::getError() const
{
    const juce::ScopedLock lock (errorLock);
    return error;
}

void SystemAudioSource::run()
{
   #if JUCE_WINDOWS
    ComScope com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    auto fail = [this] (const juce::String& message) { setError (message); started.signal(); };
    if (FAILED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS (&enumerator)))
        || FAILED (enumerator->GetDevice (endpoint.toWideCharPointer(), &device))
        || FAILED (device->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**> (client.GetAddressOf()))))
    {
        fail ("The selected playback source is unavailable. Refresh devices and select it again.");
        return;
    }
    WAVEFORMATEX* rawFormat = nullptr;
    if (FAILED (client->GetMixFormat (&rawFormat))) { fail ("Cannot read the source audio format."); return; }
    const std::unique_ptr<WAVEFORMATEX, decltype (&CoTaskMemFree)> format (rawFormat, &CoTaskMemFree);
    bool floating = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool pcm = format->wFormatTag == WAVE_FORMAT_PCM;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22)
    {
        const auto& extended = *reinterpret_cast<WAVEFORMATEXTENSIBLE*> (format.get());
        floating = extended.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        pcm = extended.SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
    }
    if (format->nChannels < 1 || format->nChannels > 2)
    {
        fail ("Set the source playback device to mono or stereo in Windows Sound settings.");
        return;
    }
    if (! ((floating && format->wBitsPerSample == 32)
        || (pcm && (format->wBitsPerSample == 16 || format->wBitsPerSample == 24 || format->wBitsPerSample == 32))))
    {
        fail ("The source format is unsupported. Choose 16-, 24- or 32-bit PCM in Windows Sound settings.");
        return;
    }
    struct EventHandle { HANDLE handle = CreateEventW (nullptr, FALSE, FALSE, nullptr); ~EventHandle() { if (handle) CloseHandle (handle); } } event;
    if (event.handle == nullptr
        || FAILED (client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                      0, 0, format.get(), nullptr))
        || FAILED (client->SetEventHandle (event.handle))
        || FAILED (client->GetService (IID_PPV_ARGS (&capture)))
        || FAILED (client->Start()))
    {
        fail ("Could not start Windows loopback capture. Check that the source is available in shared mode.");
        return;
    }
    captureRate.store (format->nSamplesPerSec);
    started.signal();
    while (! threadShouldExit())
    {
        WaitForSingleObject (event.handle, 30);
        UINT32 packetSize = 0;
        if (FAILED (capture->GetNextPacketSize (&packetSize)))
        {
            setError ("System audio source disconnected. Stop, refresh devices, and start again.");
            break;
        }
        while (packetSize > 0 && ! threadShouldExit())
        {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED (capture->GetBuffer (&data, &frames, &flags, nullptr, nullptr)))
            {
                setError ("Windows could not read the system audio source.");
                client->Stop();
                return;
            }
            {
                const auto write = ring.fifo.write (static_cast<int> (frames));
                const auto total = write.blockSize1 + write.blockSize2;
                if (total < static_cast<int> (frames)) ++ring.dropouts;
                for (int i = 0; i < total; ++i)
                {
                    const auto index = i < write.blockSize1 ? write.startIndex1 + i : write.startIndex2 + i - write.blockSize1;
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        const auto value = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 ? 0.0f
                            : readSample (data + i * format->nBlockAlign + juce::jmin (channel, static_cast<int> (format->nChannels) - 1)
                                            * (format->wBitsPerSample / 8), format->wBitsPerSample, floating);
                        ring.samples.setSample (channel, index, value);
                    }
                }
            }
            capture->ReleaseBuffer (frames);
            if (FAILED (capture->GetNextPacketSize (&packetSize))) break;
        }
    }
    client->Stop();
   #else
    setError ("System output capture is currently available on Windows only.");
    started.signal();
   #endif
}

void SystemAudioSource::Ring::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    const auto read = fifo.read (info.numSamples);
    if (read.blockSize1 + read.blockSize2 < info.numSamples) ++dropouts;
    for (int channel = 0; channel < juce::jmin (2, info.buffer->getNumChannels()); ++channel)
    {
        info.buffer->copyFrom (channel, info.startSample, samples, channel, read.startIndex1, read.blockSize1);
        info.buffer->copyFrom (channel, info.startSample + read.blockSize1, samples, channel, read.startIndex2, read.blockSize2);
    }
}

void SystemAudioSource::prepareToPlay (int blockSize, double sampleRate)
{
    outputRate = sampleRate;
    targetFill = juce::jmax (static_cast<int> (captureRate.load() * 0.03),
                           static_cast<int> (2.0 * blockSize * captureRate.load() / sampleRate));
    resampler.setResamplingRatio (captureRate.load() / outputRate);
    // Reserve the maximum drift-correction ratio before the callback starts.
    resampler.prepareToPlay (blockSize + juce::jmax (32, blockSize / 100), sampleRate);
    primed = false;
}

void SystemAudioSource::releaseResources() { resampler.releaseResources(); }

void SystemAudioSource::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    const auto ready = ring.fifo.getNumReady();
    if (! primed && ready < targetFill) { info.clearActiveBufferRegion(); return; }
    primed = true;
    if (ready > targetFill * 4)
    {
        const auto discarded = ring.fifo.read (ready - targetFill);
        juce::ignoreUnused (discarded);
        resampler.flushBuffers();
        ++ring.dropouts;
    }
    // Small adaptive corrections keep separate device clocks from accumulating delay.
    const auto correction = juce::jlimit (-0.003, 0.003, 0.001 * (ready - targetFill) / targetFill);
    resampler.setResamplingRatio (captureRate.load() / outputRate * (1.0 + correction));
    resampler.getNextAudioBlock (info);
    if (ready == 0) primed = false;
}
