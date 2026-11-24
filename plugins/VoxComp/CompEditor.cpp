#include "CompProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
class CompDisplay : public Display
{
public:
    explicit CompDisplay (CompProcessor& p) : proc (p) { history.fill ({}); }

    void refresh() override
    {
        const float gr = proc.grDb.exchange (0.0f);
        const float ds = proc.deessDb.exchange (0.0f);
        const float det = proc.detectorDb.exchange (-100.0f);
        history[(size_t) pos] = { gr, ds };
        pos = (pos + 1) % (int) history.size();
        grText = gr < grText ? gr : grText * 0.92f + gr * 0.08f;
        dsText = ds > dsText ? ds : dsText * 0.92f + ds * 0.08f;
        inputDb = det > inputDb ? det : inputDb - 1.5f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto curve = r.removeFromLeft (r.getHeight());
        r.removeFromLeft (24.0f);
        paintTransfer (g, curve);
        paintHistory (g, r);
    }

private:
    void paintTransfer (juce::Graphics& g, juce::Rectangle<float> a)
    {
        paintFrame (g, a);
        const float lo = -60.0f;
        auto toX = [&] (float db) { return a.getX() + a.getWidth() * (db - lo) / -lo; };
        auto toY = [&] (float db) { return a.getBottom() - a.getHeight() * (db - lo) / -lo; };

        g.setColour (Colours::line);
        g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);

        const float T = *proc.threshold, R = *proc.ratio, K = *proc.knee;
        juce::Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float in = lo + (float) i * (-lo / 120.0f);
            const auto pt = juce::Point<float> (toX (in), toY (in + CompProcessor::gainComputer (in, T, R, K)));
            i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
        }
        g.setColour (Colours::red);
        g.strokePath (p, juce::PathStrokeType (1.2f));

        if (inputDb > lo)
        {
            const float in = juce::jmin (inputDb, 0.0f);
            g.setColour (Colours::text);
            g.fillRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ toX (in), toY (in + CompProcessor::gainComputer (in, T, R, K)) }));
        }
        label (g, "in/out", a.reduced (6.0f), juce::Justification::topLeft);
    }

    void paintHistory (juce::Graphics& g, juce::Rectangle<float> a)
    {
        paintFrame (g, a);
        label (g, "gr " + juce::String (grText, 1) + " db", a.reduced (8.0f, 6.0f), juce::Justification::topLeft, Colours::red);
        label (g, "de-ess " + juce::String (-dsText, 1) + " db", a.reduced (8.0f, 6.0f), juce::Justification::topRight, Colours::text);

        const auto inner = a.reduced (1.0f, 24.0f);
        const float range = 24.0f;
        auto toY = [&] (float db) { return inner.getY() + inner.getHeight() * juce::jlimit (0.0f, 1.0f, -db / range); };

        const int count = (int) history.size();
        const float dx = inner.getWidth() / (float) (count - 1);
        juce::Path grPath, dsPath;
        for (int i = 0; i < count; ++i)
        {
            const auto& h = history[(size_t) ((pos + i) % count)];
            const float x = inner.getX() + dx * (float) i;
            i == 0 ? grPath.startNewSubPath (x, toY (h.gr)) : grPath.lineTo (x, toY (h.gr));
            i == 0 ? dsPath.startNewSubPath (x, toY (-h.ds)) : dsPath.lineTo (x, toY (-h.ds));
        }
        g.setColour (Colours::text.withAlpha (0.55f));
        const float dashes[] = { 2.0f, 3.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, dsPath, dashes, 2);
        g.fillPath (dashed);
        g.setColour (Colours::red);
        g.strokePath (grPath, juce::PathStrokeType (1.2f));
    }

    struct Sample { float gr = 0.0f, ds = 0.0f; };
    CompProcessor& proc;
    std::array<Sample, 200> history;
    int pos = 0;
    float grText = 0.0f, dsText = 0.0f, inputDb = -100.0f;
};
} // namespace

juce::AudioProcessorEditor* CompProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxComp";
    spec.displayHeight = 150;
    spec.sections = {
        { "compressor", { "threshold", "ratio", "attack", "release", "knee", "makeup" }, 0 },
        { "de-esser", { "dsOn", "dsFreq", "dsThresh" }, 1 },
        { "out", { "mix", "output", "limiter" }, 1 },
    };
    spec.bipolar = { "output" };
    return new Editor (*this, spec, std::make_unique<CompDisplay> (*this));
}
