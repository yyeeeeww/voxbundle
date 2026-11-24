#pragma once
#include "VoxBase.h"
#include "VoxPitch.h"
#include <random>

// Vocoder: the incoming voice (modulator) shapes a built-in polyphonic synth (carrier).
// Notes come from the internal chord: fixed ("Drone") or rooted on the sung pitch ("Follow").
// Registered as a plain AU effect (aufx) so every host, FL Studio included, can load it.
class SynthProcessor : public vox::VoxProcessorBase
{
public:
    static constexpr int numBands = 20;
    static constexpr int numVoices = 8;

    SynthProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray chordNames;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return 1.0; }

    static float bandFrequency (int b) noexcept
    {
        return 110.0f * std::pow (7600.0f / 110.0f, (float) b / (float) (numBands - 1));
    }

    std::atomic<float> *source, *droneNote, *chord, *wave, *detune, *noise, *attack, *release,
                       *response, *formant, *sibilance, *spread, *voiceMix, *output;

    std::array<std::atomic<float>, numBands> bandLevels {};
    std::atomic<int> activeNotesMask { 0 };   // bit per voice, for the display
    std::atomic<int> voiceNotes[numVoices] {};

private:
    struct Voice
    {
        int note = -1;
        bool gate = false;
        float level = 0.0f, phase1 = 0.0f, phase2 = 0.0f, phaseSub = 0.0f, pan = 0.0f;
        juce::uint32 age = 0;
    };

    void noteOn (int note);
    void noteOff (int note);
    void allOff();
    void updateNotes();
    void updateCarrierFilters (float formantSemis);
    float renderVoice (Voice& v, float wave, float detuneRatio, bool hell) noexcept;

    double fs = 44100.0;
    Voice voices[numVoices];
    juce::uint32 ageCounter = 0;
    vox::PitchDetector detector;
    int followRoot = -1, followNote = -1;
    int lastSource = -1, lastDroneRoot = -1, lastChord = -1;
    float lastFormant = 1000.0f;

    vox::Biquad modFilter[numBands], carFilter[numBands], sibHp;
    vox::BiquadState modState[numBands][2], carState[2][numBands][2], sibState[2];
    float modEnv[numBands] {}, carEnv[2][numBands] {};
    std::minstd_rand rng { 42 };
    juce::SmoothedValue<float> mixSm, outSm, sibSm, noiseSm;
};
