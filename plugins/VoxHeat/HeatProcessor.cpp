#include "HeatProcessor.h"

using namespace vox;

juce::AudioProcessorValueTreeState::ParameterLayout HeatProcessor::createLayout()
{
    auto toneText = [] (float v)
    {
        const int a = juce::roundToInt (std::abs (v));
        return a == 0 ? juce::String ("Flat") : juce::String (v < 0 ? "Dark " : "Bright ") + juce::String (a);
    };

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeFloat ("drive", "Drive", 0.0f, 36.0f, 12.0f, Unit::dB));
    p.push_back (makeChoice ("mode", "Character", { "Tape", "Tube", "Soft Clip", "Hard Clip" }, 0));
    p.push_back (makeFloat ("tighten", "Tighten", 20.0f, 800.0f, 60.0f, Unit::Hz, 150.0f));
    p.push_back (makeFloat ("tone", "Tone", -100.0f, 100.0f, 0.0f, Unit::Plain, 0.0f, toneText));
    p.push_back (makeBool ("autoGain", "Auto Gain", true));
    p.push_back (makeFloat ("mix", "Mix", 0.0f, 100.0f, 100.0f, Unit::Percent));
    p.push_back (makeFloat ("output", "Output", -18.0f, 18.0f, 0.0f, Unit::dB));
    return { p.begin(), p.end() };
}

HeatProcessor::HeatProcessor() : VoxProcessorBase ("VoxHeat", createLayout(), false)
{
    drive = raw ("drive"); mode = raw ("mode"); tighten = raw ("tighten"); tone = raw ("tone");
    mix = raw ("mix"); output = raw ("output"); autoGain = raw ("autoGain");
}

void HeatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    maxBlock = juce::jmax (samplesPerBlock, 32);
    oversampling.reset();
    oversampling.initProcessing ((size_t) maxBlock);
    const int latency = (int) std::round (oversampling.getLatencyInSamples());
    setLatencySamples (latency);

    dryDelay.prepare ({ sampleRate, (juce::uint32) maxBlock, 2 });
    dryDelay.setMaximumDelayInSamples (juce::jmax (latency + 8, 16));
    dryDelay.setDelay ((float) latency);
    dryDelay.reset();

    driveBuffer.assign ((size_t) maxBlock * 4, 1.0f);
    compBuffer.assign ((size_t) maxBlock * 4, 1.0f);
    for (auto* s : { &driveSm, &compSm })
        s->reset (sampleRate * 4.0, 0.03);
    for (auto* s : { &mixSm, &outSm, &toneSm })
        s->reset (sampleRate, 0.03);

    const int m = (int) *mode;
    const float dg = dbToGain (*drive);
    driveSm.setCurrentAndTargetValue (dg);
    compSm.setCurrentAndTargetValue (*autoGain > 0.5f ? autoGainFor (m, dg) : 1.0f);
    mixSm.setCurrentAndTargetValue (*mix * 0.01f);
    outSm.setCurrentAndTargetValue (dbToGain (*output));
    toneSm.setCurrentAndTargetValue (*tone);
    lastTone = 1.0e9f;

    for (int ch = 0; ch < 2; ++ch)
    {
        preState[ch].reset(); lowState[ch].reset(); highState[ch].reset();
        dc[ch].prepare (sampleRate); dc[ch].reset();
    }
}

void HeatProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    juce::dsp::AudioBlock<float> all (buffer);
    all = all.getSubsetChannelBlock (0, (size_t) nch);

    const int m = (int) *mode;
    const float dg = dbToGain (*drive + (devil() ? 18.0f : 0.0f));
    driveSm.setTargetValue (dg);
    compSm.setTargetValue (*autoGain > 0.5f ? autoGainFor (m, dg) : 1.0f);
    mixSm.setTargetValue (*mix * 0.01f);
    outSm.setTargetValue (dbToGain (*output));
    toneSm.setTargetValue (*tone);
    preHp.set (Biquad::Type::HighPass, fs, *tighten, 0.7071);

    for (int start = 0; start < n; start += maxBlock)
        processChunk (all.getSubBlock ((size_t) start, (size_t) juce::jmin (maxBlock, n - start)), nch);
}

void HeatProcessor::processChunk (juce::dsp::AudioBlock<float> block, int nch)
{
    const int len = (int) block.getNumSamples();
    const int m = (int) *mode;
    const bool hell = devil();

    // Tilt EQ around ~900 Hz, updated when the (smoothed) tone moves.
    const float t = toneSm.skip (len);
    if (std::abs (t - lastTone) > 0.05f)
    {
        lastTone = t;
        const double tiltDb = 6.0 * t / 100.0;
        lowShelf.set (Biquad::Type::LowShelf, fs, 900.0, 0.5, -tiltDb);
        highShelf.set (Biquad::Type::HighShelf, fs, 900.0, 0.5, tiltDb);
    }

    // Keep a latency-aligned dry copy for the mix, then tighten the wet path.
    for (int ch = 0; ch < nch; ++ch)
    {
        auto* d = block.getChannelPointer ((size_t) ch);
        for (int i = 0; i < len; ++i)
        {
            dryDelay.pushSample (ch, d[i]);
            d[i] = preState[ch].process (preHp, d[i]);
        }
    }

    auto up = oversampling.processSamplesUp (block);
    const int upLen = (int) up.getNumSamples();
    for (int i = 0; i < upLen; ++i)
    {
        driveBuffer[(size_t) i] = driveSm.getNextValue();
        compBuffer[(size_t) i] = compSm.getNextValue();
    }

    for (int ch = 0; ch < nch; ++ch)
    {
        auto* d = up.getChannelPointer ((size_t) ch);
        for (int i = 0; i < upLen; ++i)
        {
            float y = shape (m, d[i] * driveBuffer[(size_t) i]);
            if (hell) y = std::sin (y * 2.6f);   // DEVIL: fold the clipped wave back on itself
            d[i] = y * compBuffer[(size_t) i];
        }
    }

    oversampling.processSamplesDown (block);

    for (int i = 0; i < len; ++i)
    {
        const float mx = mixSm.getNextValue(), o = outSm.getNextValue();
        for (int ch = 0; ch < nch; ++ch)
        {
            auto* d = block.getChannelPointer ((size_t) ch);
            float w = dc[ch].process (d[i]);
            w = highState[ch].process (highShelf, lowState[ch].process (lowShelf, w));
            const float dry = dryDelay.popSample (ch);
            d[i] = (dry * (1.0f - mx) + w * mx) * o;
        }
    }
}
