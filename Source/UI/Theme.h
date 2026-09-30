#pragma once
#include <JuceHeader.h>

namespace ui
{
struct Palette
{
    juce::Colour background, panel, text, muted, border, grid, accent;
};

juce::Font font (float height, bool bold = false);
class Theme final : public juce::LookAndFeel_V4
{
public:
    Theme() { setLight (false); }
    void setLight (bool);
    bool isLight() const { return light; }
    Palette colours() const;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f, true); }
    juce::Font getLabelFont (juce::Label&) override { return font (12.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    void drawLinearSlider (juce::Graphics&, int, int, int, int, float, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Button* createDocumentWindowButton (int) override;
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int, int,
                                     int, int, const juce::Image*, bool) override;
    void positionDocumentWindowButtons (juce::DocumentWindow&, int, int, int, int,
                                        juce::Button*, juce::Button*, juce::Button*, bool) override;
private:
    bool light = false;
};

Palette palette (const juce::Component&);
void drawControlSurface (juce::Graphics&, juce::Rectangle<float>, const Palette&, bool hover, bool active, bool focus);
juce::Colour bandColour (int band, bool light = false);
enum class Icon { folder, play, pause, stop, exportFile, close, refresh, sun, moon, power, minimise, maximise };
class IconButton final : public juce::Button
{
public:
    IconButton (const juce::String& label, Icon glyph, bool showCaption = false);
    void setIcon (Icon value) { if (icon != value) { icon = value; repaint(); } }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    Icon icon;
    bool caption;
};
}
