#pragma once

#include <juce_core/juce_core.h>

namespace crush
{
namespace ParamIDs
{
    inline constexpr const char* excite    = "excite";
    inline constexpr const char* frequency = "frequency";
    inline constexpr const char* bypass    = "bypass";
}

/** High-pass corners of the exciter. 7 kHz is the original; the rest are Crush extensions. */
inline constexpr float kFrequencies[] = { 7000.0f, 10000.0f, 12000.0f, 15000.0f };
inline constexpr int kNumFrequencies = 4;

inline juce::StringArray getFrequencyNames()   { return { "7 kHz", "10 kHz", "12 kHz", "15 kHz" }; }

inline constexpr float kDefaultExcite = 50.0f;

/** Knob 0..100 (whole steps) -> normalised 0..1 amount, as the original's % display. */
inline float exciteToAmount (float knob)        { return juce::jlimit (0.0f, 100.0f, knob) * 0.01f; }

inline juce::String formatKnobValue (double v)  { return juce::String (juce::roundToInt (v)); }

inline juce::String describeExcite (float v)
{
    if (v < 0.5f)
        return "off (dry)";
    if (v > 99.5f)
        return "100 % excited";
    return juce::String (juce::roundToInt (v)) + " % excited / " + juce::String (juce::roundToInt (100.0f - v)) + " % dry";
}
} // namespace crush
