#include "HeatProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
class HeatDisplay : public Display
{
public:
    explicit HeatDisplay (HeatProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        paintFrame (g, a);

        g.setColour (Colours::line);
        g.fillRect (a.getX(), a.getCentreY(), a.getWidth(), 1.0f);
        g.fillRect (a.getCentreX(), a.getY(), 1.0f, a.getHeight());

        const bool hell = proc.devil();
        const int mode = (int) *proc.mode;
        const float dg = dbToGain (*proc.drive + (hell ? 18.0f : 0.0f));
        const float comp = *proc.autoGain > 0.5f ? HeatProcessor::autoGainFor (mode, dg) : 1.0f;
        const float offset = HeatProcessor::shape (mode, 0.0f);

        juce::Path curve;
        const int n = 400;
        for (int i = 0; i <= n; ++i)
        {
            const float x = -1.0f + 2.0f * (float) i / (float) n;
            float y = HeatProcessor::shape (mode, x * dg) - offset;
            if (hell) y = std::sin (y * 2.6f);
            y = juce::jlimit (-1.2f, 1.2f, y * comp);
            const auto pt = juce::Point<float> (a.getX() + a.getWidth() * (x + 1.0f) * 0.5f,
                                                a.getCentreY() - a.getHeight() * 0.4f * y);
            i == 0 ? curve.startNewSubPath (pt) : curve.lineTo (pt);
        }
        g.setColour (Colours::red);
        g.strokePath (curve, juce::PathStrokeType (1.2f));

        static const char* names[] = { "tape", "tube", "soft clip", "hard clip" };
        const auto info = a.reduced (8.0f, 6.0f);
        label (g, names[juce::jlimit (0, 3, mode)], info, juce::Justification::topLeft, Colours::text);
        label (g, "4x os", info, juce::Justification::topRight);
        if (hell)
            label (g, "folded", info, juce::Justification::bottomLeft, Colours::red);
    }

private:
    HeatProcessor& proc;
};
} // namespace

juce::AudioProcessorEditor* HeatProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxHeat";
    spec.displayHeight = 150;
    spec.sections = {
        { "saturation", { "mode", "drive", "tighten", "tone" }, 0 },
        { "out", { "autoGain", "mix", "output" }, 0 },
    };
    spec.bipolar = { "tone", "output" };
    return new Editor (*this, spec, std::make_unique<HeatDisplay> (*this));
}
