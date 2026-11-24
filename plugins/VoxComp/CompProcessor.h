#pragma once
#include "VoxBase.h"

class CompProcessor : public vox::VoxProcessorBase
{
public:
    CompProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;

    // Static gain computer (shared with the display).
    static float gainComputer (float levelDb, float threshold, float ratio, float knee) noexcept;

    // Metering for the display (consumed with exchange()).
    std::atomic<float> grDb { 0.0f }, deessDb { 0.0f }, detectorDb { -100.0f };

    std::atomic<float> *threshold, *ratio, *attack, *release, *knee, *makeup,
                       *dsOn, *dsFreq, *dsThresh, *mix, *output, *limiter;

private:
    double fs = 44100.0;
    vox::SVFSplit dsSplit[2];
    float dsEnv = 0.0f, compGr = 0.0f, limEnv = 0.0f;
    juce::SmoothedValue<float> mixSm, outSm, makeupSm;
};
