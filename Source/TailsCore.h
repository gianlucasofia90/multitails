#pragma once
// MULTITAILS tables: effects, parameters, tempo divisions. No JUCE here: the DSP and the plugin share this file.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace tails
{
constexpr float kPi = 3.14159265358979f, kTwoPi = 6.28318530717959f;
constexpr int kStreams = 2, kTick = 32;                  // control rate: everything modulated moves every 32 samples
constexpr int kLanes = 8, kSteps = 32;

enum Fx { fxConv, fxTape, fxDelay, fxChorus, fxRotary, fxFilter, kNumFx, fxStream = kNumFx };
inline constexpr const char* kFxNames[kNumFx] = { "Convolution", "Tape Echo", "Delay", "Chorus", "Rotary", "Filter" };
inline constexpr const char* kFxShort[kNumFx] = { "CONV", "TAPE", "DELAY", "CHORUS", "ROTARY", "FILTER" };
inline constexpr const char* kStreamNames[kStreams] = { "Transient", "Tonal" };

// the order of every list is saved in sessions: append only
inline constexpr const char* kIrNames[] = {
    "Plate Short", "Plate Medium", "Plate Long", "Plate Bright",
    "Digital Hall", "Digital Room", "Digital Long", "Digital Gate", "Digital Reverse",
    "Tile Room", "Living Room", "Studio", "Small Hall", "Large Hall", "Huge Hall", "Church", "Cathedral",
    "User IR" };
constexpr int kNumIrs = (int) (sizeof (kIrNames) / sizeof (kIrNames[0])), kUserIr = kNumIrs - 1;

struct Division { const char* name; double beats; };
inline constexpr Division kDivisions[] = {
    { "4 bar", 16.0 }, { "2 bar", 8.0 }, { "1 bar", 4.0 }, { "1/2", 2.0 }, { "1/2 D", 3.0 }, { "1/2 T", 4.0 / 3.0 },
    { "1/4", 1.0 }, { "1/4 D", 1.5 }, { "1/4 T", 2.0 / 3.0 }, { "1/8", 0.5 }, { "1/8 D", 0.75 }, { "1/8 T", 1.0 / 3.0 },
    { "1/16", 0.25 }, { "1/16 D", 0.375 }, { "1/16 T", 1.0 / 6.0 }, { "1/32", 0.125 }, { "1/32 T", 1.0 / 12.0 }, { "1/64", 0.0625 },
    { "5:1", 0.2 }, { "7:1", 1.0 / 7.0 }, { "9:1", 1.0 / 9.0 }, { "11:1", 1.0 / 11.0 }, { "13:1", 1.0 / 13.0 } };
constexpr int kNumDivisions = (int) (sizeof (kDivisions) / sizeof (kDivisions[0]));
constexpr int kDiv14 = 6, kDiv18D = 10, kDiv116 = 12;
inline constexpr const char* kDivisionNames[kNumDivisions] = {
    "4 bar", "2 bar", "1 bar", "1/2", "1/2 D", "1/2 T", "1/4", "1/4 D", "1/4 T", "1/8", "1/8 D", "1/8 T",
    "1/16", "1/16 D", "1/16 T", "1/32", "1/32 T", "1/64", "5:1", "7:1", "9:1", "11:1", "13:1" };

inline constexpr const char* kItemsTapeMode[] = { "Echo", "Repeat", "Swell" };
inline constexpr const char* kItemsAnalog[] = { "Off", "Analog 1", "Analog 2", "Analog 3", "Analog 4" };
inline constexpr const char* kItemsChorus[] = { "Mono", "Stereo", "Wide" };
inline constexpr const char* kItemsRotary[] = { "Stop", "Chorale", "Tremolo" };
inline constexpr const char* kItemsSlope[] = { "12 dB", "24 dB" };
inline constexpr const char* kItemsDir[] = { "Forward", "Reverse", "Pendulum", "Random", "Brownian" };
inline constexpr const char* kItemsTarget[] = { "Transient", "Tonal", "Both" };
inline constexpr const char* kItemsSource[] = { "Full Mix", "Drums", "Percussive", "Keys", "Vocal", "Pad" };
inline constexpr const char* kItemsSync[] = { "DAW", "FREE" };

enum class T { Float, Int, Choice, Bool };

struct PDef
{
    const char* id; const char* name; T type;
    float lo, hi, def, centre;    // centre > lo: skewed so this value sits at the middle of the knob
    const char* unit;
    int fx;                       // kNumFx = belongs to the stream
    bool seq;                     // the sequencer may reach it
    const char* const* items; int numItems;
};

#define TF(id, nm, lo, hi, def, ctr, unit, fx, seq) { id, nm, T::Float, lo, hi, def, ctr, unit, fx, seq, nullptr, 0 }
#define TB(id, nm, def, fx, seq)                    { id, nm, T::Bool, 0, 1, def, -1, "", fx, seq, nullptr, 0 }
#define TC(id, nm, items, def, fx, seq)             { id, nm, T::Choice, 0, (float) (sizeof (items) / sizeof (items[0])) - 1, def, -1, "", fx, seq, items, (int) (sizeof (items) / sizeof (items[0])) }

// Parameters of one stream. The order follows enum P; a new one goes at the end of both.
enum P
{
    sOn, sGain, sSolo, sOrder, sTails,
    cOn, cMix, cIr, cDecay, cDamp, cPre, cSize, cRev, cWidth, cLow, cHigh,
    tOn, tMix, tTime, tSync, tNote, tFb, tMode, tH1, tH2, tH3, tH4, tTone, tDrive, tWow, tFlut, tSpread, tNoise,
    dOn, dMix, dTime, dSync, dNote, dFb, dHp, dLp, dDepth, dRate, dPing, dAnalog, dLofi, dDual,
    hOn, hMix, hRate, hDepth, hTime, hTone, hMode, hGrit,
    rOn, rMix, rSpeed, rBal, rDrive, rMic, rAccel,
    fOn, fMix, fHpOn, fHpF, fHpQ, fHpS, fLpOn, fLpF, fLpQ, fLpS, fBpOn, fBpF, fBpQ, fBpM,
    kNumP
};

inline const PDef* streamParams()
{
    static const PDef defs[kNumP] = {
        TB ("on", "On", 1, fxStream, true),
        TF ("gain", "Gain", -60, 12, 0, -12, "dB", fxStream, true),
        TB ("solo", "Solo", 0, fxStream, false),
        { "order", "Chain Order", T::Int, 0, 719, 0, -1, "", fxStream, false, nullptr, 0 },
        TB ("tails", "Tails", 1, fxStream, false),

        TB ("c_on", "Conv On", 0, fxConv, true),
        TF ("c_mix", "Conv Mix", 0, 100, 35, -1, "%", fxConv, true),
        TC ("c_ir", "Conv IR", kIrNames, 1, fxConv, false),
        TF ("c_decay", "Conv Decay", 5, 100, 100, 40, "%", fxConv, true),
        TF ("c_damp", "Conv Damp", 0, 100, 0, -1, "%", fxConv, true),
        TF ("c_pre", "Conv Predelay", 0, 300, 0, 60, "ms", fxConv, true),
        TF ("c_size", "Conv Size", 50, 200, 100, 100, "%", fxConv, false),
        TB ("c_rev", "Conv Reverse", 0, fxConv, false),
        TF ("c_width", "Conv Width", 0, 200, 100, -1, "%", fxConv, true),
        TF ("c_low", "Conv Low Cut", 20, 1200, 20, 150, "Hz", fxConv, true),
        TF ("c_high", "Conv High Cut", 1000, 20000, 20000, 6000, "Hz", fxConv, true),

        TB ("t_on", "Tape On", 0, fxTape, true),
        TF ("t_mix", "Tape Mix", 0, 100, 35, -1, "%", fxTape, true),
        TF ("t_time", "Tape Time", 40, 1500, 300, 300, "ms", fxTape, true),
        TB ("t_sync", "Tape Sync", 0, fxTape, false),
        TC ("t_note", "Tape Note", kDivisionNames, kDiv14, fxTape, true),
        TF ("t_fb", "Tape Swell", 0, 115, 45, -1, "%", fxTape, true),
        TC ("t_mode", "Tape Mode", kItemsTapeMode, 1, fxTape, true),
        TB ("t_h1", "Tape Head 1", 0, fxTape, true),
        TB ("t_h2", "Tape Head 2", 0, fxTape, true),
        TB ("t_h3", "Tape Head 3", 1, fxTape, true),
        TB ("t_h4", "Tape Head 4", 1, fxTape, true),
        TF ("t_tone", "Tape Tone", -100, 100, 0, -1, "%", fxTape, true),
        TF ("t_drive", "Tape Drive", 0, 100, 25, -1, "%", fxTape, true),
        TF ("t_wow", "Tape Wow", 0, 100, 12, -1, "%", fxTape, true),
        TF ("t_flut", "Tape Flutter", 0, 100, 12, -1, "%", fxTape, true),
        TF ("t_spread", "Tape Spread", 0, 100, 40, -1, "%", fxTape, true),
        TF ("t_noise", "Tape Noise", 0, 100, 0, -1, "%", fxTape, false),

        TB ("d_on", "Delay On", 0, fxDelay, true),
        TF ("d_mix", "Delay Mix", 0, 100, 30, -1, "%", fxDelay, true),
        TF ("d_time", "Delay Time", 1, 3500, 375, 300, "ms", fxDelay, true),
        TB ("d_sync", "Delay Sync", 1, fxDelay, false),
        TC ("d_note", "Delay Note", kDivisionNames, kDiv18D, fxDelay, true),
        TF ("d_fb", "Delay Feedback", 0, 150, 40, -1, "%", fxDelay, true),
        TF ("d_hp", "Delay HP", 20, 2000, 20, 200, "Hz", fxDelay, true),
        TF ("d_lp", "Delay LP", 500, 20000, 20000, 5000, "Hz", fxDelay, true),
        TF ("d_depth", "Delay Mod Depth", 0, 100, 0, -1, "%", fxDelay, true),
        TF ("d_rate", "Delay Mod Rate", 0.05f, 40, 0.5f, 2, "Hz", fxDelay, true),
        TB ("d_ping", "Delay Ping Pong", 0, fxDelay, true),
        TC ("d_analog", "Delay Analog", kItemsAnalog, 2, fxDelay, true),
        TB ("d_lofi", "Delay LoFi", 0, fxDelay, true),
        TF ("d_dual", "Delay R Ratio", 50, 200, 100, 100, "%", fxDelay, true),

        TB ("h_on", "Chorus On", 0, fxChorus, true),
        TF ("h_mix", "Chorus Mix", 0, 100, 50, -1, "%", fxChorus, true),
        TF ("h_rate", "Chorus Rate", 0.05f, 12, 0.9f, 1.5f, "Hz", fxChorus, true),
        TF ("h_depth", "Chorus Depth", 0, 100, 45, -1, "%", fxChorus, true),
        TF ("h_time", "Chorus Delay", 1.5f, 25, 6, 8, "ms", fxChorus, true),
        TF ("h_tone", "Chorus Tone", 1500, 16000, 6600, 6000, "Hz", fxChorus, true),
        TC ("h_mode", "Chorus Mode", kItemsChorus, 1, fxChorus, true),
        TF ("h_grit", "Chorus Grit", 0, 100, 15, -1, "%", fxChorus, true),

        TB ("r_on", "Rotary On", 0, fxRotary, true),
        TF ("r_mix", "Rotary Mix", 0, 100, 100, -1, "%", fxRotary, true),
        TC ("r_speed", "Rotary Speed", kItemsRotary, 1, fxRotary, true),
        TF ("r_bal", "Rotary Horn/Drum", -100, 100, 0, -1, "%", fxRotary, true),
        TF ("r_drive", "Rotary Drive", 0, 100, 15, -1, "%", fxRotary, true),
        TF ("r_mic", "Rotary Mic Angle", 0, 180, 90, -1, "deg", fxRotary, true),
        TF ("r_accel", "Rotary Inertia", 25, 300, 100, 100, "%", fxRotary, true),

        TB ("f_on", "Filter On", 0, fxFilter, true),
        TF ("f_mix", "Filter Mix", 0, 100, 100, -1, "%", fxFilter, true),
        TB ("f_hpon", "HPF On", 1, fxFilter, true),
        TF ("f_hpf", "HPF Freq", 20, 20000, 80, 800, "Hz", fxFilter, true),
        TF ("f_hpq", "HPF Reso", 0, 100, 10, -1, "%", fxFilter, true),
        TC ("f_hps", "HPF Slope", kItemsSlope, 0, fxFilter, false),
        TB ("f_lpon", "LPF On", 1, fxFilter, true),
        TF ("f_lpf", "LPF Freq", 20, 20000, 12000, 800, "Hz", fxFilter, true),
        TF ("f_lpq", "LPF Reso", 0, 100, 10, -1, "%", fxFilter, true),
        TC ("f_lps", "LPF Slope", kItemsSlope, 0, fxFilter, false),
        TB ("f_bpon", "BPF On", 0, fxFilter, true),
        TF ("f_bpf", "BPF Freq", 40, 16000, 1000, 800, "Hz", fxFilter, true),
        TF ("f_bpq", "BPF Q", 0.3f, 20, 1.5f, 2, "", fxFilter, true),
        TF ("f_bpm", "BPF Amount", 0, 100, 100, -1, "%", fxFilter, true),
    };
    return defs;
}

#undef TF
#undef TB
#undef TC

/** Same curve as juce::NormalisableRange::setSkewForCentre, so the engine and the host agree on what a knob position means. */
inline float skewOf (const PDef& d) noexcept
{
    return d.centre > d.lo ? std::log (0.5f) / std::log ((d.centre - d.lo) / (d.hi - d.lo)) : 1.0f;
}
inline float toNorm (const PDef& d, float skew, float v) noexcept
{
    const float n = std::clamp ((v - d.lo) / (d.hi - d.lo), 0.0f, 1.0f);
    return skew == 1.0f ? n : std::pow (n, skew);
}
inline float fromNorm (const PDef& d, float skew, float n) noexcept
{
    n = std::clamp (n, 0.0f, 1.0f);
    return d.lo + (d.hi - d.lo) * (skew == 1.0f ? n : std::pow (n, 1.0f / skew));
}

// ---- sequencer destinations. Menu order, saved in sessions: new ones go at the end.
enum GlobalDest { gdOff, gdFocus, gdTransGain, gdTonalGain, gdMix, kNumGlobalDest };
inline constexpr const char* kGlobalDestNames[kNumGlobalDest] = { "Off", "Split Focus", "Transient Gain", "Tonal Gain", "Global Mix" };

/** Stream parameters the sequencer can reach, in table order. */
struct DestTable
{
    int count = 0;
    std::array<int, kNumP> param {};
    DestTable()
    {
        for (int p = 0; p < kNumP; ++p)
            if (streamParams()[p].seq && p != sGain) param[(size_t) count++] = p;
    }
    static const DestTable& get() { static const DestTable t; return t; }
};
inline int numDests() { return kNumGlobalDest + DestTable::get().count; }

enum LaneP { lOn, lDest, lTarget, lRate, lLen, lDir, lAmt, lSlew, lSwing, lChance, kNumLaneP };
inline constexpr const char* kLaneIds[kNumLaneP] = { "on", "dest", "target", "rate", "len", "dir", "amt", "slew", "swing", "chance" };

/** Chain order as a single number: the rank of a permutation of the six effects. */
inline void orderFromRank (int rank, int* out) noexcept
{
    int pool[kNumFx] = { 0, 1, 2, 3, 4, 5 };
    int f = 120;
    rank = std::clamp (rank, 0, 719);
    for (int i = 0; i < kNumFx; ++i)
    {
        const int left = kNumFx - i, k = left > 1 ? rank / f : 0;
        if (left > 1) { rank %= f; f /= std::max (1, left - 1); }
        out[i] = pool[k];
        for (int j = k; j < left - 1; ++j) pool[j] = pool[j + 1];
    }
}
inline int rankFromOrder (const int* order) noexcept
{
    int pool[kNumFx] = { 0, 1, 2, 3, 4, 5 };
    int f = 120, rank = 0;
    for (int i = 0; i < kNumFx - 1; ++i)
    {
        const int left = kNumFx - i;
        int k = 0;
        while (k < left && pool[k] != order[i]) ++k;
        rank += k * f;
        f /= std::max (1, left - 1);
        for (int j = k; j < left - 1; ++j) pool[j] = pool[j + 1];
    }
    return rank;
}
} // namespace tails
