#pragma once

#include <JuceHeader.h>
#include <array>
#include "EqResponse.h"
#include "SpectrumAnalyzer.h"

class TenBandEQAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int bandCount = 10;
    static constexpr std::array<float, bandCount> bandFrequencies {
        31.25f, 62.5f, 125.0f, 250.0f, 500.0f,
        1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    TenBandEQAudioProcessor();
    ~TenBandEQAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<bool> lightMode { false };
    static juce::String bandParameterId (int index);
    bool popSpectrumFrame (spectrum::Frame& frame) noexcept { return spectrumFrames.pop (frame); }
    double getDisplaySampleRate() const noexcept { return displaySampleRate.load(); }
    void setSpectrumEnabled (bool enabled) noexcept { spectrumEnabled.store (enabled, std::memory_order_relaxed); }
    // Audio-thread diagnostic used by the regression/benchmark target.
    juce::uint64 getCoefficientUpdateCount() const noexcept { return coefficientUpdates; }

private:
    struct Biquad
    {
        eq::Coefficients coefficients;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float input) noexcept
        {
            const auto output = coefficients.b0 * input + z1;
            z1 = coefficients.b1 * input - coefficients.a1 * output + z2;
            z2 = coefficients.b2 * input - coefficients.a2 * output;
            return output;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateCoefficients (int samplesToAdvance) noexcept;
    void captureSpectrum (const juce::AudioBuffer<float>&) noexcept;

    std::array<std::array<Biquad, bandCount>, 2> filters {};
    std::array<std::atomic<float>*, bandCount> bandParameters {};
    std::atomic<float>* outputParameter = nullptr;
    std::atomic<float>* bypassParameter = nullptr;
    double currentSampleRate = 44100.0;
    std::atomic<double> displaySampleRate { 44100.0 };
    spectrum::FrameQueue spectrumFrames;
    std::atomic<bool> spectrumEnabled { false };
    std::array<juce::SmoothedValue<float>, bandCount> smoothedGains;
    std::array<float, bandCount> cachedGains {};
    juce::SmoothedValue<float> smoothedOutput;
    bool wasBypassed = false;
    juce::uint64 coefficientUpdates = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TenBandEQAudioProcessor)
};
