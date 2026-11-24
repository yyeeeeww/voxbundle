#include "SynthProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{
juce::String noteName (int n)
{
    static const char* names[] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
    return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
}

class SynthDisplay : public Display
{
public:
    explicit SynthDisplay (SynthProcessor& p) : proc (p) {}

    void refresh() override
    {
        for (int b = 0; b < SynthProcessor::numBands; ++b)
        {
            const float db = gainToDb (proc.bandLevels[(size_t) b].exchange (0.0f), -90.0f);
            levels[(size_t) b] = juce::jmax (db, levels[(size_t) b] - 2.0f);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        paintFrame (g, a);

        // Active notes, as text
        juce::StringArray held;
        std::array<int, SynthProcessor::numVoices> notes {};
        for (int v = 0; v < SynthProcessor::numVoices; ++v)
            notes[(size_t) v] = proc.voiceNotes[v].load();
        std::sort (notes.begin(), notes.end());
        for (int n : notes)
            if (n >= 0) held.addIfNotAlreadyThere (noteName (n));

        const bool midi = (int) *proc.source == 1;
        const auto info = a.reduced (8.0f, 6.0f);
        label (g, midi ? "midi" : "drone", info, juce::Justification::topLeft, Colours::text);
        label (g, held.isEmpty() ? "-" : held.joinIntoString (" "), info, juce::Justification::topRight, Colours::red);

        // Band levels as hairlines
        const auto inner = a.reduced (10.0f, 1.0f).withTrimmedTop (26.0f);
        const int nb = SynthProcessor::numBands;
        const float slot = inner.getWidth() / (float) nb;
        for (int b = 0; b < nb; ++b)
        {
            const float norm = juce::jlimit (0.0f, 1.0f, (levels[(size_t) b] + 66.0f) / 60.0f);
            const float x = inner.getX() + slot * ((float) b + 0.5f);
            g.setColour (Colours::line);
            g.fillRect (x - 0.5f, inner.getY(), 1.0f, inner.getHeight());
            g.setColour (Colours::red);
            g.fillRect (x - 1.5f, inner.getBottom() - inner.getHeight() * norm, 3.0f, inner.getHeight() * norm);
        }
    }

private:
    SynthProcessor& proc;
    std::array<float, SynthProcessor::numBands> levels {};
};
} // namespace

juce::AudioProcessorEditor* SynthProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxSynth";
    spec.displayHeight = 130;
    spec.sections = {
        { "notes", { "source", "droneNote", "chord" }, 0 },
        { "carrier", { "wave", "detune", "noise", "attack", "release" }, 0 },
        { "vocoder", { "response", "formant", "sibilance" }, 1 },
        { "out", { "spread", "voiceMix", "output" }, 1 },
    };
    spec.bipolar = { "formant", "output" };
    return new Editor (*this, spec, std::make_unique<SynthDisplay> (*this));
}
