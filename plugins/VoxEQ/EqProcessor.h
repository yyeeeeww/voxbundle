#pragma once
#include "VoxBase.h"

class EqProcessor : public vox::VoxProcessorBase
{
public:
    // Filter stages: 2x high-pass, body shelf, mid bell, presence bell, air shelf
    static constexpr int numStages = 6;

    struct BandValues
    {
        float hpFreq, lowFreq, lowGain, midFreq, midGain, midQ, presFreq, presGain, presQ, airFreq, airGain;
        bool hp24;
    };

    EqProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;

    BandValues targetValues() const;
    static void computeCoefficients (const BandValues& v, double fs, vox::Biquad (&out)[numStages]);

    std::atomic<float> *hpOn, *hpFreq, *hpSlope, *lowFreq, *lowGain, *midFreq, *midGain, *midQ,
                       *presFreq, *presGain, *presQ, *airFreq, *airGain, *output;

private:
    double fs = 44100.0;
    BandValues current {};
    bool firstBlock = true;
    vox::Biquad coeffs[numStages];
    vox::BiquadState state[2][numStages];
    juce::SmoothedValue<float> outSm;
};
