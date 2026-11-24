#pragma once
#include "VoxBase.h"
#include "VoxPitch.h"

// Ad-lib chain: octave layers -> lo-fi band filter + drive -> Haas width -> tempo throw delay.
class AdlibProcessor : public vox::VoxProcessorBase
{
public:
    static constexpr int numStages = 4;

    AdlibProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray styleNames, divisionNames;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return 4.0; }

    static void styleFilters (int style, double fs, vox::Biquad (&out)[numStages]);

    std::atomic<float> *style, *drive, *octDown, *octUp, *width, *throwAmt, *throwDiv, *feedback, *output;
    std::atomic<float> throwMs { 0.0f };

private:
    double fs = 44100.0;
    vox::PitchDetector detector;
    vox::PitchShifter down, up;
    vox::Biquad filters[numStages];
    vox::BiquadState filterState[2][numStages];
    vox::DelayBuffer haas, throwL, throwR;
    vox::OnePoleHP throwHp[2];
    vox::OnePoleLP throwLp[2];
    int lastStyle = -1;
    juce::SmoothedValue<float> downSm, upSm, driveSm, widthSm, throwSm, fbSm, outSm, delaySm;
};
