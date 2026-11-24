#pragma once
#include "VoxBase.h"

class HeatProcessor : public vox::VoxProcessorBase
{
public:
    enum Mode { Tape = 0, Tube, SoftClip, HardClip };

    HeatProcessor();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void prepareToPlay (double sampleRate, int blockSize) override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, int numChannels) override;
    juce::AudioProcessorEditor* createEditor() override;

    static inline float shape (int mode, float x) noexcept
    {
        switch (mode)
        {
            case Tube:     return std::tanh (x + 0.35f) - 0.33637f;   // asymmetric => even harmonics (DC removed later)
            case SoftClip: { const float c = juce::jlimit (-1.0f, 1.0f, x * (2.0f / 3.0f)); return 1.5f * c - 0.5f * c * c * c; }
            case HardClip: return juce::jlimit (-0.9f, 0.9f, x);
            case Tape:
            default:       return std::tanh (x);
        }
    }

    // Gain that keeps a typical vocal level roughly constant as drive increases.
    static float autoGainFor (int mode, float driveGain) noexcept
    {
        constexpr float ref = 0.35f;
        const float y = std::abs (shape (mode, ref * driveGain) - shape (mode, 0.0f));
        return juce::jlimit (0.05f, 2.0f, ref / juce::jmax (y, 1.0e-4f));
    }

    std::atomic<float> *drive, *mode, *tighten, *tone, *mix, *output, *autoGain;

private:
    void processChunk (juce::dsp::AudioBlock<float> block, int nch);

    double fs = 44100.0;
    int maxBlock = 512;
    juce::dsp::Oversampling<float> oversampling { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 4096 };
    std::vector<float> driveBuffer, compBuffer;
    juce::SmoothedValue<float> driveSm, compSm, mixSm, outSm, toneSm;
    vox::Biquad preHp, lowShelf, highShelf;
    vox::BiquadState preState[2], lowState[2], highState[2];
    vox::DCBlocker dc[2];
    float lastTone = 1000.0f;
};
