#include "DoubleProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
// Voices plotted as points: x = pan, y = pitch offset; size grows with delay.
class DoubleDisplay : public Display
{
public:
    explicit DoubleDisplay (DoubleProcessor& p) : proc (p) {}

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        paintFrame (g, a);
        const auto info = a.reduced (8.0f, 6.0f);
        label (g, "l", info, juce::Justification::bottomLeft);
        label (g, "r", info, juce::Justification::bottomRight);
        label (g, "pitch / pan", info, juce::Justification::topLeft);

        const auto inner = a.reduced (30.0f, 18.0f);
        g.setColour (Colours::line);
        g.fillRect (inner.getX(), inner.getCentreY(), inner.getWidth(), 1.0f);
        g.fillRect (inner.getCentreX(), inner.getY(), 1.0f, inner.getHeight());

        // dry voice in the centre
        g.setColour (Colours::text);
        g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre (inner.getCentre()));

        static constexpr float pan[DoubleProcessor::maxVoices] = { -1.0f, 1.0f, -0.5f, 0.5f };
        const float w = *proc.width * 0.01f;
        for (int v = 0; v < DoubleProcessor::maxVoices; ++v)
        {
            const float ms = proc.voiceDelayMs[v].load();
            if (ms <= 0.0f) continue;
            const float c = juce::jlimit (-60.0f, 60.0f, proc.voiceCents[v].load());
            const juce::Point<float> pt { inner.getCentreX() + pan[v] * w * inner.getWidth() * 0.5f,
                                          inner.getCentreY() - c / 60.0f * inner.getHeight() * 0.5f };
            const float r = 3.0f + ms * 0.12f;
            g.setColour (Colours::red);
            g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pt), 1.0f);
            g.drawLine ({ inner.getCentre(), pt }, 0.5f);
        }
    }

private:
    DoubleProcessor& proc;
};
} // namespace

juce::AudioProcessorEditor* DoubleProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxDouble";
    spec.displayHeight = 150;
    spec.sections = {
        { "voices", { "voices", "detune", "delay", "drift" }, 0 },
        { "image", { "width", "lowCut" }, 0 },
        { "mix", { "doubles", "dry" }, 0 },
    };
    return new Editor (*this, spec, std::make_unique<DoubleDisplay> (*this));
}
