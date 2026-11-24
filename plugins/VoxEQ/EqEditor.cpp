#include "EqProcessor.h"
#include "VoxEditor.h"

using namespace vox;

namespace
{

class EqDisplay : public Display
{
public:
    explicit EqDisplay (EqProcessor& p) : proc (p) {}

    void refresh() override
    {
        const auto v = proc.targetValues();
        const auto same = [] (const EqProcessor::BandValues& a, const EqProcessor::BandValues& b)
        {
            using juce::exactlyEqual;
            return exactlyEqual (a.hpFreq, b.hpFreq) && a.hp24 == b.hp24 && exactlyEqual (a.lowFreq, b.lowFreq)
                && exactlyEqual (a.lowGain, b.lowGain) && exactlyEqual (a.midFreq, b.midFreq) && exactlyEqual (a.midGain, b.midGain)
                && exactlyEqual (a.midQ, b.midQ) && exactlyEqual (a.presFreq, b.presFreq) && exactlyEqual (a.presGain, b.presGain)
                && exactlyEqual (a.presQ, b.presQ) && exactlyEqual (a.airFreq, b.airFreq) && exactlyEqual (a.airGain, b.airGain);
        };
        if (! valid || ! same (v, last))
        {
            last = v;
            valid = true;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintFrame (g, getLocalBounds().toFloat());
        const auto a = plotArea();
        const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;

        g.setFont (font (9.0f));
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            const float x = freqToX (f);
            g.setColour (Colours::line);
            g.fillRect (x, a.getY(), 1.0f, a.getHeight());
            g.setColour (Colours::dim);
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                        (int) x + 3, (int) a.getBottom() - 12, 30, 11, juce::Justification::centredLeft);
        }
        g.setColour (Colours::line);
        g.fillRect (a.getX(), dbToY (0.0f), a.getWidth(), 1.0f);

        Biquad c[EqProcessor::numStages];
        EqProcessor::computeCoefficients (last, fs, c);
        juce::Path curve;
        const int points = (int) a.getWidth();
        for (int i = 0; i <= points; ++i)
        {
            const float x = a.getX() + (float) i;
            double mag = 1.0;
            for (auto& s : c)
                mag *= s.magnitude (xToFreq (x), fs);
            const float y = juce::jlimit (a.getY(), a.getBottom(), dbToY ((float) (20.0 * std::log10 (juce::jmax (mag, 1.0e-6)))));
            i == 0 ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
        }
        g.setColour (Colours::red);
        g.strokePath (curve, juce::PathStrokeType (1.2f));

        for (int h = 0; h < numHandles; ++h)
        {
            const auto pt = handlePos (h);
            const auto sq = juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt);
            g.setColour (Colours::bg);
            g.fillRect (sq);
            g.setColour (h == dragging || h == hover ? Colours::text : Colours::red);
            g.drawRect (sq, 1.0f);
        }

        label (g, proc.devil() ? "x2" : "drag", getLocalBounds().toFloat().reduced (8.0f, 6.0f),
               juce::Justification::topRight, proc.devil() ? Colours::red : Colours::dim);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitHandle (e.position);
        if (h != hover) { hover = h; repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = hitHandle (e.position);
        if (dragging < 0) return;
        for (auto* p : handleParams (dragging))
            if (p != nullptr) p->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        auto params = handleParams (dragging);
        const auto a = plotArea();
        const float x = juce::jlimit (a.getX(), a.getRight(), e.position.x);
        const float y = juce::jlimit (a.getY(), a.getBottom(), e.position.y);
        if (params[0] != nullptr) params[0]->setValueNotifyingHost (params[0]->convertTo0to1 ((float) xToFreq (x)));
        if (params[1] != nullptr) params[1]->setValueNotifyingHost (params[1]->convertTo0to1 (yToDb (y)));
        refresh();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging < 0) return;
        for (auto* p : handleParams (dragging))
            if (p != nullptr) p->endChangeGesture();
        dragging = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int h = hitHandle (e.position);
        if (h < 0) return;
        for (auto* p : handleParams (h))
            if (p != nullptr)
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
            }
    }

private:
    static constexpr int numHandles = 5;
    static constexpr float maxDb = 24.0f;

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (1.0f, 8.0f); }

    float freqToX (double f) const
    {
        const auto a = plotArea();
        return a.getX() + a.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0));
    }
    double xToFreq (float x) const
    {
        const auto a = plotArea();
        return 20.0 * std::pow (1000.0, (double) ((x - a.getX()) / a.getWidth()));
    }
    float dbToY (float db) const
    {
        const auto a = plotArea();
        return a.getCentreY() - a.getHeight() * 0.5f * db / maxDb;
    }
    float yToDb (float y) const
    {
        const auto a = plotArea();
        return (a.getCentreY() - y) / (a.getHeight() * 0.5f) * maxDb;
    }

    juce::Point<float> handlePos (int h) const
    {
        switch (h)
        {
            case 0: return { freqToX (*proc.hpFreq), dbToY (0.0f) };
            case 1: return { freqToX (last.lowFreq), dbToY (last.lowGain) };
            case 2: return { freqToX (last.midFreq), dbToY (last.midGain) };
            case 3: return { freqToX (last.presFreq), dbToY (last.presGain) };
            default: return { freqToX (last.airFreq), dbToY (last.airGain) };
        }
    }

    std::array<juce::RangedAudioParameter*, 2> handleParams (int h) const
    {
        auto& s = proc.apvts;
        switch (h)
        {
            case 0: return { s.getParameter ("hpFreq"), nullptr };
            case 1: return { s.getParameter ("lowFreq"), s.getParameter ("lowGain") };
            case 2: return { s.getParameter ("midFreq"), s.getParameter ("midGain") };
            case 3: return { s.getParameter ("presFreq"), s.getParameter ("presGain") };
            default: return { s.getParameter ("airFreq"), s.getParameter ("airGain") };
        }
    }

    int hitHandle (juce::Point<float> p) const
    {
        int best = -1;
        float bestD = 12.0f;
        for (int h = 0; h < numHandles; ++h)
        {
            const float d = handlePos (h).getDistanceFrom (p);
            if (d < bestD) { bestD = d; best = h; }
        }
        return best;
    }

    EqProcessor& proc;
    EqProcessor::BandValues last {};
    bool valid = false;
    int hover = -1, dragging = -1;
};
} // namespace

juce::AudioProcessorEditor* EqProcessor::createEditor()
{
    EditorSpec spec;
    spec.name = "VoxEQ";
    spec.displayHeight = 150;
    spec.sections = {
        { "low cut", { "hpOn", "hpFreq", "hpSlope" }, 0 },
        { "body", { "lowFreq", "lowGain" }, 0 },
        { "mid", { "midFreq", "midGain", "midQ" }, 0 },
        { "presence", { "presFreq", "presGain", "presQ" }, 1 },
        { "air", { "airFreq", "airGain" }, 1 },
        { "out", { "output" }, 1 },
    };
    spec.bipolar = { "lowGain", "midGain", "presGain", "airGain", "output" };
    return new Editor (*this, spec, std::make_unique<EqDisplay> (*this));
}
