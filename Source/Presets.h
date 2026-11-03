#pragma once
// Factory presets. They take their cue from Amber, Incunabula and Selected Ambient Works 85-92: short hits thrown into long
// grainy digital tails, tape and chorus on the sustained part, sequenced sends instead of static mixes. Starting points, not reconstructions.
#include <juce_audio_processors/juce_audio_processors.h>
#include "TailsCore.h"

class TailsProcessor;

namespace tails
{
/** Handle used by the preset recipes. */
struct PresetEdit
{
    TailsProcessor& p;
    void g (const char* id, float value);                 // global
    void tr (const char* id, float value);                // transient stream
    void to (const char* id, float value);                // tonal stream
    void chain (int stream, std::initializer_list<int> order);
    /** target: 0 transient, 1 tonal, 2 both. dest: the name shown in the sequencer menu. */
    void lane (int oneBased, const char* dest, int target, int rate, float amount, std::initializer_list<float> steps,
               float slew = 0.0f, int dir = 0);
};

struct FactoryPreset { const char* name; void (*build) (PresetEdit&); };
const std::vector<FactoryPreset>& factoryPresets();

/** Factory + user presets (XML files in ~/Music/Multitails Presets). */
class PresetManager
{
public:
    explicit PresetManager (TailsProcessor& p) : proc (p) { refresh(); }

    static juce::File userDir();
    void refresh();
    const juce::StringArray& getNames() const { return names; }
    int getCurrent() const { return current; }
    void markCurrent (int index) { current = juce::jlimit (0, juce::jmax (0, names.size() - 1), index); }
    int numFactory() const { return (int) factoryPresets().size(); }
    void load (int index);
    bool save (const juce::String& name);
    bool removeCurrent();
    void next() { load ((current + 1) % juce::jmax (1, names.size())); }
    void prev() { load ((current - 1 + names.size()) % juce::jmax (1, names.size())); }
    void clearToDefaults();

private:
    TailsProcessor& proc;
    juce::StringArray names;
    juce::Array<juce::File> userFiles;
    int current = 0;
};
} // namespace tails
