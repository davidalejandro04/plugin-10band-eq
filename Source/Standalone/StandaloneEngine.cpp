#include "StandaloneEngine.h"

StandaloneEngine::StandaloneEngine (TenBandEQAudioProcessor& p) : processor (p)
{
    formats.registerBasicFormats();
    readAhead.startThread();
}

StandaloneEngine::~StandaloneEngine()
{
    detach();
    system.stopCapture();
    transport.setSource (nullptr);
    readerSource.reset();
    readAhead.stopThread (-1);
    deviceManager.closeAudioDevice();
}

juce::Result StandaloneEngine::initialise()
{
    // No microphone is opened. The standalone defaults to quiet file playback.
    const auto error = deviceManager.initialise (0, 2, nullptr, true);
    if (error.isNotEmpty()) return juce::Result::fail (error);
   #if JUCE_WINDOWS
    deviceManager.setCurrentAudioDeviceType ("Windows Audio", true);
   #endif
    attach();
    return juce::Result::ok();
}

void StandaloneEngine::attach()
{
    if (! attached) { deviceManager.addAudioCallback (this); attached = true; }
}

void StandaloneEngine::detach()
{
    if (attached) { deviceManager.removeAudioCallback (this); attached = false; }
}

juce::StringArray StandaloneEngine::outputNames()
{
    if (auto* type = deviceManager.getCurrentDeviceTypeObject())
    {
        type->scanForDevices();
        return type->getDeviceNames (false);
    }
    return {};
}

juce::Result StandaloneEngine::selectOutput (const juce::String& name)
{
    stop();
    detach();
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.outputDeviceName = name;
    setup.inputDeviceName.clear();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = false;
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 2, true);
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    attach();
    return error.isEmpty() ? juce::Result::ok() : juce::Result::fail (error);
}

juce::Result StandaloneEngine::loadFile (const juce::File& input)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (input));
    if (reader == nullptr) return juce::Result::fail ("Cannot open this file. Choose MP3, WAV, AIFF, FLAC or Ogg audio.");
    if (reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples <= 0)
        return juce::Result::fail ("Choose a non-empty mono or stereo audio file.");
    stop();
    detach();
    transport.setSource (nullptr);
    const auto rate = reader->sampleRate;
    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader.release(), true);
    transport.setSource (readerSource.get(), 32768, &readAhead, rate, 2);
    file = input;
    attach();
    return juce::Result::ok();
}

juce::Result StandaloneEngine::startSystem (const SystemAudioSource::Endpoint& source)
{
    stop();
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return juce::Result::fail ("Choose an available output device first.");
    // Resolve the selected output to its Windows endpoint ID. Reject ambiguous names
    // and self-capture so the app cannot recursively equalize its own speaker output.
    const auto endpoints = SystemAudioSource::getEndpoints();
    juce::String outputId;
    int matches = 0;
    for (const auto& candidate : endpoints)
        if (candidate.name == currentOutput()) { outputId = candidate.id; ++matches; }
    if (matches != 1)
        return juce::Result::fail ("Cannot uniquely identify the output endpoint. Use Windows Audio and give duplicate devices unique names.");
    if (outputId == source.id)
        return juce::Result::fail ("Source and output must be different devices. Route Windows audio to a virtual playback device and select your speakers as output.");
    detach();
    systemOutputName = currentOutput();
    routeInvalid.store (false);
    const auto result = system.startCapture (source.id);
    systemRunning = result.wasOk();
    attach();
    return result;
}

void StandaloneEngine::stop()
{
    transport.stop();
    if (systemRunning)
    {
        detach();
        systemRunning = false;
        routeInvalid.store (false);
        system.stopCapture();
        attach();
    }
}

void StandaloneEngine::play()
{
    if (file == juce::File() || deviceManager.getCurrentAudioDevice() == nullptr) return;
    stop();
    if (transport.getCurrentPosition() >= transport.getLengthInSeconds()) transport.setPosition (0.0);
    transport.start();
}
void StandaloneEngine::pause() { transport.stop(); }
void StandaloneEngine::seek (double seconds) { transport.setPosition (juce::jlimit (0.0, duration(), seconds)); }

void StandaloneEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    routeInvalid.store (systemRunning && device->getName() != systemOutputName);
    const auto blockSize = juce::jmax (512, device->getCurrentBufferSizeSamples());
    const auto rate = device->getCurrentSampleRate();
    scratch.setSize (2, blockSize);
    transportWasPlaying = false;
    processor.prepareToPlay (rate, blockSize);
    transport.prepareToPlay (blockSize, rate);
    system.prepareToPlay (blockSize, rate);
}

void StandaloneEngine::audioDeviceStopped()
{
    if (systemRunning) routeInvalid.store (true);
    transport.releaseResources();
    system.releaseResources();
    processor.releaseResources();
}

void StandaloneEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs,
    int outputChannels, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int channel = 0; channel < outputChannels; ++channel)
        if (outputs[channel] != nullptr) juce::FloatVectorOperations::clear (outputs[channel], numSamples);
    if (routeInvalid.load()) return;
    const auto playing = transport.isPlaying();
    if (! systemRunning && ! playing && ! transportWasPlaying)
    {
        // Service the transport's stop acknowledgement even if Play and Pause
        // occurred between two callbacks; no EQ/FFT work is needed while idle.
        transport.getNextAudioBlock ({ &scratch, 0, juce::jmin (scratch.getNumSamples(), numSamples) });
        return;
    }
    transportWasPlaying = playing;
    for (int offset = 0; offset < numSamples; offset += scratch.getNumSamples())
    {
        const auto count = juce::jmin (scratch.getNumSamples(), numSamples - offset);
        juce::AudioSourceChannelInfo info (&scratch, 0, count);
        if (systemRunning) system.getNextAudioBlock (info);
        else transport.getNextAudioBlock (info);
        juce::AudioBuffer<float> block (scratch.getArrayOfWritePointers(), 2, count);
        processor.processBlock (block, midi);
        for (int channel = 0; channel < juce::jmin (2, outputChannels); ++channel)
            if (outputs[channel] != nullptr)
                juce::FloatVectorOperations::copy (outputs[channel] + offset, scratch.getReadPointer (channel), count);
    }
}
