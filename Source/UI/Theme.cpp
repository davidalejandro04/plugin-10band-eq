#include "Theme.h"
#include <UIFontData.h>

namespace ui
{
juce::Font font (float height, bool bold)
{
#if JUCE_MAC
    // JUCE retrieves the native UI font with CTFontCreateUIFontForLanguage.
    static const auto regular = juce::Typeface::findSystemTypeface();
    static const auto semibold = juce::Typeface::createSystemTypefaceFor (
        juce::Font (juce::FontOptions (regular->getName(), "Semibold", 13.0f)));
#else
    static const auto regular = juce::Typeface::createSystemTypefaceFor (UIFontData::InterRegular_ttf, UIFontData::InterRegular_ttfSize);
    static const auto semibold = juce::Typeface::createSystemTypefaceFor (UIFontData::InterSemiBold_ttf, UIFontData::InterSemiBold_ttfSize);
#endif
    return juce::Font (juce::FontOptions (bold && semibold != nullptr ? semibold : regular).withHeight (height));
}
juce::Typeface::Ptr Theme::getTypefaceForFont (const juce::Font& f) { return font (f.getHeight(), f.isBold()).getTypefacePtr(); }
Palette Theme::colours() const
{
    return light ? Palette { juce::Colour (0xfff3f3f1), juce::Colour (0xffeaeae7), juce::Colour (0xff232323),
                            juce::Colour (0xff666664), juce::Colour (0xffd3d3ce), juce::Colour (0xffe0e0dc), juce::Colour (0xff454545) }
                 : Palette { juce::Colour (0xff090909), juce::Colour (0xff121212), juce::Colour (0xffededeb),
                             juce::Colour (0xff999995), juce::Colour (0xff30302e), juce::Colour (0xff20201f), juce::Colour (0xffc5c5bf) };
}
void Theme::setLight (bool value)
{
    light = value;
    const auto p = colours();
    setColour (juce::Label::textColourId, p.text);
    setColour (juce::TextButton::buttonColourId, p.panel);
    setColour (juce::TextButton::buttonOnColourId, p.panel);
    setColour (juce::TextButton::textColourOffId, p.text);
    setColour (juce::TextButton::textColourOnId, p.text);
    setColour (juce::ComboBox::backgroundColourId, p.panel);
    setColour (juce::ComboBox::textColourId, p.text);
    setColour (juce::ComboBox::outlineColourId, p.border);
    setColour (juce::ComboBox::arrowColourId, p.muted);
    setColour (juce::PopupMenu::backgroundColourId, p.panel);
    setColour (juce::PopupMenu::textColourId, p.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    setColour (juce::PopupMenu::highlightedTextColourId, p.background);
    setColour (juce::Slider::backgroundColourId, p.border);
    setColour (juce::Slider::trackColourId, p.accent);
    setColour (juce::Slider::thumbColourId, p.text);
    setColour (juce::Slider::textBoxTextColourId, p.text);
    setColour (juce::Slider::textBoxBackgroundColourId, p.panel);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, p.panel);
    setColour (juce::TextEditor::textColourId, p.text);
    setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.3f));
    setColour (juce::TextEditor::outlineColourId, p.border);
    setColour (juce::TooltipWindow::backgroundColourId, p.panel);
    setColour (juce::TooltipWindow::textColourId, p.text);
    setColour (juce::TooltipWindow::outlineColourId, p.border);
    setColour (juce::ScrollBar::thumbColourId, p.muted.withAlpha (0.5f));
}
void drawControlSurface (juce::Graphics& g, juce::Rectangle<float> bounds, const Palette& p,
                         bool hover, bool active, bool focus)
{
    // Flat surfaces: no bevel, shadow, highlight edge, or offset on press.
    g.setColour (p.panel.interpolatedWith (p.text, active ? 0.10f : hover ? 0.055f : 0.0f));
    g.fillRoundedRectangle (bounds, 5.0f);
    if (focus)
    {
        g.setColour (p.accent);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);
    }
    if (active)
    {
        g.setColour (p.accent);
        g.fillRoundedRectangle (bounds.getCentreX() - 8.0f, bounds.getBottom() - 3.0f, 16.0f, 1.0f, 0.5f);
    }
}
void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool hover, bool down)
{
    drawControlSurface (g, button.getLocalBounds().toFloat().reduced (1.0f), colours(),
                        hover && button.isEnabled(), down || button.getToggleState(), button.hasKeyboardFocus (false));
}
void Theme::drawComboBox (juce::Graphics& g, int width, int height, bool down,
                          int, int, int, int, juce::ComboBox& box)
{
    const auto p = colours();
    drawControlSurface (g, box.getLocalBounds().toFloat().reduced (1.0f), p,
                        box.isMouseOver(), down, box.hasKeyboardFocus (true));
    const auto x = static_cast<float> (width - 19);
    const auto y = static_cast<float> (height) * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (x - 4, y - 2); arrow.lineTo (x, y + 2); arrow.lineTo (x + 4, y - 2);
    g.setColour (p.muted.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.35f));
    g.strokePath (arrow, juce::PathStrokeType (1.3f));
}
void Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                              float position, float, float, juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto p = colours();
    const auto horizontal = slider.isHorizontal();
    const auto centre = horizontal ? static_cast<float> (y) + static_cast<float> (height) * 0.5f
                                   : static_cast<float> (x) + static_cast<float> (width) * 0.5f;
    const auto start = horizontal ? static_cast<float> (x) : static_cast<float> (y);
    const auto length = static_cast<float> (horizontal ? width : height);
    const auto end = start + length;
    g.setColour (p.border);
    if (horizontal) g.fillRect (start, centre - 1.0f, length, 2.0f);
    else g.fillRect (centre - 1.0f, start, 2.0f, length);
    g.setColour (p.accent.withMultipliedAlpha (slider.isEnabled() ? 0.65f : 0.25f));
    if (horizontal) g.fillRect (start, centre - 1.0f, juce::jmax (0.0f, position - start), 2.0f);
    else g.fillRect (centre - 1.0f, position, 2.0f, juce::jmax (0.0f, end - position));
    g.setColour (p.text.withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.35f));
    if (horizontal) g.fillRoundedRectangle (position - 2.0f, centre - 6.0f, 4.0f, 12.0f, 1.0f);
    else g.fillRoundedRectangle (centre - 8.0f, position - 2.0f, 16.0f, 4.0f, 1.0f);
}
Palette palette (const juce::Component& c)
{
    if (auto* theme = dynamic_cast<Theme*> (&c.getLookAndFeel())) return theme->colours();
    return Theme().colours();
}
juce::Button* Theme::createDocumentWindowButton (int type)
{
    if (type == juce::DocumentWindow::closeButton) return new IconButton ("Close", Icon::close);
    if (type == juce::DocumentWindow::minimiseButton) return new IconButton ("Minimize", Icon::minimise);
    return new IconButton ("Maximize or restore", Icon::maximise);
}
void Theme::drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g,
                                       int width, int height, int titleX, int titleWidth,
                                       const juce::Image*, bool)
{
    const auto p = colours();
    g.fillAll (p.background);
    g.setColour (p.muted);
    g.setFont (font (13.0f, true));
    g.drawText (window.getName(), titleX + 12, 0, juce::jmax (0, titleWidth - 24), height,
                juce::Justification::centredLeft);
    g.setColour (p.grid);
    g.drawHorizontalLine (height - 1, 0.0f, static_cast<float> (width));
}
void Theme::positionDocumentWindowButtons (juce::DocumentWindow&, int x, int y, int width, int height,
                                          juce::Button* minimise, juce::Button* maximise,
                                          juce::Button* close, bool)
{
    auto right = x + width - 8;
    for (auto* button : { close, maximise, minimise })
        if (button != nullptr)
        {
            right -= 36;
            button->setBounds (right, y + (height - 28) / 2, 32, 28);
        }
}
juce::Colour bandColour (int band, bool light)
{
    juce::ignoreUnused (band);
    return juce::Colour (light ? 0xff555550 : 0xffc5c5bf);
}
IconButton::IconButton (const juce::String& label, Icon glyph, bool showCaption)
    : Button (label), icon (glyph), caption (showCaption)
{
    setButtonText (label);
    setTooltip (label);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}
void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto p = palette (*this);
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto active = getToggleState();
    if (! isEnabled()) g.beginTransparencyLayer (0.35f);
    drawControlSurface (g, bounds, p, highlighted, active || down, hasKeyboardFocus (false));
    g.setColour (p.text);
    const auto x = caption ? 12.0f : (static_cast<float> (getWidth()) - 20.0f) * 0.5f;
    const auto y = (static_cast<float> (getHeight()) - 20.0f) * 0.5f;
    juce::Path path;
    switch (icon)
    {
        case Icon::folder: path.startNewSubPath (2, 16); path.lineTo (2, 4); path.lineTo (8, 4); path.lineTo (10, 7); path.lineTo (18, 7); path.lineTo (18, 16); path.closeSubPath(); break;
        case Icon::play: path.addTriangle (5, 3, 17, 10, 5, 17); break;
        case Icon::pause: path.addRectangle (5, 4, 3, 12); path.addRectangle (12, 4, 3, 12); break;
        case Icon::stop: path.addRoundedRectangle (4, 4, 12, 12, 2); break;
        case Icon::close: path.startNewSubPath (5, 5); path.lineTo (15, 15); path.startNewSubPath (15, 5); path.lineTo (5, 15); break;
        case Icon::minimise: path.startNewSubPath (5, 10); path.lineTo (15, 10); break;
        case Icon::maximise:
            if (getToggleState()) { path.addRectangle (7, 4, 9, 9); path.addRectangle (4, 7, 9, 9); }
            else path.addRectangle (5, 5, 10, 10);
            break;
        case Icon::exportFile: path.startNewSubPath (3, 12); path.lineTo (3, 17); path.lineTo (17, 17); path.lineTo (17, 12); path.startNewSubPath (10, 13); path.lineTo (10, 2); path.startNewSubPath (6, 6); path.lineTo (10, 2); path.lineTo (14, 6); break;
        case Icon::refresh: path.addCentredArc (10, 10, 7, 7, 0, 0.6f, 5.7f, true); path.startNewSubPath (2, 4); path.lineTo (6, 4); path.lineTo (6, 8); break;
        case Icon::power: path.addCentredArc (10, 11, 7, 7, 0, 0.65f, 5.63f, true); path.startNewSubPath (10, 1); path.lineTo (10, 10); break;
        case Icon::moon: path.startNewSubPath (13, 2); path.cubicTo (1, 0, -1, 17, 11, 18); path.cubicTo (15, 18, 18, 15, 18, 12); path.cubicTo (9, 16, 5, 7, 13, 2); path.closeSubPath(); break;
        case Icon::sun:
            path.addEllipse (6, 6, 8, 8);
            for (int i = 0; i < 8; ++i) { const auto a = juce::MathConstants<float>::twoPi * static_cast<float> (i) / 8.0f; path.startNewSubPath (10 + 7 * std::cos (a), 10 + 7 * std::sin (a)); path.lineTo (10 + 9 * std::cos (a), 10 + 9 * std::sin (a)); }
            break;
    }
    path.applyTransform (juce::AffineTransform::translation (x, y));
    if (icon == Icon::play || icon == Icon::pause || icon == Icon::stop) g.fillPath (path);
    else g.strokePath (path, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (caption) { g.setFont (font (13.0f, true)); g.drawText (getButtonText(), 40, 0, getWidth() - 48, getHeight(), juce::Justification::centredLeft); }
    if (! isEnabled()) g.endTransparencyLayer();
}
}
