#pragma once
// Data-driven minimal editor shared by the bundle: header with the DEVIL switch,
// optional display, sections of controls generated from parameter types, IN/OUT meters.

#include <juce_audio_utils/juce_audio_utils.h>
#include "VoxBase.h"
#include "VoxLookAndFeel.h"

namespace vox
{
struct Section
{
    juce::String title;
    juce::StringArray params;
    int row = 0;
};

struct EditorSpec
{
    juce::String name;
    std::vector<Section> sections;
    juce::StringArray bipolar;   // knobs whose arc starts from the centre
    int displayHeight = 0;       // 0 = no display
};

// Plugin-specific visualisation; refresh() is called ~30x per second.
class Display : public juce::Component
{
public:
    virtual void refresh() { repaint(); }

    static void paintFrame (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Colours::line);
        g.drawRect (r, 1.0f);
    }

    static void label (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                       juce::Justification j, juce::Colour c = Colours::dim)
    {
        g.setColour (c);
        g.setFont (font (10.0f));
        g.drawText (text, r, j);
    }
};

//==============================================================================
class ControlCell : public juce::Component
{
public:
    enum class Kind { Knob, Toggle, Choice };

    ControlCell (juce::AudioProcessorValueTreeState& state, const juce::String& id, bool bipolar)
        : param (state.getParameter (id))
    {
        jassert (param != nullptr);

        if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
            kind = Kind::Toggle;
        else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            kind = Kind::Choice;
            combo.addItemList (choice->choices, 1);
        }

        switch (kind)
        {
            case Kind::Knob:
                slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                slider.setRotaryParameters (pi * 1.25f, pi * 2.75f, true);
                slider.setMouseDragSensitivity (240);
                slider.getProperties().set ("bipolar", bipolar);
                addAndMakeVisible (slider);
                sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
                slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
                break;
            case Kind::Toggle:
                addAndMakeVisible (button);
                buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, button);
                break;
            case Kind::Choice:
                combo.setJustificationType (juce::Justification::centred);
                addAndMakeVisible (combo);
                comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, combo);
                break;
        }

        for (auto* l : { &nameLabel, &valueLabel })
        {
            l->setJustificationType (juce::Justification::centred);
            l->setInterceptsMouseClicks (false, false);
            l->setBorderSize ({});
            addAndMakeVisible (*l);
        }
        nameLabel.setFont (font (10.0f));
        nameLabel.setColour (juce::Label::textColourId, Colours::dim);
        nameLabel.setText (param->getName (32).toUpperCase(), juce::dontSendNotification);
        valueLabel.setFont (font (11.0f));
        valueLabel.setColour (juce::Label::textColourId, Colours::text);
        refreshValue();
    }

    int preferredWidth() const { return kind == Kind::Choice ? 100 : 74; }

    void refreshValue()
    {
        const auto t = kind == Kind::Choice ? juce::String() : param->getCurrentValueAsText().toLowerCase();
        if (t != valueLabel.getText())
            valueLabel.setText (t, juce::dontSendNotification);
    }

    void resized() override
    {
        const int w = getWidth();
        switch (kind)
        {
            case Kind::Knob:   slider.setBounds (w / 2 - 28, 0, 56, 56); break;
            case Kind::Toggle: button.setBounds (w / 2 - 14, 14, 28, 28); break;
            case Kind::Choice: combo.setBounds (4, 16, w - 8, 24); break;
        }
        valueLabel.setBounds (0, 58, w, 14);
        nameLabel.setBounds (0, 73, w, 13);
    }

private:
    juce::RangedAudioParameter* param;
    Kind kind = Kind::Knob;
    juce::Slider slider;
    juce::ToggleButton button;
    juce::ComboBox combo;
    juce::Label nameLabel, valueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
};

//==============================================================================
class EditorBody : public juce::Component
{
public:
    static constexpr int W = 860, headerH = 56, margin = 20, meterW = 26, rowH = 118, gap = 14, cellH = 88;

    EditorBody (VoxProcessorBase& p, EditorSpec s, std::unique_ptr<Display> d)
        : proc (p), spec (std::move (s)), display (std::move (d))
    {
        for (auto& section : spec.sections)
        {
            auto& cellsForSection = cells.emplace_back();
            for (auto& id : section.params)
            {
                auto cell = std::make_unique<ControlCell> (p.apvts, id, spec.bipolar.contains (id));
                addAndMakeVisible (*cell);
                cellsForSection.push_back (std::move (cell));
            }
        }
        if (display != nullptr)
            addAndMakeVisible (*display);

        devilButton.setButtonText ("devil");
        devilButton.setClickingTogglesState (true);
        devilButton.setTooltip ("Overload everything");
        addAndMakeVisible (devilButton);
        devilAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "devil", devilButton);
        devilButton.setBounds (W - margin - 112, 14, 112, 28);

        layout();
    }

    int getDesignWidth() const { return W; }
    int getDesignHeight() const { return H; }

    void refresh (float inPk, float outPk)
    {
        for (auto& sec : cells)
            for (auto& c : sec)
                c->refreshValue();
        if (display != nullptr)
            display->refresh();

        if (proc.devil() != hell)
        {
            hell = proc.devil();
            repaint();
        }

        auto ballistics = [] (float& level, float pk)
        {
            level = juce::jmax (gainToDb (pk, -90.0f), level - 1.5f);
        };
        ballistics (inLevel, inPk);
        ballistics (outLevel, outPk);
        repaint (meterBounds);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (hell ? Colours::hellBg : Colours::bg);

        // Header: pentagram, spaced name, hairline
        g.setColour (Colours::red);
        drawPentagram (g, { (float) margin + 10.0f, 28.0f }, 9.0f, 1.0f);
        g.setColour (hell ? Colours::red : Colours::text);
        g.setFont (font (15.0f));
        g.drawText (spaced (spec.name), margin + 32, 14, 400, 28, juce::Justification::centredLeft);
        g.setColour (Colours::line);
        g.fillRect (margin, headerH, W - 2 * margin, 1);

        // Sections: label + hairline, no boxes
        for (size_t i = 0; i < sectionBounds.size(); ++i)
        {
            const auto r = sectionBounds[i];
            g.setColour (Colours::line);
            g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
            g.setColour (Colours::dim);
            g.setFont (font (10.0f));
            g.drawText (spaced (spec.sections[i].title), r.getX(), r.getY() + 6, r.getWidth(), 14, juce::Justification::centredLeft);
        }

        paintMeters (g);
    }

private:
    void layout()
    {
        const int contentW = W - 2 * margin - meterW - gap;
        int y = headerH + gap;

        if (display != nullptr)
        {
            display->setBounds (margin, y, contentW, spec.displayHeight);
            y += spec.displayHeight + gap;
        }

        int numRows = 0;
        for (auto& s : spec.sections)
            numRows = juce::jmax (numRows, s.row + 1);

        sectionBounds.assign (spec.sections.size(), {});

        for (int row = 0; row < numRows; ++row)
        {
            std::vector<size_t> idx;
            std::vector<int> natural;
            int sumNatural = 0;
            for (size_t i = 0; i < spec.sections.size(); ++i)
            {
                if (spec.sections[i].row != row)
                    continue;
                int w = 0;
                for (auto& c : cells[i])
                    w += c->preferredWidth();
                idx.push_back (i);
                natural.push_back (w);
                sumNatural += w;
            }
            if (idx.empty())
                continue;

            const int sectionGap = 24;
            const int available = contentW - sectionGap * ((int) idx.size() - 1);
            int x = margin;
            for (size_t k = 0; k < idx.size(); ++k)
            {
                const bool last = k + 1 == idx.size();
                const int w = last ? (margin + contentW - x) : natural[k] * available / sumNatural;
                sectionBounds[idx[k]] = { x, y, w, rowH };

                auto& sectionCells = cells[idx[k]];
                const float slot = (float) w / (float) sectionCells.size();
                for (size_t c = 0; c < sectionCells.size(); ++c)
                {
                    const int cw = sectionCells[c]->preferredWidth();
                    sectionCells[c]->setBounds ((int) ((float) x + slot * ((float) c + 0.5f)) - cw / 2, y + 26, cw, cellH);
                }
                x += w + sectionGap;
            }
            y += rowH + gap;
        }

        H = y - gap + margin;
        meterBounds = { W - margin - meterW, headerH + gap, meterW, H - margin - (headerH + gap) };
    }

    void paintMeters (juce::Graphics& g)
    {
        const auto r = meterBounds.toFloat().withTrimmedBottom (16.0f);
        const auto toY = [&r] (float db)
        {
            return r.getBottom() - r.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        };
        auto bar = [&] (float x, float level, const char* text)
        {
            g.setColour (Colours::line);
            g.fillRect (x, r.getY(), 2.0f, r.getHeight());
            g.setColour (level > -0.1f ? Colours::text : Colours::red);
            const float top = toY (level);
            g.fillRect (x, top, 2.0f, r.getBottom() - top);
            g.setColour (Colours::dim);
            g.setFont (font (9.0f));
            g.drawText (text, (int) x - 8, (int) r.getBottom() + 4, 18, 12, juce::Justification::centred);
        };
        g.setColour (Colours::lineHi);
        g.fillRect (r.getX(), toY (0.0f), r.getWidth(), 1.0f);
        bar (r.getX() + 5.0f, inLevel, "I");
        bar (r.getRight() - 7.0f, outLevel, "O");
    }

    VoxProcessorBase& proc;
    EditorSpec spec;
    std::unique_ptr<Display> display;
    std::vector<std::vector<std::unique_ptr<ControlCell>>> cells;
    std::vector<juce::Rectangle<int>> sectionBounds;
    juce::Rectangle<int> meterBounds;
    juce::TextButton devilButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> devilAttachment;
    int H = 400;
    bool hell = false;
    float inLevel = -90.0f, outLevel = -90.0f;
};

//==============================================================================
class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    Editor (VoxProcessorBase& p, EditorSpec s, std::unique_ptr<Display> d = nullptr)
        : AudioProcessorEditor (p), proc (p), body (p, std::move (s), std::move (d))
    {
        setLookAndFeel (&lnf);
        addAndMakeVisible (body);
        const int w = body.getDesignWidth(), h = body.getDesignHeight();
        body.setBounds (0, 0, w, h);
        setResizable (true, true);
        setResizeLimits (w * 6 / 10, h * 6 / 10, w * 2, h * 2);
        if (auto* c = getConstrainer())
            c->setFixedAspectRatio ((double) w / (double) h);
        setSize (w, h);
        startTimerHz (30);
    }

    ~Editor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override { g.fillAll (Colours::bg); }

    void resized() override
    {
        body.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) body.getDesignWidth()));
    }

    void timerCallback() override
    {
        body.refresh (proc.inPeak.exchange (0.0f), proc.outPeak.exchange (0.0f));
    }

private:
    VoxProcessorBase& proc;
    LookAndFeel lnf;
    EditorBody body;
};

} // namespace vox
