#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "TailsEngine.h"
#include "MidiMap.h"
#include "Presets.h"
#include <map>

class TailsProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    TailsProcessor();
    ~TailsProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MULTITAILS"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return (int) tails::factoryPresets().size(); }
    int getCurrentProgram() override { return juce::jmax (0, presets.getCurrent()); }
    void setCurrentProgram (int i) override { if (i >= 0 && i < getNumPrograms()) presets.load (i); }
    const juce::String getProgramName (int i) override
    {
        return i >= 0 && i < getNumPrograms() ? juce::String (tails::factoryPresets()[(size_t) i].name) : juce::String();
    }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** Parameter id of a stream parameter: sid (0, tails::cMix) == "tr_c_mix". */
    static juce::String sid (int stream, int p);
    static juce::String lid (int lane, int p);

    std::unique_ptr<juce::XmlElement> createPresetXml();
    bool applyPresetXml (const juce::XmlElement&);

    float sf (int s, int p) const noexcept { return streamPtr[(size_t) s][(size_t) p]->load (std::memory_order_relaxed); }
    float lf (int l, int p) const noexcept { return lanePtr[(size_t) l][(size_t) p]->load (std::memory_order_relaxed); }
    void setParam (const juce::String& id, float value);
    void setOrder (int stream, const int* order);
    void getOrder (int stream, int* order) const;

    /** Loads a user impulse response into a stream. Message thread. The decoding happens on the worker. */
    void loadUserIr (int stream, const juce::File&);
    juce::String irDisplayName (int stream) const;
    bool irFailed (int stream) const noexcept { return irError[(size_t) stream].load (std::memory_order_relaxed); }
    float irPreview (int stream, int i) const noexcept { return preview[(size_t) stream][(size_t) i].load (std::memory_order_relaxed); }
    float irT60 (int stream) const noexcept { return previewT60[(size_t) stream].load (std::memory_order_relaxed); }
    int irSerial() const noexcept { return previewSerial.load (std::memory_order_relaxed); }
    /** Blocks until both streams have the impulse responses their parameters ask for. Never on the audio thread. */
    void syncImpulseResponses();

    // host parameters by index, for the MIDI map
    int indexOfParam (const juce::String& id) const { return paramIndex.count (id) ? paramIndex.at (id) : -1; }
    juce::String paramName (int index) const;
    juce::String paramId (int index) const;
    void flushMidiMap();
    void applyMidi (const juce::MidiBuffer&) noexcept;

    juce::AudioProcessorValueTreeState state;
    tails::PresetManager presets;
    tails::MidiMap midiMap;
    std::unique_ptr<tails::Engine> engine = std::make_unique<tails::Engine>();

    std::atomic<bool> uiPlaying { false };
    std::atomic<float> uiBpm { 120.0f };
    std::atomic<float> uiScale { 1.0f };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    class Worker;
    struct Decoded { std::vector<float> l, r; double rate = 48000.0; };
    struct Wanted
    {
        int ir = -1; float size = 0.0f; bool reversed = false; juce::String path; double rate = 0.0;
        bool operator== (const Wanted& o) const { return ir == o.ir && std::abs (size - o.size) < 0.001f && reversed == o.reversed && path == o.path && rate == o.rate; }
    };

    void timerCallback() override;
    void pullParams() noexcept;
    void runBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&, bool bypassed);
    void render (float* l, float* r, int n, const tails::Transport&) noexcept;
    tails::Transport readTransport (int numSamples, bool bypassed);
    juce::ValueTree patternsToTree() const;
    void patternsFromTree (const juce::ValueTree&);
    juce::ValueTree midiToTree() const;
    void midiFromTree (const juce::ValueTree&);
    Wanted wanted (int stream) const;
    bool refreshIr (int stream, bool immediately);
    const Decoded* decode (const Wanted&);
    juce::String userPathOf (int stream) const;

    std::array<std::array<std::atomic<float>*, tails::kNumP>, tails::kStreams> streamPtr {};
    std::array<std::array<std::atomic<float>*, tails::kNumLaneP>, tails::kLanes> lanePtr {};
    std::atomic<float>* gSync {}, *gBpm {}, *gRun {}, *gSplit {}, *gFocus {}, *gSmooth {}, *gDecay {}, *gSource {}, *gIn {}, *gOut {}, *gMix {}, *gReset {};

    // MIDI map targets
    std::vector<juce::RangedAudioParameter*> ranged;
    std::vector<std::atomic<float>*> rawValue;
    std::unique_ptr<std::atomic<float>[]> midiPending;
    std::map<juce::String, int> paramIndex;

    // impulse responses
    juce::AudioFormatManager formats;
    juce::CriticalSection irLock;                      // never taken on the audio thread
    std::map<juce::String, Decoded> decoded;
    std::array<Wanted, tails::kStreams> built;
    std::array<juce::String, tails::kStreams> userPath;
    mutable juce::SpinLock pathLock;
    std::array<std::atomic<bool>, tails::kStreams> irError {};
    std::array<std::array<std::atomic<float>, tails::kPreviewPoints>, tails::kStreams> preview {};
    std::array<std::atomic<float>, tails::kStreams> previewT60 {};
    std::atomic<int> previewSerial { 0 };
    std::unique_ptr<Worker> worker;

    double sr = 48000.0, internalPpq = 0.0;
    bool internalRunning = false, resetHeld = false, wasPlaying = false;
    std::atomic<bool> prepared { false };
    std::vector<float> scratch;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TailsProcessor)
};
