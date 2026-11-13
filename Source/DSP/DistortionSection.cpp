#include "DistortionSection.h"

#include <cmath>
#include <initializer_list>

namespace crush
{

//==============================================================================
// ParamRamp
//==============================================================================
void ParamRamp::setTarget (float v) noexcept
{
    if (v == target && current == target)
        return;
    start = current;
    target = v;
    counter = 0;
}

float ParamRamp::read (int n) noexcept
{
    if (current != target)
    {
        counter += n;
        // rampSamples == 0: counter / 0 = +inf, clamped to 1 (jump immediately).
        float t = rampSamples > 0 ? (float) counter / (float) rampSamples : 1.0f;
        if (1.0f < t)
            t = 1.0f;
        current = (1.0f - t) * start + target * t;
    }
    return current;
}

//==============================================================================
// Biquad
//==============================================================================
void Biquad::designHighPass (float sampleRate, float f0, float bandwidthOctaves) noexcept
{
    const double w0 = ((double) f0 * 6.283185307179586) / (double) sampleRate;
    const double sn = std::sin (w0);
    const double cs = std::cos (w0);

    // ln(2)/2 = 0.34657359027997264
    const double alpha = std::sinh (((double) bandwidthOctaves * 0.34657359027997264 * w0) / sn) * sn;
    const double a0 = alpha + 1.0;

    const float nb0 = (float) (((cs + 1.0) * 0.5) / a0);
    const float na2 = (float) ((1.0 - alpha) / a0);
    const float na1 = (float) ((cs * -2.0) / a0);
    const float nb1 = (float) ((-(cs + 1.0)) / a0);

    auto flush = [] (float c) { return (c > -1.0e-15f && c < 1.0e-15f) ? 0.0f : c; };
    b0 = flush (nb0);
    b1 = flush (nb1);
    b2 = flush (nb0);
    a1 = flush (na1);
    a2 = flush (na2);
}

//==============================================================================
// EnvelopeFollower
//==============================================================================
EnvelopeFollower::EnvelopeFollower() noexcept
{
    env = 0.0f;
    attackStep  = 1.0f / (sampleRate * attackTime);
    releaseStep = 1.0f / (sampleRate * releaseTime);
}

void EnvelopeFollower::setSampleRate (float fs) noexcept
{
    sampleRate = fs;
    attackStep  = attackTime  != 0.0f ? 1.0f / (attackTime  * fs) : 0.0f;
    releaseStep = releaseTime != 0.0f ? 1.0f / (releaseTime * fs) : 0.0f;
    env = 0.0f;
}

//==============================================================================
// Exciter
//==============================================================================
Exciter::Exciter() noexcept
{
    design();
}

void Exciter::design() noexcept
{
    hpL.designHighPass (sampleRate, frequency, kBandwidth);
    hpR.designHighPass (sampleRate, frequency, kBandwidth);
}

void Exciter::setSampleRate (float fs) noexcept
{
    sampleRate = fs;
    envelope.setSampleRate (fs);
    lastGain = 0.0f;
    envelope.reset();
    hpL.reset();
    hpR.reset();
    design();
}

void Exciter::reset() noexcept
{
    lastGain = 0.0f;
    envelope.reset();
    hpL.reset();
    hpR.reset();
    design();
}

void Exciter::setFrequency (float hz) noexcept
{
    // Keep the corner safely below Nyquist at low sample rates.
    const float maxHz = sampleRate * 0.45f;
    const float f = hz < maxHz ? hz : maxHz;
    if (f == frequency)
        return;
    frequency = f;
    design();
}

void Exciter::process (float* left, float* right, int numSamples, float amount) noexcept
{
    hpL.guardDenormals();
    hpR.guardDenormals();

    float g = lastGain;
    for (int i = 0; i < numSamples; ++i)
    {
        const float xL = left[i];
        const float xR = right[i];

        const float hL = hpL.process (xL);
        const float hR = hpR.process (xR);

        const float e = envelope.process ((hR + hL) * 300.0f * 0.5f);
        g = e * 10.0f;
        if (3.0f < g)
            g = 3.0f;

        float wL = hL * g + xL;
        float wR = hR * g + xR;
        if (wL <= 0.999f) { if (wL < -0.999f) wL = -0.999f; } else wL = 0.999f;
        if (wR <= 0.999f) { if (wR < -0.999f) wR = -0.999f; } else wR = 0.999f;

        left[i]  = (1.0f - amount) * xL + amount * wL;
        right[i] = (1.0f - amount) * xR + amount * wR;
    }
    lastGain = g;
}

//==============================================================================
// BitCrusher / Mech / Tube
//==============================================================================
int bitCrusherBits (float p) noexcept
{
    return (int) ((1.0f - p) * 15.0f + 1.0f);
}

void processBitCrusher (float* x, int numSamples, float p, bool useOldBitCrush) noexcept
{
    const int bits = bitCrusherBits (p);
    const float q = (float) (1 << (bits & 31));
    const float inv = 1.0f / q;

    if (! useOldBitCrush)
    {
        for (int i = 0; i < numSamples; ++i)
            x[i] = std::floor (q * x[i] + 0.5f) * inv;
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
            x[i] = (float) (int) (q * x[i] + 0.5f) * inv;
    }
}

float mechSample (float x, float k, float s) noexcept
{
    float v = k * x;
    if (v <= 0.75f)
    {
        if (v < -0.75f)
            v = -(((-v) - 0.75f) * s + 0.75f);
    }
    else
    {
        v = (v - 0.75f) * s + 0.75f;
    }
    return v;
}

void processMech (float* x, int numSamples, float p) noexcept
{
    const float k = p * 10.0f + 1.0f;
    const float s = 0.25f / (k - 0.75f);
    for (int i = 0; i < numSamples; ++i)
        x[i] = mechSample (x[i], k, s);
}

float tubeSample (float x, float d) noexcept
{
    float v = d * x;
    if (v <= 0.6f)
    {
        if (v < -0.6f)
        {
            const float t = (1.0f - ((-v) - 0.6f) / (d - 0.6f)) * 0.707107f;
            v = -((0.5f - t * t) * 0.79999995f + 0.6f);
        }
    }
    else
    {
        const float t = (1.0f - (v - 0.6f) / (d - 0.6f)) * 0.707107f;
        v = (0.5f - t * t) * 0.79999995f + 0.6f;
    }
    return v;
}

void processTube (float* x, int numSamples, float p) noexcept
{
    const float d = p * 4.0f + 1.0f;
    for (int i = 0; i < numSamples; ++i)
        x[i] = tubeSample (x[i], d);
}

//==============================================================================
// DistortionSection
//==============================================================================
DistortionSection::DistortionSection() noexcept
{
    distOn.setImmediate (1.0f);
}

void DistortionSection::setSampleRate (double fs) noexcept
{
    exciter.setSampleRate ((float) fs);
}

void DistortionSection::reset() noexcept
{
    exciter.reset();
}

void DistortionSection::setRampSamples (int samples) noexcept
{
    for (auto* r : { &distOn, &bitCrusher, &mech, &tube, &excite })
        r->rampSamples = samples;
}

void DistortionSection::process (float* left, float* right, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    // Enables: read once per host block (advancing the ramps by the whole block).
    const bool on = 0.5f < distOn.read (numSamples);
    const bool exciteOn = on && 0.0f < excite.read (numSamples);
    const bool crushOn  = on && 0.0f < bitCrusher.read (numSamples);
    const bool mechOn   = on && 0.0f < mech.read (numSamples);
    const bool tubeOn   = on && 0.0f < tube.read (numSamples);

    for (int pos = 0; pos < numSamples; )
    {
        const int n = (numSamples - pos) < kSubBlock ? (numSamples - pos) : kSubBlock;
        float* l = left + pos;
        float* r = right + pos;

        if (exciteOn)
            exciter.process (l, r, n, excite.read (n));

        if (crushOn)
        {
            const float p = bitCrusher.read (n);
            processBitCrusher (l, n, p, useOldBitCrush);
            processBitCrusher (r, n, p, useOldBitCrush);
        }

        if (mechOn)
        {
            const float p = mech.read (n);
            processMech (l, n, p);
            processMech (r, n, p);
        }

        if (chainOrder == ChainOrder::Original && insert != nullptr)
            insert (insertContext, l, r, n);

        if (tubeOn)
        {
            const float p = tube.read (n);
            processTube (l, n, p);
            processTube (r, n, p);
        }

        if (outputHardClip)
        {
            for (int i = 0; i < n; ++i)
            {
                float a = l[i], b = r[i];
                if (a <= 0.9999f) { if (a < -0.9999f) a = -0.9999f; } else a = 0.9999f;
                if (b <= 0.9999f) { if (b < -0.9999f) b = -0.9999f; } else b = 0.9999f;
                l[i] = a;
                r[i] = b;
            }
        }

        pos += n;
    }
}

} // namespace crush
