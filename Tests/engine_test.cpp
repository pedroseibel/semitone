// Offline measurements for the Semitone pitch engine.
// Built by CMake as SemitoneEngineTest and run by ctest.
// Run it by hand with a folder name to also render the demo WAVs:  SemitoneEngineTest <folder>

#include "PitchEngine.h"

#include <chrono>
#include <cstdio>
#include <random>
#include <string>

using Signal = std::vector<float>;
static const double fs = 48000.0;

static double st (double semis) { return std::pow (2.0, semis / 12.0); }
static double dB (double x)     { return 10.0 * std::log10 (std::max (x, 1e-30)); }

struct Options
{
    int window = 4096;
    double ratio = 1.0;
    bool lock = true;
    float threshold = 3.0f;
};

/** Runs the engine in odd-sized blocks and returns the output with latency removed. */
static std::vector<Signal> run (const std::vector<Signal>& in, const Options& o)
{
    const int C = (int) in.size();
    const int len = (int) in[0].size();
    semitone::PitchEngine e;
    e.prepare (C, o.window);
    e.setRatio (o.ratio);
    e.setTransientLock (o.lock);
    e.setOnsetThreshold (o.threshold);

    const int lat = e.getLatency();
    std::vector<Signal> padded (C, Signal ((size_t) (len + lat), 0.0f)), out (C, Signal ((size_t) (len + lat), 0.0f));
    for (int c = 0; c < C; ++c)
        std::copy (in[c].begin(), in[c].end(), padded[c].begin());

    const int blocks[] = { 64, 127, 512, 33, 1024, 1 };
    int p = 0, b = 0;
    while (p < len + lat)
    {
        const int n = std::min (blocks[b++ % 6], len + lat - p);
        const float* ip[2];
        float* op[2];
        for (int c = 0; c < C; ++c) { ip[c] = padded[c].data() + p; op[c] = out[c].data() + p; }
        e.process (ip, op, n);
        p += n;
    }

    for (auto& ch : out)
        ch.erase (ch.begin(), ch.begin() + lat);
    return out;
}

static Signal run1 (const Signal& in, const Options& o) { return run ({ in }, o)[0]; }

static double power (const Signal& s, int from, int to)
{
    double p = 0;
    for (int i = from; i < to; ++i) p += (double) s[i] * s[i];
    return p / std::max (1, to - from);
}

/** Removes the best-fit sinusoid at f (Hz) from s[from, to) and returns its amplitude. */
static double removeSine (Signal& s, double f, int from, int to)
{
    double cc = 0, ss = 0, cs = 0, xc = 0, xs = 0;
    const double w = 2.0 * semitone::pi * f / fs;
    for (int i = from; i < to; ++i)
    {
        const double c = std::cos (w * i), sn = std::sin (w * i);
        cc += c * c; ss += sn * sn; cs += c * sn;
        xc += s[i] * c; xs += s[i] * sn;
    }
    const double det = cc * ss - cs * cs;
    const double A = (xc * ss - xs * cs) / det, B = (xs * cc - xc * cs) / det;
    for (int i = from; i < to; ++i)
        s[i] -= (float) (A * std::cos (w * i) + B * std::sin (w * i));
    return std::sqrt (A * A + B * B);
}

/** Finds the frequency near f0 that best explains s, by maximising the fitted amplitude. */
static double findFrequency (const Signal& s, double f0, int from, int to)
{
    double lo = f0 * 0.995, hi = f0 * 1.005;
    for (int it = 0; it < 40; ++it)
    {
        const double a = lo + (hi - lo) / 3.0, b = hi - (hi - lo) / 3.0;
        Signal t1 = s, t2 = s;
        if (removeSine (t1, a, from, to) < removeSine (t2, b, from, to)) lo = a; else hi = b;
    }
    return 0.5 * (lo + hi);
}

static void writeWav (const std::string& path, const std::vector<Signal>& ch)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::printf ("  (could not write %s)\n", path.c_str()); return; }
    const uint32_t n = (uint32_t) ch[0].size(), C = (uint32_t) ch.size(), rate = (uint32_t) fs;
    const uint32_t dataBytes = n * C * 2;
    auto u32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 ((uint16_t) C); u32 (rate); u32 (rate * C * 2); u16 ((uint16_t) (C * 2)); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t c = 0; c < C; ++c)
            u16 ((uint16_t) (int16_t) std::lrint (std::max (-1.0f, std::min (1.0f, ch[c][i])) * 32767.0f));
    std::fclose (f);
}

//==============================================================================
static int failures = 0;
static void check (bool ok, const char* what)
{
    if (! ok) { ++failures; std::printf ("  ** FAIL: %s\n", what); }
}

static void testUnity()
{
    std::printf ("\n[1] Unity ratio is a pure delay\n");
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    Signal x (48000);
    for (auto& v : x) v = d (rng);

    for (int w : { 1024, 2048, 4096 })
    {
        Signal y = run1 (x, { w, 1.0 });
        double err = 0, ref = 0;
        for (int i = w; i < (int) x.size() - w; ++i) { err += std::pow (y[i] - x[i], 2); ref += x[i] * x[i]; }
        std::printf ("  window %4d: residual %.1f dB\n", w, dB (err / ref));
        check (dB (err / ref) < -100.0, "unity null");
    }
}

static void testSines()
{
    std::printf ("\n[2] Single sine: pitch accuracy, level, and everything-that-isn't-the-sine\n");
    std::printf ("      in Hz   shift   out Hz (expected)    cents err   level dB   residual dB\n");

    for (int w : { 4096, 1024 })
    {
        std::printf ("  window %d\n", w);
        for (double f : { 82.4, 440.0, 1000.0, 3520.0, 9000.0 })
            for (double semis : { -12.0, -5.0, -0.31, 3.0, 7.0, 12.0 })
            {
                const double r = st (semis);
                if (f * r > 0.45 * fs) continue;
                const int len = 96000;
                Signal x (len);
                for (int i = 0; i < len; ++i) x[i] = 0.5f * (float) std::sin (2.0 * semitone::pi * f * i / fs);

                Signal y = run1 (x, { w, r });
                const int from = 3 * w, to = len - 3 * w;
                const double fOut = findFrequency (y, f * r, from, to);
                const double total = power (y, from, to);
                const double amp = removeSine (y, fOut, from, to);
                const double resid = power (y, from, to);
                const double cents = 1200.0 * std::log2 (fOut / (f * r));

                std::printf ("    %7.1f  %+6.2f   %8.2f (%8.2f)   %+8.3f   %+7.2f   %8.1f\n",
                             f, semis, fOut, f * r, cents, 20.0 * std::log10 (amp / 0.5), dB (resid / total));
                check (std::abs (cents) < 1.0, "sine pitch within 1 cent");
            }
    }
}

static void testHarmonic()
{
    std::printf ("\n[3] Harmonic tone (12 partials, 1/n rolloff): residual after removing shifted partials\n");
    for (int w : { 4096, 2048, 1024 })
        for (double f0 : { 110.0, 261.6 })
            for (double semis : { -12.0, -7.0, 4.0, 7.0, 12.0 })
            {
                const double r = st (semis);
                const int len = 96000;
                Signal x (len, 0.0f);
                for (int h = 1; h <= 12; ++h)
                    for (int i = 0; i < len; ++i)
                        x[i] += (float) (0.3 / h * std::sin (2.0 * semitone::pi * f0 * h * i / fs + h));

                Signal y = run1 (x, { w, r });
                const int from = 3 * w, to = len - 3 * w;
                const double total = power (y, from, to);
                for (int pass = 0; pass < 2; ++pass)
                    for (int h = 1; h <= 12; ++h)
                        removeSine (y, f0 * h * r, from, to);
                std::printf ("  window %4d  f0 %6.1f  %+5.1f st:  level %+6.2f dB   residual %6.1f dB\n",
                             w, f0, semis, dB (total / power (x, from, to)), dB (power (y, from, to) / total));
            }
}

static void testClick()
{
    std::printf ("\n[4] Click: where the energy lands (ideal = all of it at the click)\n");
    std::printf ("      window  shift   lock   peak offset   energy within +-1ms   +-5ms   pre-echo before -5ms\n");

    for (int w : { 4096, 1024 })
        for (double semis : { -12.0, -5.0, 5.0, 12.0 })
            for (bool lock : { false, true })
            {
                const int len = 48000, at = 20011;
                Signal x (len, 0.0f);
                x[at] = 1.0f;
                Signal y = run1 (x, { w, st (semis), lock });

                int peak = 0;
                for (int i = 0; i < len; ++i) if (std::abs (y[i]) > std::abs (y[peak])) peak = i;
                double total = 0, near1 = 0, near5 = 0, pre = 0;
                for (int i = 0; i < len; ++i)
                {
                    const double e = (double) y[i] * y[i];
                    total += e;
                    if (std::abs (i - at) <= 48) near1 += e;
                    if (std::abs (i - at) <= 240) near5 += e;
                    if (i < at - 240) pre += e;
                }
                std::printf ("      %5d   %+5.1f   %s   %+6d smp      %5.1f%%            %5.1f%%   %6.1f dB\n",
                             w, semis, lock ? "on " : "off", peak - at, 100.0 * near1 / total,
                             100.0 * near5 / total, dB (pre / total));
            }
}

/** Drum-like noise bursts with an instant attack, averaged over 12 hits that fall at
    different places relative to the frame grid. */
static void testBurst()
{
    std::printf ("\n[5] Noise bursts with an instant attack (average of 12 hits)\n");
    std::printf ("      pre-echo  = energy in the 10 ms before the hit, relative to the 10 ms after it\n");
    std::printf ("      attack    = energy in the first 3 ms relative to 10-13 ms later (input: %s)\n", "see first line");

    const int hits = 12, gap = 14000, len = gap * (hits + 1);
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    Signal x (len, 0.0f);
    std::vector<int> at;
    for (int h = 0; h < hits; ++h)
    {
        const int t0 = gap / 2 + h * gap + h * 97;
        at.push_back (t0);
        for (int i = 0; i < 9000; ++i) x[t0 + i] += 0.5f * d (rng) * (float) std::exp (-i / 2500.0);
    }

    auto measure = [&] (const Signal& y, double& pre, double& attack)
    {
        double ePre = 0, ePost = 0, eA = 0, eB = 0;
        for (int t0 : at)
        {
            for (int i = t0 - 480; i < t0 - 24; ++i) ePre += (double) y[i] * y[i];
            for (int i = t0; i < t0 + 480; ++i)      ePost += (double) y[i] * y[i];
            for (int i = t0; i < t0 + 144; ++i)      eA += (double) y[i] * y[i];
            for (int i = t0 + 480; i < t0 + 624; ++i) eB += (double) y[i] * y[i];
        }
        pre = dB (ePre / ePost);
        attack = dB (eA / eB);
    };

    double pre, attack;
    measure (x, pre, attack);
    std::printf ("  input:                           pre-echo %6.1f dB   attack %+5.1f dB\n", pre, attack);

    for (int w : { 4096, 2048, 1024 })
        for (double semis : { -12.0, -7.0, -3.0, 3.0, 7.0, 12.0 })
        {
            std::printf ("  window %4d %+5.1f st:", w, semis);
            for (bool lock : { false, true })
            {
                Signal y = run1 (x, { w, st (semis), lock });
                measure (y, pre, attack);
                std::printf ("   lock %s: pre-echo %6.1f dB  attack %+5.1f dB", lock ? "on " : "off", pre, attack);
            }
            std::printf ("\n");
        }
}

static void testNoiseLevel()
{
    std::printf ("\n[6] Steady white noise: output level relative to input\n");
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    Signal x (96000);
    for (auto& v : x) v = d (rng);

    for (int w : { 4096, 1024 })
    {
        std::printf ("  window %4d:", w);
        for (double semis : { -12.0, -7.0, -2.0, 2.0, 7.0, 12.0 })
        {
            Signal y = run1 (x, { w, st (semis) });
            std::printf ("  %+3.0fst %+5.2f dB", semis, dB (power (y, 8192, 88000) / power (x, 8192, 88000)));
        }
        std::printf ("\n");
    }
}

static void testStereo()
{
    std::printf ("\n[7] Stereo image: a source panned 70/30 must stay panned 70/30, in phase\n");
    const int len = 48000;
    Signal m (len, 0.0f);
    for (int h = 1; h <= 8; ++h)
        for (int i = 0; i < len; ++i)
            m[i] += (float) (0.2 / h * std::sin (2.0 * semitone::pi * 196.0 * h * i / fs));
    Signal l = m, r = m;
    for (auto& v : l) v *= 0.7f;
    for (auto& v : r) v *= 0.3f;

    auto out = run ({ l, r }, { 4096, st (5.0) });
    double err = 0, ref = 0;
    for (int i = 8192; i < len - 8192; ++i)
    {
        err += std::pow (out[0][i] * 0.3 - out[1][i] * 0.7, 2);
        ref += std::pow (out[0][i] * 0.3, 2);
    }
    std::printf ("  L/R mismatch: %.1f dB\n", dB (err / ref));
    check (dB (err / ref) < -80.0, "stereo image");
}

static void testSpeed()
{
    std::printf ("\n[8] Speed (stereo, 10 s of audio)\n");
    std::mt19937 rng (5);
    std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    Signal x (480000);
    for (auto& v : x) v = d (rng);
    for (int w : { 4096, 2048, 1024 })
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto out = run ({ x, x }, { w, st (7.0) });
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  window %4d: %.3f s  -> %.1f%% of one core in real time\n", w, secs, 100.0 * secs / 10.0);
    }
}

//==============================================================================
/** A little synthetic groove (kick, snare, hats, bass, chord stabs) to listen to. */
static std::vector<Signal> makeDemo()
{
    const int len = (int) (fs * 8.0);
    Signal L (len, 0.0f), R (len, 0.0f);
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    const double beat = fs * 60.0 / 100.0;   // 100 bpm

    auto add = [&] (int i, float v, float pan)
    {
        if (i >= 0 && i < len) { L[i] += v * (1.0f - pan); R[i] += v * pan; }
    };

    for (int step = 0; step < 16 * 2; ++step)
    {
        const int t0 = (int) (step * beat / 2.0);

        if (step % 4 == 0)                                   // kick
            for (int i = 0; i < 12000; ++i)
            {
                const double t = i / fs;
                const double ph = 2.0 * semitone::pi * (50.0 * t + (110.0 / 30.0) * (1.0 - std::exp (-30.0 * t)));
                add (t0 + i, (float) (0.8 * std::sin (ph) * std::exp (-t * 9.0)), 0.5f);
            }

        if (step % 4 == 2)                                   // snare
            for (int i = 0; i < 9000; ++i)
            {
                const double t = i / fs;
                add (t0 + i, (float) (0.35 * d (rng) * std::exp (-t * 22.0) + 0.3 * std::sin (2.0 * semitone::pi * 190.0 * t) * std::exp (-t * 30.0)), 0.5f);
            }

        for (int i = 0; i < 2400; ++i)                       // hat
        {
            static float hp = 0.0f, last = 0.0f;
            const float n = d (rng);
            hp = 0.85f * (hp + n - last); last = n;
            add (t0 + i, 0.12f * hp * (float) std::exp (-i / 400.0), step % 2 ? 0.7f : 0.3f);
        }
    }

    const double chords[4][4] = { { 110.0, 261.63, 329.63, 392.0 }, { 87.31, 220.0, 261.63, 349.23 },
                                  { 130.81, 261.63, 329.63, 392.0 }, { 98.0, 246.94, 293.66, 392.0 } };
    for (int bar = 0; bar < 4; ++bar)
    {
        const int t0 = (int) (bar * beat * 4.0 * 0.5 * 2.0 / 2.0), dur = (int) (beat * 4.0 * 0.5 * 2.0 / 2.0);
        for (int v = 0; v < 4; ++v)
            for (int h = 1; h <= (v == 0 ? 6 : 14); ++h)
            {
                const double f = chords[bar][v] * h * (1.0 + 0.0007 * v);
                if (f > 9000.0) break;
                for (int i = 0; i < dur; ++i)
                {
                    const double t = i / fs;
                    const double envl = std::min (1.0, t * 200.0) * std::exp (-t * (v == 0 ? 1.0 : 1.6)) * std::min (1.0, (dur - i) / 500.0);
                    const double vib = 1.0 + 0.003 * std::sin (2.0 * semitone::pi * 5.2 * t);
                    add (t0 + i, (float) ((v == 0 ? 0.22 : 0.07) / h * envl * std::sin (2.0 * semitone::pi * f * vib * t + h * v)), v == 0 ? 0.5f : 0.2f + 0.2f * v);
                }
            }
    }

    return { L, R };
}

int main (int argc, char** argv)
{
    testUnity();
    testSines();
    testHarmonic();
    testClick();
    testBurst();
    testNoiseLevel();
    testStereo();
    testSpeed();

    if (argc > 1)
    {
        const std::string dir = argv[1];
        auto demo = makeDemo();
        writeWav (dir + "/demo_dry.wav", demo);
        const char* names[] = { "smooth", "balanced", "tight" };
        const int windows[] = { 4096, 2048, 1024 };
        for (int m = 0; m < 3; ++m)
            for (int semis : { -12, -5, 3, 7, 12 })
            {
                char name[128];
                std::snprintf (name, sizeof (name), "/demo_%s_%+d.wav", names[m], semis);
                writeWav (dir + name, run (demo, { windows[m], st (semis) }));
            }
        std::printf ("\nWrote demo WAVs to %s\n", dir.c_str());
    }

    std::printf ("\n%s\n", failures == 0 ? "All hard checks passed." : "Some hard checks FAILED.");
    return failures == 0 ? 0 : 1;
}
