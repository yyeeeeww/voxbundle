#pragma once
// Parameter factory helpers with consistent, readable value formatting.

#include <juce_audio_processors/juce_audio_processors.h>

namespace vox
{
enum class Unit { Plain, dB, Hz, ms, s, Percent, Ratio, Semitones, Cents };

inline juce::String formatValue (float v, Unit u)
{
    switch (u)
    {
        case Unit::dB:        return juce::String (v, 1) + " dB";
        case Unit::Hz:        return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " Hz"
                                                 : juce::String (v / 1000.0f, v < 10000.0f ? 2 : 1) + " kHz";
        case Unit::ms:        return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms";
        case Unit::s:         return juce::String (v, 2) + " s";
        case Unit::Percent:   return juce::String (juce::roundToInt (v)) + " %";
        case Unit::Ratio:     return juce::String (v, 1) + ":1";
        case Unit::Semitones: return juce::String (v > 0.0f ? "+" : "") + juce::String (v, 1) + " st";
        case Unit::Cents:     return juce::String (juce::roundToInt (v)) + " ct";
        case Unit::Plain:     break;
    }
    return juce::String (v, 2);
}

inline float parseValue (const juce::String& text, Unit u)
{
    auto v = text.retainCharacters ("0123456789.-+").getFloatValue();
    if (u == Unit::Hz && text.containsIgnoreCase ("k"))
        v *= 1000.0f;
    return v;
}

using StringFn = std::function<juce::String (float)>;

inline std::unique_ptr<juce::RangedAudioParameter> makeFloat (const juce::String& id, const juce::String& name,
                                                              float min, float max, float def, Unit unit,
                                                              float skewCentre = 0.0f, StringFn customText = {})
{
    juce::NormalisableRange<float> range (min, max);
    if (skewCentre > min && skewCentre < max)
        range.setSkewForCentre (skewCentre);

    auto toText = customText ? customText : StringFn ([unit] (float v) { return formatValue (v, unit); });

    auto attrs = juce::AudioParameterFloatAttributes()
                     .withStringFromValueFunction ([toText] (float v, int) { return toText (v); })
                     .withValueFromStringFunction ([unit] (const juce::String& t) { return parseValue (t, unit); });

    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, def, attrs);
}

inline std::unique_ptr<juce::RangedAudioParameter> makeBool (const juce::String& id, const juce::String& name, bool def)
{
    auto attrs = juce::AudioParameterBoolAttributes()
                     .withStringFromValueFunction ([] (bool v, int) { return juce::String (v ? "On" : "Off"); });
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def, attrs);
}

inline std::unique_ptr<juce::RangedAudioParameter> makeChoice (const juce::String& id, const juce::String& name,
                                                               const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def);
}

inline std::unique_ptr<juce::RangedAudioParameter> makeInt (const juce::String& id, const juce::String& name,
                                                            int min, int max, int def,
                                                            std::function<juce::String (int)> toText)
{
    auto attrs = juce::AudioParameterIntAttributes()
                     .withStringFromValueFunction ([toText] (int v, int) { return toText (v); });
    return std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, min, max, def, attrs);
}

} // namespace vox
