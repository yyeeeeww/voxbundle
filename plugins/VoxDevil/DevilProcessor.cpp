#include "DevilProcessor.h"

using namespace vox;

juce::AudioProcessorValueTreeState::ParameterLayout DevilProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeFloat ("input", "Input", 0.0f, 24.0f, 6.0f, Unit::dB));
    p.push_back (makeFloat ("crush", "Crush", 0.0f, 100.0f, 50.0f, Unit::Percent));
    p.push_back (makeFloat ("attack", "Attack", 0.05f, 30.0f, 0.8f, Unit::ms, 3.0f));
    p.push_back (makeFloat ("release", "Release", 20.0f, 800.0f, 90.0f, Unit::ms, 120.0f));
    p.push_back (makeFloat ("saturate", "Saturate", 0.0f, 100.0f, 35.0f, Unit::Percent));
    p.push_back (makeFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent));
    p.push_back (makeFloat ("output", "Output", -24.0f, 12.0f, -3.0f, Unit::dB));
    return { p.begin(), p.end() };
}

DevilProcessor::DevilProcessor() : VoxProcessorBase ("VoxDevil", createLayout(), false)
{
    input = raw ("input"); crush = raw ("crush"); attack = raw ("attack"); release = raw ("release");
    saturate = raw ("saturate"); mix = raw ("mix"); output = raw ("output");
}

void DevilProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    gr = env = 0.0f;
    lastOut[0] = lastOut[1] = 0.0f;
    scHp.set (Biquad::Type::HighPass, fs, 110.0, 0.7071);
    scState.reset();
    for (auto& d : dc) { d.prepare (fs); d.reset(); }
    for (auto* s : { &inSm, &mixSm, &outSm, &satSm })
        s->reset (fs, 0.03);
    inSm.setCurrentAndTargetValue (dbToGain (*input));
    mixSm.setCurrentAndTargetValue (*mix * 0.01f);
    outSm.setCurrentAndTargetValue (dbToGain (*output));
    satSm.setCurrentAndTargetValue (*saturate * 0.01f);
}

void DevilProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    const bool hell = devil();
    // DEVIL: crush past 100 %, near-infinite ratio, fast pumping release, full saturation.
    const float c = hell ? 1.35f : *crush * 0.01f;
    const float threshold = -4.0f - 36.0f * c;
    const float ratio = 2.0f + 28.0f * c * c;
    const float aA = timeCoef ((hell ? 0.05f : attack->load()) * 0.001, fs);
    const float aR = timeCoef ((hell ? 35.0f : release->load()) * 0.001, fs);
    inSm.setTargetValue (dbToGain (*input));
    mixSm.setTargetValue (*mix * 0.01f);
    outSm.setTargetValue (dbToGain (*output));
    satSm.setTargetValue (hell ? 1.0f : *saturate * 0.01f);

    const float envRel = timeCoef (0.012, fs);
    float grMin = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float ig = inSm.getNextValue(), m = mixSm.getNextValue(), o = outSm.getNextValue(), sat = satSm.getNextValue();

        // Feedback detection: level of the previous *output*, high-passed so lows don't pump.
        float fbSum = 0.0f;
        for (int ch = 0; ch < nch; ++ch) fbSum += lastOut[ch];
        const float h = std::abs (scState.process (scHp, fbSum / (float) nch));
        env = h > env ? h : env * envRel;
        const float over = gainToDb (env) - threshold;
        const float target = over > 0.0f ? -over * (1.0f - 1.0f / ratio) : 0.0f;
        const float coef = target < gr ? aA : aR;
        gr = juce::jmax (-48.0f, coef * gr + (1.0f - coef) * target);
        grMin = juce::jmin (grMin, gr);
        const float g = dbToGain (gr);

        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = buffer.getSample (ch, i) * ig;
            float y = x * g;
            // Saturation inside the loop: asymmetric, gets dirtier as it is pushed.
            const float drive = 1.0f + 6.0f * sat;
            const float shaped = (std::tanh (drive * (y + 0.1f * sat)) - std::tanh (drive * 0.1f * sat)) / std::tanh (drive);
            y = dc[ch].process (y + (shaped - y) * sat);
            lastOut[ch] = y;
            // Parallel blend with the (input-gained) dry signal, plus fixed makeup.
            const float wet = y * dbToGain (-threshold * 0.3f * (1.0f - 1.0f / ratio));
            buffer.setSample (ch, i, (x * (1.0f - m) + wet * m) * o);
        }
    }
    atomicMin (grDb, grMin);
}
