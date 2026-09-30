#pragma once
#include "PluginProcessor.h"

namespace fileaudio
{
// Runs on a worker thread. The source is never modified. Output uses float WAV
// to preserve headroom rather than silently clipping EQ boosts into integer PCM.
juce::Result render (const juce::File& source, const juce::File& destination,
                     const juce::MemoryBlock& state, std::atomic<bool>& cancelled,
                     std::atomic<float>& progress);

class ExportJob final : public juce::Thread
{
public:
    ExportJob (juce::File input, juce::File output, juce::MemoryBlock settings)
        : Thread ("EQ file export"), source (std::move (input)), destination (std::move (output)), state (std::move (settings)) {}
    ~ExportJob() override { cancelled.store (true); waitForThreadToExit (-1); }
    void run() override { result = render (source, destination, state, cancelled, progress); }
    std::atomic<bool> cancelled { false };
    std::atomic<float> progress { 0.0f };
    // Read only after isThreadRunning() returns false.
    juce::Result result = juce::Result::ok();
private:
    juce::File source, destination;
    juce::MemoryBlock state;
};
}
