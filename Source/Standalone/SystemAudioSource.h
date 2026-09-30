#pragma once
#include <JuceHeader.h>

// Windows render-endpoint loopback -> bounded stereo FIFO -> resampled output.
// Endpoint capture and COM calls run only on the capture worker.
class SystemAudioSource final : public juce::AudioSource, private juce::Thread
{
public:
    struct Endpoint { juce::String id, name; };
    static std::vector<Endpoint> getEndpoints();
    SystemAudioSource();
    ~SystemAudioSource() override;
    juce::Result startCapture (const juce::String& endpointId);
    void stopCapture(); // Call only with the audio callback detached.
    juce::String getError() const;
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    int getDropouts() const noexcept { return ring.dropouts.load(); }
private:
    struct Ring final : juce::AudioSource
    {
        juce::AbstractFifo fifo { 65536 };
        juce::AudioBuffer<float> samples { 2, 65536 };
        std::atomic<int> dropouts { 0 };
        void prepareToPlay (int, double) override {}
        void releaseResources() override {}
        void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;
    } ring;
    void run() override;
    void setError (const juce::String&);
    juce::ResamplingAudioSource resampler { &ring, false, 2 };
    juce::String endpoint, error;
    mutable juce::CriticalSection errorLock;
    juce::WaitableEvent started;
    std::atomic<double> captureRate { 48000.0 };
    double outputRate = 48000.0;
    int targetFill = 1440;
    bool primed = false;
};
