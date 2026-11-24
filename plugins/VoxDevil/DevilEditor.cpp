#include "DevilProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
class DevilDisplay : public Display
{
public:
    explicit DevilDisplay (DevilProcessor& p) : proc (p) { hist.fill (0.0f); }

    void refresh() override
    {
        const float g = proc.grDb.exchange (0.0f);
        hist[(size_t) pos] = g;
        pos = (pos + 1) % (int) hist.size();
        shown = g < shown ? g : shown * 0.9f + g * 0.1f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        paintFrame (g, a);

        // Big number on the left
        auto left = a.removeFromLeft (220.0f);
        g.setColour (Colours::line);
        g.fillRect (left.getRight(), a.getY(), 1.0f, a.getHeight());
        g.setColour (shown < -12.0f ? Colours::red : Colours::text);
        g.setFont (font (40.0f));
        g.drawText (juce::String (shown, 1), left.withTrimmedBottom (16.0f), juce::Justification::centred);
        label (g, "db gain reduction", left.reduced (8.0f, 10.0f), juce::Justification::centredBottom);

        // History, 0..-36 dB downward
        const auto inner = a.reduced (1.0f, 10.0f);
        auto toY = [&] (float db) { return inner.getY() + inner.getHeight() * juce::jlimit (0.0f, 1.0f, -db / 36.0f); };
        g.setColour (Colours::line);
        for (float db : { -12.0f, -24.0f })
            g.fillRect (inner.getX(), toY (db), inner.getWidth(), 1.0f);

        const int count = (int) hist.size();
        const float dx = inner.getWidth() / (float) (count - 1);
        g.setColour (Colours::red);
        for (int i = 0; i < count; ++i)
        {
            const float v = hist[(size_t) ((pos + i) % count)];
            const float x = inner.getX() + dx * (float) i;
            g.fillRect (x, inner.getY(), juce::jmax (1.0f, dx - 1.0f), toY (v) - inner.getY());
        }
    }

private:
    DevilProcessor& proc;
    std::array<float, 120> hist;
    int pos = 0;
    float shown = 0.0f;
};
} // namespace

juce::AudioProcessorEditor* DevilProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxDevil";
    spec.displayHeight = 140;
    spec.sections = {
        { "drive", { "input", "crush", "saturate" }, 0 },
        { "time", { "attack", "release" }, 0 },
        { "out", { "mix", "output" }, 0 },
    };
    spec.bipolar = { "output" };
    return new Editor (*this, spec, std::make_unique<DevilDisplay> (*this));
}
