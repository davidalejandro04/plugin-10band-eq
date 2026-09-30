#include "PluginEditor.h"

TenBandEQAudioProcessorEditor::TenBandEQAudioProcessorEditor (TenBandEQAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), spectrumDisplay (p)
{
    theme.setLight (processor.lightMode.load());
    setLookAndFeel (&theme);
    tooltips.setLookAndFeel (&theme);
    addAndMakeVisible (spectrumDisplay);
    for (int band = 0; band < TenBandEQAudioProcessor::bandCount; ++band)
    {
        auto& slider = bandSliders[static_cast<size_t> (band)];
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 20);
        slider.setTextValueSuffix (" dB");
        slider.setColour (juce::Slider::trackColourId, SpectrumDisplay::bandColour (band));
        slider.setName (juce::String (TenBandEQAudioProcessor::bandFrequencies[static_cast<size_t> (band)]) + " Hz gain");
        addAndMakeVisible (slider);

        auto& label = bandLabels[static_cast<size_t> (band)];
        const auto frequency = TenBandEQAudioProcessor::bandFrequencies[static_cast<size_t> (band)];
        label.setText (frequency >= 1000.0f
                           ? juce::String (frequency / 1000.0f, frequency == 16000.0f ? 0 : 1) + "k"
                           : juce::String (frequency, frequency < 100.0f ? 1 : 0),
                       juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, SpectrumDisplay::bandColour (band));
        addAndMakeVisible (label);

        bandAttachments[static_cast<size_t> (band)] = std::make_unique<SliderAttachment> (
            processor.parameters, TenBandEQAudioProcessor::bandParameterId (band), slider);
    }

    outputSlider.setSliderStyle (juce::Slider::LinearVertical);
    outputSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 20);
    outputSlider.setTextValueSuffix (" dB");
    outputSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xffffbc69));
    addAndMakeVisible (outputSlider);
    outputAttachment = std::make_unique<SliderAttachment> (processor.parameters, "output", outputSlider);

    outputLabel.setText ("Output", juce::dontSendNotification);
    outputLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (outputLabel);

    addAndMakeVisible (bypassButton);
    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Bypass equalization");
    addAndMakeVisible (themeButton);
    themeButton.onClick = [this] { setLightMode (! theme.isLight()); };
    bypassAttachment = std::make_unique<ButtonAttachment> (processor.parameters, "bypass", bypassButton);
    setResizable (true, true);
    setResizeLimits (520, 620, 1800, 1200);
    setLightMode (theme.isLight());
    setSize (1000, 720);
}

TenBandEQAudioProcessorEditor::~TenBandEQAudioProcessorEditor()
{
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void TenBandEQAudioProcessorEditor::setLightMode (bool light)
{
    theme.setLight (light);
    processor.lightMode.store (light);
    themeButton.setIcon (light ? ui::Icon::moon : ui::Icon::sun);
    themeButton.setButtonText (light ? "Switch to dark mode" : "Switch to light mode");
    themeButton.setTooltip (themeButton.getButtonText());
    sendLookAndFeelChange();
    if (onThemeChanged) onThemeChanged();
    repaint();
}

void TenBandEQAudioProcessorEditor::lookAndFeelChanged()
{
    for (int band = 0; band < TenBandEQAudioProcessor::bandCount; ++band)
    {
        const auto colour = ui::bandColour (band, theme.isLight());
        bandSliders[static_cast<size_t> (band)].setColour (juce::Slider::trackColourId, colour);
        bandLabels[static_cast<size_t> (band)].setColour (juce::Label::textColourId, theme.colours().muted);
    }
    outputSlider.setColour (juce::Slider::trackColourId, theme.colours().accent);
}

void TenBandEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto p = theme.colours();
    g.fillAll (p.background);
    g.setColour (p.text);
    g.setFont (ui::font (23.0f, true));
    g.drawText ("TEN BAND EQ", 24, 16, 340, 34, juce::Justification::centredLeft);
    g.setColour (p.muted);
    g.setFont (ui::font (13.0f));
    g.drawText ("+/-12 dB   |   31 Hz - 16 kHz", 24, 52, 340, 22, juce::Justification::centredLeft);
    g.setColour (p.border);
    g.drawLine (22.0f, 92.0f, static_cast<float> (getWidth() - 22), 92.0f, 1.0f);
}

void TenBandEQAudioProcessorEditor::resized()
{
    const auto compact = getWidth() < 820;
    const auto graphHeight = compact ? juce::jmax (200, (getHeight() - 100) * 2 / 5) : (getHeight() - 100) * 3 / 5;
    spectrumDisplay.setBounds (20, 100, getWidth() - 40, graphHeight);
    const auto top = 100 + graphHeight + 16;
    const auto columnWidth = (getWidth() - 40) / (compact ? 5 : 11);
    const auto rowHeight = compact ? (getHeight() - top - 50) / 2 : getHeight() - top - 12;
    const auto sliderHeight = rowHeight - 28;

    for (int band = 0; band < TenBandEQAudioProcessor::bandCount; ++band)
    {
        const auto x = 20 + (compact ? band % 5 : band) * columnWidth;
        const auto y = top + (compact ? band / 5 : 0) * rowHeight;
        bandSliders[static_cast<size_t> (band)].setBounds (x, y, columnWidth - 8, sliderHeight);
        bandLabels[static_cast<size_t> (band)].setBounds (x, y + sliderHeight + 4, columnWidth - 8, 22);
    }

    outputSlider.setSliderStyle (compact ? juce::Slider::LinearHorizontal : juce::Slider::LinearVertical);
    outputSlider.setTextBoxStyle (compact ? juce::Slider::TextBoxRight : juce::Slider::TextBoxBelow, false, 64, 20);
    if (compact)
    {
        outputLabel.setBounds (20, getHeight() - 42, 70, 28);
        outputSlider.setBounds (96, getHeight() - 42, getWidth() - 120, 28);
    }
    else
    {
        outputSlider.setBounds (20 + 10 * columnWidth, top, columnWidth - 8, sliderHeight);
        outputLabel.setBounds (20 + 10 * columnWidth, top + sliderHeight + 4, columnWidth - 8, 22);
    }
    bypassButton.setBounds (getWidth() - 184, 26, 112, 36);
    themeButton.setBounds (getWidth() - 60, 26, 40, 36);
}
