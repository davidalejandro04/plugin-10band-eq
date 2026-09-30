#include "FileRenderer.h"

juce::Result fileaudio::render (const juce::File& source, const juce::File& destination,
                              const juce::MemoryBlock& state, std::atomic<bool>& cancelled,
                              std::atomic<float>& progress)
{
    progress.store (0.0f);
    if (source == destination)
        return juce::Result::fail ("Choose a different output file; the original audio is preserved.");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (source));
    if (reader == nullptr)
        return juce::Result::fail ("Cannot decode the input audio file.");
    if (reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples <= 0)
        return juce::Result::fail ("Please choose a non-empty mono or stereo audio file.");
    juce::TemporaryFile temporary (destination);
    auto stream = std::make_unique<juce::FileOutputStream> (temporary.getFile());
    if (! stream->openedOk())
        return juce::Result::fail ("Cannot write to the selected output folder.");
    juce::WavAudioFormat format;
    auto options = juce::AudioFormatWriterOptions().withSampleRate (reader->sampleRate)
        .withNumChannels (static_cast<int> (reader->numChannels)).withBitsPerSample (32)
        .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    std::unique_ptr<juce::OutputStream> outputStream (std::move (stream));
    auto writer = format.createWriterFor (outputStream, options);
    if (writer == nullptr)
        return juce::Result::fail ("Cannot create the WAV output file.");
    auto processor = std::make_unique<TenBandEQAudioProcessor>();
    processor->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    constexpr int blockSize = 4096;
    processor->prepareToPlay (reader->sampleRate, blockSize);
    juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels), blockSize);
    juce::MidiBuffer midi;
    for (juce::int64 position = 0; position < reader->lengthInSamples; position += blockSize)
    {
        if (cancelled.load())
            return juce::Result::fail ("Export cancelled.");
        const auto count = static_cast<int> (juce::jmin<juce::int64> (blockSize, reader->lengthInSamples - position));
        if (! reader->read (&buffer, 0, count, position, true, true))
            return juce::Result::fail ("An error occurred while decoding the input.");
        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), count);
        processor->processBlock (block, midi);
        if (! writer->writeFromAudioSampleBuffer (block, 0, count))
            return juce::Result::fail ("An error occurred while writing audio (check free disk space).");
        progress.store (static_cast<float> (static_cast<double> (position + count) / static_cast<double> (reader->lengthInSamples)));
    }
    writer.reset();
    if (cancelled.load())
        return juce::Result::fail ("Export cancelled.");
    if (! temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail ("Could not move the completed WAV to the chosen destination.");
    return juce::Result::ok();
}
