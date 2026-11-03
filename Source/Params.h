#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "TailsCore.h"

namespace tails
{
inline std::unique_ptr<juce::AudioParameterFloat> fparam (const juce::String& id, const juce::String& name, float lo, float hi,
                                                          float def, const juce::String& unit = {}, float skewCentre = -1.0f)
{
    juce::NormalisableRange<float> r (lo, hi);
    if (skewCentre > lo && skewCentre < hi) r.setSkewForCentre (skewCentre);
    const int dec = (hi - lo) >= 90.0f ? 0 : 1;
    auto toText = [dec, unit] (float v, int) {
        const auto number = dec > 0 ? juce::String (v, dec) : juce::String (juce::roundToInt (v));
        return number + (unit.isNotEmpty() ? " " + unit : juce::String());
    };
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                        juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (toText));
}

inline std::unique_ptr<juce::AudioParameterInt> iparam (const juce::String& id, const juce::String& name, int lo, int hi, int def)
{
    return std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, lo, hi, def);
}

// On/Off switch as a 2-item choice: unlike AudioParameterBool it always stores a snapped value,
// so state save/restore round-trips exactly (pluginval strictness 8+).
inline std::unique_ptr<juce::AudioParameterChoice> bparam (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, juce::StringArray { "Off", "On" }, def ? 1 : 0);
}

inline std::unique_ptr<juce::AudioParameterChoice> cparam (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def);
}

inline juce::StringArray itemsOf (const char* const* items, int n) { juce::StringArray a; for (int i = 0; i < n; ++i) a.add (items[i]); return a; }
} // namespace tails
