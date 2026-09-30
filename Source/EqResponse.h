#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

// Shared by the audio processor and the plotted response so both use the same filters.
namespace eq
{
struct Coefficients
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
};

inline Coefficients makePeak (double sampleRate, double frequency, double gainDb) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double q = 1.4142135623730951;
    const auto omega = 2.0 * pi * std::min (frequency, sampleRate * 0.45) / sampleRate;
    const auto alpha = std::sin (omega) / (2.0 * q);
    const auto cosine = std::cos (omega);
    const auto amplitude = std::pow (10.0, gainDb / 40.0);
    const auto a0 = 1.0 + alpha / amplitude;
    const auto b1 = static_cast<float> (-2.0 * cosine / a0);
    return { static_cast<float> ((1.0 + alpha * amplitude) / a0), b1,
             static_cast<float> ((1.0 - alpha * amplitude) / a0), b1,
             static_cast<float> ((1.0 - alpha / amplitude) / a0) };
}

inline float responseDb (const Coefficients& c, double frequency, double sampleRate) noexcept
{
    constexpr double pi = 3.14159265358979323846;
    const auto z = std::polar (1.0, -2.0 * pi * frequency / sampleRate);
    const auto numerator = static_cast<double> (c.b0) + static_cast<double> (c.b1) * z
                         + static_cast<double> (c.b2) * z * z;
    const auto denominator = 1.0 + static_cast<double> (c.a1) * z
                           + static_cast<double> (c.a2) * z * z;
    return static_cast<float> (20.0 * std::log10 (std::max (1.0e-12, std::abs (numerator / denominator))));
}
}
