// Command-line null-test harness for the distortion section.
//
//   crush_nulltest --in in.wav --out out.wav [--ref ref.wav] [stage options]
//       Renders in.wav through the module with fixed parameters, writes out.wav and,
//       with --ref, compares against a render from the original plugin.
//
//   crush_nulltest --suite <outDir> [--refdir <dir>] [--drums drums.wav] [--rate 44100]
//       Renders the validation set (1 kHz sine -6 dBFS, 10 kHz sine -12 dBFS, pink
//       noise -12 dBFS, impulses, optional drum loop) through each stage alone at
//       p = 0.25 / 0.5 / 0.75 / 1.0. With --refdir, every render is compared with the
//       file of the same name there (render those from the original plugin with the
//       bandpass, filter, flanger, EQ and compressor OFF, master mix 100 %, volume unity).
//
//   crush_nulltest --curves <outDir>
//       Writes CSV data for the Tube / Mech transfer curves and the Excite envelope step
//       response (plot with tools/plot_curves.py).
//
// Stage options: --excite p --bitcrusher p --mech p --tube p --diston 0|1
//                --order original|contiguous --oldcrush --noclip --freq hz --block n

#include "../Source/DSP/DistortionSection.h"
#include "WavFile.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
struct Settings
{
    float excite = 0.0f, bitCrusher = 0.0f, mech = 0.0f, tube = 0.0f, distOn = 1.0f;
    bool original = false, oldCrush = false, clip = true;
    float freq = 7000.0f;
    int block = 512;
};

void render (const Settings& s, wav::Audio& audio)
{
    crush::DistortionSection d;
    d.setSampleRate ((double) audio.sampleRate);
    d.exciter.setFrequency (s.freq);
    d.distOn.setImmediate (s.distOn);
    d.excite.setImmediate (s.excite);
    d.bitCrusher.setImmediate (s.bitCrusher);
    d.mech.setImmediate (s.mech);
    d.tube.setImmediate (s.tube);
    d.chainOrder = s.original ? crush::DistortionSection::ChainOrder::Original
                              : crush::DistortionSection::ChainOrder::Contiguous;
    d.useOldBitCrush = s.oldCrush;
    d.outputHardClip = s.clip;

    const int total = (int) audio.left.size();
    for (int pos = 0; pos < total; pos += s.block)
    {
        const int n = std::min (s.block, total - pos);
        d.process (audio.left.data() + pos, audio.right.data() + pos, n);
    }
}

struct Diff { double maxAbs = 0.0, rmsDb = -400.0; long firstIndex = -1; bool lengthMismatch = false; };

Diff compare (const wav::Audio& a, const wav::Audio& b)
{
    Diff d;
    const size_t n = std::min (a.left.size(), b.left.size());
    d.lengthMismatch = a.left.size() != b.left.size();
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double e[2] = { (double) a.left[i] - (double) b.left[i], (double) a.right[i] - (double) b.right[i] };
        for (double x : e)
        {
            if (x != 0.0 && d.firstIndex < 0) d.firstIndex = (long) i;
            d.maxAbs = std::max (d.maxAbs, std::abs (x));
            sum += x * x;
        }
    }
    const double rms = n > 0 ? std::sqrt (sum / (double) (2 * n)) : 0.0;
    d.rmsDb = rms > 0.0 ? 20.0 * std::log10 (rms) : -400.0;
    return d;
}

void printDiff (const char* name, const Diff& d)
{
    std::printf ("%-36s max|err| %.3e   RMS %8.2f dBFS   first diff %ld%s%s\n", name, d.maxAbs, d.rmsDb, d.firstIndex,
                 d.maxAbs >= 1.0e-6 ? "   <-- FLAG (> 1e-6)" : "", d.lengthMismatch ? "   (length mismatch)" : "");
}

//==============================================================================
wav::Audio makeSine (int fs, double hz, double dbfs, double seconds)
{
    wav::Audio a; a.sampleRate = fs;
    const size_t n = (size_t) (seconds * fs);
    const double amp = std::pow (10.0, dbfs / 20.0);
    a.left.resize (n);
    for (size_t i = 0; i < n; ++i)
        a.left[i] = (float) (amp * std::sin (2.0 * 3.14159265358979323846 * hz * (double) i / fs));
    a.right = a.left;
    return a;
}

wav::Audio makePink (int fs, double dbfs, double seconds)
{
    // Paul Kellet's pink filter on a fixed-seed LCG, normalised to the target RMS.
    wav::Audio a; a.sampleRate = fs;
    const size_t n = (size_t) (seconds * fs);
    a.left.resize (n); a.right.resize (n);
    for (int ch = 0; ch < 2; ++ch)
    {
        uint32_t seed = 12345u + (uint32_t) ch * 777u;
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, sum = 0;
        std::vector<double> v (n);
        for (size_t i = 0; i < n; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const double w = (double) (seed >> 8) / 8388608.0 - 1.0;
            b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
            v[i] = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926;
            sum += v[i] * v[i];
        }
        const double scale = std::pow (10.0, dbfs / 20.0) / std::sqrt (sum / (double) n);
        auto& out = ch == 0 ? a.left : a.right;
        for (size_t i = 0; i < n; ++i) out[i] = (float) (v[i] * scale);
    }
    return a;
}

wav::Audio makeImpulses (int fs, double seconds)
{
    wav::Audio a; a.sampleRate = fs;
    const size_t n = (size_t) (seconds * fs);
    a.left.assign (n, 0.0f);
    for (size_t i = 100; i < n; i += (size_t) (fs / 4))
        a.left[i] = 0.9f;
    a.right = a.left;
    return a;
}

int runSuite (const std::string& outDir, const std::string& refDir, const std::string& drums, int fs, const Settings& base)
{
    struct Signal { std::string name; wav::Audio audio; };
    std::vector<Signal> signals = {
        { "sine1k", makeSine (fs, 1000.0, -6.0, 2.0) },
        { "sine10k", makeSine (fs, 10000.0, -12.0, 2.0) },
        { "pink", makePink (fs, -12.0, 4.0) },
        { "impulses", makeImpulses (fs, 2.0) },
    };
    if (! drums.empty())
    {
        wav::Audio d; std::string err;
        if (! wav::read (drums, d, err)) { std::fprintf (stderr, "drums: %s\n", err.c_str()); return 1; }
        signals.push_back ({ "drums", d });
    }

    for (auto& s : signals)
        wav::write (outDir + "/input_" + s.name + ".wav", s.audio);

    const char* stages[] = { "excite", "bitcrusher", "mech", "tube" };
    const float amounts[] = { 0.25f, 0.5f, 0.75f, 1.0f };
    int flagged = 0, compared = 0;

    for (auto& sig : signals)
        for (const char* stage : stages)
            for (float p : amounts)
            {
                Settings s = base;
                s.excite = s.bitCrusher = s.mech = s.tube = 0.0f;
                if (! std::strcmp (stage, "excite"))     s.excite = p;
                if (! std::strcmp (stage, "bitcrusher")) s.bitCrusher = p;
                if (! std::strcmp (stage, "mech"))       s.mech = p;
                if (! std::strcmp (stage, "tube"))       s.tube = p;

                wav::Audio a = sig.audio;
                render (s, a);
                char name[128];
                std::snprintf (name, sizeof (name), "%s_%s_%03d.wav", sig.name.c_str(), stage, (int) std::lround (p * 100.0f));
                wav::write (outDir + "/" + name, a);

                if (! refDir.empty())
                {
                    wav::Audio ref; std::string err;
                    if (! wav::read (refDir + "/" + name, ref, err)) { std::printf ("%-36s (no reference)\n", name); continue; }
                    const Diff d = compare (a, ref);
                    printDiff (name, d);
                    ++compared;
                    if (d.maxAbs >= 1.0e-6) ++flagged;
                }
            }

    std::printf ("Rendered %zu signals x 4 stages x 4 amounts to %s\n", signals.size(), outDir.c_str());
    if (! refDir.empty())
        std::printf ("Compared %d renders, %d flagged\n", compared, flagged);
    return flagged > 0 ? 2 : 0;
}

int writeCurves (const std::string& outDir)
{
    const float amounts[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };

    for (int which = 0; which < 2; ++which)
    {
        const std::string path = outDir + (which == 0 ? "/tube_curve.csv" : "/mech_curve.csv");
        FILE* f = std::fopen (path.c_str(), "w");
        if (f == nullptr) return 1;
        std::fprintf (f, "x,p0,p25,p50,p75,p100\n");
        for (int i = -1500; i <= 1500; ++i)
        {
            const float x = (float) i / 1000.0f;
            std::fprintf (f, "%.4f", x);
            for (float p : amounts)
            {
                float y = x;
                if (which == 0) crush::processTube (&y, 1, p);
                else            crush::processMech (&y, 1, p);
                std::fprintf (f, ",%.9g", y);
            }
            std::fprintf (f, "\n");
        }
        std::fclose (f);
    }

    // Envelope step response: a 0.5 step into the detector for 882 samples (20 ms), then silence.
    FILE* f = std::fopen ((outDir + "/excite_envelope.csv").c_str(), "w");
    if (f == nullptr) return 1;
    std::fprintf (f, "sample,input,envelope,gain\n");
    crush::EnvelopeFollower env;
    env.setSampleRate (44100.0f);
    for (int i = 0; i < 6000; ++i)
    {
        const float in = (i >= 200 && i < 1082) ? 0.5f : 0.0f;   // DC step into the detector
        const float e = env.process (in);
        float g = e * 10.0f; if (3.0f < g) g = 3.0f;
        std::fprintf (f, "%d,%.6f,%.9g,%.9g\n", i, in, e, g);
    }
    std::fclose (f);
    std::printf ("Wrote tube_curve.csv, mech_curve.csv, excite_envelope.csv to %s\n", outDir.c_str());
    return 0;
}

void usage()
{
    std::puts ("usage:\n"
               "  crush_nulltest --in in.wav --out out.wav [--ref ref.wav] [options]\n"
               "  crush_nulltest --suite outDir [--refdir dir] [--drums drums.wav] [--rate 44100] [options]\n"
               "  crush_nulltest --curves outDir\n"
               "options: --excite p --bitcrusher p --mech p --tube p --diston 0|1 --order original|contiguous\n"
               "         --oldcrush --noclip --freq hz --block n");
}
} // namespace

int main (int argc, char** argv)
{
    Settings s;
    std::string in, out, ref, suite, refDir, drums, curves;
    int rate = 44100;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { if (i + 1 >= argc) { usage(); std::exit (1); } return argv[++i]; };
        if      (a == "--in") in = next();
        else if (a == "--out") out = next();
        else if (a == "--ref") ref = next();
        else if (a == "--suite") suite = next();
        else if (a == "--refdir") refDir = next();
        else if (a == "--drums") drums = next();
        else if (a == "--curves") curves = next();
        else if (a == "--rate") rate = std::atoi (next().c_str());
        else if (a == "--excite") s.excite = (float) std::atof (next().c_str());
        else if (a == "--bitcrusher") s.bitCrusher = (float) std::atof (next().c_str());
        else if (a == "--mech") s.mech = (float) std::atof (next().c_str());
        else if (a == "--tube") s.tube = (float) std::atof (next().c_str());
        else if (a == "--diston") s.distOn = (float) std::atof (next().c_str());
        else if (a == "--order") s.original = next() == "original";
        else if (a == "--oldcrush") s.oldCrush = true;
        else if (a == "--noclip") s.clip = false;
        else if (a == "--freq") s.freq = (float) std::atof (next().c_str());
        else if (a == "--block") s.block = std::max (1, std::atoi (next().c_str()));
        else { usage(); return 1; }
    }

    if (! curves.empty()) return writeCurves (curves);
    if (! suite.empty())  return runSuite (suite, refDir, drums, rate, s);
    if (in.empty() || out.empty()) { usage(); return 1; }

    wav::Audio audio; std::string err;
    if (! wav::read (in, audio, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
    render (s, audio);
    if (! wav::write (out, audio)) { std::fprintf (stderr, "cannot write %s\n", out.c_str()); return 1; }

    if (! ref.empty())
    {
        wav::Audio r;
        if (! wav::read (ref, r, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
        const Diff d = compare (audio, r);
        std::printf ("max abs diff   : %.9e\n", d.maxAbs);
        std::printf ("RMS diff       : %.2f dBFS\n", d.rmsDb);
        std::printf ("first diff idx : %ld\n", d.firstIndex);
        if (d.lengthMismatch) std::printf ("warning: lengths differ (%zu vs %zu)\n", audio.left.size(), r.left.size());
        return d.maxAbs < 1.0e-6 ? 0 : 2;
    }
    return 0;
}
