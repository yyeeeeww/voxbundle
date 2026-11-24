#include "AdlibProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
class AdlibDisplay : public Display
{
public:
    explicit AdlibDisplay (AdlibProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        auto left = a.removeFromLeft (a.getWidth() * 0.62f);
        a.removeFromLeft (24.0f);
        paintFilter (g, left);
        paintThrow (g, a);
    }

private:
    void paintFilter (juce::Graphics& g, juce::Rectangle<float> a)
    {
        paintFrame (g, a);
        const bool hell = proc.devil();
        const int st = hell ? 3 : (int) *proc.style;
        label (g, AdlibProcessor::styleNames[st].toLowerCase(), a.reduced (8.0f, 6.0f), juce::Justification::topLeft, Colours::text);
        juce::String layers;
        if (*proc.octDown > 0.5f || hell) layers << "-12 ";
        if (*proc.octUp > 0.5f) layers << "+12";
        label (g, layers.isEmpty() ? "no layers" : layers.trim(), a.reduced (8.0f, 6.0f), juce::Justification::topRight,
               layers.isEmpty() ? Colours::dim : Colours::red);

        const double fs = 48000.0;
        Biquad f[AdlibProcessor::numStages];
        AdlibProcessor::styleFilters (st, fs, f);
        const auto inner = a.reduced (1.0f, 8.0f).withTrimmedTop (18.0f);
        juce::Path p;
        for (int i = 0; i <= (int) inner.getWidth(); ++i)
        {
            const double fr = 20.0 * std::pow (1000.0, (double) i / inner.getWidth());
            double mag = 1.0;
            for (auto& s : f) mag *= s.magnitude (fr, fs);
            const float db = (float) (20.0 * std::log10 (juce::jmax (mag, 1.0e-6)));
            const float y = juce::jlimit (inner.getY(), inner.getBottom(), inner.getCentreY() - db / 30.0f * inner.getHeight() * 0.5f);
            i == 0 ? p.startNewSubPath (inner.getX() + (float) i, y) : p.lineTo (inner.getX() + (float) i, y);
        }
        g.setColour (Colours::red);
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    void paintThrow (juce::Graphics& g, juce::Rectangle<float> a)
    {
        paintFrame (g, a);
        label (g, "throw " + AdlibProcessor::divisionNames[(int) *proc.throwDiv] + " "
                  + juce::String (juce::roundToInt (proc.throwMs.load())) + " ms",
               a.reduced (8.0f, 6.0f), juce::Justification::topLeft, Colours::text);

        const auto inner = a.reduced (10.0f, 8.0f).withTrimmedTop (18.0f);
        const float fb = proc.devil() ? 0.92f : *proc.feedback * 0.01f;
        float amp = *proc.throwAmt * 0.01f;
        for (int k = 0; k < 16 && amp > 0.02f; ++k)
        {
            const float x = inner.getX() + inner.getWidth() * (float) (k + 1) / 17.0f;
            const float h = inner.getHeight() * amp;
            g.setColour (k % 2 == 0 ? Colours::red : Colours::text);
            g.fillRect (x, inner.getBottom() - h, 1.5f, h);
            amp *= fb;
        }
    }

    AdlibProcessor& proc;
};
} // namespace

juce::AudioProcessorEditor* AdlibProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxAdlib";
    spec.displayHeight = 130;
    spec.sections = {
        { "colour", { "style", "drive" }, 0 },
        { "layers", { "octDown", "octUp", "width" }, 0 },
        { "throw", { "throw", "throwDiv", "feedback" }, 1 },
        { "out", { "output" }, 1 },
    };
    spec.bipolar = { "output" };
    return new Editor (*this, spec, std::make_unique<AdlibDisplay> (*this));
}
