#include "SpaceProcessor.h"

using namespace vox;

//==============================================================================
void FdnReverb::prepare (double sampleRate)
{
    fs = sampleRate;
    const int maxLen = (int) std::ceil (baseMs[N - 1] * 0.001 * 1.7 * fs) + 64;
    for (auto& l : lines)
        l.setSize (maxLen);
    for (int i = 0; i < 4; ++i)
    {
        diffusers[i].delay = (float) (diffMs[i] * 0.001 * fs);
        diffusers[i].buf.setSize ((int) diffusers[i].delay + 8);
    }
    for (int i = 0; i < N; ++i)
    {
        phase[i] = (float) i / (float) N;
        phaseInc[i] = (float) ((0.35 + 0.11 * i) / fs);
    }
    modDepth = (float) (0.00025 * fs);   // ~0.25 ms of slow wander
    reset();
}

void FdnReverb::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& d : diffusers) d.buf.reset();
    std::fill (std::begin (lpState), std::end (lpState), 0.0f);
    sizeCurrent = -1.0f;
}

void FdnReverb::setParameters (float size01, float decaySeconds, float dampHz)
{
    // Glide the room size slowly so line lengths never jump.
    sizeCurrent = sizeCurrent < 0.0f ? size01 : sizeCurrent + (size01 - sizeCurrent) * 0.15f;
    const float scale = 0.35f + 1.3f * sizeCurrent;
    for (int i = 0; i < N; ++i)
    {
        lengths[i] = (float) (baseMs[i] * 0.001 * scale * fs);
        const float seconds = lengths[i] / (float) fs;
        gains[i] = std::pow (10.0f, -3.0f * seconds / (1.1f * juce::jmax (0.1f, decaySeconds)));
    }
    damp = (float) std::exp (-2.0 * piD * dampHz / fs);
}

void FdnReverb::process (float in, float& outL, float& outR) noexcept
{
    float d = in;
    for (auto& ap : diffusers)
        d = ap.process (d, 0.62f);

    float o[N];
    float sum = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        phase[i] += phaseInc[i];
        if (phase[i] >= 1.0f) phase[i] -= 1.0f;
        const float mod = modDepth * (1.0f + std::sin (2.0f * pi * phase[i]));
        float v = lines[i].read (lengths[i] + mod);
        lpState[i] = v + damp * (lpState[i] - v);   // in-loop damping
        o[i] = lpState[i] * gains[i];
        sum += o[i];
    }

    const float h = sum * (2.0f / (float) N);   // Householder reflection
    float l = 0.0f, r = 0.0f;
    for (int i = 0; i < N; ++i)
    {
        lines[i].push (o[i] - h + d * inSign[i]);
        l += o[i] * lSign[i];
        r += o[i] * rSign[i];
    }
    outL = l * 0.35f;
    outR = r * 0.35f;
}

//==============================================================================
const juce::StringArray SpaceProcessor::divisionNames { "1/2", "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T", "1/16" };

double SpaceProcessor::divisionBeats (int i)
{
    static const double beats[] = { 2.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25 };
    return beats[juce::jlimit (0, 7, i)];
}

juce::AudioProcessorValueTreeState::ParameterLayout SpaceProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (makeFloat ("size", "Size", 0.0f, 100.0f, 55.0f, Unit::Percent));
    p.push_back (makeFloat ("decay", "Decay", 0.2f, 10.0f, 1.8f, Unit::s, 2.0f));
    p.push_back (makeFloat ("predelay", "Pre-Delay", 0.0f, 250.0f, 25.0f, Unit::ms, 50.0f));
    p.push_back (makeFloat ("damping", "Damping", 1000.0f, 20000.0f, 7000.0f, Unit::Hz, 6000.0f));
    p.push_back (makeFloat ("revLowCut", "Low Cut", 20.0f, 1000.0f, 220.0f, Unit::Hz, 200.0f));
    p.push_back (makeFloat ("revLevel", "Reverb", 0.0f, 100.0f, 30.0f, Unit::Percent));

    p.push_back (makeBool ("delaySync", "Sync", true));
    p.push_back (makeFloat ("delayTime", "Time", 1.0f, 2000.0f, 350.0f, Unit::ms, 350.0f));
    p.push_back (makeChoice ("delayDiv", "Division", divisionNames, 5));
    p.push_back (makeFloat ("feedback", "Feedback", 0.0f, 95.0f, 30.0f, Unit::Percent));
    p.push_back (makeBool ("pingPong", "Ping-Pong", true));
    p.push_back (makeFloat ("delayTone", "Tone", 1000.0f, 20000.0f, 5000.0f, Unit::Hz, 5000.0f));
    p.push_back (makeFloat ("delayLevel", "Delay", 0.0f, 100.0f, 20.0f, Unit::Percent));

    p.push_back (makeFloat ("duck", "Ducking", 0.0f, 100.0f, 35.0f, Unit::Percent));
    p.push_back (makeFloat ("width", "Width", 0.0f, 150.0f, 100.0f, Unit::Percent));
    p.push_back (makeFloat ("dry", "Dry", 0.0f, 100.0f, 100.0f, Unit::Percent));
    return { p.begin(), p.end() };
}

SpaceProcessor::SpaceProcessor() : VoxProcessorBase ("VoxSpace", createLayout(), true)
{
    size = raw ("size"); decay = raw ("decay"); predelay = raw ("predelay"); damping = raw ("damping");
    revLowCut = raw ("revLowCut"); revLevel = raw ("revLevel");
    delaySync = raw ("delaySync"); delayTime = raw ("delayTime"); delayDiv = raw ("delayDiv");
    feedback = raw ("feedback"); pingPong = raw ("pingPong"); delayTone = raw ("delayTone"); delayLevel = raw ("delayLevel");
    duck = raw ("duck"); width = raw ("width"); dry = raw ("dry");
}

void SpaceProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    reverb.prepare (sampleRate);
    preDelayLine.setSize ((int) (0.26 * fs) + 8);
    delayL.setSize ((int) (4.2 * fs));
    delayR.setSize ((int) (4.2 * fs));
    revHpState.reset();
    for (int ch = 0; ch < 2; ++ch) { fbLp[ch].reset(); fbHp[ch].reset(); fbHp[ch].setCutoff (fs, 120.0); }
    duckEnv = 0.0f;

    delaySamplesSm.reset (sampleRate, 0.25);
    predelaySm.reset (sampleRate, 0.1);
    for (auto* s : { &revLevelSm, &delLevelSm, &dryLevelSm, &widthSm, &feedbackSm })
        s->reset (sampleRate, 0.03);

    delaySamplesSm.setCurrentAndTargetValue ((float) (*delayTime * 0.001 * fs));
    predelaySm.setCurrentAndTargetValue ((float) (*predelay * 0.001 * fs));
    revLevelSm.setCurrentAndTargetValue (*revLevel * 0.01f);
    delLevelSm.setCurrentAndTargetValue (*delayLevel * 0.01f);
    dryLevelSm.setCurrentAndTargetValue (*dry * 0.01f);
    widthSm.setCurrentAndTargetValue (*width * 0.01f);
    feedbackSm.setCurrentAndTargetValue (*feedback * 0.01f);
}

void SpaceProcessor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&, int nch)
{
    const int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer (0);
    auto* R = nch > 1 ? buffer.getWritePointer (1) : nullptr;

    // Delay time: free (ms) or synced to the host tempo.
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = juce::jlimit (20.0, 400.0, *b);
    hostBpm.store ((float) bpm);

    double delayMs = *delaySync > 0.5f ? divisionBeats ((int) *delayDiv) * 60000.0 / bpm : (double) *delayTime;
    delayMs = juce::jlimit (1.0, 4000.0, delayMs);
    currentDelayMs.store ((float) delayMs);

    delaySamplesSm.setTargetValue ((float) (delayMs * 0.001 * fs));
    predelaySm.setTargetValue ((float) juce::jmax (1.0, *predelay * 0.001 * fs));
    revLevelSm.setTargetValue (*revLevel * 0.01f);
    delLevelSm.setTargetValue (*delayLevel * 0.01f);
    dryLevelSm.setTargetValue (*dry * 0.01f);
    widthSm.setTargetValue (*width * 0.01f);
    // DEVIL: delay feedback past unity (tanh in the loop keeps it bounded = self-oscillation),
    // reverb tail x3, wet signal driven into saturation.
    const bool hell = devil();
    feedbackSm.setTargetValue (hell ? juce::jmin (1.15f, *feedback * 0.013f + 0.35f) : *feedback * 0.01f);

    reverb.setParameters (*size * 0.01f, *decay * (hell ? 3.0f : 1.0f), *damping);
    revHp.set (Biquad::Type::HighPass, fs, *revLowCut, 0.7071);
    for (auto& f : fbLp) f.setCutoff (fs, *delayTone);

    const bool ping = *pingPong > 0.5f;
    const float duckAmt = *duck * 0.01f;
    const float duckA = timeCoef (0.005, fs), duckR = timeCoef (0.3, fs);
    float duckMin = 0.0f, wl = 0.0f, wr = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i];
        const float xr = R != nullptr ? R[i] : xl;
        const float mono = 0.5f * (xl + xr);

        // --- Reverb
        const float pd = preDelayLine.read (predelaySm.getNextValue());
        preDelayLine.push (mono);
        float rl, rr;
        reverb.process (revHpState.process (revHp, pd), rl, rr);

        // --- Delay
        const float ds = delaySamplesSm.getNextValue();
        const float dl = delayL.read (ds), dr = delayR.read (ds);
        const float fb = feedbackSm.getNextValue();
        const float fbl = std::tanh (fbHp[0].process (fbLp[0].process (dl)) * fb);
        const float fbr = std::tanh (fbHp[1].process (fbLp[1].process (dr)) * fb);
        if (ping)
        {
            delayL.push (mono + fbr);
            delayR.push (fbl);
        }
        else
        {
            delayL.push (xl + fbl);
            delayR.push (xr + fbr);
        }

        // --- Ducking: wet ducks under the dry vocal
        const float lvl = std::abs (mono);
        const float c = lvl > duckEnv ? duckA : duckR;
        duckEnv = c * duckEnv + (1.0f - c) * lvl;
        const float duckDbNow = -duckAmt * 18.0f * juce::jlimit (0.0f, 1.0f, (gainToDb (duckEnv) + 42.0f) / 30.0f);
        duckMin = juce::jmin (duckMin, duckDbNow);
        const float duckGain = dbToGain (duckDbNow);

        const float rv = revLevelSm.getNextValue(), dv = delLevelSm.getNextValue();
        float wetL = (rl * rv + dl * dv) * duckGain;
        float wetR = (rr * rv + dr * dv) * duckGain;
        if (hell)
        {
            wetL = std::tanh (wetL * 5.0f) * 0.35f;
            wetR = std::tanh (wetR * 5.0f) * 0.35f;
        }

        // --- Width (mid/side on the wet signal only)
        const float w = widthSm.getNextValue();
        const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * w;
        wetL = mid + side;
        wetR = mid - side;
        wl = juce::jmax (wl, std::abs (wetL));
        wr = juce::jmax (wr, std::abs (wetR));

        const float dryG = dryLevelSm.getNextValue();
        if (R != nullptr)
        {
            L[i] = xl * dryG + wetL;
            R[i] = xr * dryG + wetR;
        }
        else
        {
            L[i] = xl * dryG + 0.5f * (wetL + wetR);
        }
    }

    atomicMin (duckDb, duckMin);
    atomicMax (wetPeakL, wl);
    atomicMax (wetPeakR, wr);
}
