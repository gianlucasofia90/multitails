#pragma once
// MIDI map: 64 slots, each from one CC, note, pitch bend or channel pressure to one parameter. The table is made of atomics
// because the audio thread reads it while the interface edits it.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace tails
{
enum MidiSource { msOff, msCC, msNote, msBend, msPressure };
enum MidiMode { mmAbsolute, mmToggle, mmMomentary, mmRelative };
constexpr int kMidiSlots = 64;

struct MidiSlot
{
    std::atomic<int> source { msOff }, number { 1 }, channel { 0 }, param { -1 }, mode { mmAbsolute };   // channel 0 = any
    std::atomic<float> lo { 0.0f }, hi { 1.0f };

    void clear() noexcept
    {
        source = msOff; number = 1; channel = 0; param = -1; mode = mmAbsolute; lo = 0.0f; hi = 1.0f;
    }
};

class MidiMap
{
public:
    std::array<MidiSlot, kMidiSlots> slots;
    std::atomic<int> learnParam { -1 };      // armed: the next message seen is mapped to this parameter
    std::atomic<int> learnedSerial { 0 };    // bumped when a mapping is learned, so the interface refreshes
    std::atomic<int> lastSource { 0 }, lastNumber { 0 }, lastChannel { 0 }, lastSerial { 0 };   // monitor for the MIDI page

    void clearAll() noexcept { for (auto& s : slots) s.clear(); learnedSerial.fetch_add (1); }

    int slotOf (int param) const noexcept
    {
        for (int i = 0; i < kMidiSlots; ++i)
            if (slots[(size_t) i].source.load() != msOff && slots[(size_t) i].param.load() == param) return i;
        return -1;
    }

    void clearParam (int param) noexcept
    {
        for (auto& s : slots) if (s.param.load() == param) s.clear();
        learnedSerial.fetch_add (1);
    }

    int firstFree() const noexcept
    {
        for (int i = 0; i < kMidiSlots; ++i) if (slots[(size_t) i].source.load() == msOff) return i;
        return -1;
    }

    /** Audio thread. apply (paramIndex, 0..1); current (paramIndex) returns 0..1. */
    template <class Apply, class Current>
    void handle (const juce::MidiMessage& m, Apply&& apply, Current&& current) noexcept
    {
        int source = msOff, number = 0;
        float value = 0.0f;
        bool down = false, up = false;
        if (m.isController())              { source = msCC; number = m.getControllerNumber(); value = (float) m.getControllerValue() / 127.0f; down = m.getControllerValue() >= 64; up = ! down; }
        else if (m.isNoteOn())             { source = msNote; number = m.getNoteNumber(); value = (float) m.getVelocity() / 127.0f; down = true; }
        else if (m.isNoteOff())            { source = msNote; number = m.getNoteNumber(); up = true; }
        else if (m.isPitchWheel())         { source = msBend; value = (float) m.getPitchWheelValue() / 16383.0f; down = value >= 0.5f; up = ! down; }
        else if (m.isChannelPressure())    { source = msPressure; value = (float) m.getChannelPressureValue() / 127.0f; down = value >= 0.5f; up = ! down; }
        if (source == msOff) return;
        const int ch = m.getChannel();

        lastSource.store (source, std::memory_order_relaxed);
        lastNumber.store (number, std::memory_order_relaxed);
        lastChannel.store (ch, std::memory_order_relaxed);
        lastSerial.fetch_add (1, std::memory_order_relaxed);

        const int armed = learnParam.load (std::memory_order_relaxed);
        if (armed >= 0 && ! up)
        {
            int slot = slotOf (armed);
            if (slot < 0) slot = firstFree();
            if (slot >= 0)
            {
                auto& s = slots[(size_t) slot];
                s.param = armed; s.number = number; s.channel = ch; s.lo = 0.0f; s.hi = 1.0f;
                s.mode = source == msNote ? mmMomentary : mmAbsolute;
                s.source = source;
            }
            learnParam.store (-1, std::memory_order_relaxed);
            learnedSerial.fetch_add (1, std::memory_order_relaxed);
        }

        for (auto& s : slots)
        {
            if (s.source.load (std::memory_order_relaxed) != source) continue;
            if (source <= msNote && s.number.load (std::memory_order_relaxed) != number) continue;
            const int c = s.channel.load (std::memory_order_relaxed);
            if (c != 0 && c != ch) continue;
            const int p = s.param.load (std::memory_order_relaxed);
            if (p < 0) continue;
            const float lo = s.lo.load (std::memory_order_relaxed), hi = s.hi.load (std::memory_order_relaxed);
            switch (s.mode.load (std::memory_order_relaxed))
            {
                case mmToggle:
                    if (down) apply (p, std::abs (current (p) - lo) < std::abs (current (p) - hi) ? hi : lo);
                    break;
                case mmMomentary:
                    if (down) apply (p, hi); else if (up) apply (p, lo);
                    break;
                case mmRelative:
                    if (source == msCC)
                    {   // 1..63 up, 65..127 down, the usual two's-complement encoder
                        const int raw = m.getControllerValue();
                        const int delta = raw < 64 ? raw : raw - 128;
                        apply (p, juce::jlimit (juce::jmin (lo, hi), juce::jmax (lo, hi), current (p) + (float) delta * 0.005f));
                    }
                    break;
                default:
                    if (source != msNote || down) apply (p, lo + value * (hi - lo));
                    break;
            }
        }
    }
};
} // namespace tails
