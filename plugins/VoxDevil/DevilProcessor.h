#pragma once
#include "VoxBase.h"

// Brutal feedback compressor: one "Crush" knob drives threshold and ratio together,
// saturation sits inside the gain loop, parallel mix for NY-style blending.
class DevilProcessor : public vox::VoxProcessorBase
{
public:
    DevilProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;

    std::atomic<float> *input, *crush, *attack, *release, *saturate, *mix, *output;
    std::atomic<float> grDb { 0.0f };

private:
    double fs = 44100.0;
    float gr = 0.0f, env = 0.0f, lastOut[2] {};
    vox::Biquad scHp;
    vox::BiquadState scState;
    vox::DCBlocker dc[2];
    juce::SmoothedValue<float> inSm, mixSm, outSm, satSm;
};
