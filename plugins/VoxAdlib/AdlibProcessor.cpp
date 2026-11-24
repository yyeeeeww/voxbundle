#include "AdlibProcessor.h"

using namespace vox;

const juce::StringArray AdlibProcessor::styleNames { "Clean", "Phone", "Radio", "Megaphone" };
const juce::StringArray AdlibProcessor::divisionNames { "1/4", "1/8 D", "1/8", "1/16" };

namespace
{
double divisionBeats (int i)
{
    static const double beats[] = { 1.0, 0.75, 0.5, 0.25 };
    return beats[juce::jlimit (0, 3, i)];
}
}

void AdlibProcessor::styleFilters (int s, double fs, Biquad (&f)[numStages])
{
    using T = Biquad::Type;
    switch (s)
    {
        case 1:   // Phone: 400 Hz - 3.4 kHz, steep
            f[0].set (T::HighPass, fs, 400.0, 0.5412); f[1].set (T::HighPass, fs, 400.0, 1.3066);
            f[2].set (T::LowPass, fs, 3400.0, 0.5412); f[3].set (T::LowPass, fs, 3400.0, 1.3066); break;
        case 2:   // Radio: 250 Hz - 5 kHz with a nasal bump
            f[0].set (T::HighPass, fs, 250.0, 0.7071); f[1].set (T::Peak, fs, 1500.0, 1.2, 5.0);
            f[2].set (T::LowPass, fs, 5000.0, 0.7071); f[3].set (T::LowPass, fs, 5000.0, 0.7071); break;
        case 3:   // Megaphone: narrow and honky
            f[0].set (T::HighPass, fs, 600.0, 0.9); f[1].set (T::Peak, fs, 1800.0, 2.0, 10.0);
            f[2].set (T::LowPass, fs, 2900.0, 0.9); f[3].set (T::HighPass, fs, 600.0, 0.9); break;
        default:  // Clean: just tidy the lows
            f[0].set (T::HighPass, fs, 140.0, 0.7071); f[1].set (T::Identity, fs, 1000.0, 1.0);
            f[2].set (T::Identity, fs, 1000.0, 1.0); f[3].set (T::Identity, fs, 1000.0, 1.0); break;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AdlibProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeChoice ("style", "Style", styleNames, 1));
    p.push_back (makeFloat ("drive", "Drive", 0.0f, 100.0f, 30.0f, Unit::Percent));
    p.push_back (makeFloat ("octDown", "Oct Down", 0.0f, 100.0f, 0.0f, Unit::Percent));
    p.push_back (makeFloat ("octUp", "Oct Up", 0.0f, 100.0f, 0.0f, Unit::Percent));
    p.push_back (makeFloat ("width", "Width", 0.0f, 100.0f, 60.0f, Unit::Percent));
    p.push_back (makeFloat ("throw", "Throw", 0.0f, 100.0f, 30.0f, Unit::Percent));
    p.push_back (makeChoice ("throwDiv", "Time", divisionNames, 1));
    p.push_back (makeFloat ("feedback", "Repeats", 0.0f, 90.0f, 35.0f, Unit::Percent));
    p.push_back (makeFloat ("output", "Output", -18.0f, 18.0f, 0.0f, Unit::dB));
    return { p.begin(), p.end() };
}

AdlibProcessor::AdlibProcessor() : VoxProcessorBase ("VoxAdlib", createLayout(), true)
{
    style = raw ("style"); drive = raw ("drive"); octDown = raw ("octDown"); octUp = raw ("octUp");
    width = raw ("width"); throwAmt = raw ("throw"); throwDiv = raw ("throwDiv"); feedback = raw ("feedback");
    output = raw ("output");
}

void AdlibProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    detector.prepare (fs);
    down.prepare (fs, 0.08); up.prepare (fs, 0.08);
    haas.setSize ((int) (0.03 * fs));
    throwL.setSize ((int) (2.1 * fs)); throwR.setSize ((int) (2.1 * fs));
    for (int ch = 0; ch < 2; ++ch)
    {
        for (auto& s : filterState[ch]) s.reset();
        throwHp[ch].setCutoff (fs, 300.0); throwHp[ch].reset();
        throwLp[ch].setCutoff (fs, 4500.0); throwLp[ch].reset();
    }
    lastStyle = -1;
    for (auto* s : { &downSm, &upSm, &driveSm, &widthSm, &throwSm, &fbSm, &outSm })
        s->reset (fs, 0.03);
    delaySm.reset (fs, 0.2);
    downSm.setCurrentAndTargetValue (*octDown * 0.01f);
    upSm.setCurrentAndTargetValue (*octUp * 0.01f);
    driveSm.setCurrentAndTargetValue (*drive * 0.01f);
    widthSm.setCurrentAndTargetValue (*width * 0.01f);
    throwSm.setCurrentAndTargetValue (*throwAmt * 0.01f);
    fbSm.setCurrentAndTargetValue (*feedback * 0.01f);
    outSm.setCurrentAndTargetValue (dbToGain (*output));
    delaySm.setCurrentAndTargetValue ((float) (0.375 * fs));
}

void AdlibProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    const bool hell = devil();

    const int st = hell ? 3 : (int) *style;   // DEVIL: megaphone, heavy octave-down, endless throws
    if (st != lastStyle)
    {
        styleFilters (st, fs, filters);
        lastStyle = st;
    }

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (20.0, 400.0, *b);
    const double ms = divisionBeats ((int) *throwDiv) * 60000.0 / bpm;
    throwMs.store ((float) ms);
    delaySm.setTargetValue ((float) juce::jmin (2.0 * fs, ms * 0.001 * fs));

    downSm.setTargetValue (hell ? juce::jmax (0.8f, *octDown * 0.01f) : *octDown * 0.01f);
    upSm.setTargetValue (*octUp * 0.01f);
    driveSm.setTargetValue (hell ? 1.0f : *drive * 0.01f);
    widthSm.setTargetValue (*width * 0.01f);
    throwSm.setTargetValue (*throwAmt * 0.01f);
    fbSm.setTargetValue (hell ? 0.92f : *feedback * 0.01f);
    outSm.setTargetValue (dbToGain (*output));

    for (int i = 0; i < n; ++i)
    {
        const float xl = buffer.getSample (0, i);
        const float xr = nch > 1 ? buffer.getSample (1, i) : xl;
        const float mono = 0.5f * (xl + xr);

        if (detector.push (mono))
        {
            const float f0 = detector.frequency();
            down.setPitchSyncWindow (f0, 0.03f);
            up.setPitchSyncWindow (f0, 0.02f);
        }
        const float dn = downSm.getNextValue(), upv = upSm.getNextValue();
        const float layers = dn * down.process (mono, 0.5f) + upv * 0.7f * up.process (mono, 2.0f);

        const float dr = driveSm.getNextValue();
        const float k = 1.0f + 14.0f * dr;
        const float norm = 1.0f / std::tanh (k * 0.3f) * 0.3f;
        float y[2] = { xl + layers, xr + layers };
        for (int ch = 0; ch < 2; ++ch)
        {
            float v = y[ch];
            for (int s = 0; s < numStages; ++s)
                v = filterState[ch][s].process (filters[s], v);
            y[ch] = dr > 0.001f ? std::tanh (k * v) * norm : v;
        }

        // Haas width: right channel delayed up to 18 ms, then mid/side normalised.
        const float w = widthSm.getNextValue();
        haas.push (y[1]);
        const float delayedR = haas.read (1.0f + w * 0.018f * (float) fs);
        y[1] = y[1] + (delayedR - y[1]) * juce::jmin (1.0f, w * 2.0f);

        // Throw: ping-pong, band-limited repeats
        const float ds = delaySm.getNextValue(), fb = fbSm.getNextValue(), th = throwSm.getNextValue();
        const float tl = throwL.read (ds), tr = throwR.read (ds);
        throwL.push (0.5f * (y[0] + y[1]) + std::tanh (throwLp[1].process (throwHp[1].process (tr)) * fb));
        throwR.push (std::tanh (throwLp[0].process (throwHp[0].process (tl)) * fb));

        const float o = outSm.getNextValue();
        const float outL = (y[0] + tl * th) * o, outR = (y[1] + tr * th) * o;
        if (nch > 1) { buffer.setSample (0, i, outL); buffer.setSample (1, i, outR); }
        else         buffer.setSample (0, i, 0.5f * (outL + outR));
    }
}
