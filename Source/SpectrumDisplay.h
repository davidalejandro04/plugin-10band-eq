#pragma once

#include "PluginProcessor.h"

class SpectrumDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumDisplay (TenBandEQAudioProcessor&);
    ~SpectrumDisplay() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override { responseImage = {}; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    static juce::Colour bandColour (int band);

private:
    void timerCallback() override;
    juce::Rectangle<float> plotBounds() const;
    float maximumFrequency() const;
    float frequencyToX (float frequency) const;
    float xToFrequency (float x) const;
    float gainToY (float gain) const;
    juce::Point<float> bandPosition (int band) const;
    int findBand (juce::Point<float>) const;
    void endDrag();
    bool updateResponseCache();
    void rebuildResponse();

    TenBandEQAudioProcessor& processor;
    spectrum::Analyzer analyzer;
    spectrum::Frame frame;
    std::array<float, spectrum::binCount> displayedLevels {};
    std::array<std::atomic<float>*, TenBandEQAudioProcessor::bandCount> gains {};
    std::atomic<float>* outputGain = nullptr;
    std::atomic<float>* bypass = nullptr;
    double sampleRate = 44100.0;
    double lastFrameTime = 0.0;
    int draggedBand = -1;
    juce::Image responseImage;
    std::array<float, TenBandEQAudioProcessor::bandCount> cachedGains {};
    float cachedOutput = 0.0f, cachedBypass = 0.0f;
    double cachedSampleRate = 0.0;
    int cachedDraggedBand = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumDisplay)
};
