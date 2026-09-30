#include "SpectrumDisplay.h"
#include "UI/Theme.h"

SpectrumDisplay::SpectrumDisplay (TenBandEQAudioProcessor& p) : processor (p)
{
    for (int band = 0; band < TenBandEQAudioProcessor::bandCount; ++band)
        gains[static_cast<size_t> (band)] = processor.parameters.getRawParameterValue (
            TenBandEQAudioProcessor::bandParameterId (band));
    outputGain = processor.parameters.getRawParameterValue ("output");
    bypass = processor.parameters.getRawParameterValue ("bypass");
    sampleRate = processor.getDisplaySampleRate();
    displayedLevels.fill (spectrum::floorDb);
    processor.setSpectrumEnabled (true);
    // Discard frames captured before the editor opened.
    for (int i = 0; i < 4 && processor.popSpectrumFrame (frame); ++i) {}
    setTitle ("Frequency spectrum and equalizer response");
    setDescription ("Drag a band point vertically to change its gain. Double-click to reset it.");
    startTimerHz (30);
}

SpectrumDisplay::~SpectrumDisplay()
{
    stopTimer();
    processor.setSpectrumEnabled (false);
    endDrag();
}

juce::Colour SpectrumDisplay::bandColour (int band)
{
    return ui::bandColour (band);
}

juce::Rectangle<float> SpectrumDisplay::plotBounds() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (48.0f).withTrimmedRight (44.0f)
                                   .withTrimmedTop (42.0f).withTrimmedBottom (30.0f);
}

float SpectrumDisplay::maximumFrequency() const
{
    return static_cast<float> (juce::jmin (20000.0, sampleRate * 0.49));
}

float SpectrumDisplay::frequencyToX (float frequency) const
{
    const auto plot = plotBounds();
    return plot.getX() + plot.getWidth() * std::log (frequency / 20.0f) / std::log (maximumFrequency() / 20.0f);
}

float SpectrumDisplay::xToFrequency (float x) const
{
    const auto plot = plotBounds();
    return 20.0f * std::pow (maximumFrequency() / 20.0f, (x - plot.getX()) / plot.getWidth());
}

float SpectrumDisplay::gainToY (float gain) const
{
    return juce::jmap (gain, -24.0f, 24.0f, plotBounds().getBottom(), plotBounds().getY());
}

juce::Point<float> SpectrumDisplay::bandPosition (int band) const
{
    const auto frequency = juce::jmin (TenBandEQAudioProcessor::bandFrequencies[static_cast<size_t> (band)],
                                      static_cast<float> (sampleRate * 0.45));
    return { frequencyToX (frequency), gainToY (gains[static_cast<size_t> (band)]->load()) };
}

void SpectrumDisplay::timerCallback()
{
    const auto showing = isShowing();
    processor.setSpectrumEnabled (showing);
    if (! showing)
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto newSampleRate = processor.getDisplaySampleRate();
    if (newSampleRate != sampleRate)
    {
        sampleRate = newSampleRate;
        displayedLevels.fill (spectrum::floorDb);
        lastFrameTime = 0.0;
    }

    bool received = false;
    for (int i = 0; i < 4; ++i)
    {
        if (! processor.popSpectrumFrame (frame))
            break;
        received = true;
    }
    if (received && frame.sampleRate == sampleRate)
    {
        analyzer.analyze (frame);
        lastFrameTime = now;
    }

    const auto stale = now - lastFrameTime > juce::jmax (250.0, 2000.0 * spectrum::fftSize / sampleRate);
    const auto& target = analyzer.getLevels();
    bool changed = updateResponseCache();
    for (size_t bin = 0; bin < displayedLevels.size(); ++bin)
    {
        const auto next = stale ? spectrum::floorDb : target[bin];
        const auto difference = (next - displayedLevels[bin]) * (next > displayedLevels[bin] ? 0.8f : 0.18f);
        changed = changed || std::abs (difference) > 0.02f;
        displayedLevels[bin] += difference;
    }
    if (changed)
        repaint();
}

void SpectrumDisplay::paint (juce::Graphics& g)
{
    const auto p = ui::palette (*this);
    const auto bounds = getLocalBounds().toFloat();
    const auto plot = plotBounds();
    const auto isBypassed = bypass->load() >= 0.5f;
    g.setColour (p.background);
    g.fillRoundedRectangle (bounds, 10.0f);
    g.setColour (p.border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    g.setFont (ui::font (11.0f));
    g.setColour (p.accent);
    g.drawText ("OUTPUT SPECTRUM / dBFS", 16, 10, 188, 20, juce::Justification::centredLeft);
    g.setColour (p.text);
    g.drawText (isBypassed ? "EQ BYPASSED" : "EQ RESPONSE / dB", 216, 10, 150, 20, juce::Justification::centredLeft);
    g.setColour (p.muted);
    if (getWidth() >= 650)
        g.drawText ("Drag points to adjust gain", getWidth() - 224, 10, 206, 20, juce::Justification::centredRight);

    constexpr std::array<float, 10> ticks { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (const auto frequency : ticks)
    {
        if (frequency > maximumFrequency())
            continue;
        const auto x = frequencyToX (frequency);
        g.setColour (p.grid);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (p.muted);
        const auto label = frequency >= 1000.0f ? juce::String (frequency / 1000.0f, 0) + "k"
                                               : juce::String (frequency, 0);
        g.drawText (label, juce::roundToInt (x) - 22, juce::roundToInt (plot.getBottom()) + 6,
                    44, 18, juce::Justification::centred);
    }

    for (int db = -24; db <= 24; db += 12)
    {
        const auto y = gainToY (static_cast<float> (db));
        g.setColour (db == 0 ? p.border : p.grid);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (p.muted);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::roundToInt (plot.getRight()) + 6,
                    juce::roundToInt (y) - 8, 35, 16, juce::Justification::centredLeft);
    }
    for (int db = -90; db <= 0; db += 30)
    {
        const auto y = juce::jmap (static_cast<float> (db), spectrum::floorDb, 0.0f, plot.getBottom(), plot.getY());
        g.setColour (p.accent);
        g.drawText (juce::String (db), 4, juce::roundToInt (y) - 8, 36, 16, juce::Justification::centredRight);
    }

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (plot.toNearestInt());
    juce::Path spectrumPath;
    const auto points = juce::jmax (2, juce::roundToInt (plot.getWidth() / 2.0f));
    for (int point = 0; point <= points; ++point)
    {
        const auto x = plot.getX() + plot.getWidth() * static_cast<float> (point) / static_cast<float> (points);
        const auto bin = xToFrequency (x) * static_cast<float> (spectrum::fftSize / sampleRate);
        const auto lower = juce::jlimit (0, spectrum::binCount - 2, static_cast<int> (bin));
        auto db = juce::jmap (bin - static_cast<float> (lower), displayedLevels[static_cast<size_t> (lower)],
                            displayedLevels[static_cast<size_t> (lower + 1)]);
        // Preserve narrow peaks where several FFT bins occupy one screen segment.
        const auto nextX = juce::jmin (plot.getRight(), x + plot.getWidth() / static_cast<float> (points));
        const auto upper = juce::jlimit (lower, spectrum::binCount - 1,
            static_cast<int> (xToFrequency (nextX) * static_cast<float> (spectrum::fftSize / sampleRate)));
        for (int b = lower + 1; b <= upper; ++b)
            db = juce::jmax (db, displayedLevels[static_cast<size_t> (b)]);
        const auto y = juce::jmap (juce::jlimit (spectrum::floorDb, 0.0f, db), spectrum::floorDb, 0.0f,
                                   plot.getBottom(), plot.getY());
        if (point == 0) spectrumPath.startNewSubPath (x, y);
        else spectrumPath.lineTo (x, y);
    }
    auto spectrumFill = spectrumPath;
    spectrumFill.lineTo (plot.getBottomRight());
    spectrumFill.lineTo (plot.getBottomLeft());
    spectrumFill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.22f), plot.getTopLeft(),
                                           p.accent.withAlpha (0.015f), plot.getBottomLeft(), false));
    g.fillPath (spectrumFill);
    g.setColour (p.muted);
    g.strokePath (spectrumPath, juce::PathStrokeType (1.35f, juce::PathStrokeType::curved));

    updateResponseCache();
    g.drawImageAt (responseImage, 0, 0);
}

bool SpectrumDisplay::updateResponseCache()
{
    auto dirty = responseImage.getWidth() != getWidth() || responseImage.getHeight() != getHeight()
              || cachedSampleRate != sampleRate || cachedDraggedBand != draggedBand
              || cachedOutput != outputGain->load() || cachedBypass != bypass->load();
    for (size_t band = 0; band < cachedGains.size(); ++band)
    {
        const auto gain = gains[band]->load();
        dirty = dirty || cachedGains[band] != gain;
        cachedGains[band] = gain;
    }
    cachedOutput = outputGain->load();
    cachedBypass = bypass->load();
    cachedSampleRate = sampleRate;
    cachedDraggedBand = draggedBand;
    if (dirty && getWidth() > 0 && getHeight() > 0)
        rebuildResponse();
    return dirty;
}

void SpectrumDisplay::rebuildResponse()
{
    const auto p = ui::palette (*this);
    const auto light = p.background.getBrightness() > 0.5f;
    responseImage = juce::Image (juce::Image::ARGB, getWidth(), getHeight(), true);
    juce::Graphics g (responseImage);
    const auto plot = plotBounds();
    const auto isBypassed = cachedBypass >= 0.5f;
    g.reduceClipRegion (plot.toNearestInt());
    std::array<float, 640> total {};
    total.fill (isBypassed ? 0.0f : outputGain->load());
    for (int band = 0; band < TenBandEQAudioProcessor::bandCount && ! isBypassed; ++band)
    {
        const auto gain = gains[static_cast<size_t> (band)]->load();
        const auto coefficients = eq::makePeak (sampleRate,
            TenBandEQAudioProcessor::bandFrequencies[static_cast<size_t> (band)], gain);
        juce::Path bandPath;
        bandPath.startNewSubPath (plot.getX(), gainToY (0.0f));
        for (size_t point = 0; point < total.size(); ++point)
        {
            const auto x = plot.getX() + plot.getWidth() * static_cast<float> (point) / static_cast<float> (total.size() - 1);
            const auto db = eq::responseDb (coefficients, xToFrequency (x), sampleRate);
            total[point] += db;
            bandPath.lineTo (x, gainToY (db));
        }
        bandPath.lineTo (plot.getRight(), gainToY (0.0f));
        bandPath.closeSubPath();
        const auto colour = ui::bandColour (band, light);
        if (std::abs (gain) > 0.05f)
        {
            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.38f), 0.0f, gainToY (gain),
                                                   colour.withAlpha (0.015f), 0.0f, gainToY (0.0f), false));
            g.fillPath (bandPath);
        }
    }

    juce::Path response;
    for (size_t point = 0; point < total.size(); ++point)
    {
        const auto x = plot.getX() + plot.getWidth() * static_cast<float> (point) / static_cast<float> (total.size() - 1);
        const auto y = gainToY (total[point]);
        if (point == 0) response.startNewSubPath (x, y);
        else response.lineTo (x, y);
    }
    g.setColour (p.text.withAlpha (0.08f));
    g.strokePath (response, juce::PathStrokeType (6.0f));
    g.setColour (isBypassed ? p.muted : p.text);
    g.strokePath (response, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

    if (! isBypassed)
        for (int band = 0; band < TenBandEQAudioProcessor::bandCount; ++band)
        {
            const auto position = bandPosition (band);
            const auto colour = ui::bandColour (band, light);
            g.setColour (colour.withAlpha (band == draggedBand ? 0.3f : 0.12f));
            g.fillEllipse (position.x - 11.0f, position.y - 11.0f, 22.0f, 22.0f);
            g.setColour (p.panel);
            g.fillEllipse (position.x - 5.0f, position.y - 5.0f, 10.0f, 10.0f);
            g.setColour (colour);
            g.drawEllipse (position.x - 5.0f, position.y - 5.0f, 10.0f, 10.0f, 1.5f);
            g.fillEllipse (position.x - 2.0f, position.y - 2.0f, 4.0f, 4.0f);
        }
}

int SpectrumDisplay::findBand (juce::Point<float> position) const
{
    if (bypass->load() >= 0.5f)
        return -1;
    auto nearest = -1;
    auto distance = 16.0f;
    for (int band = 0; band < TenBandEQAudioProcessor::bandCount; ++band)
    {
        const auto next = position.getDistanceFrom (bandPosition (band));
        if (next < distance)
        {
            distance = next;
            nearest = band;
        }
    }
    return nearest;
}

void SpectrumDisplay::mouseDown (const juce::MouseEvent& event)
{
    endDrag();
    draggedBand = findBand (event.position);
    if (draggedBand >= 0)
        processor.parameters.getParameter (TenBandEQAudioProcessor::bandParameterId (draggedBand))->beginChangeGesture();
}

void SpectrumDisplay::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedBand < 0)
        return;
    const auto plot = plotBounds();
    const auto gain = juce::jlimit (-12.0f, 12.0f, juce::jmap (event.position.y, plot.getBottom(), plot.getY(), -24.0f, 24.0f));
    auto* parameter = processor.parameters.getParameter (TenBandEQAudioProcessor::bandParameterId (draggedBand));
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (gain));
    repaint();
}

void SpectrumDisplay::endDrag()
{
    if (draggedBand >= 0)
        processor.parameters.getParameter (TenBandEQAudioProcessor::bandParameterId (draggedBand))->endChangeGesture();
    draggedBand = -1;
}

void SpectrumDisplay::mouseUp (const juce::MouseEvent&) { endDrag(); }

void SpectrumDisplay::mouseDoubleClick (const juce::MouseEvent& event)
{
    endDrag();
    const auto band = findBand (event.position);
    if (band < 0)
        return;
    auto* parameter = processor.parameters.getParameter (TenBandEQAudioProcessor::bandParameterId (band));
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.0f));
    parameter->endChangeGesture();
    repaint();
}
