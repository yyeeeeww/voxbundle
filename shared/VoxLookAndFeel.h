#pragma once
// Minimal black/red look shared by the whole bundle.

#include <juce_gui_basics/juce_gui_basics.h>

namespace vox
{
namespace Colours
{
    inline const juce::Colour bg       { 0xff050505 };
    inline const juce::Colour hellBg   { 0xff120203 };   // background while DEVIL is engaged
    inline const juce::Colour line     { 0xff1c1c1c };
    inline const juce::Colour lineHi   { 0xff333333 };
    inline const juce::Colour text     { 0xffd6d6d6 };
    inline const juce::Colour dim      { 0xff5e5e5e };
    inline const juce::Colour red      { 0xffd4001a };
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

// Letter-spaced uppercase, e.g. "VOXCOMP" -> "V O X C O M P".
inline juce::String spaced (const juce::String& s)
{
    juce::String out;
    for (auto c : s.toUpperCase())
    {
        if (out.isNotEmpty()) out << ' ';
        out << juce::String::charToString (c);
    }
    return out;
}

// Thin inverted pentagram.
inline void drawPentagram (juce::Graphics& g, juce::Point<float> c, float r, float thickness)
{
    juce::Path p;
    for (int i = 0; i <= 5; ++i)
    {
        const float a = juce::MathConstants<float>::pi + (float) ((i * 2) % 5) * juce::MathConstants<float>::twoPi / 5.0f;
        const auto pt = c.getPointOnCircumference (r, a);
        i == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
    }
    g.strokePath (p, juce::PathStrokeType (thickness));
    g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c).expanded (thickness), thickness);
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
        setColour (juce::Label::textColourId, Colours::text);
        setColour (juce::ComboBox::textColourId, Colours::text);
        setColour (juce::PopupMenu::backgroundColourId, Colours::bg);
        setColour (juce::PopupMenu::textColourId, Colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::red);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (8.0f);
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = a0 + pos * (a1 - a0);
        const bool hot = s.isMouseOverOrDragging();

        g.setColour (hot ? Colours::lineHi.brighter (0.2f) : Colours::lineHi);
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.0f);

        // end stops
        g.setColour (Colours::dim);
        for (float a : { a0, a1 })
            g.drawLine ({ c.getPointOnCircumference (r + 3.0f, a), c.getPointOnCircumference (r + 6.0f, a) }, 1.0f);

        // value arc, hairline
        juce::Path arc;
        const bool bipolar = (bool) s.getProperties()["bipolar"];
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;
        arc.addCentredArc (c.x, c.y, r + 4.5f, r + 4.5f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (Colours::red);
        g.strokePath (arc, juce::PathStrokeType (1.0f));

        g.setColour (hot ? Colours::text : Colours::red);
        g.drawLine ({ c.getPointOnCircumference (r * 0.25f, angle), c.getPointOnCircumference (r, angle) }, 1.5f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool) override
    {
        const auto sq = juce::Rectangle<float> (12.0f, 12.0f).withCentre (b.getLocalBounds().toFloat().getCentre());
        g.setColour (b.getToggleState() ? Colours::red : Colours::dim);
        g.drawRect (sq, 1.0f);
        if (b.getToggleState())
            g.fillRect (sq.reduced (3.0f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        if (b.getToggleState())
        {
            g.setColour (Colours::red);
            g.fillRect (r);
        }
        else
        {
            g.setColour (over ? Colours::red : Colours::red.withAlpha (0.6f));
            g.drawRect (r, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setColour (b.getToggleState() ? juce::Colours::black : Colours::red);
        g.setFont (font (12.0f, true));
        g.drawText (spaced (b.getButtonText()), b.getLocalBounds(), juce::Justification::centred);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        g.setColour (box.isMouseOver (true) ? Colours::red : Colours::lineHi);
        g.fillRect (0.0f, (float) h - 1.0f, (float) w, 1.0f);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (0, 0, box.getWidth(), box.getHeight());
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }

    void drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool) override
    {
        g.setColour (over ? Colours::red : Colours::lineHi);
        for (float k : { 0.35f, 0.7f })
            g.drawLine ((float) w * k, (float) h, (float) w, (float) h * k, 1.0f);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (Colours::bg);
        g.setColour (Colours::lineHi);
        g.drawRect (0, 0, w, h, 1);
    }
};

} // namespace vox
