#include "DoubleProcessor.h"

using namespace vox;

namespace
{
// Per-voice spread: pitch sign/scale, delay scale, pan position.
constexpr float pitchScale[DoubleProcessor::maxVoices] = { 1.0f, -1.0f, 0.55f, -0.6f };
constexpr float delayScale[DoubleProcessor::maxVoices] = { 1.0f, 0.72f, 1.45f, 1.2f };
constexpr float panPos[DoubleProcessor::maxVoices]     = { -1.0f, 1.0f, -0.5f, 0.5f };
}

juce::AudioProcessorValueTreeState::ParameterLayout DoubleProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeChoice ("voices", "Voices", { "2", "4" }, 0));
    p.push_back (makeFloat ("detune", "Detune", 0.0f, 40.0f, 9.0f, Unit::Cents));
    p.push_back (makeFloat ("delay", "Delay", 2.0f, 60.0f, 16.0f, Unit::ms, 18.0f));
    p.push_back (makeFloat ("drift", "Drift", 0.0f, 100.0f, 40.0f, Unit::Percent));
    p.push_back (makeFloat ("width", "Width", 0.0f, 100.0f, 90.0f, Unit::Percent));
    p.push_back (makeFloat ("lowCut", "Low Cut", 20.0f, 800.0f, 160.0f, Unit::Hz, 150.0f));
    p.push_back (makeFloat ("doubles", "Doubles", 0.0f, 100.0f, 70.0f, Unit::Percent));
    p.push_back (makeFloat ("dry", "Dry", 0.0f, 100.0f, 100.0f, Unit::Percent));
    return { p.begin(), p.end() };
}

DoubleProcessor::DoubleProcessor() : VoxProcessorBase ("VoxDouble", createLayout(), true)
{
    voices = raw ("voices"); detune = raw ("detune"); delay = raw ("delay"); drift = raw ("drift");
    width = raw ("width"); lowCut = raw ("lowCut"); doubles = raw ("doubles"); dry = raw ("dry");
}

void DoubleProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (int v = 0; v < maxVoices; ++v)
    {
        shifter[v].prepare (fs, 0.06);
        shifter[v].setWindowSeconds (0.045f);
        line[v].setSize ((int) (0.2 * fs));
        timeDrift[v].prepare (fs, 0.7f + 0.23f * (float) v, 1000u + 77u * (unsigned) v);
        pitchDrift[v].prepare (fs, 0.45f + 0.17f * (float) v, 5000u + 31u * (unsigned) v);
        hpState[v].reset();
        countSm[v].reset (fs, 0.05);
        countSm[v].setCurrentAndTargetValue (v < 2 || *voices > 0.5f ? 1.0f : 0.0f);
    }
    for (auto* s : { &levelSm, &drySm, &widthSm })
        s->reset (fs, 0.03);
    levelSm.setCurrentAndTargetValue (*doubles * 0.01f);
    drySm.setCurrentAndTargetValue (*dry * 0.01f);
    widthSm.setCurrentAndTargetValue (*width * 0.01f);
}

void DoubleProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    const bool hell = devil();
    // DEVIL: all four voices, detune x4, maximum drift => a smeared, unstable choir.
    const bool four = hell || *voices > 0.5f;
    const float cents = *detune * (hell ? 4.0f : 1.0f);
    const float driftAmt = hell ? 1.0f : *drift * 0.01f;
    const float baseDelay = *delay * 0.001f * (float) fs;
    hp.set (Biquad::Type::HighPass, fs, *lowCut, 0.7071);
    levelSm.setTargetValue (*doubles * 0.01f);
    drySm.setTargetValue (*dry * 0.01f);
    widthSm.setTargetValue (*width * 0.01f);
    for (int v = 0; v < maxVoices; ++v)
        countSm[v].setTargetValue (v < 2 || four ? 1.0f : 0.0f);

    float lastCents[maxVoices] {}, lastDelay[maxVoices] {};

    for (int i = 0; i < n; ++i)
    {
        const float xl = buffer.getSample (0, i);
        const float xr = nch > 1 ? buffer.getSample (1, i) : xl;
        const float mono = 0.5f * (xl + xr);
        const float lvl = levelSm.getNextValue(), d = drySm.getNextValue(), w = widthSm.getNextValue();

        float outL = 0.0f, outR = 0.0f;
        for (int v = 0; v < maxVoices; ++v)
        {
            const float active = countSm[v].getNextValue();
            const float pd = pitchDrift[v].next(), td = timeDrift[v].next();
            const float c = cents * pitchScale[v] * (1.0f + 0.5f * driftAmt * pd);
            const float delaySamples = baseDelay * delayScale[v] * (1.0f + 0.35f * driftAmt * td);
            line[v].push (mono);
            float s = line[v].read (juce::jmax (1.0f, delaySamples));
            s = shifter[v].process (s, std::exp2 (c / 1200.0f));
            s = hpState[v].process (hp, s) * active;
            const float angle = (panPos[v] * w + 1.0f) * pi * 0.25f;
            outL += s * std::cos (angle);
            outR += s * std::sin (angle);
            lastCents[v] = c * active;
            lastDelay[v] = active > 0.5f ? delaySamples / (float) fs * 1000.0f : 0.0f;
        }
        const float norm = lvl * (four ? 0.5f : 0.75f);
        outL *= norm;
        outR *= norm;
        if (hell) { outL = std::tanh (outL * 2.5f) * 0.6f; outR = std::tanh (outR * 2.5f) * 0.6f; }

        if (nch > 1)
        {
            buffer.setSample (0, i, xl * d + outL);
            buffer.setSample (1, i, xr * d + outR);
        }
        else
            buffer.setSample (0, i, xl * d + 0.5f * (outL + outR));
    }

    for (int v = 0; v < maxVoices; ++v)
    {
        voiceCents[v].store (lastCents[v]);
        voiceDelayMs[v].store (lastDelay[v]);
    }
}
