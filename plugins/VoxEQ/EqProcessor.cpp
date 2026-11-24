#include "EqProcessor.h"

using namespace vox;

juce::AudioProcessorValueTreeState::ParameterLayout EqProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeBool ("hpOn", "Low Cut", true));
    p.push_back (makeFloat ("hpFreq", "Freq", 20.0f, 600.0f, 90.0f, Unit::Hz, 120.0f));
    p.push_back (makeChoice ("hpSlope", "Slope", { "12 dB/oct", "24 dB/oct" }, 1));
    p.push_back (makeFloat ("lowFreq", "Freq", 50.0f, 600.0f, 220.0f, Unit::Hz, 180.0f));
    p.push_back (makeFloat ("lowGain", "Gain", -15.0f, 15.0f, 0.0f, Unit::dB));
    p.push_back (makeFloat ("midFreq", "Freq", 200.0f, 5000.0f, 900.0f, Unit::Hz, 1000.0f));
    p.push_back (makeFloat ("midGain", "Gain", -15.0f, 15.0f, 0.0f, Unit::dB));
    p.push_back (makeFloat ("midQ", "Q", 0.3f, 8.0f, 1.0f, Unit::Plain, 1.5f));
    p.push_back (makeFloat ("presFreq", "Freq", 1000.0f, 10000.0f, 3500.0f, Unit::Hz, 3500.0f));
    p.push_back (makeFloat ("presGain", "Gain", -15.0f, 15.0f, 0.0f, Unit::dB));
    p.push_back (makeFloat ("presQ", "Q", 0.3f, 8.0f, 1.2f, Unit::Plain, 1.5f));
    p.push_back (makeFloat ("airFreq", "Freq", 5000.0f, 20000.0f, 12000.0f, Unit::Hz, 11000.0f));
    p.push_back (makeFloat ("airGain", "Gain", -15.0f, 15.0f, 0.0f, Unit::dB));
    p.push_back (makeFloat ("output", "Output", -18.0f, 18.0f, 0.0f, Unit::dB));
    return { p.begin(), p.end() };
}

EqProcessor::EqProcessor() : VoxProcessorBase ("VoxEQ", createLayout(), false)
{
    hpOn = raw ("hpOn"); hpFreq = raw ("hpFreq"); hpSlope = raw ("hpSlope");
    lowFreq = raw ("lowFreq"); lowGain = raw ("lowGain");
    midFreq = raw ("midFreq"); midGain = raw ("midGain"); midQ = raw ("midQ");
    presFreq = raw ("presFreq"); presGain = raw ("presGain"); presQ = raw ("presQ");
    airFreq = raw ("airFreq"); airGain = raw ("airGain"); output = raw ("output");
}

EqProcessor::BandValues EqProcessor::targetValues() const
{
    BandValues v;
    // Bypassing the low cut glides the corner down to 10 Hz instead of switching => no clicks.
    v.hpFreq = *hpOn > 0.5f ? hpFreq->load() : 10.0f;
    v.hp24 = *hpSlope > 0.5f;
    v.lowFreq = *lowFreq; v.lowGain = *lowGain;
    v.midFreq = *midFreq; v.midGain = *midGain; v.midQ = *midQ;
    v.presFreq = *presFreq; v.presGain = *presGain; v.presQ = *presQ;
    v.airFreq = *airFreq; v.airGain = *airGain;
    if (devil())   // DEVIL: every boost and cut is doubled
        for (auto* g : { &v.lowGain, &v.midGain, &v.presGain, &v.airGain })
            *g = juce::jlimit (-24.0f, 24.0f, *g * 2.0f);
    return v;
}

void EqProcessor::computeCoefficients (const BandValues& v, double fs, Biquad (&c)[numStages])
{
    using T = Biquad::Type;
    if (v.hp24)
    {
        c[0].set (T::HighPass, fs, v.hpFreq, 0.5412);   // 4th-order Butterworth
        c[1].set (T::HighPass, fs, v.hpFreq, 1.3066);
    }
    else
    {
        c[0].set (T::HighPass, fs, v.hpFreq, 0.7071);
        c[1].set (T::Identity, fs, 1000.0, 1.0);
    }
    c[2].set (T::LowShelf, fs, v.lowFreq, 0.7071, v.lowGain);
    c[3].set (T::Peak, fs, v.midFreq, v.midQ, v.midGain);
    c[4].set (T::Peak, fs, v.presFreq, v.presQ, v.presGain);
    c[5].set (T::HighShelf, fs, v.airFreq, 0.7071, v.airGain);
}

void EqProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& ch : state)
        for (auto& s : ch)
            s.reset();
    firstBlock = true;
    outSm.reset (sampleRate, 0.03);
    outSm.setCurrentAndTargetValue (dbToGain (*output));
}

void EqProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    auto* const* data = buffer.getArrayOfWritePointers();
    const auto target = targetValues();

    if (firstBlock)
    {
        current = target;
        firstBlock = false;
        computeCoefficients (current, fs, coeffs);
    }
    if (target.hp24 != current.hp24)
    {
        current.hp24 = target.hp24;
        for (auto& ch : state) ch[1].reset();
    }

    outSm.setTargetValue (dbToGain (*output));

    // Parameter glide: coefficients are recomputed every 32 samples while values move.
    constexpr int chunk = 32;
    const float k = 1.0f - std::exp (-(float) chunk / (0.025f * (float) fs));

    for (int start = 0; start < n; start += chunk)
    {
        bool moving = false;
        auto glide = [&] (float& cur, float tgt, bool logScale)
        {
            if (std::abs (cur - tgt) < (logScale ? cur * 1.0e-4f : 1.0e-4f)) { cur = tgt; return; }
            cur = logScale ? cur * std::pow (tgt / cur, k) : cur + (tgt - cur) * k;
            moving = true;
        };
        glide (current.hpFreq, target.hpFreq, true);
        glide (current.lowFreq, target.lowFreq, true);
        glide (current.lowGain, target.lowGain, false);
        glide (current.midFreq, target.midFreq, true);
        glide (current.midGain, target.midGain, false);
        glide (current.midQ, target.midQ, true);
        glide (current.presFreq, target.presFreq, true);
        glide (current.presGain, target.presGain, false);
        glide (current.presQ, target.presQ, true);
        glide (current.airFreq, target.airFreq, true);
        glide (current.airGain, target.airGain, false);
        if (moving || start == 0)
            computeCoefficients (current, fs, coeffs);

        const int end = juce::jmin (n, start + chunk);
        for (int i = start; i < end; ++i)
        {
            const float g = outSm.getNextValue();
            for (int ch = 0; ch < nch; ++ch)
            {
                float x = data[ch][i];
                for (int s = 0; s < numStages; ++s)
                    x = state[ch][s].process (coeffs[s], x);
                data[ch][i] = x * g;
            }
        }
    }
}
