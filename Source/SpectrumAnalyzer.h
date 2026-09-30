#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <algorithm>
#include <cmath>

namespace spectrum
{
constexpr int fftOrder = 12;
constexpr int fftSize = 1 << fftOrder;
constexpr int binCount = fftSize / 2 + 1;
constexpr float floorDb = -90.0f;

struct Frame
{
    std::array<float, fftSize> left {}, right {};
    double sampleRate = 44100.0;
};

// One producer (audio thread), one consumer (editor). Storage is allocated once.
// A slow/closed editor drops visualization frames without blocking the audio thread.
class FrameQueue
{
public:
    void prepare (double sampleRate) noexcept
    {
        pending.sampleRate = sampleRate;
        used = 0;
        // Do not reset the FIFO here: the editor may be reading during a device change.
    }

    void push (const juce::AudioBuffer<float>& buffer) noexcept
    {
        if (buffer.getNumChannels() == 0)
            return;

        const auto* left = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);
        int offset = 0;
        while (offset < buffer.getNumSamples())
        {
            const auto count = std::min (fftSize - used, buffer.getNumSamples() - offset);
            std::copy_n (left + offset, count, pending.left.data() + used);
            std::copy_n (right + offset, count, pending.right.data() + used);
            offset += count;
            used += count;
            if (used == fftSize)
            {
                const auto write = fifo.write (1);
                if (write.blockSize1 > 0)
                    frames[static_cast<size_t> (write.startIndex1)] = pending;
                used = 0;
            }
        }
    }

    bool pop (Frame& destination) noexcept
    {
        const auto read = fifo.read (1);
        if (read.blockSize1 == 0)
            return false;
        destination = frames[static_cast<size_t> (read.startIndex1)];
        return true;
    }

private:
    juce::AbstractFifo fifo { 5 };
    std::array<Frame, 5> frames {};
    Frame pending;
    int used = 0;
};

class Analyzer
{
public:
    Analyzer() { levels.fill (floorDb); }

    void analyze (const Frame& frame)
    {
        left.fill (0.0f);
        right.fill (0.0f);
        for (size_t i = 0; i < fftSize; ++i)
        {
            left[i] = std::isfinite (frame.left[i]) ? frame.left[i] : 0.0f;
            right[i] = std::isfinite (frame.right[i]) ? frame.right[i] : 0.0f;
        }
        window.multiplyWithWindowingTable (left.data(), fftSize);
        window.multiplyWithWindowingTable (right.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (left.data(), true);
        fft.performFrequencyOnlyForwardTransform (right.data(), true);

        for (size_t bin = 0; bin < binCount; ++bin)
        {
            // Power averaging retains opposite-polarity stereo signals. The normalized
            // Hann window makes a bin-centred, full-scale sine read approximately 0 dBFS.
            const auto scale = (bin == 0 || bin == fftSize / 2) ? 1.0f / fftSize : 2.0f / fftSize;
            const auto amplitude = std::hypot (left[bin], right[bin]) * scale / std::sqrt (2.0f);
            levels[bin] = juce::Decibels::gainToDecibels (amplitude, floorDb);
        }
    }

    const std::array<float, binCount>& getLevels() const noexcept { return levels; }

private:
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann, true };
    std::array<float, 2 * fftSize> left {}, right {};
    std::array<float, binCount> levels {};
};
}
