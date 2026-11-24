#pragma once
#include "VoxBase.h"
#include "VoxPitch.h"

class TuneProcessor : public vox::VoxProcessorBase
{
public:
    TuneProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray keyNames, scaleNames;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;

    // Nearest allowed note (fractional MIDI in, integer MIDI out) for key/scale.
    static int snap (float midi, int key, int scale) noexcept;

    std::atomic<float> *key, *scale, *retune, *amount, *humanize, *mix, *output;
    std::atomic<float> detectedMidi { 0.0f }, targetMidi { 0.0f };   // 0 = unvoiced

private:
    double fs = 44100.0;
    vox::PitchDetector detector;
    vox::PitchShifter shifter[2], demon[2];
    vox::DelayBuffer dryDelay[2];
    float f0 = 0.0f, correction = 0.0f, correctionTarget = 0.0f;
    int heldNote = -1;
    juce::SmoothedValue<float> mixSm, outSm, demonSm;
};
