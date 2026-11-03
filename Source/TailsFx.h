#pragma once
// The six effects. No JUCE, no allocation after prepare(), no locks. Each process() works on at most kTick samples,
// replaces the input with the wet signal, and reads its settings from `v`, the stream's physical parameter values.
#include "TailsCore.h"
#include "TailsConv.h"
#include <vector>

namespace tails
{
struct Noise
{
    uint32_t s = 0x9E3779B9u;
    float next() noexcept
    {   // xorshift32, -1..1
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (int32_t) s * 4.656612873e-10f;
    }
};

struct OnePole
{
    float a = 1.0f, z = 0.0f;
    void set (float hz, double sr) noexcept { a = 1.0f - std::exp (-kTwoPi * std::min (hz, (float) sr * 0.49f) / (float) sr); }
    float lp (float x) noexcept { z += a * (x - z); return z; }
    float hp (float x) noexcept { return x - lp (x); }
    void reset() noexcept { z = 0.0f; }
};

/** Topology-preserving state-variable filter: stays stable while its cutoff moves. */
struct Svf
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.414f, s1 = 0, s2 = 0;
    void set (float hz, float q, double sr) noexcept
    {
        const float g = std::tan (kPi * std::clamp (hz, 5.0f, (float) sr * 0.47f) / (float) sr);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void run (float x, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float low (float x) noexcept { float l, b, h; run (x, l, b, h); return l; }
    float high (float x) noexcept { float l, b, h; run (x, l, b, h); return h; }
    float band (float x) noexcept { float l, b, h; run (x, l, b, h); return b * k; }   // unity at the peak
    void reset() noexcept { s1 = s2 = 0.0f; }
};

/** Circular buffer with 4-point Hermite reads. */
struct Line
{
    std::vector<float> d;
    int mask = 0, w = 0;
    void prepare (double samples)
    {
        int n = 16;
        while (n < (int) samples + 8) n <<= 1;
        d.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0;
    }
    void clear() noexcept { std::fill (d.begin(), d.end(), 0.0f); w = 0; }
    void push (float x) noexcept { d[(size_t) w] = x; w = (w + 1) & mask; }
    /** delay in samples, >= 2: 1 is the sample pushed last. */
    float read (float delay) const noexcept
    {
        const float pos = (float) w - delay;
        const float fl = std::floor (pos);
        const float t = pos - fl;
        const int i = (int) fl;
        const float a = d[(size_t) ((i - 1) & mask)], b = d[(size_t) (i & mask)], c = d[(size_t) ((i + 1) & mask)], e = d[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (c - a), c2 = a - 2.5f * b + 2.0f * c - 0.5f * e, c3 = 0.5f * (e - a) + 1.5f * (b - c);
        return ((c3 * t + c2) * t + c1) * t + b;
    }
};

/** Valve-like curve: asymmetric, odd and even harmonics, unity slope at zero after makeup. */
inline float valve (float x, float drive) noexcept
{
    const float bias = 0.18f * std::min (1.0f, drive - 1.0f);
    const float tb = std::tanh (bias);
    const float slope = 1.0f - tb * tb;
    return (std::tanh (x * drive + bias) - tb) / (drive * slope);
}

// ---------------------------------------------------------------- convolution

class ConvFx
{
public:
    Convolver conv;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        conv.prepare (sr);
        pre[0].prepare (sr * 0.32); pre[1].prepare (sr * 0.32);
        reset();
    }
    void reset() noexcept
    {
        conv.reset(); pre[0].clear(); pre[1].clear();
        low[0].reset(); low[1].reset(); high[0].reset(); high[1].reset();
        preSm = -1.0f;
    }
    void update (const float* v, double) noexcept
    {
        decay = v[cDecay] * 0.01f; damp = v[cDamp] * 0.01f;
        preT = v[cPre] * 0.001f * (float) sr;
        width = v[cWidth] * 0.01f;
        lowOn = v[cLow] > 21.0f; highOn = v[cHigh] < 19900.0f;
        for (auto& f : low) f.set (v[cLow], sr);
        for (auto& f : high) f.set (v[cHigh], sr);
        if (preSm < 0.0f) preSm = preT;
    }
    void process (float* l, float* r, int n) noexcept
    {
        const float c = 0.0015f;
        for (int i = 0; i < n; ++i)
        {
            pre[0].push (l[i]); pre[1].push (r[i]);
            preSm += c * (preT - preSm);
            if (preSm > 2.0f) { l[i] = pre[0].read (preSm + 1.0f); r[i] = pre[1].read (preSm + 1.0f); }
        }
        conv.process (l, r, n, decay, damp);
        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            if (lowOn) { a = low[0].hp (a); b = low[1].hp (b); }
            if (highOn) { a = high[0].lp (a); b = high[1].lp (b); }
            const float m = 0.5f * (a + b), s = 0.5f * (a - b) * width;
            l[i] = m + s; r[i] = m - s;
        }
    }
private:
    double sr = 48000.0;
    Line pre[2];
    OnePole low[2], high[2];
    float decay = 1, damp = 0, preT = 0, preSm = -1, width = 1;
    bool lowOn = false, highOn = false;
};

// ---------------------------------------------------------------- tape echo (Binson Echorec line)

/** One track on a rotating memory, one record head, four playback heads evenly spaced behind it. Time is the delay of
    head 4. Selector: Echo plays the heads once; Repeat feeds them back, with Swell (the "length of swell" control) as
    the amount; Swell adds a little of all four heads, the reverb-like setting. The core is mono, as on the machine. */
class TapeFx
{
public:
    float gate = 1.0f;     // the hiss stops with the send, so a switched-off effect can go to sleep

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        line.prepare (sr * 3.4);
        hpf.set (110.0f, sr);
        gap.set (11000.0f, sr);
        reset();
    }
    void reset() noexcept
    {
        line.clear(); hpf.reset(); gap.reset(); tone.reset();
        timeS = -1.0f; phase = wn = fn = 0.0f; p1 = p2 = 0.0f;
        for (auto& h : headPole) h.reset();
    }
    void update (const float* v, double bpm) noexcept
    {
        float ms = v[tTime];
        if (v[tSync] > 0.5f)
            ms = (float) (kDivisions[(int) v[tNote]].beats * 60000.0 / std::max (1.0, bpm));
        timeT = std::clamp (ms, 40.0f, 1500.0f) * 0.001f * (float) sr;
        if (timeS < 0.0f) timeS = timeT;
        fb = v[tFb] * 0.01f;
        mode = (int) v[tMode];
        for (int h = 0; h < 4; ++h) heads[h] = v[tH1 + h] > 0.5f;
        drive = 1.0f + v[tDrive] * 0.05f;
        wow = v[tWow] * 0.01f; flutter = v[tFlut] * 0.01f;
        spread = v[tSpread] * 0.01f;
        hiss = v[tNoise] * 0.01f;
        const float tn = v[tTone] * 0.01f;
        const float cut = tn >= 0 ? 7500.0f * std::pow (2.0f, tn * 0.9f) : 7500.0f * std::pow (2.0f, tn * 1.8f);
        tone.set (cut, 0.6f, sr);
        for (auto& h : headPole) h.set (cut * 1.4f, sr);
    }
    void process (float* l, float* r, int n) noexcept
    {
        const float inertia = 1.0f / (0.14f * (float) sr);
        int active = 0;
        for (bool h : heads) if (h) ++active;
        const float norm = active > 1 ? 1.0f / std::sqrt ((float) active) : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            timeS += inertia * (timeT - timeS);
            // the drum is never perfectly round: a cycle per turn plus a little fast bearing noise
            phase += (float) (1.0 / std::max (1.0f, timeS * 1.333f));
            if (phase > 1.0f) phase -= 1.0f;
            wn += 0.0008f * (noise.next() - wn);
            fn += 0.02f * (noise.next() - fn);
            p1 += (float) (9.3 / sr); if (p1 > 1.0f) p1 -= 1.0f;
            p2 += (float) (13.7 / sr); if (p2 > 1.0f) p2 -= 1.0f;
            const float wob = 1.0f + wow * (0.007f * std::sin (kTwoPi * phase) + 0.05f * wn)
                                   + flutter * (0.0009f * std::sin (kTwoPi * p1) + 0.0006f * std::sin (kTwoPi * p2) + 0.008f * fn);

            float sum = 0.0f, feed = 0.0f, oL = 0.0f, oR = 0.0f;
            for (int h = 0; h < 4; ++h)
            {
                const bool sel = heads[h];
                const float gain = sel ? 1.0f : (mode == 2 ? 0.32f : 0.0f);
                if (gain == 0.0f) continue;
                const float y = line.read (std::max (8.0f, 0.25f * (float) (h + 1) * timeS * wob)) * gain;
                const float pan = (h % 2 == 0 ? -1.0f : 1.0f) * spread;
                oL += y * (1.0f - std::max (0.0f, pan)); oR += y * (1.0f + std::min (0.0f, pan));
                sum += y;
                if (sel) feed += y;
            }
            float lo, bp, hi;
            tone.run (hpf.hp (feed * norm), lo, bp, hi);
            const float fbk = mode == 0 ? 0.0f : fb * (mode == 2 ? 0.8f : 1.0f);
            const float input = 0.5f * (l[i] + r[i]) + gate * hiss * 0.012f * noise.next();
            const float rec = gap.lp (valve (input + fbk * lo, drive));
            line.push (rec);
            l[i] = headPole[0].lp (oL) * norm;
            r[i] = headPole[1].lp (oR) * norm;
        }
    }
private:
    double sr = 48000.0;
    Line line;
    OnePole hpf, gap, headPole[2];
    Svf tone;
    Noise noise;
    float timeT = 0, timeS = -1, fb = 0, drive = 1, wow = 0, flutter = 0, spread = 0, hiss = 0, phase = 0, wn = 0, fn = 0, p1 = 0, p2 = 0;
    int mode = 1;
    bool heads[4] = { false, false, true, true };
};

// ---------------------------------------------------------------- delay (H-Delay line)

/** Stereo delay up to 3.5 s. Feedback goes past 100 % and a soft ceiling keeps it from running away. The filters sit in
    the repeats. Analog 1-4 are four grades of an imaginary analogue path (saturation and bandwidth), LoFi the narrow band
    and coarse words of an early digital line. */
class DelayFx
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        line[0].prepare (sr * 7.5); line[1].prepare (sr * 7.5);
        reset();
    }
    void reset() noexcept
    {
        line[0].clear(); line[1].clear();
        for (int c = 0; c < 2; ++c) { hp[c].reset(); lp[c].reset(); ana[c].reset(); lofiHp[c].reset(); lofiLp[c].reset(); lofiPre[c].reset(); lofiPost[c].reset(); held[c] = 0; }
        time = -1; hold = 0; lfo = 0;
    }
    void update (const float* v, double bpm) noexcept
    {
        float ms = v[dTime];
        if (v[dSync] > 0.5f) ms = (float) (kDivisions[(int) v[dNote]].beats * 60000.0 / std::max (1.0, bpm));
        target = std::clamp (ms, 1.0f, 3500.0f) * 0.001f * (float) sr;
        if (time < 0) time = target;
        ratio = v[dDual] * 0.01f;
        fb = v[dFb] * 0.01f;
        depth = v[dDepth] * 0.01f * 0.006f * (float) sr;    // up to +-6 ms
        rate = (float) (v[dRate] / sr);
        ping = v[dPing] > 0.5f;
        lofi = v[dLofi] > 0.5f;
        static constexpr float driveTab[5] = { 1.0f, 1.25f, 1.7f, 2.4f, 3.4f };
        static constexpr float bandTab[5] = { 20000.0f, 13000.0f, 9000.0f, 6500.0f, 4500.0f };
        const int a = (int) v[dAnalog];
        analog = a; drive = driveTab[a];
        for (int c = 0; c < 2; ++c)
        {
            hp[c].set (v[dHp], 0.707f, sr);
            lp[c].set (v[dLp], 0.707f, sr);
            ana[c].set (bandTab[a], sr);
            lofiHp[c].set (180.0f, sr);
            lofiLp[c].set (5600.0f, 0.54f, sr);     // 4th order, the anti-alias filter of a 16 kHz converter
            lofiPre[c].set (5600.0f, 1.31f, sr);
            lofiPost[c].set (6400.0f, 0.71f, sr);
        }
        hpOn = v[dHp] > 21.0f; lpOn = v[dLp] < 19900.0f;
    }
    void process (float* l, float* r, int n) noexcept
    {
        const float glide = (float) (1.0 / (0.05 * sr));
        const int holdLen = std::max (1, (int) (sr / 16000.0));
        for (int i = 0; i < n; ++i)
        {
            time += glide * (target - time);
            lfo += rate; if (lfo > 1.0f) lfo -= 1.0f;
            const float tri = 4.0f * std::abs (lfo - 0.5f) - 1.0f;
            const float dl = std::clamp (time + depth * tri, 4.0f, (float) line[0].mask - 8.0f);
            const float dr = std::clamp (time * ratio + depth * tri * (ping ? -1.0f : 1.0f), 4.0f, (float) line[1].mask - 8.0f);
            float y[2] = { line[0].read (dl), line[1].read (dr) };
            for (int c = 0; c < 2; ++c)
            {
                float s = y[c];
                if (hpOn) s = hp[c].high (s);
                if (lpOn) s = lp[c].low (s);
                if (analog > 0) s = ana[c].lp (valve (s, drive));
                if (lofi)
                {
                    s = lofiLp[c].low (lofiPre[c].low (lofiHp[c].hp (s)));
                    if (hold == 0) held[c] = std::floor (s * 2048.0f + 0.5f) / 2048.0f;
                    s = lofiPost[c].low (held[c]);
                }
                y[c] = s;
            }
            if (++hold >= holdLen) hold = 0;
            const float a = ping ? 0.5f * (l[i] + r[i]) : l[i];
            const float b = ping ? 0.0f : r[i];
            const float back0 = ping ? y[1] : y[0], back1 = ping ? y[0] : y[1];
            line[0].push (ceiling (a + fb * back0));
            line[1].push (ceiling (b + fb * back1));
            l[i] = y[0]; r[i] = y[1];
        }
    }
private:
    static float ceiling (float x) noexcept { const float a = std::abs (x); return a < 1.0f ? x : (x > 0 ? 1.0f : -1.0f) * (1.0f + std::tanh (a - 1.0f)); }
    double sr = 48000.0;
    Line line[2];
    Svf hp[2], lp[2];
    OnePole ana[2], lofiHp[2];
    Svf lofiLp[2], lofiPre[2], lofiPost[2];
    float held[2] = { 0, 0 };
    float target = 0, time = -1, ratio = 1, fb = 0, depth = 0, rate = 0, lfo = 0, drive = 1;
    int analog = 0, hold = 0;
    bool ping = false, lofi = false, hpOn = false, lpOn = false;
};

// ---------------------------------------------------------------- chorus (bx_bluechorus2 / Boss CE-2 line)

/** A bucket-brigade voice: one short line swept by a triangle LFO, inside the band of the BBD's filters (about 48 Hz to
    6.6 kHz on the CE-2). Mono is the pedal, Stereo puts the LFO in opposite phase on the two sides, Wide adds a second
    voice in quadrature. Grit stands for the BBD: level-dependent softness and clock hiss. */
class ChorusFx
{
public:
    float gate = 1.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        line[0].prepare (sr * 0.09); line[1].prepare (sr * 0.09);
        reset();
    }
    void reset() noexcept
    {
        line[0].clear(); line[1].clear();
        for (int c = 0; c < 2; ++c) { pre[c].reset(); post[c].reset(); hp[c].reset(); }
        lfo = 0; smoothDelay = -1;
    }
    void update (const float* v, double) noexcept
    {
        inc = (float) (v[hRate] / sr);
        baseT = v[hTime] * 0.001f * (float) sr;
        if (smoothDelay < 0) smoothDelay = baseT;
        depth = v[hDepth] * 0.01f;
        mode = (int) v[hMode];
        grit = v[hGrit] * 0.01f;
        for (int c = 0; c < 2; ++c)
        {
            pre[c].set (v[hTone], 0.8f, sr);
            post[c].set (v[hTone] * 1.05f, 0.8f, sr);
            hp[c].set (48.0f, sr);
        }
    }
    void process (float* l, float* r, int n) noexcept
    {
        const float g = 1.0f + 2.5f * grit;
        for (int i = 0; i < n; ++i)
        {
            lfo += inc; if (lfo >= 1.0f) lfo -= 1.0f;
            smoothDelay += 0.002f * (baseT - smoothDelay);
            const float inL = mode == 0 ? 0.5f * (l[i] + r[i]) : l[i], inR = mode == 0 ? inL : r[i];
            line[0].push (pre[0].low (valve (hp[0].hp (inL), g)));
            line[1].push (pre[1].low (valve (hp[1].hp (inR), g)));
            const float swing = smoothDelay * 0.82f * depth;
            const float a = tri (lfo), b = tri (lfo + 0.5f);
            float wl = line[0].read (std::max (3.0f, smoothDelay + swing * a));
            float wr = line[1].read (std::max (3.0f, smoothDelay + swing * (mode == 0 ? a : b)));
            if (mode == 2)
            {
                const float a2 = tri (lfo + 0.25f), b2 = tri (lfo + 0.75f);
                wl = 0.7071f * (wl + line[1].read (std::max (3.0f, smoothDelay * 1.37f + swing * a2)));
                wr = 0.7071f * (wr + line[0].read (std::max (3.0f, smoothDelay * 1.37f + swing * b2)));
            }
            const float n1 = gate * grit * 0.0016f * noise.next(), n2 = mode == 0 ? n1 : gate * grit * 0.0016f * noise.next();
            l[i] = post[0].low (wl) + n1;
            r[i] = post[1].low (wr) + n2;
        }
    }
private:
    static float tri (float p) noexcept { p -= std::floor (p); return 4.0f * std::abs (p - 0.5f) - 1.0f; }
    double sr = 48000.0;
    Line line[2];
    Svf pre[2], post[2];
    OnePole hp[2];
    Noise noise;
    float inc = 0, lfo = 0, baseT = 0, smoothDelay = -1, depth = 0, grit = 0;
    int mode = 1;
};

// ---------------------------------------------------------------- rotary speaker

/** Horn above 800 Hz, drum below. Each rotor has its own inertia. Two microphones hear the Doppler shift and the amplitude
    of the horn from their own angle, plus the reflection of the horn from the back of the cabinet; the drum is mostly a
    rotating baffle, so nearly all amplitude. */
class RotaryFx
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& l : horn) l.prepare (sr * 0.008);
        for (auto& l : reflect) l.prepare (sr * 0.012);
        drumLine.prepare (sr * 0.006);
        reset();
    }
    void reset() noexcept
    {
        for (auto& l : horn) l.clear();
        for (auto& l : reflect) l.clear();
        drumLine.clear();
        for (auto& f : xoLo) f.reset();
        for (auto& f : xoHi) f.reset();
        for (auto& f : tone) f.reset();
        hornA = 0.3f; drumA = 1.7f; hornHz = drumHz = -1;
    }
    void update (const float* v, double) noexcept
    {
        const int speed = (int) v[rSpeed];
        static constexpr float horns[3] = { 0.0f, 0.83f, 6.67f };   // 50 and 400 rpm
        static constexpr float drums[3] = { 0.0f, 0.67f, 5.67f };   // 40 and 340 rpm
        hornT = horns[speed]; drumT = drums[speed];
        if (hornHz < 0) { hornHz = hornT; drumHz = drumT; }
        inertia = v[rAccel] * 0.01f;
        balance = v[rBal] * 0.01f;
        drive = 1.0f + v[rDrive] * 0.04f;
        mic = v[rMic] * kPi / 180.0f;
        for (auto& f : xoLo) f.set (800.0f, 0.7071f, sr);
        for (auto& f : xoHi) f.set (800.0f, 0.7071f, sr);
    }
    void process (float* l, float* r, int n) noexcept
    {
        // time constants are estimates: the horn is light and spins up in about a second, the drum takes several
        const float rateS = (float) (1.0 / sr);
        const float hTau = (hornT > hornHz ? 0.55f : 0.9f) * inertia, dTau = (drumT > drumHz ? 2.6f : 3.4f) * inertia;
        const float hA = rateS / hTau, dA = rateS / dTau;
        const float maxD = 0.15f / 343.0f * (float) sr;     // 15 cm of horn radius
        for (int i = 0; i < n; ++i)
        {
            hornHz += hA * (hornT - hornHz); drumHz += dA * (drumT - drumHz);
            hornA += kTwoPi * hornHz * rateS; if (hornA > kTwoPi) hornA -= kTwoPi;
            drumA += kTwoPi * drumHz * rateS; if (drumA > kTwoPi) drumA -= kTwoPi;

            const float x = valve (0.5f * (l[i] + r[i]), drive);
            const float lo = xoLo[1].low (xoLo[0].low (x));
            const float hi = xoHi[1].high (xoHi[0].high (x));
            horn[0].push (hi); reflect[0].push (hi);
            drumLine.push (lo);

            float out[2];
            for (int m = 0; m < 2; ++m)
            {
                const float ang = hornA - (m == 0 ? -0.5f : 0.5f) * mic;
                const float c = std::cos (ang);
                const float direct = horn[0].read (3.0f + maxD * (1.0f - c));
                const float back = reflect[0].read (0.0016f * (float) sr + maxD * (1.0f + c));
                const float h = direct * (0.62f + 0.38f * c) + 0.32f * back * (0.62f - 0.38f * c);
                const float dang = drumA - (m == 0 ? -0.5f : 0.5f) * mic;
                const float dc = std::cos (dang);
                const float d = drumLine.read (3.0f + 0.45f * maxD * (1.0f - dc)) * (0.78f + 0.22f * dc);
                tone[m].set (5200.0f + 4200.0f * c, sr);
                const float hg = balance > 0 ? 1.0f : 1.0f + balance, dg = balance < 0 ? 1.0f : 1.0f - balance;
                out[m] = tone[m].lp (h) * hg + d * dg;
            }
            l[i] = out[0]; r[i] = out[1];
        }
    }
    float hornSpeed() const noexcept { return hornHz; }
    float hornAngle() const noexcept { return hornA; }
    float drumAngle() const noexcept { return drumA; }
private:
    double sr = 48000.0;
    Line horn[1], reflect[1], drumLine;
    Svf xoLo[2], xoHi[2];
    OnePole tone[2];
    float hornA = 0, drumA = 0, hornHz = -1, drumHz = -1, hornT = 0, drumT = 0, inertia = 1, balance = 0, drive = 1, mic = 1;
};

// ---------------------------------------------------------------- filters

/** HPF, LPF and band pass in series, each with its own switch. The band pass has an Amount, because alone it empties the signal. */
class FilterFx
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() noexcept
    {
        for (auto& s : hp) s.reset();
        for (auto& s : lp) s.reset();
        for (auto& s : bp) s.reset();
        hf = lf = bf = -1;
    }
    void update (const float* v, double) noexcept
    {
        hpOn = v[fHpOn] > 0.5f; lpOn = v[fLpOn] > 0.5f; bpOn = v[fBpOn] > 0.5f;
        hp24 = v[fHpS] > 0.5f; lp24 = v[fLpS] > 0.5f;
        amount = v[fBpM] * 0.01f;
        const float c = 0.35f;
        hf = hf < 0 ? v[fHpF] : hf + c * (v[fHpF] - hf);
        lf = lf < 0 ? v[fLpF] : lf + c * (v[fLpF] - lf);
        bf = bf < 0 ? v[fBpF] : bf + c * (v[fBpF] - bf);
        auto reso = [] (float pc) { return 0.5f * std::pow (32.0f, pc * 0.01f); };   // 0.5 .. 16
        const float hq = reso (v[fHpQ]), lq = reso (v[fLpQ]);
        for (int c2 = 0; c2 < 2; ++c2)
        {
            hp[c2].set (hf, hq, sr); hp[c2 + 2].set (hf, 0.7071f, sr);
            lp[c2].set (lf, lq, sr); lp[c2 + 2].set (lf, 0.7071f, sr);
            bp[c2].set (bf, v[fBpQ], sr);
        }
    }
    void process (float* l, float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r[i];
            if (hpOn)
            {
                a = hp[0].high (a); b = hp[1].high (b);
                if (hp24) { a = hp[2].high (a); b = hp[3].high (b); }
            }
            if (lpOn)
            {
                a = lp[0].low (a); b = lp[1].low (b);
                if (lp24) { a = lp[2].low (a); b = lp[3].low (b); }
            }
            if (bpOn) { a += amount * (bp[0].band (a) - a); b += amount * (bp[1].band (b) - b); }
            l[i] = a; r[i] = b;
        }
    }
private:
    double sr = 48000.0;
    Svf hp[4], lp[4], bp[2];
    float hf = -1, lf = -1, bf = -1, amount = 1;
    bool hpOn = true, lpOn = true, bpOn = false, hp24 = false, lp24 = false;
};
} // namespace tails
