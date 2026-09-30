#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>

TenBandEQAudioProcessor::TenBandEQAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "TenBandEQState", createParameterLayout())
{
    for (int band = 0; band < bandCount; ++band)
        bandParameters[static_cast<size_t> (band)] = parameters.getRawParameterValue (bandParameterId (band));

    outputParameter = parameters.getRawParameterValue ("output");
    bypassParameter = parameters.getRawParameterValue ("bypass");
}

juce::String TenBandEQAudioProcessor::bandParameterId (int index)
{
    return "band" + juce::String (index + 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout TenBandEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int band = 0; band < bandCount; ++band)
    {
        const auto frequency = bandFrequencies[static_cast<size_t> (band)];
        const auto name = frequency >= 1000.0f
            ? juce::String (frequency / 1000.0f, frequency == 16000.0f ? 0 : 1) + " kHz"
            : juce::String (frequency, frequency < 100.0f ? 2 : 0) + " Hz";

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { bandParameterId (band), 1 }, name,
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));
    return layout;
}

void TenBandEQAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    displaySampleRate.store (sampleRate);
    spectrumFrames.prepare (sampleRate);
    for (auto& channel : filters)
        for (auto& filter : channel)
            filter.reset();
    for (size_t band = 0; band < bandCount; ++band)
    {
        smoothedGains[band].reset (sampleRate, 0.015);
        smoothedGains[band].setCurrentAndTargetValue (bandParameters[band]->load());
        cachedGains[band] = std::numeric_limits<float>::quiet_NaN();
    }
    smoothedOutput.reset (sampleRate, 0.015);
    smoothedOutput.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParameter->load()));
    wasBypassed = false;
    updateCoefficients (0);
}

void TenBandEQAudioProcessor::releaseResources() {}

bool TenBandEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && layouts.getMainOutputChannelSet() == input;
}

void TenBandEQAudioProcessor::updateCoefficients (int samplesToAdvance) noexcept
{
    for (size_t band = 0; band < bandCount; ++band)
    {
        const auto gain = smoothedGains[band].skip (samplesToAdvance);
        if (gain == cachedGains[band])
            continue;
        cachedGains[band] = gain;
        const auto coefficients = eq::makePeak (currentSampleRate, bandFrequencies[band], gain);
        ++coefficientUpdates;
        for (auto& channel : filters)
        {
            channel[band].coefficients = coefficients;
            if (gain == 0.0f)
                channel[band].reset();
        }
    }
}

void TenBandEQAudioProcessor::captureSpectrum (const juce::AudioBuffer<float>& buffer) noexcept
{
    if (spectrumEnabled.load (std::memory_order_relaxed))
        spectrumFrames.push (buffer);
}

void TenBandEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    if (bypassParameter->load() >= 0.5f)
    {
        wasBypassed = true;
        captureSpectrum (buffer);
        return;
    }

    if (wasBypassed)
    {
        for (auto& channel : filters)
            for (auto& filter : channel)
                filter.reset();
        wasBypassed = false;
    }
    bool smoothing = false;
    for (size_t band = 0; band < bandCount; ++band)
    {
        smoothedGains[band].setTargetValue (bandParameters[band]->load());
        smoothing = smoothing || smoothedGains[band].isSmoothing();
    }
    smoothedOutput.setTargetValue (juce::Decibels::decibelsToGain (outputParameter->load()));
    const auto channels = juce::jmin (buffer.getNumChannels(), 2);
    const auto chunkSize = smoothing ? 64 : juce::jmax (1, buffer.getNumSamples());
    for (int offset = 0; offset < buffer.getNumSamples(); offset += chunkSize)
    {
        const auto count = juce::jmin (chunkSize, buffer.getNumSamples() - offset);
        if (smoothing)
            updateCoefficients (count);
        int activeCount = 0;
        for (const auto gain : cachedGains)
            activeCount += gain != 0.0f ? 1 : 0;
        if (activeCount == 0)
            continue;
        const auto processChannels = [&] (auto allBandsActive)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                auto* samples = buffer.getWritePointer (channel, offset);
                auto& channelFilters = filters[static_cast<size_t> (channel)];
                for (int sample = 0; sample < count; ++sample)
                {
                    auto value = samples[sample];
                    for (size_t band = 0; band < bandCount; ++band)
                        if (allBandsActive || cachedGains[band] != 0.0f)
                            value = channelFilters[band].process (value);
                    samples[sample] = value;
                }
            }
        };
        if (activeCount == bandCount) processChannels (std::true_type {});
        else processChannels (std::false_type {});
    }
    smoothedOutput.applyGain (buffer, buffer.getNumSamples());
    captureSpectrum (buffer);
}

juce::AudioProcessorEditor* TenBandEQAudioProcessor::createEditor()
{
    return new TenBandEQAudioProcessorEditor (*this);
}

void TenBandEQAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = parameters.copyState().createXml())
    {
        xml->setAttribute ("lightMode", lightMode.load());
        copyXmlToBinary (*xml, destination);
    }
}

void TenBandEQAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (parameters.state.getType()))
        {
            lightMode.store (xml->getBoolAttribute ("lightMode", false));
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TenBandEQAudioProcessor();
}
