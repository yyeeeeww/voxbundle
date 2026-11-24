#include "SpaceProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
class SpaceDisplay : public Display
{
public:
    explicit SpaceDisplay (SpaceProcessor& p) : proc (p) {}

    void refresh() override
    {
        const float d = proc.duckDb.exchange (0.0f);
        duck = d < duck ? d : duck * 0.85f + d * 0.15f;
        proc.wetPeakL.exchange (0.0f);
        proc.wetPeakR.exchange (0.0f);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto left = r.removeFromLeft (r.getWidth() * 0.5f - 12.0f);
        r.removeFromLeft (24.0f);
        paintReverb (g, left);
        paintDelay (g, r);
    }

private:
    void paintReverb (juce::Graphics& g, juce::Rectangle<float> a)
    {
        paintFrame (g, a);
        const bool hell = proc.devil();
        const float dec = *proc.decay * (hell ? 3.0f : 1.0f);
        label (g, "reverb " + juce::String (dec, 2) + " s", a.reduced (8.0f, 6.0f), juce::Justification::topLeft, Colours::text);

        const auto inner = a.reduced (1.0f).withTrimmedTop (24.0f);
        const float total = 8.0f, pre = *proc.predelay * 0.001f;
        const float peakY = inner.getBottom() - inner.getHeight() * (0.15f + 0.8f * *proc.revLevel * 0.01f);
        auto toX = [&] (float t) { return inner.getX() + inner.getWidth() * juce::jmin (1.0f, t / total); };

        juce::Path env;
        env.startNewSubPath (inner.getX(), inner.getBottom());
        env.lineTo (toX (pre), inner.getBottom());
        for (int i = 0; i <= 160; ++i)
        {
            const float t = pre + (total - pre) * (float) i / 160.0f;
            const float db = -60.0f * (t - pre) / juce::jmax (0.1f, dec);
            env.lineTo (toX (t), inner.getBottom() - (inner.getBottom() - peakY) * std::pow (10.0f, db / 40.0f));
        }
        g.setColour (Colours::red);
        g.strokePath (env, juce::PathStrokeType (1.2f));
    }

    void paintDelay (juce::Graphics& g, juce::Rectangle<float> a)
    {
        paintFrame (g, a);
        const bool sync = *proc.delaySync > 0.5f;
        juce::String t = "delay ";
        if (sync) t << SpaceProcessor::divisionNames[(int) *proc.delayDiv] << " ";
        t << juce::roundToInt (proc.currentDelayMs.load()) << " ms";
        label (g, t, a.reduced (8.0f, 6.0f), juce::Justification::topLeft, Colours::text);
        label (g, "duck " + juce::String (duck, 1), a.reduced (8.0f, 6.0f), juce::Justification::topRight);

        const auto inner = a.reduced (8.0f, 1.0f).withTrimmedTop (24.0f);
        const bool hell = proc.devil();
        const float fb = hell ? juce::jmin (1.0f, *proc.feedback * 0.013f + 0.35f) : *proc.feedback * 0.01f;
        const float ms = proc.currentDelayMs.load();
        const bool ping = *proc.pingPong > 0.5f;
        const float window = 2.4f;

        float amp = 1.0f;
        for (int k = 1; k < 64; ++k)
        {
            const float tk = (float) k * ms * 0.001f;
            if (tk > window || amp < 0.02f) break;
            const float x = inner.getX() + inner.getWidth() * tk / window;
            const float h = inner.getHeight() * (0.15f + 0.8f * *proc.delayLevel * 0.01f) * amp;
            g.setColour (ping && k % 2 == 0 ? Colours::text : Colours::red);
            g.fillRect (x, inner.getBottom() - h, 1.5f, h);
            amp *= juce::jmax (fb, 0.0001f);
        }
    }

    SpaceProcessor& proc;
    float duck = 0.0f;
};
} // namespace

juce::AudioProcessorEditor* SpaceProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxSpace";
    spec.displayHeight = 130;
    spec.sections = {
        { "reverb", { "size", "decay", "predelay", "damping", "revLowCut", "revLevel" }, 0 },
        { "mix", { "duck", "width", "dry" }, 0 },
        { "delay", { "delaySync", "delayDiv", "delayTime", "feedback", "pingPong", "delayTone", "delayLevel" }, 1 },
    };
    return new Editor (*this, spec, std::make_unique<SpaceDisplay> (*this));
}
