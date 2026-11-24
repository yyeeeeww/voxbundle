#include "CompProcessor.h"

using namespace vox;

juce::AudioProcessorValueTreeState::ParameterLayout CompProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeFloat ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, Unit::dB));
    p.push_back (makeFloat ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, Unit::Ratio, 4.0f));
    p.push_back (makeFloat ("attack", "Attack", 0.1f, 100.0f, 5.0f, Unit::ms, 10.0f));
    p.push_back (makeFloat ("release", "Release", 10.0f, 1000.0f, 120.0f, Unit::ms, 150.0f));
    p.push_back (makeFloat ("knee", "Knee", 0.0f, 18.0f, 6.0f, Unit::dB));
    p.push_back (makeFloat ("makeup", "Makeup", 0.0f, 24.0f, 3.0f, Unit::dB));
    p.push_back (makeBool ("dsOn", "De-Ess", true));
    p.push_back (makeFloat ("dsFreq", "Freq", 2000.0f, 12000.0f, 6500.0f, Unit::Hz, 5500.0f));
    p.push_back (makeFloat ("dsThresh", "Sens", -60.0f, 0.0f, -30.0f, Unit::dB));
    p.push_back (makeFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent));
    p.push_back (makeFloat ("output", "Output", -18.0f, 18.0f, 0.0f, Unit::dB));
    p.push_back (makeBool ("limiter", "Limiter", true));
    return { p.begin(), p.end() };
}

CompProcessor::CompProcessor() : VoxProcessorBase ("VoxComp", createLayout(), false)
{
    threshold = raw ("threshold"); ratio = raw ("ratio"); attack = raw ("attack"); release = raw ("release");
    knee = raw ("knee"); makeup = raw ("makeup"); dsOn = raw ("dsOn"); dsFreq = raw ("dsFreq");
    dsThresh = raw ("dsThresh"); mix = raw ("mix"); output = raw ("output"); limiter = raw ("limiter");
}

void CompProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& s : dsSplit) s.reset();
    dsEnv = compGr = limEnv = 0.0f;
    for (auto* sm : { &mixSm, &outSm, &makeupSm })
        sm->reset (sampleRate, 0.03);
    mixSm.setCurrentAndTargetValue (*mix * 0.01f);
    outSm.setCurrentAndTargetValue (dbToGain (*output));
    makeupSm.setCurrentAndTargetValue (*makeup);
}

float CompProcessor::gainComputer (float x, float T, float R, float W) noexcept
{
    const float slope = 1.0f / R - 1.0f;
    const float d = x - T;
    if (2.0f * d < -W) return 0.0f;
    if (W > 0.0f && 2.0f * std::abs (d) <= W)
    {
        const float t = d + W * 0.5f;
        return slope * t * t / (2.0f * W);
    }
    return slope * d;
}

void CompProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    auto* const* data = buffer.getArrayOfWritePointers();

    // DEVIL: slam everything into a 20:1 wall with instant attack and pumping release.
    const bool hell = devil();
    const float T = hell ? *threshold - 10.0f : threshold->load(), R = hell ? 20.0f : ratio->load(), W = hell ? 0.0f : knee->load();
    const float aA = timeCoef ((hell ? 0.1f : attack->load()) * 0.001, fs);
    const float aR = timeCoef ((hell ? 40.0f : release->load()) * 0.001, fs);
    const bool deess = *dsOn > 0.5f, limit = *limiter > 0.5f;
    const float dsT = *dsThresh;
    const float dsA = timeCoef (0.0008, fs), dsR = timeCoef (0.06, fs);
    const float limR = timeCoef (0.08, fs), ceiling = dbToGain (-0.5f);

    for (int ch = 0; ch < nch; ++ch)
        dsSplit[ch].setCutoff (fs, *dsFreq);

    mixSm.setTargetValue (*mix * 0.01f);
    outSm.setTargetValue (dbToGain (*output));
    makeupSm.setTargetValue (*makeup + (hell ? 8.0f : 0.0f));

    float grMin = 0.0f, dsMax = 0.0f, detMax = -100.0f;

    for (int i = 0; i < n; ++i)
    {
        float dry[2] = {}, x[2] = {};
        for (int ch = 0; ch < nch; ++ch)
            dry[ch] = x[ch] = data[ch][i];

        // --- De-esser: split-band, reduce only the high band.
        if (deess)
        {
            float lo[2] = {}, hi[2] = {}, det = 0.0f;
            for (int ch = 0; ch < nch; ++ch)
            {
                dsSplit[ch].process (x[ch], lo[ch], hi[ch]);
                det = juce::jmax (det, std::abs (hi[ch]));
            }
            const float c = det > dsEnv ? dsA : dsR;
            dsEnv = c * dsEnv + (1.0f - c) * det;
            const float over = gainToDb (dsEnv) - dsT;
            const float red = over > 0.0f ? juce::jmin (over * (1.0f - 1.0f / 6.0f), 24.0f) : 0.0f;
            dsMax = juce::jmax (dsMax, red);
            const float g = dbToGain (-red);
            for (int ch = 0; ch < nch; ++ch)
                x[ch] = lo[ch] + hi[ch] * g;
        }

        // --- Compressor: stereo-linked, smooth decoupled detector in the log domain.
        float peak = 0.0f;
        for (int ch = 0; ch < nch; ++ch)
            peak = juce::jmax (peak, std::abs (x[ch]));
        const float level = gainToDb (peak);
        detMax = juce::jmax (detMax, level);
        const float target = gainComputer (level, T, R, W);
        const float coef = target < compGr ? aA : aR;
        compGr = coef * compGr + (1.0f - coef) * target;
        grMin = juce::jmin (grMin, compGr);

        const float g = dbToGain (compGr + makeupSm.getNextValue());
        const float m = mixSm.getNextValue(), o = outSm.getNextValue();
        float y[2] = {};
        float ypk = 0.0f;
        for (int ch = 0; ch < nch; ++ch)
        {
            y[ch] = (dry[ch] * (1.0f - m) + x[ch] * g * m) * o;
            ypk = juce::jmax (ypk, std::abs (y[ch]));
        }

        // --- Output limiter: instant attack peak follower => never exceeds the ceiling.
        if (limit)
        {
            limEnv = juce::jmax (ypk, limEnv * limR);
            const float lg = limEnv > ceiling ? ceiling / limEnv : 1.0f;
            for (int ch = 0; ch < nch; ++ch)
                y[ch] *= lg;
        }

        for (int ch = 0; ch < nch; ++ch)
            data[ch][i] = y[ch];
    }

    atomicMin (grDb, grMin);
    atomicMax (deessDb, dsMax);
    atomicMax (detectorDb, detMax);
}
