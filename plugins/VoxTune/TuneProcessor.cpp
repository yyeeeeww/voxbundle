#include "TuneProcessor.h"

using namespace vox;

const juce::StringArray TuneProcessor::keyNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
const juce::StringArray TuneProcessor::scaleNames { "Chromatic", "Major", "Minor", "Harm Minor", "Maj Penta", "Min Penta" };

namespace
{
const std::vector<std::vector<int>> scaleSteps = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
    { 0, 2, 4, 5, 7, 9, 11 },
    { 0, 2, 3, 5, 7, 8, 10 },
    { 0, 2, 3, 5, 7, 8, 11 },
    { 0, 2, 4, 7, 9 },
    { 0, 3, 5, 7, 10 },
};
}

int TuneProcessor::snap (float midi, int keyIndex, int scaleIndex) noexcept
{
    const auto& steps = scaleSteps[(size_t) juce::jlimit (0, (int) scaleSteps.size() - 1, scaleIndex)];
    int best = juce::roundToInt (midi);
    float bestDist = 1.0e9f;
    const int base = (int) std::floor (midi) - 12;
    for (int n = base; n <= base + 24; ++n)
    {
        const int pc = ((n - keyIndex) % 12 + 12) % 12;
        if (std::find (steps.begin(), steps.end(), pc) == steps.end()) continue;
        const float d = std::abs ((float) n - midi);
        if (d < bestDist) { bestDist = d; best = n; }
    }
    return best;
}

juce::AudioProcessorValueTreeState::ParameterLayout TuneProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeChoice ("key", "Key", keyNames, 9));
    p.push_back (makeChoice ("scale", "Scale", scaleNames, 2));
    p.push_back (makeFloat ("retune", "Retune", 0.0f, 400.0f, 20.0f, Unit::ms, 60.0f));
    p.push_back (makeFloat ("amount", "Amount", 0.0f, 100.0f, 100.0f, Unit::Percent));
    p.push_back (makeFloat ("humanize", "Humanize", 0.0f, 100.0f, 0.0f, Unit::Percent));
    p.push_back (makeFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent));
    p.push_back (makeFloat ("output", "Output", -18.0f, 18.0f, 0.0f, Unit::dB));
    return { p.begin(), p.end() };
}

TuneProcessor::TuneProcessor() : VoxProcessorBase ("VoxTune", createLayout(), false)
{
    key = raw ("key"); scale = raw ("scale"); retune = raw ("retune"); amount = raw ("amount");
    humanize = raw ("humanize"); mix = raw ("mix"); output = raw ("output");
}

void TuneProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    detector.prepare (fs);
    for (int ch = 0; ch < 2; ++ch)
    {
        shifter[ch].prepare (fs); demon[ch].prepare (fs);
        dryDelay[ch].setSize ((int) (0.1 * fs));
    }
    f0 = correction = correctionTarget = 0.0f;
    heldNote = -1;
    // The shifter's mean delay is ~12 ms; report it so the host can compensate.
    setLatencySamples ((int) std::round (0.012 * fs));
    for (auto* s : { &mixSm, &outSm, &demonSm })
        s->reset (fs, 0.03);
    mixSm.setCurrentAndTargetValue (*mix * 0.01f);
    outSm.setCurrentAndTargetValue (dbToGain (*output));
    demonSm.setCurrentAndTargetValue (devil() ? 1.0f : 0.0f);
}

void TuneProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    const bool hell = devil();
    const int k = (int) *key, sc = (int) *scale;
    const float amt = *amount * 0.01f;
    // DEVIL: zero retune time (hard robot) + an octave-down "demon" layer.
    const float retuneMs = hell ? 0.0f : retune->load();
    const float corrCoef = timeCoef (retuneMs * 0.001, fs);
    const float relaxCoef = timeCoef (0.04, fs);
    const float hum = *humanize * 0.01f;
    mixSm.setTargetValue (*mix * 0.01f);
    outSm.setTargetValue (dbToGain (*output));
    demonSm.setTargetValue (hell ? 1.0f : 0.0f);
    const float latency = (float) getLatencySamples();

    float lastDetected = 0.0f, lastTarget = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < nch; ++ch) mono += buffer.getSample (ch, i);
        mono /= (float) nch;

        if (detector.push (mono))
        {
            f0 = detector.frequency();
            if (f0 > 0.0f)
            {
                const float midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
                // Hysteresis: stay on the held note unless clearly closer to another one.
                int note = snap (midi, k, sc);
                if (heldNote >= 0 && note != heldNote && std::abs (midi - (float) heldNote) < 0.62f)
                    note = heldNote;
                heldNote = note;
                // Humanize: let sustained deviations through partially.
                const float err = (float) note - midi;
                correctionTarget = amt * err * (1.0f - hum * juce::jlimit (0.0f, 1.0f, std::abs (err) * 3.0f));
                lastDetected = midi;
                lastTarget = midi + correctionTarget;
            }
            else
            {
                correctionTarget = 0.0f;
                heldNote = -1;
            }
            for (auto& s : shifter) s.setPitchSyncWindow (f0);
            for (auto& s : demon) s.setPitchSyncWindow (f0 * 0.5f, 0.03f);
        }

        const float c = f0 > 0.0f ? corrCoef : relaxCoef;
        correction = correctionTarget + c * (correction - correctionTarget);
        const float ratio = std::exp2 (correction / 12.0f);

        const float m = mixSm.getNextValue(), o = outSm.getNextValue(), dm = demonSm.getNextValue();
        for (int ch = 0; ch < nch; ++ch)
        {
            const float x = buffer.getSample (ch, i);
            dryDelay[ch].push (x);
            const float dry = dryDelay[ch].read (1.0f + latency);
            float wet = 1.19f * shifter[ch].process (x, ratio);   // +1.5 dB grain crossfade loss
            wet += dm * 0.8f * demon[ch].process (x, ratio * 0.5f);
            buffer.setSample (ch, i, (dry * (1.0f - m) + wet * m) * o);
        }
    }

    if (lastDetected > 0.0f)
    {
        detectedMidi.store (lastDetected);
        targetMidi.store (lastTarget);
    }
    else if (f0 <= 0.0f)
    {
        detectedMidi.store (0.0f);
        targetMidi.store (0.0f);
    }
}
