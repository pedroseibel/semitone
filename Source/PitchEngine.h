// Semitone pitch engine
//
// A real-time, constant-latency spectral pitch shifter. No external dependencies.
//
// How it works, per overlapping frame (Hann window, 75% overlap, 2x zero-padded FFT):
//
//  1. Find spectral peaks in the energy summed over all channels. Each peak owns the
//     region of bins around it, out to the valley on either side.
//  2. Measure every peak's true frequency from its phase advance since the last frame.
//  3. Move each peak to (ratio x frequency). The bins closest to the peak (its "core")
//     are translated without stretching, so a partial keeps its exact shape and all of
//     its bins stay phase-locked to it (Laroche/Dolson peak locking). The valleys
//     between cores are stretched or squeezed to fill the space that opens or closes.
//  4. Rotate each region's phase so that the shifted partial stays continuous from one
//     frame to the next. When a region's energy jumps (an onset), the rotation is
//     re-anchored to where the energy sits inside the frame (its group delay) instead,
//     which keeps attacks tight instead of smearing them across the window.
//  5. The same map and the same rotations are applied to every channel, so the stereo
//     image is preserved exactly.
//
// Latency is exactly one window (getLatency()), whatever the ratio.

#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <vector>

namespace semitone
{

constexpr double pi = 3.14159265358979323846;

//==============================================================================
/** Minimal in-place radix-2 complex FFT. */
class FFT
{
public:
    void init (int size)
    {
        n = size;
        rev.resize ((size_t) n);
        int levels = 0;
        while ((1 << levels) < n) ++levels;

        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < levels; ++b)
                if (i & (1 << b)) r |= 1 << (levels - 1 - b);
            rev[(size_t) i] = r;
        }

        cosT.resize ((size_t) n / 2);
        sinT.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            const double a = -2.0 * pi * (double) i / (double) n;
            cosT[(size_t) i] = (float) std::cos (a);
            sinT[(size_t) i] = (float) std::sin (a);
        }
    }

    void forward (std::complex<float>* d) const { transform (d, 1.0f); }
    void inverse (std::complex<float>* d) const { transform (d, -1.0f); }  // unnormalised

private:
    void transform (std::complex<float>* d, float sign) const
    {
        auto* f = reinterpret_cast<float*> (d);

        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (i < j) std::swap (d[i], d[j]);
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1;
            const int step = n / len;

            for (int i = 0; i < n; i += len)
            {
                float* a = f + 2 * i;
                float* b = f + 2 * (i + half);

                for (int j = 0, t = 0; j < half; ++j, t += step)
                {
                    const float wr = cosT[(size_t) t];
                    const float wi = sign * sinT[(size_t) t];
                    const float br = b[2 * j], bi = b[2 * j + 1];
                    const float vr = br * wr - bi * wi;
                    const float vi = br * wi + bi * wr;
                    const float ur = a[2 * j], ui = a[2 * j + 1];
                    a[2 * j]     = ur + vr;
                    a[2 * j + 1] = ui + vi;
                    b[2 * j]     = ur - vr;
                    b[2 * j + 1] = ui - vi;
                }
            }
        }
    }

    int n = 0;
    std::vector<int> rev;
    std::vector<float> cosT, sinT;
};

//==============================================================================
class PitchEngine
{
public:
    /** windowSize must be a power of two (>= 256). Allocates; call off the audio thread. */
    void prepare (int numChannels, int windowSize)
    {
        C = std::max (1, numChannels);
        N = windowSize;
        H = N / 4;
        M = N * 2;
        K = M / 2 + 1;

        fft.init (M);

        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) N));

        // Hann^2 at 75% overlap sums to 1.5; the FFT pair is unnormalised.
        outputScale = 1.0f / (1.5f * (float) M);

        inBuf.assign ((size_t) C, std::vector<float> ((size_t) N, 0.0f));
        ola.assign ((size_t) C, std::vector<float> ((size_t) N, 0.0f));
        outReady.assign ((size_t) C, std::vector<float> ((size_t) H, 0.0f));

        const size_t padded = (size_t) (K + 2 * pad);
        X.assign ((size_t) C, std::vector<cf> (padded));
        Xprev.assign ((size_t) C, std::vector<cf> (padded));
        Y.assign ((size_t) K, cf());
        work.assign ((size_t) M, cf());

        E.assign ((size_t) K, 0.0f);
        Eprev.assign ((size_t) K, 0.0f);
        thetaPrev.assign ((size_t) K, 0.0f);
        thetaNew.assign ((size_t) K, 0.0f);
        hold.assign ((size_t) K, 0);

        peaks.assign ((size_t) K, 0);
        peakOnset.assign ((size_t) K, 0);
        peakSlope.assign ((size_t) K, cf());
        bounds.assign ((size_t) K + 1, 0);
        segments.assign ((size_t) K * 3 + 3, Segment());

        buildKernel();
        reset();
    }

    void reset()
    {
        for (auto& v : inBuf)    std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : ola)      std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : outReady) std::fill (v.begin(), v.end(), 0.0f);
        for (auto& v : X)        std::fill (v.begin(), v.end(), cf());
        for (auto& v : Xprev)    std::fill (v.begin(), v.end(), cf());
        std::fill (Eprev.begin(), Eprev.end(), 0.0f);
        std::fill (thetaPrev.begin(), thetaPrev.end(), 0.0f);
        std::fill (hold.begin(), hold.end(), (uint8_t) 0);
        pos = 0;
    }

    /** Pitch ratio: 2.0 = up an octave, 0.5 = down an octave. Takes effect on the next frame. */
    void setRatio (double r)              { ratio = std::min (4.0, std::max (0.25, r)); }

    /** How large a jump in a partial's level (as a linear amplitude ratio between
        consecutive frames) counts as an attack. Lower = more attacks re-anchored. */
    void setOnsetThreshold (float t)      { onsetThreshold = std::max (1.05f, t); }

    /** Turns attack re-anchoring off entirely (plain peak-locked vocoder). */
    void setTransientLock (bool on)       { transientLock = on; }

    int getLatency() const                { return N; }
    int getWindowSize() const             { return N; }
    int getNumChannels() const            { return C; }

    /** in and out may alias. Both hold getNumChannels() channel pointers. */
    void process (const float* const* in, float* const* out, int numSamples)
    {
        int done = 0;

        while (done < numSamples)
        {
            const int n = std::min (numSamples - done, H - pos);

            for (int c = 0; c < C; ++c)
            {
                float* dst = inBuf[(size_t) c].data() + (N - H) + pos;
                const float* ready = outReady[(size_t) c].data() + pos;
                const float* src = in[c] + done;
                float* o = out[c] + done;

                for (int i = 0; i < n; ++i)
                {
                    const float x = src[i];   // read before write: in/out may alias
                    o[i] = ready[i];
                    dst[i] = x;
                }
            }

            pos += n;
            done += n;

            if (pos == H)
            {
                processFrame();
                pos = 0;
            }
        }
    }

private:
    using cf = std::complex<float>;

    static constexpr int pad = 4;            // mirrored guard bins either side of the spectrum
    static constexpr int taps = 6;           // spectral interpolation kernel length
    static constexpr int kernelPhases = 512;
    static constexpr int coreHalfWidth = 8;  // bins either side of a peak that move rigidly
    static constexpr int holdFrames = 4;     // frames before a region may re-anchor again

    struct Segment
    {
        int o0 = 0, o1 = 0;      // output bin range [o0, o1)
        double m0 = 0, dm = 0;   // input position at o0, and its step per output bin
        double ph0 = 0, dph = 0; // phase rotation at o0, and its step per output bin
        float gain = 1.0f;
    };

    //==========================================================================
    void buildKernel()
    {
        // Kaiser-windowed sinc. The spectrum is 2x oversampled (zero-padded frame),
        // which is what lets such a short kernel shift it by fractions of a bin.
        kernel.assign ((size_t) (kernelPhases + 1) * taps, 0.0f);
        const double beta = 6.5;

        auto bessel = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 40; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }
            return sum;
        };

        const double norm = bessel (beta);
        const double halfLen = taps / 2.0;

        for (int p = 0; p <= kernelPhases; ++p)
        {
            const double fr = (double) p / (double) kernelPhases;
            double sum = 0.0;
            double w[taps];

            for (int t = 0; t < taps; ++t)
            {
                const double x = fr - (double) (t - (taps / 2 - 1));
                const double s = std::abs (x) < 1e-12 ? 1.0 : std::sin (pi * x) / (pi * x);
                const double r = x / halfLen;
                const double k = std::abs (r) >= 1.0 ? 0.0 : bessel (beta * std::sqrt (1.0 - r * r)) / norm;
                w[t] = s * k;
                sum += w[t];
            }

            for (int t = 0; t < taps; ++t)
                kernel[(size_t) (p * taps + t)] = (float) (w[t] / sum);
        }
    }

    static double wrapPhase (double p)
    {
        return p - 2.0 * pi * std::floor (p / (2.0 * pi) + 0.5);
    }

    double parabolicPeak (int k) const
    {
        if (k <= 0 || k >= K - 1) return (double) k;
        const double a = std::log ((double) E[(size_t) k - 1] + 1e-30);
        const double b = std::log ((double) E[(size_t) k] + 1e-30);
        const double c = std::log ((double) E[(size_t) k + 1] + 1e-30);
        const double den = a - 2.0 * b + c;
        if (den > -1e-12) return (double) k;
        return (double) k + std::min (0.5, std::max (-0.5, 0.5 * (a - c) / den));
    }

    //==========================================================================
    void processFrame()
    {
        // ---- analysis ------------------------------------------------------
        for (int c = 0; c < C; ++c)
        {
            std::fill (work.begin(), work.end(), cf());
            const float* x = inBuf[(size_t) c].data();

            // zero-phase layout: the frame centre sits at index 0
            for (int n = 0; n < N; ++n)
                work[(size_t) ((n + M - N / 2) & (M - 1))] = cf (x[n] * window[(size_t) n], 0.0f);

            fft.forward (work.data());

            cf* Xc = X[(size_t) c].data() + pad;
            std::copy (work.begin(), work.begin() + K, Xc);
            for (int j = 1; j <= pad; ++j)
            {
                Xc[-j] = std::conj (Xc[j]);
                Xc[K - 1 + j] = std::conj (Xc[K - 1 - j]);
            }
        }

        float maxE = 0.0f;
        for (int k = 0; k < K; ++k)
        {
            float e = 0.0f;
            for (int c = 0; c < C; ++c)
                e += std::norm (X[(size_t) c][(size_t) (k + pad)]);
            E[(size_t) k] = e;
            maxE = std::max (maxE, e);
        }

        const double alpha = ratio;
        const bool unity = std::abs (alpha - 1.0) < 1e-9;
        int numSegments = 0;

        if (unity)
        {
            std::fill (thetaNew.begin(), thetaNew.end(), 0.0f);
        }
        else if (maxE > 1e-24f)
        {
            numSegments = buildMap (alpha, maxE);
        }

        // ---- synthesis -----------------------------------------------------
        for (int c = 0; c < C; ++c)
        {
            const cf* Xc = X[(size_t) c].data() + pad;

            if (unity)
            {
                std::copy (Xc, Xc + K, Y.begin());
            }
            else
            {
                std::fill (Y.begin(), Y.end(), cf());
                for (int s = 0; s < numSegments; ++s)
                    applySegment (segments[(size_t) s], Xc);
            }

            work[0] = cf (Y[0].real(), 0.0f);
            work[(size_t) (M / 2)] = cf (Y[(size_t) (M / 2)].real(), 0.0f);
            for (int k = 1; k < M / 2; ++k)
            {
                work[(size_t) k] = Y[(size_t) k];
                work[(size_t) (M - k)] = std::conj (Y[(size_t) k]);
            }

            fft.inverse (work.data());

            float* o = ola[(size_t) c].data();
            for (int n = 0; n < N; ++n)
                o[n] += work[(size_t) ((n + M - N / 2) & (M - 1))].real() * window[(size_t) n] * outputScale;

            std::copy (o, o + H, outReady[(size_t) c].begin());
            std::memmove (o, o + H, sizeof (float) * (size_t) (N - H));
            std::fill (o + (N - H), o + N, 0.0f);

            float* x = inBuf[(size_t) c].data();
            std::memmove (x, x + H, sizeof (float) * (size_t) (N - H));
        }

        // ---- carry state to the next frame -----------------------------------
        for (int c = 0; c < C; ++c)
            std::swap (X[(size_t) c], Xprev[(size_t) c]);

        std::swap (E, Eprev);
        std::swap (thetaNew, thetaPrev);

        for (auto& h : hold)
            if (h > 0) --h;
    }

    //==========================================================================
    /** Builds the output->input bin map and phase rotations for this frame. */
    int buildMap (double alpha, float maxE)
    {
        // ---- peaks ---------------------------------------------------------
        const float floorE = maxE * 1e-13f;
        int numPeaks = 0;

        auto at = [this] (int k) { return (k < 0 || k >= K) ? 0.0f : E[(size_t) k]; };

        for (int k = 1; k < K - 1; ++k)
        {
            const float e = E[(size_t) k];
            if (e > floorE
                && e > at (k - 1) && e >= at (k + 1)
                && e > at (k - 2) && e >= at (k + 2)
                && e > at (k - 3) && e >= at (k + 3))
                peaks[(size_t) numPeaks++] = k;
        }

        if (numPeaks == 0)
        {
            std::fill (thetaNew.begin(), thetaNew.end(), 0.0f);
            return 0;
        }

        bounds[0] = 0;
        for (int i = 1; i < numPeaks; ++i)
        {
            int best = peaks[(size_t) i - 1] + 1;
            for (int k = best; k < peaks[(size_t) i]; ++k)
                if (E[(size_t) k] < E[(size_t) best]) best = k;
            bounds[(size_t) i] = best;
        }
        bounds[(size_t) numPeaks] = K - 1;

        // ---- regions -------------------------------------------------------
        const double phasePerBin = pi / 4.0;   // 2*pi*H/M: phase advance per frame, per bin
        const float thr2 = onsetThreshold * onsetThreshold;
        const float absFloor = maxE * 1e-9f;
        int numSegments = 0;

        // pass 1: which partials have just jumped in level, and where in the frame
        // does the energy of each one sit (its group delay, as a phase slope across bins)?
        cf sharedSlope;

        for (int i = 0; i < numPeaks; ++i)
        {
            const int kp = peaks[(size_t) i];

            float prevMax = 0.0f;
            for (int k = std::max (0, kp - 2); k <= std::min (K - 1, kp + 2); ++k)
                prevMax = std::max (prevMax, Eprev[(size_t) k]);

            const bool onset = transientLock
                               && hold[(size_t) kp] == 0
                               && E[(size_t) kp] > thr2 * (prevMax + absFloor);

            cf slope;
            for (int c = 0; c < C; ++c)
            {
                const cf* Xc = X[(size_t) c].data() + pad;
                for (int k = kp - 2; k <= kp + 1; ++k)
                    slope += Xc[k + 1] * std::conj (Xc[k]);
            }

            peakOnset[(size_t) i] = onset ? 1 : 0;
            peakSlope[(size_t) i] = slope;
            if (onset) sharedSlope += slope;
        }

        // Everything that starts in the same frame is treated as one event and anchored
        // to one moment, which is what keeps a broadband attack sharp.
        double sharedPsi = 0.0;

        if (std::norm (sharedSlope) > 0.0f)
        {
            // Prefer the moment the attack actually starts (found in the time domain);
            // fall back to the centre of gravity of the new energy if there is no clear edge.
            const int edge = findAttackEdge();
            sharedPsi = edge >= 0 ? -2.0 * pi * (double) (edge - N / 2) / (double) M
                                  : (double) std::arg (sharedSlope);
        }

        // pass 2: place every region
        for (int i = 0; i < numPeaks; ++i)
        {
            const int kp = peaks[(size_t) i];
            const int a = bounds[(size_t) i];
            const int b = bounds[(size_t) i + 1];
            const bool onset = peakOnset[(size_t) i] != 0;
            const bool held = ! onset && hold[(size_t) kp] > 0;

            // true frequency of the peak, in bins
            double fp = parabolicPeak (kp);

            if (! onset)
            {
                cf adv;
                for (int c = 0; c < C; ++c)
                    adv += X[(size_t) c][(size_t) (kp + pad)] * std::conj (Xprev[(size_t) c][(size_t) (kp + pad)]);

                if (std::norm (adv) > 0.0f)
                {
                    const double dev = wrapPhase ((double) std::arg (adv) - phasePerBin * (double) kp);
                    const double est = (double) kp + dev / phasePerBin;
                    if (std::abs (est - (double) kp) <= 1.5)
                        fp = est;
                }
            }

            const cf slope = peakSlope[(size_t) i];
            const double psi = onset ? sharedPsi
                                     : (std::norm (slope) > 0.0f ? (double) std::arg (slope) : 0.0);

            // where the region lands
            const double outA = alpha * (double) a;
            const double outB = alpha * (double) b;
            const double c0 = std::min (outB, std::max (outA, (double) kp + (alpha - 1.0) * fp));
            const double delta = c0 - (double) kp;

            double theta;
            int wl = 0, wr = 0;

            if (onset)
            {
                theta = psi * delta;
                for (int k = a; k <= b; ++k)
                    hold[(size_t) k] = (uint8_t) holdFrames;
            }
            else
            {
                theta = (double) thetaPrev[(size_t) kp] + delta * phasePerBin;

                wl = (int) std::floor (std::min ({ (double) coreHalfWidth, (double) (kp - a), c0 - outA }) + 1e-9);
                wr = (int) std::floor (std::min ({ (double) coreHalfWidth, (double) (b - kp), outB - c0 }) + 1e-9);
                wl = std::max (0, wl);
                wr = std::max (0, wr);

                // keep one input bin of valley to stretch over any gap that opens
                if (wl > 0 && wl == kp - a && c0 - (double) wl > outA + 1e-6) --wl;
                if (wr > 0 && wr == b - kp && c0 + (double) wr < outB - 1e-6) --wr;
            }

            const double inL = (double) (kp - wl), inR = (double) (kp + wr);
            const double outL = c0 - (double) wl, outR = c0 + (double) wr;
            const double sL = (inL - (double) a) > 0.0 ? (outL - outA) / (inL - (double) a) : 0.0;
            const double sR = ((double) b - inR) > 0.0 ? (outB - outR) / ((double) b - inR) : 0.0;

            // rotation actually applied to each input bin, remembered for the next frame
            const int kEnd = (i == numPeaks - 1) ? b : b - 1;
            for (int k = a; k <= kEnd; ++k)
            {
                double g;
                if ((double) k < inL)       g = outA + ((double) k - (double) a) * sL;
                else if ((double) k > inR)  g = outR + ((double) k - inR) * sR;
                else                        g = (double) k + delta;

                thetaNew[(size_t) k] = (float) wrapPhase (theta + psi * (g - (double) k - delta));
            }

            // output bin ranges
            const int oStart = (int) std::ceil (outA - 1e-9);
            const int oEnd = std::min (K, (i == numPeaks - 1) ? (int) std::floor (outB + 1e-9) + 1
                                                               : (int) std::ceil (outB - 1e-9));
            const int coreStart = std::min (oEnd, std::max (oStart, (int) std::ceil (outL - 1e-9)));
            const int coreEnd = std::min (oEnd, std::max (coreStart, (int) std::floor (outR + 1e-9) + 1));

            // Valley gain. Steady material: keep the power the same when a valley is
            // stretched or squeezed. Attack frame: the whole region is scaled as one, so one
            // gain. Frames right after an attack: leave valleys alone so the spectrum of the
            // transient stays as flat as it was (ripple there turns into pre-echo).
            const float onsetGain = (float) (1.0 / std::sqrt (alpha));
            auto flankGain = [onset, held, onsetGain] (double s)
            {
                if (onset) return onsetGain;
                if (held)  return 1.0f;
                return (float) std::min (2.0, std::max (0.5, 1.0 / std::sqrt (std::max (s, 1e-9))));
            };

            if (coreStart > oStart && sL > 1e-9)
            {
                auto& seg = segments[(size_t) numSegments++];
                seg.o0 = oStart;
                seg.o1 = coreStart;
                seg.dm = 1.0 / sL;
                seg.m0 = (double) a + ((double) oStart - outA) * seg.dm;
                seg.dph = psi * (1.0 - seg.dm);
                seg.ph0 = theta + psi * ((double) oStart - seg.m0 - delta);
                seg.gain = flankGain (sL);
            }

            if (coreEnd > coreStart)
            {
                auto& seg = segments[(size_t) numSegments++];
                seg.o0 = coreStart;
                seg.o1 = coreEnd;
                seg.dm = 1.0;
                seg.m0 = (double) coreStart - delta;
                seg.dph = 0.0;
                seg.ph0 = theta;
                seg.gain = 1.0f;
            }

            if (oEnd > coreEnd && sR > 1e-9)
            {
                auto& seg = segments[(size_t) numSegments++];
                seg.o0 = coreEnd;
                seg.o1 = oEnd;
                seg.dm = 1.0 / sR;
                seg.m0 = inR + ((double) coreEnd - outR) * seg.dm;
                seg.dph = psi * (1.0 - seg.dm);
                seg.ph0 = theta + psi * ((double) coreEnd - seg.m0 - delta);
                seg.gain = flankGain (sR);
            }
        }

        return numSegments;
    }

    //==========================================================================
    /** Looks for a sudden rise in level in the newest part of the input frame.
        Returns its position as a frame index, or -1 if nothing stands out. */
    int findAttackEdge() const
    {
        const int block = std::max (8, N / 128);
        const int first = N - 3 * H;
        const int numBlocks = (N - first) / block;
        const int history = 4;

        double best = 0.0, total = 0.0;
        int bestBlock = -1;
        double prev[history] = { 0.0, 0.0, 0.0, 0.0 };

        for (int j = -history; j < numBlocks; ++j)
        {
            double e = 0.0;
            for (int c = 0; c < C; ++c)
            {
                const float* x = inBuf[(size_t) c].data() + first + j * block;
                for (int i = 0; i < block; ++i)
                    e += (double) x[i] * (double) x[i];
            }

            if (j >= 0)
            {
                const double before = (prev[0] + prev[1] + prev[2] + prev[3]) / history;
                const double score = e / (before + 1e-12);
                if (score > best) { best = score; bestBlock = j; }
                total += e;
            }

            prev[0] = prev[1]; prev[1] = prev[2]; prev[2] = prev[3]; prev[3] = e;
        }

        if (bestBlock < 0 || best < 4.0 || total <= 0.0)
            return -1;

        // refine to the first sample in that block that is clearly part of the attack
        const int start = first + bestBlock * block;
        float peak = 0.0f;
        for (int c = 0; c < C; ++c)
            for (int i = 0; i < block; ++i)
                peak = std::max (peak, std::abs (inBuf[(size_t) c][(size_t) (start + i)]));

        for (int i = 0; i < block; ++i)
            for (int c = 0; c < C; ++c)
                if (std::abs (inBuf[(size_t) c][(size_t) (start + i)]) >= 0.25f * peak)
                    return start + i;

        return start;
    }

    //==========================================================================
    void applySegment (const Segment& s, const cf* Xc)
    {
        double rotR = std::cos (s.ph0) * (double) s.gain;
        double rotI = std::sin (s.ph0) * (double) s.gain;
        const double stepR = std::cos (s.dph);
        const double stepI = std::sin (s.dph);
        const bool rotating = s.dph != 0.0;
        const double maxPos = (double) (K - 1);

        for (int o = s.o0; o < s.o1; ++o)
        {
            const double p = std::min (maxPos, std::max (0.0, s.m0 + (double) (o - s.o0) * s.dm));
            int i = (int) p;
            int phase = (int) ((p - (double) i) * (double) kernelPhases + 0.5);
            if (phase >= kernelPhases) { phase = 0; ++i; }

            const float* h = kernel.data() + (size_t) phase * taps;
            const cf* src = Xc + i - (taps / 2 - 1);

            float re = 0.0f, im = 0.0f;
            for (int t = 0; t < taps; ++t)
            {
                re += src[t].real() * h[t];
                im += src[t].imag() * h[t];
            }

            Y[(size_t) o] = cf ((float) ((double) re * rotR - (double) im * rotI),
                                (float) ((double) re * rotI + (double) im * rotR));

            if (rotating)
            {
                const double nr = rotR * stepR - rotI * stepI;
                rotI = rotR * stepI + rotI * stepR;
                rotR = nr;
            }
        }
    }

    //==========================================================================
    int C = 2, N = 0, H = 0, M = 0, K = 0, pos = 0;
    double ratio = 1.0;
    float onsetThreshold = 3.0f;
    bool transientLock = true;
    float outputScale = 1.0f;

    FFT fft;
    std::vector<float> window, kernel;
    std::vector<std::vector<float>> inBuf, ola, outReady;
    std::vector<std::vector<cf>> X, Xprev;
    std::vector<cf> Y, work;
    std::vector<float> E, Eprev, thetaPrev, thetaNew;
    std::vector<uint8_t> hold, peakOnset;
    std::vector<cf> peakSlope;
    std::vector<int> peaks, bounds;
    std::vector<Segment> segments;
};

} // namespace semitone
