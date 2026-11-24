#pragma once
// Common AudioProcessor base: state, bus layouts, metering.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "VoxParams.h"
#include "VoxDSP.h"

namespace vox
{
inline void atomicMax (std::atomic<float>& a, float v) noexcept
{
    float prev = a.load (std::memory_order_relaxed);
    while (v > prev && ! a.compare_exchange_weak (prev, v, std::memory_order_relaxed)) {}
}

inline void atomicMin (std::atomic<float>& a, float v) noexcept
{
    float prev = a.load (std::memory_order_relaxed);
    while (v < prev && ! a.compare_exchange_weak (prev, v, std::memory_order_relaxed)) {}
}

class VoxProcessorBase : public juce::AudioProcessor
{
public:
    VoxProcessorBase (juce::String pluginName,
                      juce::AudioProcessorValueTreeState::ParameterLayout layout,
                      bool allowMonoToStereo)
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "VOXSTATE", withDevil (std::move (layout))),
          name (std::move (pluginName)),
          monoToStereo (allowMonoToStereo)
    {
        devilParam = apvts.getRawParameterValue ("devil");
    }

    // DEVIL: every plugin gets this switch. The base adds a hard, asymmetric overload
    // stage after the plugin's own processing; plugins also push their DSP to extremes.
    bool devil() const noexcept { return devilParam->load() > 0.5f; }

    //==============================================================================
    const juce::String getName() const override { return name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    bool hasEditor() const override { return true; }
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto in = layouts.getMainInputChannelSet();
        const auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;
        if (in == out)
            return true;
        return monoToStereo && in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo();
    }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    using AudioProcessor::processBlock;

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) final
    {
        juce::ScopedNoDenormals noDenormals;
        const int numIn = getTotalNumInputChannels();
        const int numOut = juce::jmin (getTotalNumOutputChannels(), buffer.getNumChannels());
        const int n = buffer.getNumSamples();

        if (numIn == 1 && numOut >= 2)
            buffer.copyFrom (1, 0, buffer, 0, 0, n);
        else
            for (int ch = numIn; ch < numOut; ++ch)
                buffer.clear (ch, 0, n);

        atomicMax (inPeak, peakOf (buffer, juce::jmax (1, juce::jmin (numIn, numOut)), n));
        process (buffer, midi, numOut);
        applyDevil (buffer, numOut, n);
        atomicMax (outPeak, peakOf (buffer, numOut, n));
    }

    // Implemented by each plugin. `numChannels` is 1 or 2.
    virtual void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, int numChannels) = 0;

    std::atomic<float>* raw (const char* id) const
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout withDevil (juce::AudioProcessorValueTreeState::ParameterLayout l)
    {
        l.add (makeBool ("devil", "Devil", false));
        return l;
    }

    void applyDevil (juce::AudioBuffer<float>& buffer, int nch, int n)
    {
        if (! juce::exactlyEqual (devilFs, getSampleRate()))
        {
            devilFs = getSampleRate() > 0 ? getSampleRate() : 44100.0;
            devilMix.reset (devilFs, 0.04);
            devilMix.setCurrentAndTargetValue (devil() ? 1.0f : 0.0f);
            for (auto& d : devilDc) { d.prepare (devilFs); d.reset(); }
        }
        devilMix.setTargetValue (devil() ? 1.0f : 0.0f);
        if (! devilMix.isSmoothing() && devilMix.getCurrentValue() < 1.0e-4f)
            return;

        constexpr float drive = 7.0f, bias = 0.2f, level = 0.42f;
        const float offset = std::tanh (drive * bias);
        for (int i = 0; i < n; ++i)
        {
            const float m = devilMix.getNextValue();
            for (int ch = 0; ch < nch; ++ch)
            {
                float* d = buffer.getWritePointer (ch);
                const float hot = devilDc[ch].process ((std::tanh (drive * (d[i] + bias)) - offset) * level);
                d[i] += (hot - d[i]) * m;
            }
        }
    }

    std::atomic<float>* devilParam = nullptr;
    double devilFs = 0.0;
    juce::SmoothedValue<float> devilMix;
    DCBlocker devilDc[2];

    static float peakOf (const juce::AudioBuffer<float>& b, int chans, int n)
    {
        float m = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            m = juce::jmax (m, b.getMagnitude (ch, 0, n));
        return m;
    }

    juce::String name;
    bool monoToStereo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxProcessorBase)
};

} // namespace vox
