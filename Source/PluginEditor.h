#pragma once

#include "PluginProcessor.h"
#include "SpectrumDisplay.h"
#include "UI/Theme.h"

class TenBandEQAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit TenBandEQAudioProcessorEditor (TenBandEQAudioProcessor&);
    ~TenBandEQAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void setLightMode (bool);
    ui::Theme& getTheme() { return theme; }
    void setTooltipParent (juce::Component& parent) { parent.addChildComponent (tooltips); }
    std::function<void()> onThemeChanged;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    TenBandEQAudioProcessor& processor;
    ui::Theme theme;
    juce::TooltipWindow tooltips { this, 600 };
    SpectrumDisplay spectrumDisplay;
    std::array<juce::Slider, TenBandEQAudioProcessor::bandCount> bandSliders;
    std::array<juce::Label, TenBandEQAudioProcessor::bandCount> bandLabels;
    std::array<std::unique_ptr<SliderAttachment>, TenBandEQAudioProcessor::bandCount> bandAttachments;
    juce::Slider outputSlider;
    juce::Label outputLabel;
    std::unique_ptr<SliderAttachment> outputAttachment;
    ui::IconButton bypassButton { "Bypass", ui::Icon::power, true };
    ui::IconButton themeButton { "Switch to light mode", ui::Icon::sun };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TenBandEQAudioProcessorEditor)
};
