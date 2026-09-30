#pragma once
#include "PluginProcessor.h"
#include "SystemAudioSource.h"

class StandaloneEngine final : private juce::AudioIODeviceCallback
{
public:
    explicit StandaloneEngine (TenBandEQAudioProcessor&);
    ~StandaloneEngine() override;
    juce::Result initialise();
    juce::StringArray outputNames();
    juce::Result selectOutput (const juce::String&);
    juce::Result loadFile (const juce::File&);
    juce::Result startSystem (const SystemAudioSource::Endpoint&);
    void stop();
    void play();
    void pause();
    void seek (double seconds);
    bool isPlaying() const { return transport.isPlaying(); }
    bool isSystemRunning() const noexcept { return systemRunning; }
    double position() const { return transport.getCurrentPosition(); }
    double duration() const { return transport.getLengthInSeconds(); }
    const juce::File& loadedFile() const { return file; }
    double cpuUsage() const { return deviceManager.getCpuUsage(); }
    juce::String currentOutput() const { return deviceManager.getAudioDeviceSetup().outputDeviceName; }
    juce::String captureError() const
    {
        return routeInvalid.load() ? "Output device changed or disconnected. Select a distinct output and restart system EQ." : system.getError();
    }
    int dropouts() const { return system.getDropouts(); }
private:
    void attach();
    void detach();
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                          const juce::AudioIODeviceCallbackContext&) override;
    TenBandEQAudioProcessor& processor;
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formats;
    juce::TimeSliceThread readAhead { "Audio file read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transport;
    SystemAudioSource system;
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midi;
    juce::File file;
    bool attached = false;
    bool systemRunning = false; // Mutated only with the audio callback detached.
    bool transportWasPlaying = false; // Audio thread only, retains the final fade-out block.
    juce::String systemOutputName;
    std::atomic<bool> routeInvalid { false };
};
