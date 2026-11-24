#pragma once
#include "VoxBase.h"

// 8-line feedback delay network reverb with input diffusion and modulated lines.
class FdnReverb
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate);
    void reset();
    void setParameters (float size01, float decaySeconds, float dampHz);
    void process (float in, float& outL, float& outR) noexcept;

private:
    static constexpr float baseMs[N] = { 31.3f, 37.9f, 41.7f, 45.1f, 49.3f, 54.7f, 60.1f, 66.9f };
    static constexpr float diffMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
    static constexpr float inSign[N] = { 1, -1, 1, 1, -1, 1, -1, -1 };
    static constexpr float lSign[N] = { 1, -1, 1, -1, 1, -1, 1, -1 };
    static constexpr float rSign[N] = { 1, 1, -1, -1, 1, 1, -1, -1 };

    double fs = 44100.0;
    vox::DelayBuffer lines[N];
    vox::Allpass diffusers[4];
    float lengths[N] {}, gains[N] {}, lpState[N] {}, phase[N] {}, phaseInc[N] {};
    float damp = 0.3f, modDepth = 8.0f;
    float sizeCurrent = -1.0f;
};

class SpaceProcessor : public vox::VoxProcessorBase
{
public:
    SpaceProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray divisionNames;
    static double divisionBeats (int index);

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return 12.0; }

    std::atomic<float> *size, *decay, *predelay, *damping, *revLowCut, *revLevel,
                       *delaySync, *delayTime, *delayDiv, *feedback, *pingPong, *delayTone, *delayLevel,
                       *duck, *width, *dry;

    // For the display
    std::atomic<float> currentDelayMs { 0.0f }, duckDb { 0.0f }, hostBpm { 120.0f };
    std::atomic<float> wetPeakL { 0.0f }, wetPeakR { 0.0f };

private:
    double fs = 44100.0;
    FdnReverb reverb;
    vox::DelayBuffer preDelayLine, delayL, delayR;
    vox::Biquad revHp;
    vox::BiquadState revHpState;
    vox::OnePoleLP fbLp[2];
    vox::OnePoleHP fbHp[2];
    float duckEnv = 0.0f;
    juce::SmoothedValue<float> delaySamplesSm, predelaySm, revLevelSm, delLevelSm, dryLevelSm, widthSm, feedbackSm;
};
