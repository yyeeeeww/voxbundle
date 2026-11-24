#pragma once
// Pitch tools shared by VoxTune / VoxPitch / VoxDouble / VoxAdlib:
//  - PitchDetector: YIN on a ~12 kHz decimated signal, new estimate every ~5 ms
//  - PitchShifter:  two-tap rotating delay with sin^2 crossfade; the window can be
//                   made pitch-synchronous so the taps sum in phase (no comb/metal)

#include "VoxDSP.h"
#include <array>
#include <cstdint>

namespace vox
{
class PitchDetector
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        decim = std::max (1, (int) std::round (fs / 12000.0));
        dsFs = fs / decim;
        lp1.set (Biquad::Type::LowPass, fs, dsFs * 0.4, 0.5412);
        lp2.set (Biquad::Type::LowPass, fs, dsFs * 0.4, 1.3066);
        window = (int) (0.022 * dsFs);
        tauMax = std::min ((int) (dsFs / 65.0), (int) ring.size() - window - 2);
        tauMin = std::max (2, (int) (dsFs / 1100.0));
        hop = std::max (1, (int) (0.005 * dsFs));
        reset();
    }

    void reset()
    {
        ring.fill (0.0f);
        s1.reset(); s2.reset();
        writePos = decimCount = hopCount = 0;
        freq = 0.0f;
    }

    // Feed one full-rate sample. Returns true when a new estimate is available.
    bool push (float x) noexcept
    {
        const float y = s2.process (lp2, s1.process (lp1, x));
        if (++decimCount < decim) return false;
        decimCount = 0;
        ring[(size_t) writePos] = y;
        writePos = (writePos + 1) & mask;
        if (++hopCount < hop) return false;
        hopCount = 0;
        analyse();
        return true;
    }

    float frequency() const noexcept { return freq; }   // 0 when unvoiced / silent

private:
    void analyse() noexcept
    {
        const int n = window + tauMax + 1;
        float energy = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            lin[(size_t) i] = ring[(size_t) ((writePos - n + i) & mask)];
            if (i < window) energy += lin[(size_t) i] * lin[(size_t) i];
        }
        if (energy / (float) window < 1.0e-6f) { freq = 0.0f; return; }   // below ~-60 dBFS

        // Cumulative mean normalised difference
        float running = 0.0f;
        diff[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const float delta = lin[(size_t) j] - lin[(size_t) (j + tau)];
                d += delta * delta;
            }
            running += d;
            diff[(size_t) tau] = running > 0.0f ? d * (float) tau / running : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (diff[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && diff[(size_t) (tau + 1)] < diff[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0) { freq = 0.0f; return; }

        const float a = diff[(size_t) (best - 1)], b = diff[(size_t) best], c = diff[(size_t) (best + 1)];
        const float den = a - 2.0f * b + c;
        const float shift = std::abs (den) > 1.0e-9f ? 0.5f * (a - c) / den : 0.0f;
        freq = (float) (dsFs / ((float) best + std::clamp (shift, -1.0f, 1.0f)));
    }

    static constexpr int ringSize = 1024, mask = ringSize - 1;
    double fs = 44100.0, dsFs = 11025.0;
    int decim = 4, window = 256, tauMax = 180, tauMin = 10, hop = 55;
    int writePos = 0, decimCount = 0, hopCount = 0;
    float freq = 0.0f;
    Biquad lp1, lp2;
    BiquadState s1, s2;
    std::array<float, ringSize> ring {}, lin {}, diff {};
};

//==============================================================================
class PitchShifter
{
public:
    void prepare (double sampleRate, double maxWindowSeconds = 0.08)
    {
        fs = sampleRate;
        buf.setSize ((int) (maxWindowSeconds * fs) + 16);
        maxWindow = (float) (maxWindowSeconds * fs);
        setWindowSeconds (0.03f);
        latched[0] = latched[1] = targetWindow;
        reset();
    }

    void reset()
    {
        buf.reset();
        phase = 0.0f;
        prevFrac[0] = 0.0f; prevFrac[1] = 0.5f;
        latched[0] = latched[1] = targetWindow;
    }

    void setWindowSamples (float w) noexcept { targetWindow = std::clamp (w, 64.0f, maxWindow); }
    void setWindowSeconds (float s) noexcept { setWindowSamples (s * (float) fs); }

    // Window that is an even number of pitch periods (~targetSeconds long).
    void setPitchSyncWindow (float f0, float targetSeconds = 0.024f) noexcept
    {
        if (f0 <= 0.0f) { setWindowSeconds (targetSeconds * 1.25f); return; }
        const float period = (float) fs / f0;
        const float m = std::max (1.0f, std::round (targetSeconds * (float) fs / (2.0f * period)));
        setWindowSamples (2.0f * m * period);
    }

    float averageDelay() const noexcept { return 0.5f * targetWindow; }

    inline float process (float x, float ratio) noexcept
    {
        buf.push (x);
        phase += (1.0f - ratio) / targetWindow;
        phase -= std::floor (phase);

        float y = 0.0f;
        for (int t = 0; t < 2; ++t)
        {
            float frac = phase + 0.5f * (float) t;
            if (frac >= 1.0f) frac -= 1.0f;
            if (std::abs (frac - prevFrac[t]) > 0.5f)   // tap wrapped (silent) => adopt new window
                latched[t] = targetWindow;
            prevFrac[t] = frac;
            const float s = std::sin (pi * frac);
            y += s * s * buf.read (1.0f + frac * latched[t]);
        }
        return y;
    }

private:
    double fs = 44100.0;
    DelayBuffer buf;
    float maxWindow = 2048.0f, targetWindow = 1024.0f, phase = 0.0f;
    float latched[2] { 1024.0f, 1024.0f }, prevFrac[2] { 0.0f, 0.5f };
};

// Smoothed random walk used for natural doubling drift.
struct Drift
{
    float value = 0.0f, target = 0.0f, coef = 0.999f;
    int counter = 0, period = 4800;
    std::uint32_t seed = 1;

    void prepare (double fs, float rateHz, std::uint32_t s)
    {
        period = std::max (1, (int) (fs / rateHz));
        coef = timeCoef (0.5 / rateHz, fs);
        seed = s;
        counter = 0;
    }
    inline float next() noexcept   // -1..1, slowly wandering
    {
        if (--counter <= 0)
        {
            counter = period;
            seed = seed * 1664525u + 1013904223u;
            target = (float) (seed >> 8) / (float) (1 << 24) * 2.0f - 1.0f;
        }
        value = target + coef * (value - target);
        return value;
    }
};

} // namespace vox
