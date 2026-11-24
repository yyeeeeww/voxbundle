#include "PitchProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
class PitchDisplay : public Display
{
public:
    explicit PitchDisplay (PitchProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        paintFrame (g, a);

        const float st = proc.totalSemitones();
        const auto info = a.reduced (8.0f, 6.0f);
        label (g, *proc.track > 0.5f ? "pitch-synced grains" : "fixed grains", info, juce::Justification::topLeft);
        if (proc.devil())
            label (g, "+ demon -18 st", info, juce::Justification::topRight, Colours::red);

        g.setColour (Colours::text);
        g.setFont (font (34.0f));
        g.drawText ((st > 0.05f ? "+" : "") + juce::String (st, 2), a.withTrimmedBottom (34.0f), juce::Justification::centred);

        // Ruler -24..+24
        const auto ruler = a.reduced (24.0f, 0.0f).removeFromBottom (34.0f).withTrimmedBottom (14.0f);
        auto toX = [&] (float s) { return ruler.getX() + ruler.getWidth() * (s + 24.0f) / 48.0f; };
        for (int s = -24; s <= 24; ++s)
        {
            const bool major = s % 12 == 0;
            g.setColour (major ? Colours::lineHi : Colours::line);
            const float h = major ? ruler.getHeight() : ruler.getHeight() * 0.4f;
            g.fillRect (toX ((float) s), ruler.getBottom() - h, 1.0f, h);
        }
        g.setColour (Colours::red);
        g.fillRect (toX (juce::jlimit (-24.0f, 24.0f, st)) - 1.0f, ruler.getY() - 4.0f, 2.0f, ruler.getHeight() + 4.0f);
    }

    void refresh() override
    {
        const float st = proc.totalSemitones();
        const bool d = proc.devil();
        if (! juce::exactlyEqual (st, last) || d != lastDevil) { last = st; lastDevil = d; repaint(); }
    }

private:
    PitchProcessor& proc;
    float last = 1.0e9f;
    bool lastDevil = false;
};
} // namespace

juce::AudioProcessorEditor* PitchProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxPitch";
    spec.displayHeight = 140;
    spec.sections = {
        { "pitch", { "shift", "fine" }, 0 },
        { "grain", { "track", "grain", "tone" }, 0 },
        { "out", { "mix", "output" }, 0 },
    };
    spec.bipolar = { "shift", "fine", "output" };
    return new Editor (*this, spec, std::make_unique<PitchDisplay> (*this));
}
