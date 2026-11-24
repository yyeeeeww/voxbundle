#pragma once
#include "VoxBase.h"
#include "VoxPitch.h"

class DoubleProcessor : public vox::VoxProcessorBase
{
public:
    static constexpr int maxVoices = 4;

    DoubleProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;

    std::atomic<float> *voices, *detune, *delay, *drift, *width, *lowCut, *doubles, *dry;

    // per-voice state for the display: pitch offset (cents) and delay (ms)
    std::atomic<float> voiceCents[maxVoices] {}, voiceDelayMs[maxVoices] {};

private:
    double fs = 44100.0;
    vox::PitchShifter shifter[maxVoices];
    vox::DelayBuffer line[maxVoices];
    vox::Drift timeDrift[maxVoices], pitchDrift[maxVoices];
    vox::Biquad hp;
    vox::BiquadState hpState[maxVoices];
    juce::SmoothedValue<float> levelSm, drySm, widthSm, countSm[maxVoices];
};
