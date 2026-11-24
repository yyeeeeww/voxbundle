#pragma once
// Small, allocation-free DSP building blocks shared by all Vox plugins.

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace vox
{
constexpr float pi = 3.14159265358979323846f;
constexpr double piD = 3.14159265358979323846;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -120.0f) noexcept
{
    return g > 1.0e-6f ? 20.0f * std::log10 (g) : floorDb;
}

// One-pole smoothing coefficient for a time constant in seconds.
inline float timeCoef (double seconds, double sampleRate) noexcept
{
    return seconds <= 0.0 ? 0.0f : (float) std::exp (-1.0 / (seconds * sampleRate));
}

//==============================================================================
// RBJ-cookbook biquad coefficients (transposed direct form II processing).
struct Biquad
{
    enum class Type { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf, Identity };

    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    void set (Type type, double fs, double freq, double q, double gainDb = 0.0) noexcept
    {
        if (type == Type::Identity) { b0 = 1; b1 = b2 = a1 = a2 = 0; return; }

        freq = std::clamp (freq, 5.0, fs * 0.49);
        q = std::max (q, 0.05);
        const double w0 = 2.0 * piD * freq / fs;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;

        switch (type)
        {
            case Type::LowPass:
                B0 = (1 - cw) * 0.5; B1 = 1 - cw; B2 = B0;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::HighPass:
                B0 = (1 + cw) * 0.5; B1 = -(1 + cw); B2 = B0;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::BandPass:
                B0 = alpha; B1 = 0; B2 = -alpha;
                A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::Peak:
                B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A;
                A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case Type::LowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s);
                B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s;
                A1 = -2 * ((A - 1) + (A + 1) * cw);
                A2 = (A + 1) + (A - 1) * cw - s; break;
            }
            case Type::HighShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s);
                B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s;
                A1 = 2 * ((A - 1) - (A + 1) * cw);
                A2 = (A + 1) - (A - 1) * cw - s; break;
            }
            case Type::Identity: break;
        }

        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    double magnitude (double freq, double fs) const noexcept
    {
        const double w = 2.0 * piD * freq / fs;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto num = (double) b0 + (double) b1 * z1 + (double) b2 * z2;
        const auto den = 1.0 + (double) a1 * z1 + (double) a2 * z2;
        return std::abs (num / den);
    }
};

struct BiquadState
{
    float z1 = 0, z2 = 0;
    inline float process (const Biquad& c, float x) noexcept
    {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

//==============================================================================
// TPT state-variable filter, used as a perfectly-complementary band splitter.
struct SVFSplit
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.414f, ic1 = 0, ic2 = 0;

    void setCutoff (double fs, double freq, double q = 0.7071) noexcept
    {
        const double g = std::tan (piD * std::clamp (freq, 10.0, fs * 0.45) / fs);
        k = (float) (1.0 / q);
        a1 = (float) (1.0 / (1.0 + g * (g + k)));
        a2 = (float) g * a1;
        a3 = (float) g * a2;
    }
    // Splits x into a high band and the remainder (low + x - high == x exactly).
    inline void process (float x, float& low, float& high) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        high = x - k * v1 - v2;
        low = x - high;
    }
    void reset() noexcept { ic1 = ic2 = 0; }
};

//==============================================================================
struct OnePoleLP
{
    float a = 0, z = 0;
    void setCutoff (double fs, double freq) noexcept { a = (float) std::exp (-2.0 * piD * freq / fs); }
    inline float process (float x) noexcept { z = x + a * (z - x); return z; }
    void reset() noexcept { z = 0; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (double fs, double freq) noexcept { lp.setCutoff (fs, freq); }
    inline float process (float x) noexcept { return x - lp.process (x); }
    void reset() noexcept { lp.reset(); }
};

struct DCBlocker
{
    float x1 = 0, y1 = 0, r = 0.995f;
    void prepare (double fs) noexcept { r = (float) std::exp (-2.0 * piD * 20.0 / fs); }
    inline float process (float x) noexcept { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset() noexcept { x1 = y1 = 0; }
};

//==============================================================================
// Circular delay buffer with fractional (linear-interpolated) reads.
struct DelayBuffer
{
    std::vector<float> data;
    int writePos = 0;

    void setSize (int n) { data.assign ((size_t) std::max (n, 4), 0.0f); writePos = 0; }
    void reset() { std::fill (data.begin(), data.end(), 0.0f); writePos = 0; }
    int size() const noexcept { return (int) data.size(); }

    // Read the value written `delay` samples ago (delay >= 1). Call before push().
    inline float read (float delay) const noexcept
    {
        // Integer index arithmetic: a float read position can round up to exactly n
        // and index past the end of the buffer.
        const int n = (int) data.size();
        delay = std::clamp (delay, 1.0f, (float) (n - 2));
        const int whole = (int) delay;
        const float frac = delay - (float) whole;
        int i0 = writePos - whole;          // written `whole` samples ago
        if (i0 < 0) i0 += n;
        int i1 = i0 - 1;                    // one sample older
        if (i1 < 0) i1 += n;
        return data[(size_t) i0] + frac * (data[(size_t) i1] - data[(size_t) i0]);
    }
    inline void push (float x) noexcept
    {
        data[(size_t) writePos] = x;
        if (++writePos == (int) data.size()) writePos = 0;
    }
};

// Schroeder allpass diffuser.
struct Allpass
{
    DelayBuffer buf;
    float delay = 1;
    inline float process (float x, float g) noexcept
    {
        const float d = buf.read (delay);
        const float w = x + g * d;
        buf.push (w);
        return d - g * w;
    }
};

} // namespace vox
