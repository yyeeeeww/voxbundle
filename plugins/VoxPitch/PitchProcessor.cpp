#include "PitchProcessor.h"

using namespace vox;

juce::AudioProcessorValueTreeState::ParameterLayout PitchProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto semis = [] (float v) { return juce::String (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; };
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "shift", 1 }, "Shift", juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([semis] (float v, int) { return semis (v); })));
    p.push_back (makeFloat ("fine", "Fine", -100.0f, 100.0f, 0.0f, Unit::Cents));
    p.push_back (makeFloat ("grain", "Grain", 10.0f, 80.0f, 30.0f, Unit::ms, 30.0f));
    p.push_back (makeBool ("track", "Track", true));
    p.push_back (makeFloat ("tone", "Tone", 1000.0f, 20000.0f, 20000.0f, Unit::Hz, 6000.0f));
    p.push_back (makeFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent));
    p.push_back (makeFloat ("output", "Output", -18.0f, 18.0f, 0.0f, Unit::dB));
    return { p.begin(), p.end() };
}

PitchProcessor::PitchProcessor() : VoxProcessorBase ("VoxPitch", createLayout(), false)
{
    shift = raw ("shift"); fine = raw ("fine"); grain = raw ("grain"); track = raw ("track");
    tone = raw ("tone"); mix = raw ("mix"); output = raw ("output");
}

void PitchProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    detector.prepare (fs);
    for (int ch = 0; ch < 2; ++ch)
    {
        shifter[ch].prepare (fs, 0.1); demon[ch].prepare (fs, 0.1);
        dryDelay[ch].setSize ((int) (0.1 * fs));
        toneState[ch].reset();
    }
    f0 = 0.0f;
    setLatencySamples ((int) std::round (0.015 * fs));
    ratioSm.reset (fs, 0.05);
    ratioSm.setCurrentAndTargetValue (std::exp2 (totalSemitones() / 12.0f));
    for (auto* s : { &mixSm, &outSm, &demonSm })
        s->reset (fs, 0.03);
    mixSm.setCurrentAndTargetValue (*mix * 0.01f);
    outSm.setCurrentAndTargetValue (dbToGain (*output));
    demonSm.setCurrentAndTargetValue (devil() ? 1.0f : 0.0f);
}

void PitchProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    const bool hell = devil();
    const bool tracking = *track > 0.5f;
    ratioSm.setTargetValue (std::exp2 (totalSemitones() / 12.0f));
    mixSm.setTargetValue (*mix * 0.01f);
    outSm.setTargetValue (dbToGain (*output));
    demonSm.setTargetValue (hell ? 1.0f : 0.0f);   // DEVIL: extra layer an octave + tritone below
    toneLp.set (Biquad::Type::LowPass, fs, *tone, 0.7071);
    const float latency = (float) getLatencySamples();

    if (! tracking)
        for (int ch = 0; ch < 2; ++ch) { shifter[ch].setWindowSeconds (*grain * 0.001f); demon[ch].setWindowSeconds (*grain * 0.0012f); }

    for (int i = 0; i < n; ++i)
    {
        if (tracking)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < nch; ++ch) mono += buffer.getSample (ch, i);
            if (detector.push (mono / (float) nch))
            {
                f0 = detector.frequency();
                for (int ch = 0; ch < 2; ++ch)
                {
                    shifter[ch].setPitchSyncWindow (f0, *grain * 0.001f);
                    demon[ch].setPitchSyncWindow (f0, *grain * 0.0012f);
                }
            }
        }

        const float ratio = ratioSm.getNextValue();
        const float m = mixSm.getNextValue(), o = outSm.getNextValue(), dm = demonSm.getNextValue();
        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            dryDelay[ch].push (x);
            const float dry = dryDelay[ch].read (1.0f + latency);
            float wet = 1.19f * shifter[ch].process (x, ratio);   // +1.5 dB grain crossfade loss
            wet += dm * 0.9f * demon[ch].process (x, ratio * 0.3536f);   // -18 st
            wet = toneState[ch].process (toneLp, wet);
            buffer.setSample (ch, i, (dry * (1.0f - m) + wet * m) * o);
        }
    }
}
