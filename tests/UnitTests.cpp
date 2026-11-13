// Unit tests for each stage of the distortion section. No framework needed:
// returns non-zero on failure.

#include "../Source/DSP/DistortionSection.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
int failures = 0, checks = 0;

void check (bool ok, const char* what)
{
    ++checks;
    if (! ok) { ++failures; std::printf ("FAIL: %s\n", what); }
}

void checkNear (double a, double b, double tol, const char* what)
{
    ++checks;
    if (! (std::abs (a - b) <= tol)) { ++failures; std::printf ("FAIL: %s  got %.12g expected %.12g\n", what, a, b); }
}

//==============================================================================
// Independent reference design in long double, rounded to float.
void referenceDesign (double fs, double f0, double bw, float c[5])
{
    const long double pi = 3.14159265358979323846264338327950288L;
    const long double w0 = 2.0L * pi * f0 / fs;
    const long double sn = std::sin (w0), cs = std::cos (w0);
    const long double alpha = sn * std::sinh (std::log (2.0L) / 2.0L * bw * w0 / sn);
    const long double a0 = 1.0L + alpha;
    c[0] = (float) ((1.0L + cs) * 0.5L / a0);
    c[1] = (float) (-(1.0L + cs) / a0);
    c[2] = c[0];
    c[3] = (float) (-2.0L * cs / a0);
    c[4] = (float) ((1.0L - alpha) / a0);
}

void testBiquadCoefficients()
{
    for (double fs : { 44100.0, 48000.0 })
    {
        crush::Biquad b;
        b.designHighPass ((float) fs, 7000.0f, 1.0f);
        float ref[5];
        referenceDesign (fs, 7000.0, 1.0, ref);
        const float got[5] = { b.b0, b.b1, b.b2, b.a1, b.a2 };
        for (int i = 0; i < 5; ++i)
        {
            char msg[96];
            std::snprintf (msg, sizeof (msg), "biquad coef %d at %.0f Hz (within 1 float ulp)", i, fs);
            checkNear (got[i], ref[i], std::abs (ref[i]) * 1.2e-7, msg);
        }
        check (b.b2 == b.b0, "b2 == b0");

        // High-pass: zero gain at DC, unity at Nyquist.
        const double dc = (b.b0 + b.b1 + b.b2) / (1.0 + b.a1 + b.a2);
        const double ny = (b.b0 - b.b1 + b.b2) / (1.0 - b.a1 + b.a2);
        checkNear (dc, 0.0, 1.0e-6, "biquad DC gain = 0");
        checkNear (ny, 1.0, 1.0e-5, "biquad Nyquist gain = 1");
    }

    // Exact float values at 44.1 kHz (regression lock).
    crush::Biquad b;
    b.designHighPass (44100.0f, 7000.0f, 1.0f);
    std::printf ("44.1k coefs: b0=%.9g b1=%.9g b2=%.9g a1=%.9g a2=%.9g\n", b.b0, b.b1, b.b2, b.a1, b.a2);
    b.designHighPass (48000.0f, 7000.0f, 1.0f);
    std::printf ("48k   coefs: b0=%.9g b1=%.9g b2=%.9g a1=%.9g a2=%.9g\n", b.b0, b.b1, b.b2, b.a1, b.a2);
}

void testBiquadProcessing()
{
    crush::Biquad b;
    b.designHighPass (44100.0f, 7000.0f, 1.0f);
    // impulse response starts with b0, then b1 - a1*b0
    const float h0 = b.process (1.0f);
    const float h1 = b.process (0.0f);
    check (h0 == b.b0, "impulse h[0] == b0");
    const float expected = ((1.0f * b.b1 + 0.0f * b.b0 + b.b2 * 0.0f) - h0 * b.a1) - b.a2 * 0.0f;
    check (h1 == expected, "impulse h[1] matches evaluation order");

    b.y1 = 1.0e-16f; b.y2 = 0.5f;
    b.guardDenormals();
    check (b.y1 == 0.0f && b.y2 == 0.0f, "denormal guard clears y1 and y2");
    b.y1 = 1.0e-3f; b.y2 = 0.5f;
    b.guardDenormals();
    check (b.y1 == 1.0e-3f && b.y2 == 0.5f, "denormal guard leaves normal values");
}

void testEnvelope()
{
    crush::EnvelopeFollower e;
    e.setSampleRate (44100.0f);
    check (e.attackStep == 1.0f / (0.01f * 44100.0f), "attack step = 1/(0.01 fs)");
    check (e.releaseStep == 1.0f / (0.1f * 44100.0f), "release step = 1/(0.1 fs)");

    // Step: each sample decays by releaseStep, then rises by at most attackStep,
    // so the net rise is attackStep - releaseStep per sample.
    int samplesToCap = -1;
    float prev = 0.0f;
    for (int i = 0; i < 1000; ++i)
    {
        const float v = e.process (0.5f);
        if (i < 100)
            checkNear (v - prev, e.attackStep - e.releaseStep, 1.0e-6, "attack slope = attackStep - releaseStep");
        if (samplesToCap < 0 && v * 10.0f >= 3.0f) samplesToCap = i + 1;
        prev = v;
    }
    std::printf ("envelope gain cap (0.3) reached after %d samples at 44.1 kHz\n", samplesToCap);
    const int expectedCap = (int) std::ceil (0.3 / ((double) e.attackStep - (double) e.releaseStep));
    check (std::abs (samplesToCap - expectedCap) <= 1, "gain cap after 0.3 / (attackStep - releaseStep) samples");
    checkNear (prev, 0.5, 1.0e-6, "envelope settles on the input level");

    // Release: linear decay of releaseStep per sample down to 0.
    const float start = e.env;
    const float v1 = e.process (0.0f);
    checkNear (start - v1, e.releaseStep, 1.0e-7, "release slope");
    int toZero = 1;
    while (e.process (0.0f) > 0.0f && toZero < 100000) ++toZero;
    std::printf ("release from 0.5 to 0 took %d samples\n", toZero + 1);
    check (std::abs ((toZero + 1) - 0.5 / e.releaseStep) < 3.0, "release time = level / releaseStep");
    check (e.env == 0.0f, "envelope clamps at 0");

    // Negative input is rectified.
    crush::EnvelopeFollower n;
    n.setSampleRate (44100.0f);
    check (n.process (-0.5f) == -n.releaseStep + n.attackStep, "negative input rectified");

    // Sample-rate change resets.
    n.setSampleRate (48000.0f);
    check (n.env == 0.0f, "sample-rate change clears env");
}

void testExciteMix()
{
    // amount 0 is a pure pass-through (wet path still runs).
    crush::Exciter ex;
    ex.setSampleRate (44100.0f);
    std::vector<float> l (256), r (256), l0, r0;
    for (int i = 0; i < 256; ++i) { l[i] = 0.4f * std::sin (0.9f * i); r[i] = 0.3f * std::sin (1.3f * i); }
    l0 = l; r0 = r;
    ex.process (l.data(), r.data(), 256, 0.0f);
    bool same = true;
    for (int i = 0; i < 256; ++i) same &= (l[i] == l0[i] && r[i] == r0[i]);
    check (same, "excite amount 0 = dry");

    // Wet clip at +-0.999.
    crush::Exciter ex2;
    ex2.setSampleRate (44100.0f);
    std::vector<float> a (2000), b (2000);
    for (int i = 0; i < 2000; ++i) a[i] = b[i] = (i & 1) ? 0.95f : -0.95f;
    ex2.process (a.data(), b.data(), 2000, 1.0f);
    float peak = 0.0f;
    for (float v : a) peak = std::max (peak, std::abs (v));
    check (peak <= 0.999f, "wet path clipped at 0.999");
    checkNear (ex2.getLastGain(), 3.0, 1.0e-6, "gain reaches cap of 3");
}

void testBitCrusher()
{
    check (crush::bitCrusherBits (0.0f) == 16, "p=0 -> 16 bits");
    check (crush::bitCrusherBits (1.0f) == 1, "p=1 -> 1 bit");
    check (crush::bitCrusherBits (0.5f) == 8, "p=0.5 -> 8 bits");

    float x[6] = { 0.3f, -0.3f, 0.26f, -0.26f, -0.25f, 0.74f };
    crush::processBitCrusher (x, 6, 1.0f, false);   // q = 2: steps of 0.5
    check (x[0] == 0.5f && x[1] == -0.5f, "1 bit floor rounding +-0.3");
    check (x[2] == 0.5f && x[3] == -0.5f, "1 bit floor rounding +-0.26");
    check (x[4] == 0.0f, "floor(-0.5+0.5) = 0");
    check (x[5] == 0.5f, "0.74 -> 0.5");

    float y[3] = { -0.3f, -0.26f, 0.3f };
    crush::processBitCrusher (y, 3, 1.0f, true);    // truncation toward zero
    check (y[0] == 0.0f, "legacy: (int)(-0.1) = 0");
    check (y[1] == 0.0f, "legacy: (int)(-0.02) = 0");
    check (y[2] == 0.5f, "legacy: positive rounds as default");

    float z = 0.123456f;
    crush::processBitCrusher (&z, 1, 0.0f, false);
    check (z == std::floor (65536.0f * 0.123456f + 0.5f) / 65536.0f, "16-bit quantisation");
}

void testMech()
{
    for (float p : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const float k = p * 10.0f + 1.0f, s = 0.25f / (k - 0.75f);
        checkNear (crush::mechSample (1.0f, k, s), 1.0, 1.0e-6, "mech(1) = 1");
        checkNear (crush::mechSample (-1.0f, k, s), -1.0, 1.0e-6, "mech(-1) = -1");
        const float knee = 0.75f / k;
        checkNear (crush::mechSample (knee * 0.5f, k, s), k * knee * 0.5f, 1.0e-7, "mech linear below knee");
        // slope above the knee
        const float a = crush::mechSample (0.9f, k, s), b = crush::mechSample (0.8f, k, s);
        if (k > 0.75f / 0.8f) checkNear ((a - b) / 0.1f, k * s, 1.0e-4, "mech slope above knee = k * s");
    }
}

void testTube()
{
    for (float p : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
    {
        const float d = p * 4.0f + 1.0f;
        float one = 1.0f, minusOne = -1.0f;
        crush::processTube (&one, 1, p);
        crush::processTube (&minusOne, 1, p);
        checkNear (one, 1.0, 2.0e-6, "tube(1) = 1");
        checkNear (minusOne, -1.0, 2.0e-6, "tube(-1) = -1");
        const float lin = 0.5f * 0.6f / d;
        checkNear (crush::tubeSample (lin, d), d * lin, 1.0e-7, "tube linear below knee");
        // continuity at the knee
        const float knee = 0.6f / d;
        checkNear (crush::tubeSample (knee * 1.0001f, d), crush::tubeSample (knee * 0.9999f, d), 1.0e-3, "tube continuous at knee");
    }
    // fold-back above |x| = 1
    check (crush::tubeSample (1.5f, 5.0f) < 1.0f, "tube folds back above 1");
}

void testSubBlocksAndEnables()
{
    // Processing a block of 1000 must equal processing it as 64-sample sub-blocks
    // with the same host blocking (state continuity).
    crush::DistortionSection a, b;
    for (auto* d : { &a, &b })
    {
        d->setSampleRate (44100.0);
        d->excite.setImmediate (0.7f);
        d->tube.setImmediate (0.4f);
        d->mech.setImmediate (0.2f);
        d->bitCrusher.setImmediate (0.3f);
    }
    std::vector<float> l1 (1000), r1 (1000);
    for (int i = 0; i < 1000; ++i) { l1[i] = 0.5f * std::sin (0.21f * i); r1[i] = 0.5f * std::sin (0.37f * i); }
    auto l2 = l1, r2 = r1;
    a.process (l1.data(), r1.data(), 1000);
    for (int pos = 0; pos < 1000; pos += 64)
        b.process (l2.data() + pos, r2.data() + pos, std::min (64, 1000 - pos));
    bool same = true;
    for (int i = 0; i < 1000; ++i) same &= (l1[i] == l2[i] && r1[i] == r2[i]);
    check (same, "64-sample sub-blocking is transparent for static parameters");

    // DistOn off: only the output clip remains.
    crush::DistortionSection c;
    c.setSampleRate (44100.0);
    c.distOn.setImmediate (0.0f);
    c.excite.setImmediate (1.0f);
    float L[3] = { 0.5f, 1.2f, -1.5f }, R[3] = { 0.1f, 0.2f, 0.3f };
    c.process (L, R, 3);
    check (L[0] == 0.5f && L[1] == 0.9999f && L[2] == -0.9999f && R[0] == 0.1f, "DistOn off + output clip");

    // Ramp: rampSamples 0 jumps; otherwise linear per read.
    crush::ParamRamp ramp;
    ramp.setImmediate (0.0f);
    ramp.setTarget (1.0f);
    check (ramp.read (64) == 1.0f, "ramp with 0 samples jumps");
    ramp.rampSamples = 128;
    ramp.setTarget (0.0f);
    checkNear (ramp.read (64), 0.5, 1.0e-7, "ramp halfway");
    checkNear (ramp.read (64), 0.0, 1.0e-7, "ramp done");
}
} // namespace

int main()
{
    testBiquadCoefficients();
    testBiquadProcessing();
    testEnvelope();
    testExciteMix();
    testBitCrusher();
    testMech();
    testTube();
    testSubBlocksAndEnables();

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
