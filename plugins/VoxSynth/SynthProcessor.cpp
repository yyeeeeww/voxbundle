#include "SynthProcessor.h"

using namespace vox;

namespace
{
juce::String noteName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
}

const std::vector<std::vector<int>> chordIntervals = {
    { 0 }, { 0, 12 }, { 0, 7, 12 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 5, 7 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 7, 12, 16, 19 }
};

inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

inline float blepSaw (float phase, float dt) noexcept
{
    return 2.0f * phase - 1.0f - polyBlep (phase, dt);
}

inline float blepPulse (float phase, float dt) noexcept
{
    float p2 = phase + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;
    return blepSaw (phase, dt) - blepSaw (p2, dt);
}
} // namespace

const juce::StringArray SynthProcessor::chordNames { "Unison", "Octave", "Power", "Major", "Minor", "Sus4", "Maj7", "Min7", "Wide" };

juce::AudioProcessorValueTreeState::ParameterLayout SynthProcessor::createLayout()
{
    auto waveText = [] (float v)
    {
        if (v < 0.02f) return juce::String ("Saw");
        if (v > 0.98f) return juce::String ("Square");
        return juce::String (juce::roundToInt (100.0f * (1.0f - v))) + "/" + juce::String (juce::roundToInt (100.0f * v));
    };

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeChoice ("source", "Notes", { "Drone", "MIDI" }, 0));
    p.push_back (makeInt ("droneNote", "Root", 36, 72, 48, [] (int n) { return noteName (n); }));
    p.push_back (makeChoice ("chord", "Chord", chordNames, 4));
    p.push_back (makeFloat ("wave", "Wave", 0.0f, 1.0f, 0.0f, Unit::Plain, 0.0f, waveText));
    p.push_back (makeFloat ("detune", "Detune", 0.0f, 50.0f, 12.0f, Unit::Cents));
    p.push_back (makeFloat ("noise", "Noise", 0.0f, 100.0f, 10.0f, Unit::Percent));
    p.push_back (makeFloat ("attack", "Attack", 1.0f, 2000.0f, 10.0f, Unit::ms, 100.0f));
    p.push_back (makeFloat ("release", "Release", 10.0f, 4000.0f, 250.0f, Unit::ms, 400.0f));
    p.push_back (makeFloat ("response", "Response", 5.0f, 300.0f, 35.0f, Unit::ms, 40.0f));
    p.push_back (makeFloat ("formant", "Formant", -12.0f, 12.0f, 0.0f, Unit::Semitones));
    p.push_back (makeFloat ("sibilance", "Sibilance", 0.0f, 100.0f, 35.0f, Unit::Percent));
    p.push_back (makeFloat ("spread", "Spread", 0.0f, 100.0f, 60.0f, Unit::Percent));
    p.push_back (makeFloat ("voiceMix", "Dry Voice", 0.0f, 100.0f, 0.0f, Unit::Percent));
    p.push_back (makeFloat ("output", "Output", -18.0f, 18.0f, 0.0f, Unit::dB));
    return { p.begin(), p.end() };
}

SynthProcessor::SynthProcessor() : VoxProcessorBase ("VoxSynth", createLayout(), true)
{
    source = raw ("source"); droneNote = raw ("droneNote"); chord = raw ("chord"); wave = raw ("wave");
    detune = raw ("detune"); noise = raw ("noise"); attack = raw ("attack"); release = raw ("release");
    response = raw ("response"); formant = raw ("formant"); sibilance = raw ("sibilance");
    spread = raw ("spread"); voiceMix = raw ("voiceMix"); output = raw ("output");
    for (auto& v : voiceNotes) v.store (-1);
}

void SynthProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (int b = 0; b < numBands; ++b)
    {
        modFilter[b].set (Biquad::Type::BandPass, fs, bandFrequency (b), 3.5);
        for (auto& s : modState[b]) s.reset();
        for (auto& ch : carState) for (auto& s : ch[b]) s.reset();
        modEnv[b] = 0.0f;
        carEnv[0][b] = carEnv[1][b] = 0.0f;
    }
    lastFormant = 1000.0f;
    updateCarrierFilters (*formant);
    sibHp.set (Biquad::Type::HighPass, fs, 5000.0, 0.7071);
    for (auto& s : sibState) s.reset();

    for (auto& v : voices) v = Voice();
    lastSource = lastDroneRoot = lastChord = -1;
    bendRatio = 1.0f;

    for (auto* s : { &mixSm, &outSm, &sibSm, &noiseSm })
        s->reset (sampleRate, 0.03);
    mixSm.setCurrentAndTargetValue (*voiceMix * 0.01f);
    outSm.setCurrentAndTargetValue (dbToGain (*output));
    sibSm.setCurrentAndTargetValue (*sibilance * 0.01f);
    noiseSm.setCurrentAndTargetValue (*noise * 0.01f);
}

void SynthProcessor::updateCarrierFilters (float semis)
{
    if (std::abs (semis - lastFormant) < 0.01f) return;
    lastFormant = semis;
    const float ratio = std::pow (2.0f, semis / 12.0f);
    for (int b = 0; b < numBands; ++b)
        carFilter[b].set (Biquad::Type::BandPass, fs, bandFrequency (b) * ratio, 3.5);
}

void SynthProcessor::noteOn (int note)
{
    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.note == note) { target = &v; break; }
    if (target == nullptr)
        for (auto& v : voices)
            if (! v.gate && v.level < 1.0e-4f) { target = &v; break; }
    if (target == nullptr)   // steal the oldest
    {
        target = &voices[0];
        for (auto& v : voices)
            if (v.age < target->age) target = &v;
    }
    if (target->note != note && target->level < 1.0e-4f)
    {
        target->phase1 = (float) (rng() % 1000) / 1000.0f;
        target->phase2 = (float) (rng() % 1000) / 1000.0f;
    }
    target->note = note;
    target->gate = true;
    target->age = ++ageCounter;
}

void SynthProcessor::noteOff (int note)
{
    for (auto& v : voices)
        if (v.note == note) v.gate = false;
}

void SynthProcessor::allOff()
{
    for (auto& v : voices) v.gate = false;
}

void SynthProcessor::updateDrone()
{
    const int src = (int) *source, root = (int) *droneNote, ch = juce::jlimit (0, (int) chordIntervals.size() - 1, (int) *chord);
    if (src == lastSource && root == lastDroneRoot && ch == lastChord)
        return;
    if (src != lastSource)
        allOff();
    lastSource = src; lastDroneRoot = root; lastChord = ch;
    if (src != 0)
        return;

    const auto& iv = chordIntervals[(size_t) ch];
    for (auto& v : voices)
        if (v.gate && std::find (iv.begin(), iv.end(), v.note - root) == iv.end())
            v.gate = false;
    for (int i : iv)
        noteOn (root + i);
}

float SynthProcessor::renderVoice (Voice& v, float waveMix, float detuneRatio, bool hell) noexcept
{
    const float f = 440.0f * std::pow (2.0f, (float) (v.note - 69) / 12.0f) * bendRatio;
    const float dt1 = juce::jmin (0.45f, f / (float) fs), dt2 = juce::jmin (0.45f, f * detuneRatio / (float) fs);
    const float s1 = (1.0f - waveMix) * blepSaw (v.phase1, dt1) + waveMix * blepPulse (v.phase1, dt1);
    const float s2 = (1.0f - waveMix) * blepSaw (v.phase2, dt2) + waveMix * blepPulse (v.phase2, dt2);
    v.phase1 += dt1; if (v.phase1 >= 1.0f) v.phase1 -= 1.0f;
    v.phase2 += dt2; if (v.phase2 >= 1.0f) v.phase2 -= 1.0f;
    if (! hell)
        return 0.5f * (s1 + s2);
    // DEVIL: square sub-octave underneath
    const float dts = dt1 * 0.5f;
    const float sub = blepPulse (v.phaseSub, dts);
    v.phaseSub += dts; if (v.phaseSub >= 1.0f) v.phaseSub -= 1.0f;
    return 0.4f * (s1 + s2) + 0.5f * sub;
}

void SynthProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int nch)
{
    const int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer (0);
    auto* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;

    updateDrone();
    const bool hell = devil();
    updateCarrierFilters (*formant - (hell ? 5.0f : 0.0f));

    const bool midiMode = (int) *source == 1;
    const float waveMix = *wave, detuneRatio = std::pow (2.0f, *detune / 1200.0f);
    const float attInc = 1.0f / juce::jmax (1.0f, *attack * 0.001f * (float) fs);
    const float relCoef = timeCoef (*release * 0.001 / 4.6, fs);   // ~-40 dB after `release`
    const float envA = timeCoef (0.0015, fs), envR = timeCoef (*response * 0.001, fs);
    const float carA = timeCoef (0.002, fs), carR = timeCoef (0.03, fs);
    const float spreadAmt = *spread * 0.01f;
    mixSm.setTargetValue (*voiceMix * 0.01f);
    outSm.setTargetValue (dbToGain (*output));
    sibSm.setTargetValue (*sibilance * 0.01f);
    noiseSm.setTargetValue (*noise * (hell ? 0.025f : 0.01f));

    for (int v = 0; v < numVoices; ++v)
        voices[v].pan = spreadAmt * ((v % 2 == 0) ? -1.0f : 1.0f) * (0.4f + 0.6f * (float) ((v / 2) % 2));

    float bandPeak[numBands] {};
    auto midiIt = midi.cbegin();
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);

    for (int i = 0; i < n; ++i)
    {
        // Sample-accurate MIDI
        for (; midiIt != midi.cend() && (*midiIt).samplePosition <= i; ++midiIt)
        {
            if (! midiMode) continue;
            const auto msg = (*midiIt).getMessage();
            if (msg.isNoteOn()) noteOn (msg.getNoteNumber());
            else if (msg.isNoteOff()) noteOff (msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) allOff();
            else if (msg.isPitchWheel()) bendRatio = std::pow (2.0f, (float) (msg.getPitchWheelValue() - 8192) / 8192.0f * 2.0f / 12.0f);
        }

        const float xl = L[i];
        const float xr = R != nullptr ? R[i] : xl;
        const float mod = 0.5f * (xl + xr);

        // --- Carrier: polyphonic synth, panned per voice, plus gated noise
        float cl = 0.0f, cr = 0.0f, gate = 0.0f;
        for (auto& v : voices)
        {
            if (v.note < 0) continue;
            if (v.gate) v.level = juce::jmin (1.0f, v.level + attInc);
            else        v.level *= relCoef;
            if (v.level < 1.0e-5f && ! v.gate) { v.level = 0.0f; continue; }
            const float s = renderVoice (v, waveMix, detuneRatio, hell) * v.level;
            const float angle = (v.pan + 1.0f) * pi * 0.25f;
            cl += s * std::cos (angle);
            cr += s * std::sin (angle);
            gate = juce::jmax (gate, v.level);
        }
        const float nz = noiseSm.getNextValue() * gate;
        cl += nz * uni (rng);
        cr += nz * uni (rng);

        // --- Filter bank: modulator envelopes shape the (level-normalised) carrier bands
        float outL = 0.0f, outR = 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            const float mb = modState[b][1].process (modFilter[b], modState[b][0].process (modFilter[b], mod));
            const float ma = std::abs (mb);
            modEnv[b] = (ma > modEnv[b] ? envA : envR) * (modEnv[b] - ma) + ma;
            bandPeak[b] = juce::jmax (bandPeak[b], modEnv[b]);

            const float c0 = carState[0][b][1].process (carFilter[b], carState[0][b][0].process (carFilter[b], cl));
            const float c1 = carState[1][b][1].process (carFilter[b], carState[1][b][0].process (carFilter[b], cr));
            const float a0 = std::abs (c0), a1 = std::abs (c1);
            carEnv[0][b] = (a0 > carEnv[0][b] ? carA : carR) * (carEnv[0][b] - a0) + a0;
            carEnv[1][b] = (a1 > carEnv[1][b] ? carA : carR) * (carEnv[1][b] - a1) + a1;

            outL += c0 * modEnv[b] / (carEnv[0][b] + 1.0e-3f);
            outR += c1 * modEnv[b] / (carEnv[1][b] + 1.0e-3f);
        }
        outL *= gate;
        outR *= gate;

        // --- Sibilance pass-through keeps consonants intelligible
        const float sib = sibSm.getNextValue();
        const float sl = sibState[1].process (sibHp, sibState[0].process (sibHp, mod)) * sib;

        const float dryAmt = mixSm.getNextValue(), o = outSm.getNextValue();
        const float yl = (outL * 1.8f + sl) * (1.0f - dryAmt * 0.5f) + xl * dryAmt;
        const float yr = (outR * 1.8f + sl) * (1.0f - dryAmt * 0.5f) + xr * dryAmt;

        if (R != nullptr) { L[i] = yl * o; R[i] = yr * o; }
        else              { L[i] = 0.5f * (yl + yr) * o; }
    }

    for (int b = 0; b < numBands; ++b)
        atomicMax (bandLevels[(size_t) b], bandPeak[b]);
    int mask = 0;
    for (int v = 0; v < numVoices; ++v)
    {
        const bool on = voices[v].note >= 0 && (voices[v].gate || voices[v].level > 1.0e-3f);
        if (on) mask |= 1 << v;
        voiceNotes[v].store (on ? voices[v].note : -1);
    }
    activeNotesMask.store (mask);
}
