#pragma once
#include "VoxBase.h"
#include "VoxPitch.h"

class PitchProcessor : public vox::VoxProcessorBase
{
public:
    PitchProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;

    float totalSemitones() const noexcept { return shift->load() + fine->load() * 0.01f; }

    std::atomic<float> *shift, *fine, *grain, *track, *tone, *mix, *output;

private:
    double fs = 44100.0;
    vox::PitchDetector detector;
    vox::PitchShifter shifter[2], demon[2];
    vox::DelayBuffer dryDelay[2];
    vox::Biquad toneLp;
    vox::BiquadState toneState[2];
    float f0 = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> ratioSm;
    juce::SmoothedValue<float> mixSm, outSm, demonSm;
};
