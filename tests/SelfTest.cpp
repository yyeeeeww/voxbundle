// Offline self-test for the Vox bundle DSP + editor screenshots.
// Not shipped; used on Linux/CI to sanity-check processing without a DAW.
//
//   VoxSelfTest                 run DSP checks
//   VoxSelfTest --shots <dir>   also render editor screenshots (needs a display, e.g. xvfb-run)

#include <juce_audio_utils/juce_audio_utils.h>
#include <random>
#include "VoxEditor.h"
#include "VoxPitch.h"

#if VOX_HAS_VOXCOMP
 #include "CompProcessor.h"
#endif
#if VOX_HAS_VOXEQ
 #include "EqProcessor.h"
#endif
#if VOX_HAS_VOXHEAT
 #include "HeatProcessor.h"
#endif
#if VOX_HAS_VOXSPACE
 #include "SpaceProcessor.h"
#endif
#if VOX_HAS_VOXSYNTH
 #include "SynthProcessor.h"
#endif
#if VOX_HAS_VOXDOUBLE
 #include "DoubleProcessor.h"
#endif
#if VOX_HAS_VOXTUNE
 #include "TuneProcessor.h"
#endif
#if VOX_HAS_VOXPITCH
 #include "PitchProcessor.h"
#endif
#if VOX_HAS_VOXDEVIL
 #include "DevilProcessor.h"
#endif
#if VOX_HAS_VOXADLIB
 #include "AdlibProcessor.h"
#endif

namespace
{
constexpr double fs = 48000.0;

struct PluginEntry
{
    const char* name;
    std::function<vox::VoxProcessorBase*()> make;
};

std::vector<PluginEntry> plugins()
{
    std::vector<PluginEntry> v;
   #if VOX_HAS_VOXCOMP
    v.push_back ({ "VoxComp", [] { return new CompProcessor(); } });
   #endif
   #if VOX_HAS_VOXEQ
    v.push_back ({ "VoxEQ", [] { return new EqProcessor(); } });
   #endif
   #if VOX_HAS_VOXHEAT
    v.push_back ({ "VoxHeat", [] { return new HeatProcessor(); } });
   #endif
   #if VOX_HAS_VOXSPACE
    v.push_back ({ "VoxSpace", [] { return new SpaceProcessor(); } });
   #endif
   #if VOX_HAS_VOXSYNTH
    v.push_back ({ "VoxSynth", [] { return new SynthProcessor(); } });
   #endif
   #if VOX_HAS_VOXDOUBLE
    v.push_back ({ "VoxDouble", [] { return new DoubleProcessor(); } });
   #endif
   #if VOX_HAS_VOXTUNE
    v.push_back ({ "VoxTune", [] { return new TuneProcessor(); } });
   #endif
   #if VOX_HAS_VOXPITCH
    v.push_back ({ "VoxPitch", [] { return new PitchProcessor(); } });
   #endif
   #if VOX_HAS_VOXDEVIL
    v.push_back ({ "VoxDevil", [] { return new DevilProcessor(); } });
   #endif
   #if VOX_HAS_VOXADLIB
    v.push_back ({ "VoxAdlib", [] { return new AdlibProcessor(); } });
   #endif
    return v;
}

// Synthetic "voice": vibrato'd harmonic source through three formants, syllable
// envelope, plus periodic sibilant noise bursts. Normalised to about -18 dBFS RMS.
juce::AudioBuffer<float> makeVoice (double seconds, int channels)
{
    const int n = (int) (seconds * fs);
    juce::AudioBuffer<float> b (channels, n);
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> uni (-1.0f, 1.0f);
    double phase = 0.0;
    float n1 = 0, n2 = 0;
    const double formants[] = { 700.0, 1200.0, 2600.0 };

    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double f0 = 165.0 * (1.0 + 0.012 * std::sin (2.0 * vox::piD * 5.5 * t)) * (1.0 + 0.15 * std::sin (2.0 * vox::piD * 0.25 * t));
        phase += f0 / fs;
        const double syl = std::pow (0.5 - 0.5 * std::cos (2.0 * vox::piD * 3.1 * t), 1.5);

        double s = 0.0;
        for (int k = 1; k <= 40; ++k)
        {
            const double fk = k * f0;
            if (fk > 16000.0) break;
            double amp = 0.0;
            for (double f : formants)
            {
                const double d = std::log2 (fk / f) * 4.0;
                amp += std::exp (-d * d);
            }
            s += (0.15 + amp) / k * std::sin (2.0 * vox::piD * phase * k);
        }
        s *= syl;

        // sibilance burst: 90 ms every 1.3 s, strongly high-passed noise
        const double cyc = std::fmod (t, 1.3);
        const float w = uni (rng);
        const float hp = w - n1 - (n1 - n2);
        n2 = n1; n1 = w;
        if (cyc > 1.0 && cyc < 1.09)
            s += 0.6 * hp;

        for (int ch = 0; ch < channels; ++ch)
            b.setSample (ch, i, (float) s);
    }

    const float rms = b.getRMSLevel (0, 0, n);
    b.applyGain (vox::dbToGain (-18.0f) / juce::jmax (rms, 1.0e-6f));
    return b;
}

struct Stats
{
    bool finite = true;
    float peak = 0.0f, rmsDb = -120.0f;
};

Stats measure (const juce::AudioBuffer<float>& b)
{
    Stats s;
    double sum = 0.0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (ch, i);
            if (! std::isfinite (v)) { s.finite = false; continue; }
            s.peak = juce::jmax (s.peak, std::abs (v));
            sum += (double) v * v;
        }
    s.rmsDb = vox::gainToDb ((float) std::sqrt (sum / juce::jmax (1, b.getNumChannels() * b.getNumSamples())));
    return s;
}

bool setLayout (juce::AudioProcessor& p, int in, int out)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (in == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.outputBuses.add (out == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    return p.setBusesLayout (l);
}

// Process `input` in blocks of varying size; returns the output buffer.
juce::AudioBuffer<float> run (vox::VoxProcessorBase& p, const juce::AudioBuffer<float>& input, int maxBlock,
                              std::function<void (juce::MidiBuffer&, int)> midiGen = {})
{
    const int outCh = p.getTotalNumOutputChannels();
    const int bufCh = juce::jmax (outCh, p.getTotalNumInputChannels());
    p.setRateAndBufferSizeDetails (fs, maxBlock);
    p.prepareToPlay (fs, maxBlock);

    juce::AudioBuffer<float> out (bufCh, input.getNumSamples());
    out.clear();
    for (int ch = 0; ch < p.getTotalNumInputChannels(); ++ch)
        out.copyFrom (ch, 0, input, juce::jmin (ch, input.getNumChannels() - 1), 0, input.getNumSamples());

    std::mt19937 rng (99);
    int pos = 0;
    while (pos < out.getNumSamples())
    {
        const int len = juce::jmin (out.getNumSamples() - pos, 1 + (int) (rng() % (unsigned) maxBlock));
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), bufCh, pos, len);
        juce::MidiBuffer midi;
        if (midiGen) midiGen (midi, pos);
        p.processBlock (view, midi);
        pos += len;
    }
    p.releaseResources();
    out.setSize (outCh, out.getNumSamples(), true);
    return out;
}

void randomise (juce::AudioProcessor& p, std::mt19937& rng)
{
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (auto* param : p.getParameters())
        param->setValueNotifyingHost (u (rng));
}

void resetDefaults (juce::AudioProcessor& p)
{
    for (auto* param : p.getParameters())
        param->setValueNotifyingHost (param->getDefaultValue());
}

int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok) { ++failures; std::printf ("    FAIL: %s\n", what.toRawUTF8()); }
}

void testPlugin (const PluginEntry& e)
{
    std::printf ("== %s\n", e.name);
    std::unique_ptr<vox::VoxProcessorBase> p (e.make());
    const auto voice = makeVoice (4.0, 2);
    const auto in = measure (voice);

    // MIDI generator for the vocoder (used when its source is set to MIDI)
    auto midiGen = [] (juce::MidiBuffer& m, int pos)
    {
        const int period = (int) fs;
        if (pos % period < 512) m.addEvent (juce::MidiMessage::noteOn (1, 57 + (pos / period) % 5, (juce::uint8) 100), 0);
        if (pos % period > period / 2 && pos % period < period / 2 + 512) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    };

    // 1. defaults, stereo
    {
        auto out = run (*p, voice, 512, midiGen);
        auto s = measure (out);
        std::printf ("  defaults   in %.1f dB RMS -> out %.1f dB RMS, peak %.3f, latency %d\n", in.rmsDb, s.rmsDb, s.peak, p->getLatencySamples());
        check (s.finite, "non-finite output (defaults)");
        check (s.peak < 4.0f, "output peak too high (defaults)");
        check (s.rmsDb > -60.0f, "output is (nearly) silent with defaults");
    }

    // 1b. DEVIL engaged
    {
        auto* d = p->apvts.getParameter ("devil");
        d->setValueNotifyingHost (1.0f);
        auto s = measure (run (*p, voice, 512, midiGen));
        std::printf ("  DEVIL      out %.1f dB RMS, peak %.3f\n", s.rmsDb, s.peak);
        check (s.finite && s.peak < 4.0f, "bad output with DEVIL on");
        d->setValueNotifyingHost (0.0f);
    }

    // 2. mono in/out and mono->stereo
    for (auto cfg : { std::pair<int, int> { 1, 1 }, std::pair<int, int> { 1, 2 } })
    {
        if (! setLayout (*p, cfg.first, cfg.second))
        {
            std::printf ("  layout %d->%d not supported\n", cfg.first, cfg.second);
            continue;
        }
        auto s = measure (run (*p, voice, 256, midiGen));
        std::printf ("  layout %d->%d out %.1f dB RMS, peak %.3f\n", cfg.first, cfg.second, s.rmsDb, s.peak);
        check (s.finite && s.peak < 4.0f, "bad output in layout test");
    }
    setLayout (*p, 2, 2);

    // 3. random parameter stress
    std::mt19937 rng (7);
    float worst = 0.0f;
    const auto shortVoice = makeVoice (0.8, 2);
    for (int k = 0; k < 40; ++k)
    {
        randomise (*p, rng);
        auto s = measure (run (*p, shortVoice, 1024, midiGen));
        check (s.finite, "non-finite output under random params #" + juce::String (k));
        worst = juce::jmax (worst, s.peak);
    }
    std::printf ("  random x40 worst peak %.3f\n", worst);
    check (worst < 32.0f, "runaway output under random params");
    resetDefaults (*p);

    // 4. state round-trip
    {
        juce::MemoryBlock mb;
        randomise (*p, rng);
        p->getStateInformation (mb);
        std::unique_ptr<vox::VoxProcessorBase> q (e.make());
        q->setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int i = 0; i < p->getParameters().size(); ++i)
        {
            const auto a = p->getParameters()[i]->getCurrentValueAsText();
            const auto b = q->getParameters()[i]->getCurrentValueAsText();
            if (a != b)
            {
                same = false;
                std::printf ("    param %s: %s vs %s\n", p->getParameters()[i]->getName (32).toRawUTF8(), a.toRawUTF8(), b.toRawUTF8());
            }
        }
        check (same, "state save/restore mismatch");
        resetDefaults (*p);
    }
}


#if VOX_HAS_VOXSPACE
// Measures the reverb's real RT60 (Schroeder integration) against the Decay knob.
void analyseReverb()
{
    std::printf ("== VoxSpace reverb analysis\n");
    for (float decaySec : { 0.8f, 1.8f, 4.0f })
    {
        SpaceProcessor p;
        auto set = [&p] (const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); };
        set ("dry", 0.0f); set ("duck", 0.0f); set ("delayLevel", 0.0f); set ("revLevel", 100.0f);
        set ("decay", decaySec); set ("predelay", 0.0f); set ("revLowCut", 20.0f); set ("damping", 20000.0f);
        juce::AudioBuffer<float> imp (2, (int) (fs * (decaySec * 2.5 + 1.0)));
        imp.clear();
        imp.setSample (0, 0, 1.0f); imp.setSample (1, 0, 1.0f);
        auto out = run (p, imp, 512);
        std::vector<double> e ((size_t) out.getNumSamples());
        double acc = 0.0;
        for (int i = out.getNumSamples() - 1; i >= 0; --i)
        {
            acc += (double) out.getSample (0, i) * out.getSample (0, i) + (double) out.getSample (1, i) * out.getSample (1, i);
            e[(size_t) i] = acc;
        }
        const double e0 = e[0];
        int t5 = -1, t35 = -1;
        for (size_t i = 0; i < e.size(); ++i)
        {
            const double db = 10.0 * std::log10 (e[i] / e0 + 1e-30);
            if (t5 < 0 && db <= -5.0) t5 = (int) i;
            if (t35 < 0 && db <= -35.0) { t35 = (int) i; break; }
        }
        const double rt60 = (t35 - t5) / fs * 2.0;
        std::printf ("  decay knob %.1f s -> measured RT60 %.2f s\n", decaySec, rt60);
        check (rt60 > decaySec * 0.8 && rt60 < decaySec * 1.25, "reverb RT60 far from Decay setting");

        SpaceProcessor q;
        auto setq = [&q] (const char* id, float v) { auto* r = q.apvts.getParameter (id); r->setValueNotifyingHost (r->convertTo0to1 (v)); };
        setq ("dry", 0.0f); setq ("duck", 0.0f); setq ("delayLevel", 0.0f); setq ("revLevel", 100.0f); setq ("decay", decaySec);
        auto wet = measure (run (q, makeVoice (4.0, 2), 512));
        std::printf ("  voice -18 dB RMS -> 100%% wet reverb %.1f dB RMS\n", wet.rmsDb);
    }
}
#endif



#if VOX_HAS_VOXTUNE || VOX_HAS_VOXPITCH
// Feeds a sawtooth at `inHz`, measures the output pitch with the shared YIN detector.
float measureOutputPitch (vox::VoxProcessorBase& p, float inHz)
{
    juce::AudioBuffer<float> b (2, (int) (fs * 2.0));
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        ph += inHz / fs; ph -= std::floor (ph);
        const float v = 0.25f * (float) (2.0 * ph - 1.0);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    auto out = run (p, b, 512);
    vox::PitchDetector det;
    det.prepare (fs);
    std::vector<float> est;
    for (int i = 0; i < out.getNumSamples(); ++i)
        if (det.push (out.getSample (0, i)) && i > (int) fs && det.frequency() > 0.0f)
            est.push_back (det.frequency());
    if (est.empty()) return 0.0f;
    std::sort (est.begin(), est.end());
    return est[est.size() / 2];
}

void pitchAccuracy()
{
    std::printf ("== pitch accuracy\n");
    auto cents = [] (float a, float b) { return 1200.0f * std::log2 (a / b); };
   #if VOX_HAS_VOXTUNE
    {
        TuneProcessor t;   // default: A minor, 100 %, 20 ms retune
        for (float in : { 452.0f, 425.0f, 300.0f })
        {
            const float out = measureOutputPitch (t, in);
            const float midi = 69.0f + 12.0f * std::log2 (in / 440.0f);
            const float expect = 440.0f * std::exp2 ((float) (TuneProcessor::snap (midi, 9, 2) - 69) / 12.0f);
            std::printf ("  VoxTune  in %.1f Hz -> out %.1f Hz (target %.1f, err %.1f ct)\n", in, out, expect, cents (out, expect));
            check (std::abs (cents (out, expect)) < 15.0f, "VoxTune does not land on the scale note");
        }
    }
   #endif
   #if VOX_HAS_VOXPITCH
    {
        PitchProcessor pp;
        for (float st : { 12.0f, -12.0f, 7.0f })
        {
            auto* sp = pp.apvts.getParameter ("shift");
            sp->setValueNotifyingHost (sp->convertTo0to1 (st));
            const float out = measureOutputPitch (pp, 220.0f);
            const float expect = 220.0f * std::exp2 (st / 12.0f);
            std::printf ("  VoxPitch %+.0f st: 220 Hz -> %.1f Hz (expect %.1f, err %.1f ct)\n", st, out, expect, cents (out, expect));
            check (std::abs (cents (out, expect)) < 15.0f, "VoxPitch shift is off");
        }
    }
   #endif
}
#endif

void screenshot (const PluginEntry& e, const juce::File& dir, bool devil = false)
{
    std::unique_ptr<vox::VoxProcessorBase> p (e.make());
    if (devil) p->apvts.getParameter ("devil")->setValueNotifyingHost (1.0f);
    p->setRateAndBufferSizeDetails (fs, 512);
    p->prepareToPlay (fs, 512);
    const auto voice = makeVoice (6.0, 2);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setSize (ed->getWidth(), ed->getHeight());

    juce::AudioBuffer<float> block (2, 512);
    for (int pos = 0; pos + 512 <= voice.getNumSamples(); pos += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom (ch, 0, voice, ch, pos, 512);
        juce::MidiBuffer midi;
        p->processBlock (block, midi);
        if ((pos / 512) % 3 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (8);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (40);

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    auto file = dir.getChildFile (juce::String (e.name) + (devil ? "-devil" : "") + ".png");
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("  screenshot -> %s (%dx%d)\n", file.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::String shotsDir;
    for (int i = 1; i + 1 < argc; ++i)
        if (juce::String (argv[i]) == "--shots")
            shotsDir = argv[i + 1];

    for (auto& e : plugins())
        testPlugin (e);
   #if VOX_HAS_VOXSPACE
    analyseReverb();
   #endif
   #if VOX_HAS_VOXTUNE || VOX_HAS_VOXPITCH
    pitchAccuracy();
   #endif

    if (shotsDir.isNotEmpty())
    {
        juce::File dir (juce::File::getCurrentWorkingDirectory().getChildFile (shotsDir));
        dir.createDirectory();
        for (auto& e : plugins())
            screenshot (e, dir, false);
        for (auto& e : plugins())
            if (juce::String (e.name) == "VoxDevil")
                screenshot (e, dir, true);
    }

    if (failures == 0) std::printf ("\nALL CHECKS PASSED\n");
    else               std::printf ("\n%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
