#pragma once

// Distortion section (Excite, BitCrusher, Mech, Tube) of a legacy multi-effect.
// Written from the reverse-engineered specification; every constant is kept
// exactly as it appears in the shipped binary.
//
// Rules for a bit-exact match:
//   * 32-bit float for all audio-path state and arithmetic.
//   * double only for the biquad coefficient design (then cast to float).
//   * No FMA contraction / fast-math (-ffp-contract=off, MSVC /fp:precise).
//
// No framework dependencies.

#include <cstdint>

namespace crush
{

//==============================================================================
/** Linear parameter ramp, advanced once per read by the number of samples the
    read covers. rampSamples == 0 makes a new target take effect immediately. */
struct ParamRamp
{
    float current = 0.0f, start = 0.0f, target = 0.0f;
    int counter = 0;
    int rampSamples = 0;

    void setImmediate (float v) noexcept   { current = start = target = v; counter = 0; }
    void setTarget (float v) noexcept;

    /** Advances the ramp by n samples (only while it is moving) and returns the value. */
    float read (int n) noexcept;
};

//==============================================================================
/** RBJ high-pass biquad, bandwidth form. Coefficients are designed in double
    and stored as float; the filter runs in Direct Form I in float. */
struct Biquad
{
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f, x1 = 0.0f, x2 = 0.0f;

    void designHighPass (float sampleRate, float f0, float bandwidthOctaves) noexcept;
    void reset() noexcept                  { y1 = y2 = x1 = x2 = 0.0f; }

    /** Called once per sub-block, before processing. */
    void guardDenormals() noexcept
    {
        if (y1 > -1.0e-15f && y1 < 1.0e-15f) { y1 = 0.0f; y2 = 0.0f; }
    }

    float process (float x) noexcept
    {
        const float px1 = x1, px2 = x2, py1 = y1, py2 = y2;
        x1 = x;  x2 = px1;
        y2 = py1;
        const float y = ((px1 * b1 + x * b0 + b2 * px2) - py1 * a1) - a2 * py2;
        y1 = y;
        return y;
    }
};

//==============================================================================
/** Linear-slew peak detector: linear release, rate-limited linear attack. */
struct EnvelopeFollower
{
    float sampleRate  = 44100.0f;
    float attackTime  = 0.01f;    // seconds
    float releaseTime = 0.1f;     // seconds
    float env         = 0.0f;
    float attackStep  = 0.0f;
    float releaseStep = 0.0f;

    EnvelopeFollower() noexcept;
    void setSampleRate (float fs) noexcept;   // recomputes steps, env = 0
    void reset() noexcept                     { env = 0.0f; }

    float process (float in) noexcept
    {
        float f = env - releaseStep;
        float d = 0.0f;
        if      (in >  f) d =  in - f;
        else if (in < -f) d = -in - f;
        if (attackStep < d) d = attackStep;
        f = f + d;
        if (0.0f <= f) { env = f; return f; }
        env = 0.0f;
        return 0.0f;
    }
};

//==============================================================================
/** Dynamic high-frequency exciter. */
class Exciter
{
public:
    static constexpr float kOriginalFrequency = 7000.0f;
    static constexpr float kBandwidth         = 1.0f;

    Exciter() noexcept;

    void setSampleRate (float fs) noexcept;   // also resets
    void reset() noexcept;

    /** Moves the high-pass corner. 7000 Hz is the original; any other value is a
        Crush extension. Filter state is kept so the change does not click. */
    void setFrequency (float hz) noexcept;
    float getFrequency() const noexcept       { return frequency; }

    /** amount = wet/dry crossfade 0..1. Processes left and right in place. */
    void process (float* left, float* right, int numSamples, float amount) noexcept;

    /** Excite gain (0..3) reached at the end of the last call, for metering. */
    float getLastGain() const noexcept        { return lastGain; }

    Biquad hpL, hpR;
    EnvelopeFollower envelope;

private:
    void design() noexcept;

    float sampleRate = 44100.0f;
    float frequency = kOriginalFrequency;
    float lastGain = 0.0f;
};

//==============================================================================
// Stateless stages. p is the stage's 0..1 parameter for this sub-block.
void processBitCrusher (float* x, int numSamples, float p, bool useOldBitCrush) noexcept;
void processMech (float* x, int numSamples, float p) noexcept;
void processTube (float* x, int numSamples, float p) noexcept;

int   bitCrusherBits (float p) noexcept;
float mechSample (float x, float k, float s) noexcept;
float tubeSample (float x, float d) noexcept;

//==============================================================================
/** The whole distortion section, processed in sub-blocks of at most 64 samples. */
class DistortionSection
{
public:
    static constexpr int kSubBlock = 64;

    enum class ChainOrder
    {
        Original,     // Excite -> BitCrusher -> Mech -> [insert] -> Tube
        Contiguous    // Excite -> BitCrusher -> Mech -> Tube
    };

    /** External processing for ChainOrder::Original (filter, flanger in the full plugin). */
    using InsertCallback = void (*) (void* context, float* left, float* right, int numSamples);

    // Parameters, normalised 0..1 (index in the original plugin in brackets).
    ParamRamp distOn;       // [7]  > 0.5 = on
    ParamRamp bitCrusher;   // [8]
    ParamRamp mech;         // [9]
    ParamRamp tube;         // [10]
    ParamRamp excite;       // [11]

    ChainOrder chainOrder   = ChainOrder::Contiguous;
    bool useOldBitCrush     = false;   // legacy int-truncation quantiser
    bool outputHardClip     = true;    // final +-0.9999 clip of the full plugin

    InsertCallback insert   = nullptr;
    void* insertContext     = nullptr;

    Exciter exciter;

    DistortionSection() noexcept;

    void setSampleRate (double fs) noexcept;
    void reset() noexcept;
    void setRampSamples (int samples) noexcept;

    /** Processes one host block in place. Stage enables are latched once per call
        (as in the binary); stage amounts once per 64-sample sub-block. */
    void process (float* left, float* right, int numSamples) noexcept;
};

} // namespace crush
