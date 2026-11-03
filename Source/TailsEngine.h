#pragma once
// MULTITAILS engine: split, two chains of six effects in any order, sequencer. No JUCE, no allocation after prepare(), no
// locks. All control runs on a grid of kTick samples counted from prepare(), and the sequencer reads musical time, so the
// output does not depend on the host's buffer size.
#include "TailsCore.h"
#include "TailsConv.h"
#include "TailsFx.h"
#include "TailsSplit.h"
#include <memory>

namespace tails
{
struct Transport { double ppq = 0.0, bpm = 120.0; bool playing = false; };

struct GlobalParams
{
    bool split = true, bypass = false;
    float focus = 0, smooth = 30, decay = 40, input = 0, output = 0, mix = 100;
    int source = 0;
};

struct LaneParams { float v[kNumLaneP] {}; };

struct Pattern
{
    std::array<std::atomic<float>, kSteps> step;     // -1..1
    Pattern() { for (auto& s : step) s.store (0.0f, std::memory_order_relaxed); }
};

/** What the interface reads. Written by the audio thread with relaxed atomics. */
struct UiState
{
    static constexpr int kWave = 342;
    std::array<std::atomic<int>, kLanes> pos {};
    std::array<std::atomic<float>, kLanes> value {};
    std::array<std::array<std::atomic<float>, kNumP>, kStreams> norm {};  // sequenced position of a knob, < 0 = not sequenced
    std::array<std::atomic<float>, kNumGlobalDest> gnorm {};
    std::array<std::atomic<float>, kWave> in {}, tr {}, to {};
    std::atomic<int> waveHead { 0 };
    std::array<std::atomic<float>, kStreams> hornHz {}, hornAngle {}, drumAngle {};
    std::array<std::array<std::atomic<float>, kNumFx>, kStreams> fxLevel {};
    std::atomic<float> outPeak { 0.0f }, inPeak { 0.0f };
};

class Stream
{
public:
    ConvFx conv; TapeFx tape; DelayFx delay; ChorusFx chorus; RotaryFx rotary; FilterFx filter;
    float cur[kNumP] {};

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        conv.prepare (sr); tape.prepare (sr); delay.prepare (sr); chorus.prepare (sr); rotary.prepare (sr); filter.prepare (sr);
        reset();
    }

    void reset() noexcept
    {
        conv.reset(); tape.reset(); delay.reset(); chorus.reset(); rotary.reset(); filter.reset();
        for (auto& s : slots) s = {};
        duck = 1.0f; started = false;
    }

    /** Called once per control tick. */
    void update (double bpm) noexcept
    {
        const int rank = (int) cur[sOrder];
        if (! started) { orderFromRank (rank, order); shownRank = rank; started = true; }
        wantRank = rank;
        const bool streamOn = cur[sOn] > 0.5f;
        tailsOn = cur[sTails] > 0.5f;
        static constexpr int onP[kNumFx] = { cOn, tOn, dOn, hOn, rOn, fOn };
        static constexpr int mixP[kNumFx] = { cMix, tMix, dMix, hMix, rMix, fMix };
        for (int f = 0; f < kNumFx; ++f)
        {
            auto& s = slots[f];
            s.onTarget = streamOn && cur[onP[f]] > 0.5f ? 1.0f : 0.0f;
            s.mixTarget = cur[mixP[f]] * 0.01f;
            if (! s.asleep || s.onTarget > 0.0f)
                switch (f)
                {
                    case fxConv:   conv.update (cur, bpm); break;
                    case fxTape:   tape.update (cur, bpm); break;
                    case fxDelay:  delay.update (cur, bpm); break;
                    case fxChorus: chorus.update (cur, bpm); break;
                    case fxRotary: rotary.update (cur, bpm); break;
                    default:       filter.update (cur, bpm); break;
                }
        }
    }

    void process (float* l, float* r, int n, std::array<std::atomic<float>, kNumFx>& levels) noexcept
    {
        // a new order: dip this stream for a few milliseconds and swap at the bottom
        const float duckStep = (float) (1.0 / (0.004 * sr));
        for (int i = 0; i < kNumFx; ++i)
        {
            const int f = order[i];
            auto& s = slots[f];
            if (s.asleep && s.onTarget == 0.0f) continue;
            s.asleep = false;

            const float mixStep = s.mixInit ? (s.mixTarget - s.mix) / (float) n : 0.0f;
            if (! s.mixInit) { s.mix = s.mixTarget; s.mixInit = true; }
            float send = s.send, mix = s.mix, peak = 0.0f;
            const float slew = 1.0f / (0.003f * (float) sr / (float) n);  // 3 ms switch at the send
            const float sendEnd = s.send + std::clamp (s.onTarget - s.send, -slew, slew);
            const float realStep = (sendEnd - s.send) / (float) n;

            for (int k = 0; k < n; ++k)
            {
                send += realStep;
                wl[(size_t) k] = l[k] * send;
                wr[(size_t) k] = r[k] * send;
            }
            run (f, s.send, n);

            send = s.send;
            for (int k = 0; k < n; ++k)
            {
                send += realStep; mix += mixStep;
                const float a = wl[(size_t) k], b = wr[(size_t) k];
                peak = std::max ({ peak, std::abs (a), std::abs (b) });
                const float wetG = tailsOn ? mix : mix * send;
                l[k] = l[k] * (1.0f - mix * send) + a * wetG;
                r[k] = r[k] * (1.0f - mix * send) + b * wetG;
            }
            s.send = sendEnd;
            s.mix = s.mixTarget;
            levels[(size_t) f].store (peak * s.mix, std::memory_order_relaxed);

            if (s.send <= 0.0f && peak < 3.0e-6f)
            {
                s.quiet += n;
                if (s.quiet > (int) sr) { s.asleep = true; levels[(size_t) f].store (0.0f, std::memory_order_relaxed); }
            }
            else s.quiet = 0;
        }

        if (wantRank != shownRank || duck < 1.0f)
            for (int k = 0; k < n; ++k)
            {
                if (wantRank != shownRank)
                {
                    duck -= duckStep;
                    if (duck <= 0.0f) { duck = 0.0f; orderFromRank (wantRank, order); shownRank = wantRank; }
                }
                else duck = std::min (1.0f, duck + duckStep);
                l[k] *= duck; r[k] *= duck;
            }
    }

    const int* currentOrder() const noexcept { return order; }

private:
    struct Slot
    {
        float send = 0, mix = 0, onTarget = 0, mixTarget = 0;
        int quiet = 0;
        bool asleep = true, mixInit = false;
    };

    void run (int f, float gate, int n) noexcept
    {
        switch (f)
        {
            case fxConv:   conv.process (wl.data(), wr.data(), n); break;
            case fxTape:   tape.gate = gate; tape.process (wl.data(), wr.data(), n); break;
            case fxDelay:  delay.process (wl.data(), wr.data(), n); break;
            case fxChorus: chorus.gate = gate; chorus.process (wl.data(), wr.data(), n); break;
            case fxRotary: rotary.process (wl.data(), wr.data(), n); break;
            default:       filter.process (wl.data(), wr.data(), n); break;
        }
    }

    double sr = 48000.0;
    Slot slots[kNumFx];
    int order[kNumFx] = { 0, 1, 2, 3, 4, 5 };
    int wantRank = 0, shownRank = 0;
    float duck = 1.0f;
    bool tailsOn = true, started = false;
    std::array<float, kTick> wl {}, wr {};
};

class Engine
{
public:
    float base[kStreams][kNumP] {};
    GlobalParams global;
    LaneParams lanes[kLanes];
    std::array<Pattern, kLanes> patterns;
    UiState ui;
    std::array<std::unique_ptr<Stream>, kStreams> streams;

    Engine()
    {
        streams[0] = std::make_unique<Stream>();
        streams[1] = std::make_unique<Stream>();
        for (int p = 0; p < kNumP; ++p) skew[p] = skewOf (streamParams()[p]);
        for (auto& st : ui.norm) for (auto& v : st) v.store (-1.0f, std::memory_order_relaxed);
        for (auto& v : ui.gnorm) v.store (-1.0f, std::memory_order_relaxed);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        split.prepare (sr);
        for (auto& s : streams) s->prepare (sr);
        reset();
    }

    int latency() const noexcept { return split.latency(); }

    void reset() noexcept
    {
        split.reset();
        for (auto& s : streams) s->reset();
        for (auto& l : laneState) l = {};
        tickLeft = 0; gMix = -1.0f; gIn = gOut = -1.0f; gTr = gTo = -1.0f; origin = 0.0; waveCount = 0; peakIn = peakT = peakS = 0.0f;
    }

    /** Sequencer back to step 1 at this musical position. */
    void seqReset (double ppq) noexcept { origin = ppq; for (auto& l : laneState) { l.step = -1; l.brown = 0; } }

    void process (float* l, float* r, int n, const Transport& t) noexcept
    {
        const double beatsPerSample = t.bpm / (60.0 * sr);
        float outPeak = 0.0f, inPeak = 0.0f, check = 0.0f;
        int done = 0;
        while (done < n)
        {
            if (tickLeft == 0) { controlTick (t, t.ppq + done * beatsPerSample); tickLeft = kTick; }
            const int m = std::min (tickLeft, n - done);
            float* ol = l + done;  float* or_ = r + done;

            const float gainIn = dbGain (global.input);
            for (int i = 0; i < m; ++i)
            {
                // a NaN or an absurd value at the input would stay in every feedback path
                if (! (std::abs (ol[i]) < 64.0f)) ol[i] = 0.0f;
                if (! (std::abs (or_[i]) < 64.0f)) or_[i] = 0.0f;
                ol[i] *= gainIn; or_[i] *= gainIn;
                inPeak = std::max ({ inPeak, std::abs (ol[i]), std::abs (or_[i]) });
            }

            Splitter::Settings s;
            s.on = global.split; s.focus = focusNow; s.smooth = global.smooth * 0.01f; s.decayMs = global.decay; s.source = global.source;
            split.process (ol, or_, dl.data(), dr.data(), xl[0].data(), xr[0].data(), xl[1].data(), xr[1].data(), m, s);
            meter (m);

            for (int c = 0; c < kStreams; ++c)
                streams[(size_t) c]->process (xl[(size_t) c].data(), xr[(size_t) c].data(), m, ui.fxLevel[(size_t) c]);

            const bool anySolo = streams[0]->cur[sSolo] > 0.5f || streams[1]->cur[sSolo] > 0.5f;
            float tg[2];
            for (int c = 0; c < kStreams; ++c)
            {
                const float db = streams[(size_t) c]->cur[sGain];
                const bool audible = ! anySolo || streams[(size_t) c]->cur[sSolo] > 0.5f;
                tg[c] = audible && db > -59.9f ? dbGain (db) : 0.0f;
            }
            const float mixT = global.bypass ? 0.0f : mixNow * 0.01f, outG = dbGain (global.output);
            if (gMix < 0.0f) { gMix = mixT; gTr = tg[0]; gTo = tg[1]; gOut = outG; }
            const float a = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
            for (int i = 0; i < m; ++i)
            {
                gMix += a * (mixT - gMix); gTr += a * (tg[0] - gTr); gTo += a * (tg[1] - gTo); gOut += a * (outG - gOut);
                const float wl = gTr * xl[0][(size_t) i] + gTo * xl[1][(size_t) i];
                const float wr = gTr * xr[0][(size_t) i] + gTo * xr[1][(size_t) i];
                const float out0 = gOut * (dl[(size_t) i] + gMix * (wl - dl[(size_t) i]));
                const float out1 = gOut * (dr[(size_t) i] + gMix * (wr - dr[(size_t) i]));
                ol[i] = out0; or_[i] = out1;
                outPeak = std::max ({ outPeak, std::abs (out0), std::abs (out1) });
                check += out0 + out1;
            }
            done += m;
            tickLeft -= m;
        }
        if (! std::isfinite (check) || outPeak > 1024.0f)
        {   // never let one bad value ring on through the feedback paths
            for (auto& s : streams) s->reset();
            split.reset();
            for (int i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
        }
        ui.outPeak.store (outPeak, std::memory_order_relaxed);
        ui.inPeak.store (inPeak, std::memory_order_relaxed);
        for (int c = 0; c < kStreams; ++c)
        {
            auto& rot = streams[(size_t) c]->rotary;
            ui.hornHz[(size_t) c].store (rot.hornSpeed(), std::memory_order_relaxed);
            ui.hornAngle[(size_t) c].store (rot.hornAngle(), std::memory_order_relaxed);
            ui.drumAngle[(size_t) c].store (rot.drumAngle(), std::memory_order_relaxed);
        }
    }

private:
    struct LaneState { long long step = -1; float target = 0, value = 0, gateValue = 0; int brown = 0, index = 0; };

    static float dbGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    static uint32_t hash (uint32_t a, uint32_t b) noexcept
    {
        uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
        h ^= h >> 15; h *= 0xC2B2AE3Du; h ^= h >> 13;
        return h;
    }

    void meter (int m) noexcept
    {
        for (int i = 0; i < m; ++i)
        {
            peakIn = std::max (peakIn, std::abs (dl[(size_t) i]) + std::abs (dr[(size_t) i]));
            peakT = std::max (peakT, std::abs (xl[0][(size_t) i]) + std::abs (xr[0][(size_t) i]));
            peakS = std::max (peakS, std::abs (xl[1][(size_t) i]) + std::abs (xr[1][(size_t) i]));
        }
        waveCount += m;
        if (waveCount >= 256)
        {
            waveCount = 0;
            const int h = (ui.waveHead.load (std::memory_order_relaxed) + 1) % UiState::kWave;
            ui.in[(size_t) h].store (peakIn * 0.5f, std::memory_order_relaxed);
            ui.tr[(size_t) h].store (peakT * 0.5f, std::memory_order_relaxed);
            ui.to[(size_t) h].store (peakS * 0.5f, std::memory_order_relaxed);
            ui.waveHead.store (h, std::memory_order_relaxed);
            peakIn = peakT = peakS = 0.0f;
        }
    }

    /** One step of a lane's pattern for the given step number. */
    int indexFor (long long n, int len, int dir, int lane) noexcept
    {
        switch (dir)
        {
            case 1:  return len - 1 - (int) (n % len);
            case 2:
            {
                if (len < 2) return 0;
                const int period = 2 * (len - 1), i = (int) (n % period);
                return i < len ? i : period - i;
            }
            case 3:  return (int) (hash ((uint32_t) lane + 11u, (uint32_t) n) % (uint32_t) len);
            case 4:
            {
                auto& st = laneState[lane];
                const int d = (int) (hash ((uint32_t) lane + 23u, (uint32_t) n) % 3u) - 1;
                st.brown = ((st.brown + d) % len + len) % len;
                return st.brown;
            }
            default: return (int) (n % len);
        }
    }

    void controlTick (const Transport& t, double ppq) noexcept
    {
        for (int c = 0; c < kStreams; ++c)
            std::copy (base[c], base[c] + kNumP, streams[(size_t) c]->cur);
        focusNow = global.focus * 0.01f;
        mixNow = global.mix;
        if (ppq < origin) origin = 0.0;     // the host jumped back past a reset: return to the bar grid

        float mods[kStreams][kNumP] {};   // summed offsets, in knob travel
        bool touched[kStreams][kNumP] {};
        float gmods[kNumGlobalDest] {};
        bool gtouched[kNumGlobalDest] {};
        const auto& dests = DestTable::get();
        const float tickSec = (float) (kTick / sr);

        for (int l = 0; l < kLanes; ++l)
        {
            const auto& p = lanes[l].v;
            auto& st = laneState[l];
            const int dest = (int) p[lDest];
            if (p[lOn] < 0.5f || dest <= 0) continue;

            const int len = std::clamp ((int) p[lLen], 1, kSteps);
            const double stepBeats = kDivisions[std::clamp ((int) p[lRate], 0, kNumDivisions - 1)].beats;
            if (t.playing)
            {
                // swing lengthens the even step of each pair
                const double x = std::max (0.0, ppq - origin) / stepBeats + 1.0e-7;   // hosts round: a tick on a step boundary must not fall on either side
                const long long pair = (long long) (x * 0.5);
                const double inPair = x - 2.0 * (double) pair;
                const double sw = p[lSwing] * 0.005;
                const long long n = 2 * pair + (inPair < 1.0 + sw ? 0 : 1);
                if (n != st.step)
                {
                    st.step = n;
                    st.index = indexFor (n, len, (int) p[lDir], l);
                    const float roll = (float) (hash ((uint32_t) l + 41u, (uint32_t) n) & 0xffffu) / 65535.0f;
                    if (roll <= p[lChance] * 0.01f)
                    {
                        st.target = patterns[(size_t) l].step[(size_t) st.index].load (std::memory_order_relaxed);
                        ui.pos[(size_t) l].store (st.index, std::memory_order_relaxed);
                    }
                }
            }
            const float sec = (float) (stepBeats * 60.0 / t.bpm);
            const float slewTime = std::max (0.002f, p[lSlew] * 0.01f * sec * 0.5f);
            st.value += (1.0f - std::exp (-tickSec / slewTime)) * (st.target - st.value);
            ui.value[(size_t) l].store (st.value, std::memory_order_relaxed);

            const float amt = p[lAmt] * 0.01f;
            if (dest < kNumGlobalDest) { gmods[dest] += amt * st.value; gtouched[dest] = true; continue; }

            const int param = dests.param[(size_t) (dest - kNumGlobalDest)];
            const auto& def = streamParams()[param];
            const int target = (int) p[lTarget];
            for (int c = 0; c < kStreams; ++c)
            {
                if (target != 2 && target != c) continue;
                if (def.type == T::Bool) streams[(size_t) c]->cur[param] = st.target * amt > 0.0f ? 1.0f : 0.0f;
                else if (def.type == T::Choice)
                    streams[(size_t) c]->cur[param] = (float) std::lround ((st.target * amt * 0.5f + 0.5f) * (float) (def.numItems - 1));
                else mods[c][param] += amt * st.value;
                touched[c][param] = true;
            }
        }

        for (int c = 0; c < kStreams; ++c)
        {
            auto* cur = streams[(size_t) c]->cur;
            for (int p = 0; p < kNumP; ++p)
            {
                if (touched[c][p] != uiTouched[c][p]) { uiTouched[c][p] = touched[c][p]; if (! touched[c][p]) ui.norm[(size_t) c][(size_t) p].store (-1.0f, std::memory_order_relaxed); }
                if (! touched[c][p]) continue;
                const auto& def = streamParams()[p];
                if (def.type == T::Float)
                {
                    const float nv = std::clamp (toNorm (def, skew[p], cur[p]) + mods[c][p], 0.0f, 1.0f);
                    cur[p] = fromNorm (def, skew[p], nv);
                    ui.norm[(size_t) c][(size_t) p].store (nv, std::memory_order_relaxed);
                }
                else ui.norm[(size_t) c][(size_t) p].store (cur[p] / std::max (1.0f, def.hi), std::memory_order_relaxed);
            }
        }
        // the two global gains go through the same curve as the stream Gain knob
        const auto& gainDef = streamParams()[sGain];
        for (int c = 0; c < kStreams; ++c)
        {
            const int d = c == 0 ? gdTransGain : gdTonalGain;
            if (gtouched[d])
            {
                auto* cur = streams[(size_t) c]->cur;
                const float nv = std::clamp (toNorm (gainDef, skew[sGain], cur[sGain]) + gmods[d], 0.0f, 1.0f);
                cur[sGain] = fromNorm (gainDef, skew[sGain], nv);
                ui.norm[(size_t) c][sGain].store (nv, std::memory_order_relaxed);
            }
            else if (uiTouched[c][sGain] != gtouched[d]) ui.norm[(size_t) c][sGain].store (-1.0f, std::memory_order_relaxed);
            uiTouched[c][sGain] = gtouched[d];
        }
        if (gtouched[gdFocus]) focusNow = std::clamp (focusNow + gmods[gdFocus] * 2.0f, -1.0f, 1.0f);
        if (gtouched[gdMix]) mixNow = std::clamp (mixNow + gmods[gdMix] * 100.0f, 0.0f, 100.0f);
        ui.gnorm[gdFocus].store (gtouched[gdFocus] ? 0.5f * (focusNow + 1.0f) : -1.0f, std::memory_order_relaxed);
        ui.gnorm[gdMix].store (gtouched[gdMix] ? mixNow * 0.01f : -1.0f, std::memory_order_relaxed);

        for (auto& s : streams) s->update (t.bpm);
    }

    double sr = 48000.0, origin = 0.0;
    Splitter split;
    float skew[kNumP] {};
    LaneState laneState[kLanes];
    bool uiTouched[kStreams][kNumP] {};
    std::array<float, kTick> dl {}, dr {};
    std::array<std::array<float, kTick>, kStreams> xl {}, xr {};
    float gMix = -1, gIn = -1, gOut = -1, gTr = -1, gTo = -1, focusNow = 0, mixNow = 100, peakIn = 0, peakT = 0, peakS = 0;
    int tickLeft = 0, waveCount = 0;
};
} // namespace tails
