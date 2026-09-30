#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Standalone/FileRenderer.h"
#include "Standalone/SystemAudioSource.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void setParameter (TenBandEQAudioProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.parameters.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void testSpectrum()
{
    auto frame = std::make_unique<spectrum::Frame>();
    auto analyzer = std::make_unique<spectrum::Analyzer>();
    constexpr size_t toneBin = 85;
    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0 })
    {
        frame->sampleRate = sampleRate;
        for (size_t sample = 0; sample < spectrum::fftSize; ++sample)
        {
            const auto value = 0.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi
                * static_cast<double> (toneBin * sample) / spectrum::fftSize));
            frame->left[sample] = value;
            frame->right[sample] = -value;
        }
        analyzer->analyze (*frame);
        const auto& levels = analyzer->getLevels();
        require (static_cast<size_t> (std::distance (levels.begin(), std::max_element (levels.begin(), levels.end())))
                 == toneBin, "FFT peak frequency is incorrect");
        require (std::abs (levels[toneBin] + 6.0206f) < 0.05f,
                 "FFT normalization or opposite-polarity stereo handling is incorrect");
    }
    frame->left.fill (0.0f);
    frame->right.fill (0.0f);
    analyzer->analyze (*frame);
    for (auto level : analyzer->getLevels())
        require (level == spectrum::floorDb, "Silence must reach the spectrum floor");
    std::cout << "PASS spectrum frequency, dBFS normalization, stereo polarity, silence\n";
}

void testQueue()
{
    auto queue = std::make_unique<spectrum::FrameQueue>();
    auto frame = std::make_unique<spectrum::Frame>();
    queue->prepare (48000.0);
    juce::AudioBuffer<float> block (1, 137);
    int supplied = 0;
    int consumed = 0;
    for (int iteration = 0; iteration < 120; ++iteration)
    {
        for (int i = 0; i < block.getNumSamples(); ++i)
            block.setSample (0, i, static_cast<float> (supplied++));
        queue->push (block);
        while (queue->pop (*frame))
        {
            for (size_t i = 0; i < spectrum::fftSize; ++i)
            {
                require (frame->left[i] == static_cast<float> (consumed++), "FIFO lost sample ordering");
                require (frame->right[i] == frame->left[i], "Mono capture must populate both FFT channels");
            }
            require (frame->sampleRate == 48000.0, "FIFO sample rate metadata is incorrect");
        }
    }
    queue->prepare (96000.0);
    juce::AudioBuffer<float> largeBlock (2, spectrum::fftSize * 10);
    largeBlock.clear();
    queue->push (largeBlock);
    int count = 0;
    while (queue->pop (*frame))
    {
        ++count;
        require (frame->sampleRate == 96000.0, "Sample rate change mixed capture frames");
    }
    require (count == 4, "FIFO must stay bounded when the editor cannot keep up");
    queue->push (largeBlock);
    require (queue->pop (*frame), "FIFO must recover after dropped frames");
    std::cout << "PASS FIFO ordering, mono, sample rate changes, overflow recovery\n";
}

void testResponseAndProcessor()
{
    for (const auto sampleRate : { 22050.0, 44100.0, 48000.0, 96000.0 })
        for (const auto frequency : TenBandEQAudioProcessor::bandFrequencies)
            for (const auto gain : { -12.0f, 0.0f, 12.0f })
            {
                const auto coefficients = eq::makePeak (sampleRate, frequency, gain);
                const auto centre = std::min (static_cast<double> (frequency), sampleRate * 0.45);
                require (std::abs (eq::responseDb (coefficients, centre, sampleRate) - gain) < 0.03f,
                         "Plotted band centre must match the audio filter's gain");
                for (double f = 20; f < sampleRate * 0.49; f *= 1.1)
                    require (std::isfinite (eq::responseDb (coefficients, f, sampleRate)), "Non-finite response");
            }

    auto processor = std::make_unique<TenBandEQAudioProcessor>();
    processor->setSpectrumEnabled (true);
    processor->prepareToPlay (51200.0, spectrum::fftSize);
    setParameter (*processor, "band6", 6.0f); // 1 kHz is exactly FFT bin 80 at this rate.
    setParameter (*processor, "output", -3.0f);
    juce::AudioBuffer<float> buffer (2, spectrum::fftSize);
    juce::MidiBuffer midi;
    auto frame = std::make_unique<spectrum::Frame>();
    auto analyzer = std::make_unique<spectrum::Analyzer>();
    for (int pass = 0; pass < 2; ++pass)
    {
        setParameter (*processor, "bypass", static_cast<float> (pass));
        for (int block = 0; block < 8; ++block)
        {
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                const auto value = 0.25f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 80 * i / spectrum::fftSize));
                buffer.setSample (0, i, value);
                buffer.setSample (1, i, -value);
            }
            processor->processBlock (buffer, midi);
            require (processor->popSpectrumFrame (*frame), "Processor must supply output spectrum, including bypass");
        }
        analyzer->analyze (*frame);
        const auto expected = -12.0412f + (pass == 0 ? 3.0f : 0.0f);
        require (std::abs (analyzer->getLevels()[80] - expected) < 0.08f,
                 "Spectrum must show actual post-EQ and post-trim output, or dry output when bypassed");
    }
    std::cout << "PASS filter response, processor output capture, trim and bypass\n";
}

void testOptimizations()
{
    auto processor = std::make_unique<TenBandEQAudioProcessor>();
    processor->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    auto frame = std::make_unique<spectrum::Frame>();
    const auto initialUpdates = processor->getCoefficientUpdateCount();
    for (int block = 0; block < 16; ++block)
    {
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 512; ++i) buffer.setSample (channel, i, 0.1f);
        processor->processBlock (buffer, midi);
        require (buffer.getSample (0, 200) == 0.1f, "Neutral EQ must remain a transparent pass-through");
    }
    require (processor->getCoefficientUpdateCount() == initialUpdates, "Unchanged bands must not rebuild coefficients");
    require (! processor->popSpectrumFrame (*frame), "Closed editor must not capture FFT data");
    setParameter (*processor, "band6", 9.0f);
    for (int block = 0; block < 4; ++block) { buffer.clear(); processor->processBlock (buffer, midi); }
    const auto settledUpdates = processor->getCoefficientUpdateCount();
    require (settledUpdates > initialUpdates && settledUpdates < initialUpdates + 20, "Only the changed band should interpolate");
    for (int block = 0; block < 16; ++block) processor->processBlock (buffer, midi);
    require (processor->getCoefficientUpdateCount() == settledUpdates, "Settled automation must reuse coefficients");
    std::cout << "PASS coefficient caching, neutral pass-through, smoothing and disabled analyzer\n";
}

void testFileExport()
{
    const auto folder = juce::File (TENBAND_EQ_TEST_OUTPUT_DIR);
    require (folder.createDirectory().wasOk(), "Cannot create test audio folder");
    const auto input = folder.getChildFile ("input.wav");
    const auto output = folder.getChildFile ("equalized.wav");
    {
        std::unique_ptr<juce::OutputStream> stream = input.createOutputStream();
        require (stream != nullptr, "Cannot create WAV fixture");
        stream->setPosition (0);
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (48000)
            .withNumChannels (1).withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        require (writer != nullptr, "Cannot create test WAV writer");
        juce::AudioBuffer<float> signal (1, 48000);
        for (int i = 0; i < signal.getNumSamples(); ++i)
            signal.setSample (0, i, 0.1f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * i / 48.0)));
        require (writer->writeFromAudioSampleBuffer (signal, 0, signal.getNumSamples()), "Cannot write test WAV");
    }
    juce::MemoryBlock original;
    input.loadFileAsData (original);
    auto processor = std::make_unique<TenBandEQAudioProcessor>();
    setParameter (*processor, "band6", 6.0f);
    juce::MemoryBlock state;
    processor->getStateInformation (state);
    std::atomic<bool> cancel { false };
    std::atomic<float> progress { 0.0f };
    require (fileaudio::render (input, output, state, cancel, progress).wasOk(), "WAV export failed");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (output));
    require (reader && reader->lengthInSamples == 48000 && reader->numChannels == 1 && reader->sampleRate == 48000,
             "Export must preserve duration, channel count and sample rate");
    juce::AudioBuffer<float> audio (1, 4800);
    require (reader->read (&audio, 0, 4800, 12000, true, false), "Cannot read export");
    const auto gain = juce::Decibels::gainToDecibels (audio.getRMSLevel (0, 0, 4800) / (0.1f / std::sqrt (2.0f)));
    require (std::abs (gain - 6.0f) < 0.06f, "Exported EQ response is incorrect");
    reader.reset();
    juce::MemoryBlock after;
    input.loadFileAsData (after);
    require (original == after, "Export changed the original source");
    require (fileaudio::render (input, input, state, cancel, progress).failed(), "Export must reject the original filename");
    juce::MemoryBlock completed;
    output.loadFileAsData (completed);
    cancel.store (true);
    require (fileaudio::render (input, output, state, cancel, progress).failed(), "Cancelled export must not succeed");
    after.reset();
    output.loadFileAsData (after);
    require (completed == after, "Cancelled export must preserve an existing destination");
    std::cout << "PASS WAV export, original preservation, output gain, metadata and cancellation\n";
}

void testMp3 (const juce::File& input)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (input));
    require (reader && reader->lengthInSamples > 0, "MP3 decoding failed");
    auto processor = std::make_unique<TenBandEQAudioProcessor>();
    juce::MemoryBlock state;
    processor->getStateInformation (state);
    std::atomic<bool> cancel { false };
    std::atomic<float> progress { 0.0f };
    const auto result = fileaudio::render (input,
        juce::File (TENBAND_EQ_TEST_OUTPUT_DIR).getChildFile ("mp3-equalized.wav"), state, cancel, progress);
    require (result.wasOk(), "MP3-to-WAV export failed");
    std::cout << "PASS MP3 decoding and equalized WAV export\n";
}

void benchmark()
{
    constexpr int blockSize = 512, iterations = 2000;
    juce::AudioBuffer<float> input (2, blockSize), buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < blockSize; ++i)
            input.setSample (channel, i, 0.1f * static_cast<float> (std::sin (i * 0.13)));
    for (const auto activeBands : { 0, 3, 10 })
    {
        auto processor = std::make_unique<TenBandEQAudioProcessor>();
        std::array<float, 10> gains {};
        for (int band = 0; band < activeBands; ++band)
        {
            gains[static_cast<size_t> (band)] = 3.0f;
            setParameter (*processor, TenBandEQAudioProcessor::bandParameterId (band), 3.0f);
        }
        processor->prepareToPlay (48000.0, blockSize);
        struct Filter { eq::Coefficients c; float z1 = 0, z2 = 0; };
        std::array<std::array<Filter, 10>, 2> filters {};
        auto start = juce::Time::getMillisecondCounterHiRes();
        volatile float sink = 0.0f;
        for (int block = 0; block < iterations; ++block)
        {
            for (size_t band = 0; band < 10; ++band)
            {
                const auto coefficients = eq::makePeak (48000, TenBandEQAudioProcessor::bandFrequencies[band], gains[band]);
                for (auto& channel : filters)
                    channel[band].c = coefficients;
            }
            for (int channel = 0; channel < 2; ++channel)
            {
                buffer.copyFrom (channel, 0, input, channel, 0, blockSize);
                auto* samples = buffer.getWritePointer (channel);
                for (int i = 0; i < blockSize; ++i)
                {
                    auto value = samples[i];
                    for (auto& filter : filters[static_cast<size_t> (channel)])
                    {
                        const auto output = filter.c.b0 * value + filter.z1;
                        filter.z1 = filter.c.b1 * value - filter.c.a1 * output + filter.z2;
                        filter.z2 = filter.c.b2 * value - filter.c.a2 * output;
                        value = output;
                    }
                    samples[i] = value;
                }
            }
            sink = buffer.getSample (0, 100);
        }
        const auto baseline = juce::Time::getMillisecondCounterHiRes() - start;
        start = juce::Time::getMillisecondCounterHiRes();
        for (int block = 0; block < iterations; ++block)
        {
            for (int channel = 0; channel < 2; ++channel) buffer.copyFrom (channel, 0, input, channel, 0, blockSize);
            processor->processBlock (buffer, midi);
            sink = buffer.getSample (0, 100);
        }
        const auto optimized = juce::Time::getMillisecondCounterHiRes() - start;
        juce::ignoreUnused (sink);
        std::cout << "BENCH " << activeBands << " active bands / 21.33s stereo: baseline " << baseline
                  << " ms; optimized " << optimized << " ms; speedup " << baseline / optimized << "x\n";
    }
}

void writePreview (const juce::File& destination, bool compact, bool light)
{
    auto processor = std::make_unique<TenBandEQAudioProcessor>();
    processor->prepareToPlay (48000.0, spectrum::fftSize);
    constexpr std::array<float, 10> gains { 0, 2, 3, -4, 7, 1, -7, 4, 2, 4 };
    for (int band = 0; band < TenBandEQAudioProcessor::bandCount; ++band)
        setParameter (*processor, TenBandEQAudioProcessor::bandParameterId (band), gains[static_cast<size_t> (band)]);
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());
    if (compact) editor->setSize (520, 620);
    static_cast<TenBandEQAudioProcessorEditor*> (editor.get())->setLightMode (light);
    // The analyzer deliberately sleeps without a window peer. An offscreen peer
    // lets this preview exercise the same timer path as the real editor.
    editor->setTopLeftPosition (-10000, -10000);
    editor->addToDesktop (juce::ComponentPeer::windowIsTemporary
                         | juce::ComponentPeer::windowIgnoresMouseClicks
                         | juce::ComponentPeer::windowIgnoresKeyPresses);
    editor->setVisible (true);
    juce::AudioBuffer<float> buffer (2, spectrum::fftSize);
    juce::MidiBuffer midi;
    juce::Random random (42);
    float lowpass = 0.0f;
    for (int block = 0; block < 45; ++block)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto noise = (random.nextFloat() - 0.5f) * 0.12f;
            lowpass += 0.12f * (noise - lowpass);
            const auto t = static_cast<double> (block * spectrum::fftSize + i) / 48000.0;
            const auto value = lowpass + 0.05f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 110.0 * t))
                                      + 0.02f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 440.0 * t));
            buffer.setSample (0, i, value);
            buffer.setSample (1, i, -value);
        }
        processor->processBlock (buffer, midi);
        juce::Thread::sleep (10);
        juce::Timer::callPendingTimersSynchronously();
    }
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    juce::FileOutputStream stream (destination);
    require (stream.openedOk(), "Cannot write preview");
    stream.setPosition (0);
    stream.truncate();
    require (juce::PNGImageFormat().writeImageToStream (image, stream), "Cannot encode preview");
    std::cout << "Wrote UI preview: " << destination.getFullPathName() << '\n';
}
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialise;
    try
    {
        testSpectrum();
        testQueue();
        testResponseAndProcessor();
        testOptimizations();
        testFileExport();
        if (argc >= 3 && juce::String (argv[1]) == "--preview")
        {
            const juce::StringArray args (argv, argc);
            writePreview (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), args.contains ("--compact"), args.contains ("--light"));
        }
        if (argc == 3 && juce::String (argv[1]) == "--mp3")
            testMp3 (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        if (argc == 2 && juce::String (argv[1]) == "--benchmark") benchmark();
        if (argc == 2 && juce::String (argv[1]) == "--devices")
            for (const auto& endpoint : SystemAudioSource::getEndpoints()) std::cout << endpoint.name << '\n';
        if (argc == 2 && juce::String (argv[1]) == "--capture-smoke")
        {
            const auto endpoints = SystemAudioSource::getEndpoints();
            require (! endpoints.empty(), "No Windows playback endpoints available for the capture smoke test");
            auto source = std::make_unique<SystemAudioSource>();
            const auto result = source->startCapture (endpoints.front().id);
            if (result.failed()) throw std::runtime_error (result.getErrorMessage().toStdString());
            source->prepareToPlay (512, 48000.0);
            juce::AudioBuffer<float> scratch (2, 512);
            for (int i = 0; i < 20; ++i)
            {
                juce::Thread::sleep (15);
                source->getNextAudioBlock ({ &scratch, 0, 512 });
                require (std::isfinite (scratch.getRMSLevel (0, 0, 512)), "Capture produced invalid audio");
            }
            source->stopCapture();
            require (source->getError().isEmpty(), "Capture failed while running");
            std::cout << "PASS Windows loopback open/read/close on " << endpoints.front().name << " (no playback or recording)\n";
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
