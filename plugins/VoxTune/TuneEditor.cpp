#include "TuneProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
juce::String midiName (float m)
{
    static const char* names[] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
    const int n = juce::roundToInt (m);
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
}

class TuneDisplay : public Display
{
public:
    explicit TuneDisplay (TuneProcessor& p) : proc (p) { hist.fill ({}); }

    void refresh() override
    {
        hist[(size_t) pos] = { proc.detectedMidi.load(), proc.targetMidi.load() };
        pos = (pos + 1) % (int) hist.size();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        paintFrame (g, a);

        const auto& now = hist[(size_t) ((pos + (int) hist.size() - 1) % (int) hist.size())];
        const auto info = a.reduced (8.0f, 6.0f);
        juce::String head = TuneProcessor::keyNames[(int) *proc.key].toLowerCase() + " "
                          + TuneProcessor::scaleNames[(int) *proc.scale].toLowerCase();
        label (g, head, info, juce::Justification::topLeft, Colours::text);
        if (now.det > 0.0f)
        {
            const int cents = juce::roundToInt ((now.det - std::round (now.det)) * 100.0f);
            label (g, midiName (now.det) + (cents >= 0 ? " +" : " ") + juce::String (cents) + "ct  >  " + midiName (now.tgt),
                   info, juce::Justification::topRight, Colours::red);
        }
        else
            label (g, "-", info, juce::Justification::topRight);

        // Pitch history: centre on the recent average note, +-7 semitones
        float centre = 0.0f; int cnt = 0;
        for (auto& h : hist) if (h.det > 0.0f) { centre += h.det; ++cnt; }
        centre = cnt > 0 ? std::round (centre / (float) cnt) : 60.0f;

        const auto inner = a.reduced (1.0f).withTrimmedTop (24.0f);
        const float span = 7.0f;
        auto toY = [&] (float m) { return inner.getCentreY() - (m - centre) / span * inner.getHeight() * 0.5f; };

        float lastLabelY = -1.0e9f;
        for (int s = 6; s >= -6; --s)
        {
            const int note = (int) centre + s;
            const bool inScale = TuneProcessor::snap ((float) note, (int) *proc.key, (int) *proc.scale) == note;
            if (! inScale) continue;
            g.setColour (Colours::line);
            g.fillRect (inner.getX(), toY ((float) note), inner.getWidth(), 1.0f);
            if (toY ((float) note) - lastLabelY < 14.0f) continue;
            lastLabelY = toY ((float) note);
            label (g, midiName ((float) note), { inner.getX() + 4.0f, lastLabelY - 11.0f, 40.0f, 10.0f }, juce::Justification::left);
        }

        const int count = (int) hist.size();
        const float dx = inner.getWidth() / (float) (count - 1);
        juce::Path det, tgt;
        bool penDet = false, penTgt = false;
        for (int i = 0; i < count; ++i)
        {
            const auto& h = hist[(size_t) ((pos + i) % count)];
            const float x = inner.getX() + dx * (float) i;
            if (h.det > 0.0f && std::abs (h.det - centre) < span)
            {
                penDet ? det.lineTo (x, toY (h.det)) : det.startNewSubPath (x, toY (h.det));
                penTgt ? tgt.lineTo (x, toY (h.tgt)) : tgt.startNewSubPath (x, toY (h.tgt));
                penDet = penTgt = true;
            }
            else penDet = penTgt = false;
        }
        g.setColour (Colours::dim);
        g.strokePath (det, juce::PathStrokeType (1.0f));
        g.setColour (Colours::red);
        g.strokePath (tgt, juce::PathStrokeType (1.5f));
    }

private:
    struct P { float det = 0.0f, tgt = 0.0f; };
    TuneProcessor& proc;
    std::array<P, 150> hist;
    int pos = 0;
};
} // namespace

juce::AudioProcessorEditor* TuneProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxTune";
    spec.displayHeight = 160;
    spec.sections = {
        { "scale", { "key", "scale" }, 0 },
        { "correction", { "retune", "amount", "humanize" }, 0 },
        { "out", { "mix", "output" }, 0 },
    };
    spec.bipolar = { "output" };
    return new Editor (*this, spec, std::make_unique<TuneDisplay> (*this));
}
