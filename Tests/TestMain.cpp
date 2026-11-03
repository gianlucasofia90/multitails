// MULTITAILS headless tests: drive the processor with a fake host and measure the audio it produces.
// Run:  MultitailsTests                                   -> unit tests (exit code 1 on failure)
//       MultitailsTests --snap file [page] [stream] [fx] [preset] [scale]  -> render the editor to a PNG
//                                     page 0 = effects, 1 = MIDI map; preset < 0 keeps the defaults
#include <juce_audio_processors/juce_audio_processors.h>
#include "TailsProcessor.h"
#include "TailsEditor.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <chrono>
#include <random>
#include <vector>

using namespace tails;

// counts heap use on the audio thread: the engine must not touch the allocator while it runs
static thread_local bool watchAllocations = false;
static std::atomic<int> allocationsSeen { 0 };
void* operator new (std::size_t n)
{
    if (watchAllocations) allocationsSeen.fetch_add (1);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { if (watchAllocations) allocationsSeen.fetch_add (1); std::free (p); }
void operator delete[] (void* p) noexcept { operator delete (p); }
void operator delete (void* p, std::size_t) noexcept { operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { operator delete (p); }

struct FakeHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

static int failures = 0, passes = 0;
#define CHECK(c, msg) do { if (! (c)) { std::printf ("FAIL: %s\n", msg); ++failures; } else ++passes; } while (0)
#define CHECKV(c, msg, v) do { if (! (c)) { std::printf ("FAIL: %s (%.4f)\n", msg, (double) (v)); ++failures; } else ++passes; } while (0)

using Sig = std::vector<float>;
struct Stereo { Sig l, r; };

struct Rig
{
    std::unique_ptr<TailsProcessor> owned = std::make_unique<TailsProcessor>();
    TailsProcessor& p = *owned;
    FakeHead head;
    double sr;
    int block;

    explicit Rig (int blockSize = 512, double rate = 48000.0) : sr (rate), block (blockSize)
    {
        p.setPlayHead (&head);
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
    }
    void set (const juce::String& id, float v)
    {
        auto* r = p.state.getParameter (id);
        if (r == nullptr) { std::printf ("no parameter %s\n", id.toRawUTF8()); ++failures; return; }
        r->setValueNotifyingHost (r->convertTo0to1 (v));
    }
    void set (int s, int param, float v) { set (TailsProcessor::sid (s, param), v); }
    /** Everything into one chain, split off, with no dry: measures one effect on its own. */
    void only (int s) { set ("g_split", 0); set ("g_focus", s == 0 ? 100.0f : -100.0f); }
    Stereo run (const Stereo& in)
    {
        Stereo out = in;
        juce::MidiBuffer midi;
        const int n = (int) in.l.size();
        for (int i = 0; i < n; i += block)
        {
            const int m = juce::jmin (block, n - i);
            float* ch[2] = { out.l.data() + i, out.r.data() + i };
            juce::AudioBuffer<float> buf (ch, 2, m);
            head.ppq += 0.0;
            p.processBlock (buf, midi);
            head.ppq += m * head.bpm / (60.0 * sr);
        }
        return out;
    }
    void syncIr() { p.syncImpulseResponses(); }
};

static Stereo silence (int n) { return { Sig ((size_t) n, 0.0f), Sig ((size_t) n, 0.0f) }; }
static Stereo impulse (int n, int at = 0) { auto s = silence (n); s.l[(size_t) at] = s.r[(size_t) at] = 1.0f; return s; }
static Stereo sine (int n, float hz, float amp = 0.25f, double sr = 48000.0)
{
    auto s = silence (n);
    for (int i = 0; i < n; ++i) s.l[(size_t) i] = s.r[(size_t) i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr);
    return s;
}
static Stereo noise (int n, float amp = 0.1f, unsigned seed = 3)
{
    std::mt19937 g (seed); std::normal_distribution<float> d;
    auto s = silence (n);
    for (int i = 0; i < n; ++i) { s.l[(size_t) i] = amp * d (g); s.r[(size_t) i] = amp * d (g); }
    return s;
}
static double rms (const Sig& x, int from, int to)
{
    double e = 0;
    to = juce::jmin (to, (int) x.size());
    for (int i = from; i < to; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (e / juce::jmax (1, to - from));
}
static double db (double v) { return 20.0 * std::log10 (v + 1.0e-12); }
static double peak (const Sig& x, int from = 0, int to = -1)
{
    double m = 0;
    if (to < 0 || to > (int) x.size()) to = (int) x.size();
    for (int i = from; i < to; ++i) m = juce::jmax (m, (double) std::abs (x[(size_t) i]));
    return m;
}
static int firstAbove (const Sig& x, float level, int from = 0)
{
    for (int i = from; i < (int) x.size(); ++i) if (std::abs (x[(size_t) i]) > level) return i;
    return -1;
}
/** Goertzel magnitude of one frequency, as a ratio to a full-scale sine. */
static double tone (const Sig& x, double hz, int from, int to, double sr = 48000.0)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr, c = 2.0 * std::cos (w);
    double a = 0, b = 0;
    for (int i = from; i < to; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (i - from) / (to - from));
        const double s = x[(size_t) i] * win + c * a - b;
        b = a; a = s;
    }
    return std::sqrt (a * a + b * b - c * a * b) * 4.0 / (to - from);
}

// ---------------------------------------------------------------- split and summing

static void testNull()
{
    Rig r;
    const int lat = r.p.getLatencySamples();
    CHECK (lat > 0 && lat < 1024, "latency reported");
    auto in = noise (48000);
    auto out = r.run (in);
    double worst = 0;
    for (int i = lat; i < 48000; ++i) worst = juce::jmax (worst, (double) std::abs (out.l[(size_t) i] - in.l[(size_t) (i - lat)]));
    CHECKV (db (worst) < -120.0, "no effects on: output is the input, delayed by the reported latency", db (worst));
}

static void testSplitSeparates()
{
    Rig r;
    r.set (0, sGain, 0);
    const int n = 96000, lat = r.p.getLatencySamples();
    // steady tones stay out of the transient stream
    {
        Rig t;
        t.set (1, sGain, -60);     // hear the transient stream alone
        auto out = t.run (sine (n, 440.0f));
        CHECKV (db (rms (out.l, 48000, n)) < -60.0, "steady tone: nothing in the transient stream", db (rms (out.l, 48000, n)));
    }
    // a click train lands in the transient stream, and only the click
    {
        Rig t;
        t.set ("g_source", 1);
        t.set (1, sGain, -60);
        auto in = silence (n);
        std::mt19937 g (5); std::normal_distribution<float> d;
        for (int hit = 24000; hit < n; hit += 24000)
            for (int i = 0; i < 400; ++i) in.l[(size_t) (hit + i)] = in.r[(size_t) (hit + i)] = 0.5f * d (g) * std::exp (-(float) i / 80.0f);
        auto out = t.run (in);
        const double inE = rms (in.l, 24000, 24240), outE = rms (out.l, 24000 + lat, 24240 + lat);
        CHECKV (db (outE / inE) > -3.0, "hit: its energy goes to the transient stream", db (outE / inE));
    }
    // Focus at the ends sends everything one way
    {
        Rig t;
        t.set ("g_focus", 100); t.set (1, sGain, -60);
        auto in = sine (n, 440.0f);
        auto out = t.run (in);
        CHECKV (std::abs (db (rms (out.l, 48000, n) / rms (in.l, 48000, n))) < 0.5, "Focus +100: a steady tone is all transient", db (rms (out.l, 48000, n) / rms (in.l, 48000, n)));
    }
    // both streams work on their own chain: the delay on the transient chain leaves a steady tone dry
    {
        Rig t;
        t.set (0, dOn, 1); t.set (0, dSync, 0); t.set (0, dTime, 300); t.set (0, dMix, 100); t.set (0, dFb, 0);
        auto in = sine (n, 440.0f);
        auto out = t.run (in);
        CHECKV (std::abs (db (rms (out.l, 48000, n) / rms (in.l, 48000, n))) < 0.3, "effect on the transient chain leaves the tonal part alone", db (rms (out.l, 48000, n) / rms (in.l, 48000, n)));
    }
}

// ---------------------------------------------------------------- effects

/** The convolver on its own: its impulse response must be the IR, 64 samples late, across the head, the bridge and tier B. */
static void testConvolverAlignment()
{
    constexpr int len = 12000;
    std::mt19937 g (11); std::normal_distribution<float> d;
    std::vector<float> irl ((size_t) len), irr ((size_t) len);
    for (int i = 0; i < len; ++i)
    {
        const float env = std::exp (-(float) i / 5000.0f);
        irl[(size_t) i] = env * d (g); irr[(size_t) i] = env * d (g);
    }
    auto response = [&] (float decay, float damp, int block, int at) {
        auto conv = std::make_unique<Convolver>();
        conv->prepare (48000.0);
        conv->installNow (buildIr (irl, irr, 48000.0, false));
        Stereo out = impulse (len + 4096, at);
        for (int i = 0; i < (int) out.l.size(); i += block)
            conv->process (out.l.data() + i, out.r.data() + i, juce::jmin (block, (int) out.l.size() - i), decay, damp);
        return out;
    };
    for (int block : { 32, 128, 480 })
        for (int at : { 0, 700, 1023, 1500 })
        {
            const auto o = response (1.0f, 0.0f, block, at);
            double worst = 0.0;
            for (int i = 0; i < len; ++i)
                worst = juce::jmax (worst, (double) std::abs (o.l[(size_t) (at + kBlockA + i)] - irl[(size_t) i]), (double) std::abs (o.r[(size_t) (at + kBlockA + i)] - irr[(size_t) i]));
            CHECKV (db (worst) < -90.0, "convolver: the output is the IR, 64 samples late, wherever the input falls", db (worst));
        }
    // Decay: the bridge plays like the first partition of tier B, and the next partition one step lower
    {
        const auto plain = response (1.0f, 0.0f, 64, 0), low = response (0.5f, 0.0f, 64, 0);
        auto ratio = [&] (int from, int to) {
            double a = 0.0, b = 0.0;
            for (int i = from; i < to; ++i) { a += (double) plain.l[(size_t) (kBlockA + i)] * plain.l[(size_t) (kBlockA + i)]; b += (double) low.l[(size_t) (kBlockA + i)] * low.l[(size_t) (kBlockA + i)]; }
            return std::sqrt (b / a);
        };
        const double t60 = estimateT60 (irl, irr, 48000.0);
        const double extra = 6.9078 / t60;
        CHECKV (std::abs (ratio (0, kHeadSamples) - 1.0) < 1.0e-3, "Decay leaves the first 960 samples alone", ratio (0, kHeadSamples));
        CHECKV (std::abs (ratio (kHeadSamples, kTailStart) / std::exp (-extra * kHeadSamples / 48000.0) - 1.0) < 1.0e-3, "Decay on the bridge", ratio (kHeadSamples, kTailStart));
        CHECKV (std::abs (ratio (kTailStart, kTailStart + kBlockB) / std::exp (-extra * kTailStart / 48000.0) - 1.0) < 1.0e-3, "Decay on tier B", ratio (kTailStart, kTailStart + kBlockB));
    }
}

static void testConvolution()
{
    Rig r;
    r.only (1);
    r.set (1, cOn, 1); r.set (1, cMix, 100);
    r.syncIr();
    auto dry = impulse (96000, 2000);
    r.run (silence (4800));
    auto out = r.run (dry);
    const int lat = r.p.getLatencySamples();
    const int onset = firstAbove (out.l, 1.0e-4f);
    CHECK (onset >= 2000 + lat + 64 && onset < 2000 + lat + 64 + 48, "the wet signal starts 64 samples after the dry path");
    CHECK (rms (out.l, 12000, 24000) > 1.0e-4, "plate tail present after 200 ms");
    double diff = 0;
    for (int i = 4000; i < 96000; ++i) diff += std::abs (out.l[(size_t) i] - out.r[(size_t) i]);
    CHECK (diff > 0.1, "two pickups: the plate is really stereo");

    // Decay shortens the tail without a rebuild, Damp darkens it
    auto tail = [&] (float decay, float damp) {
        Rig t; t.only (1); t.set (1, cOn, 1); t.set (1, cMix, 100); t.set (1, cDecay, decay); t.set (1, cDamp, damp);
        t.syncIr();
        t.run (silence (4800));
        return t.run (impulse (96000, 2000));
    };
    auto full = tail (100, 0), short_ = tail (25, 0), dark = tail (100, 100);
    const double a = rms (full.l, 24000, 48000), b = rms (short_.l, 24000, 48000);
    CHECKV (db (b / a) < -12.0, "Decay 25 % cuts the tail after half a second", db (b / a));
    auto high = [] (const Sig& x, int from, int to) { double e = 0; for (int i = from + 1; i < to; ++i) { const double d = x[(size_t) i] - x[(size_t) i - 1]; e += d * d; } return std::sqrt (e); };
    CHECKV (high (dark.l, 24000, 48000) < 0.5 * high (full.l, 24000, 48000), "Damp takes the highs out of the tail", high (dark.l, 24000, 48000) / high (full.l, 24000, 48000));

    // every factory response loads and has the level of the others
    double lo = 1.0e9, hi = 0;
    for (int ir = 0; ir < kUserIr; ++ir)
    {
        Rig t; t.only (1); t.set (1, cOn, 1); t.set (1, cMix, 100); t.set (1, cIr, (float) ir);
        t.syncIr();
        CHECK (! t.p.irFailed (1), "factory IR decodes");
        CHECK (t.p.irT60 (1) > 0.03f, "factory IR has a decay");
        auto o = t.run (noise (144000));
        const double level = rms (o.l, 96000, 144000);
        lo = juce::jmin (lo, level); hi = juce::jmax (hi, level);
    }
    CHECKV (db (hi / lo) < 6.0, "factory IRs are within 6 dB of each other on noise", db (hi / lo));

    // changing the response on the fly: no click, no gap
    {
        Rig t; t.only (1); t.set (1, cOn, 1); t.set (1, cMix, 100);
        t.syncIr();
        t.run (noise (48000));
        t.set (1, cIr, 4);
        t.syncIr();
        auto o = t.run (noise (24000, 0.1f, 9));
        double jump = 0;
        for (int i = 1; i < 24000; ++i) jump = juce::jmax (jump, (double) std::abs (o.l[(size_t) i] - o.l[(size_t) i - 1]));
        CHECKV (jump < 0.6, "IR swap: no discontinuity", jump);
        CHECK (rms (o.l, 12000, 24000) > 1.0e-3, "IR swap: the new response plays");
    }
}

static void testUserIr()
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("multitails_test_ir.wav");
    file.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (file), 44100.0, 2, 24, {}, 0));
        juce::AudioBuffer<float> b (2, 22050);
        b.clear();
        b.setSample (0, 1000, 1.0f); b.setSample (1, 2000, 1.0f);    // two clean echoes, different on each side
        w->writeFromAudioSampleBuffer (b, 0, 22050);
    }
    Rig r;
    r.only (1);
    r.set (1, cOn, 1); r.set (1, cMix, 100);
    r.p.loadUserIr (1, file);
    r.syncIr();
    CHECK (! r.p.irFailed (1), "user WAV loads");
    CHECK (r.p.irDisplayName (1) == "multitails_test_ir", "the display shows the file name");
    auto out = r.run (impulse (20000, 100));
    const int lat = r.p.getLatencySamples();
    auto where = [] (const Sig& x) { return (int) (std::max_element (x.begin(), x.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - x.begin()); };
    const int el = where (out.l), er = where (out.r);
    const int wantL = 100 + lat + 64 + juce::roundToInt (1000 * 48000.0 / 44100.0), wantR = 100 + lat + 64 + juce::roundToInt (2000 * 48000.0 / 44100.0);
    CHECK (std::abs (el - wantL) <= 2 && std::abs (er - wantR) <= 2, "user IR resampled to the session rate, with its timing");

    // saved in the session
    juce::MemoryBlock mb;
    r.p.getStateInformation (mb);
    Rig q;
    q.p.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (q.p.irDisplayName (1) == "multitails_test_ir", "the user IR comes back with the session");
    file.deleteFile();
    Rig m;
    m.p.loadUserIr (0, file);
    m.syncIr();
    CHECK (m.p.irFailed (0), "a missing file is reported, not played");
}

static void testTape()
{
    Rig r;
    r.only (1);
    r.set (1, tOn, 1); r.set (1, tMix, 100); r.set (1, tTime, 400); r.set (1, tMode, 0);
    r.set (1, tH1, 1); r.set (1, tH2, 1); r.set (1, tH3, 1); r.set (1, tH4, 1);
    r.set (1, tWow, 0); r.set (1, tFlut, 0); r.set (1, tSpread, 0);
    r.run (silence (9600));
    auto out = r.run (impulse (48000, 1000));
    const int lat = r.p.getLatencySamples();
    int found = 0;
    for (int h = 1; h <= 4; ++h)
    {
        const int want = 1000 + lat + (int) (0.1 * h * 48000.0);
        if (peak (out.l, want - 40, want + 80) > 0.02) ++found;
    }
    CHECK (found == 4, "four heads, evenly spaced behind the record head");
    CHECK (peak (out.l, 1000 + lat + 20200, 48000) < 0.002, "Echo mode: one pass, no repeats");

    auto head = [&] (bool h1, bool h4) {
        Rig t; t.only (1); t.set (1, tOn, 1); t.set (1, tMix, 100); t.set (1, tTime, 400); t.set (1, tMode, 0);
        t.set (1, tH1, h1 ? 1.0f : 0.0f); t.set (1, tH2, 0); t.set (1, tH3, 0); t.set (1, tH4, h4 ? 1.0f : 0.0f); t.set (1, tWow, 0); t.set (1, tFlut, 0);
        t.run (silence (9600));
        return t.run (impulse (48000, 1000));
    };
    auto only1 = head (true, false);
    CHECK (peak (only1.l, 1000 + lat + 4700, 1000 + lat + 5100) > 0.02 && peak (only1.l, 1000 + lat + 19000, 1000 + lat + 20000) < 0.002, "a head switched off stays silent");

    // Repeat recirculates, and past 100 % the valve holds it instead of letting it run away
    Rig fb;
    fb.only (1); fb.set (1, tOn, 1); fb.set (1, tMix, 100); fb.set (1, tTime, 200); fb.set (1, tMode, 1); fb.set (1, tFb, 115); fb.set (1, tH1, 0); fb.set (1, tH2, 0); fb.set (1, tH3, 0); fb.set (1, tH4, 1);
    auto long1 = fb.run (noise (9600, 0.3f));
    auto o = fb.run (silence (480000));
    CHECK (rms (o.l, 400000, 480000) > 1.0e-3, "Swell 115 %: self-oscillates");
    CHECKV (peak (o.l) < 4.0, "Swell 115 %: stays bounded", peak (o.l));

    // the wow bends the pitch of a held tone
    Rig w;
    w.only (1); w.set (1, tOn, 1); w.set (1, tMix, 100); w.set (1, tMode, 0); w.set (1, tH1, 0); w.set (1, tH2, 0); w.set (1, tH3, 0); w.set (1, tH4, 1); w.set (1, tWow, 100); w.set (1, tFlut, 100); w.set (1, tDrive, 0);
    auto bent = w.run (sine (96000, 1000.0f));
    const double on = tone (bent.l, 1000.0, 48000, 96000);
    Rig w0;
    w0.only (1); w0.set (1, tOn, 1); w0.set (1, tMix, 100); w0.set (1, tMode, 0); w0.set (1, tH1, 0); w0.set (1, tH2, 0); w0.set (1, tH3, 0); w0.set (1, tH4, 1); w0.set (1, tWow, 0); w0.set (1, tFlut, 0); w0.set (1, tDrive, 0);
    auto clean = w0.run (sine (96000, 1000.0f));
    CHECKV (on < tone (clean.l, 1000.0, 48000, 96000) * 0.6, "wow and flutter spread a steady tone", on / tone (clean.l, 1000.0, 48000, 96000));
}

static void testDelay()
{
    const int bpmSamples = (int) (0.75 * 60.0 / 120.0 * 48000.0);    // dotted eighth at 120: 375 ms... of a beat of 500 ms
    (void) bpmSamples;
    Rig r;
    r.only (1);
    r.set (1, dOn, 1); r.set (1, dMix, 100); r.set (1, dFb, 50); r.set (1, dSync, 1); r.set (1, dNote, (float) kDiv14); r.set (1, dAnalog, 0);
    r.run (silence (9600));
    auto out = r.run (impulse (96000, 500));
    const int lat = r.p.getLatencySamples();
    const int first = firstAbove (out.l, 0.3f);
    CHECK (std::abs (first - (500 + lat + 24000)) <= 2, "synced to the host: a quarter at 120 BPM is 500 ms");
    CHECKV (std::abs (db (peak (out.l, 500 + lat + 47000, 500 + lat + 49000) / peak (out.l, first - 10, first + 10)) + 6.0) < 0.5, "feedback 50 %: the second repeat is 6 dB down", db (peak (out.l, 500 + lat + 47000, 500 + lat + 49000)));

    // free time
    Rig f;
    f.only (1); f.set (1, dOn, 1); f.set (1, dMix, 100); f.set (1, dFb, 0); f.set (1, dSync, 0); f.set (1, dTime, 1000); f.set (1, dAnalog, 0);
    f.run (silence (24000));
    auto o = f.run (impulse (96000, 0));
    CHECK (std::abs (firstAbove (o.l, 0.3f) - (lat + 48000)) <= 2, "free time: 1000 ms");

    // ping pong goes from one side to the other
    Rig p;
    p.only (1); p.set (1, dOn, 1); p.set (1, dMix, 100); p.set (1, dFb, 80); p.set (1, dSync, 0); p.set (1, dTime, 100); p.set (1, dPing, 1); p.set (1, dAnalog, 0);
    p.run (silence (9600));
    auto pp = p.run (impulse (48000, 0));
    const int t1 = lat + 4800, t2 = lat + 9600;
    CHECK (peak (pp.l, t1 - 20, t1 + 20) > 0.3 && peak (pp.r, t1 - 20, t1 + 20) < 0.01, "ping pong: first repeat on the left");
    CHECK (peak (pp.r, t2 - 20, t2 + 20) > 0.2 && peak (pp.l, t2 - 20, t2 + 20) < 0.01, "ping pong: second repeat on the right");

    // filters in the repeats
    auto filtered = [&] (float hz) {
        Rig t; t.only (1); t.set (1, dOn, 1); t.set (1, dMix, 100); t.set (1, dFb, 0); t.set (1, dSync, 0); t.set (1, dTime, 10); t.set (1, dAnalog, 0); t.set (1, dLp, 1000);
        auto x = t.run (sine (24000, hz));
        return rms (x.l, 12000, 24000);
    };
    CHECKV (db (filtered (8000.0f) / filtered (200.0f)) < -24.0, "Low Pass 1 kHz takes down 8 kHz", db (filtered (8000.0f) / filtered (200.0f)));

    // Feedback 150 %: loud but bounded
    Rig hot;
    hot.only (1); hot.set (1, dOn, 1); hot.set (1, dMix, 100); hot.set (1, dFb, 150); hot.set (1, dSync, 0); hot.set (1, dTime, 50); hot.set (1, dAnalog, 0);
    hot.run (noise (4800, 0.3f));
    auto h = hot.run (silence (240000));
    CHECKV (peak (h.l) < 3.5, "feedback 150 %: stays bounded", peak (h.l));

    // the four analog grades and LoFi all narrow the band a little more
    auto bright = [&] (float analog, bool lofi) {
        Rig t; t.only (1); t.set (1, dOn, 1); t.set (1, dMix, 100); t.set (1, dFb, 0); t.set (1, dSync, 0); t.set (1, dTime, 10); t.set (1, dAnalog, analog); t.set (1, dLofi, lofi ? 1.0f : 0.0f);
        auto x = t.run (sine (24000, 9000.0f, 0.1f));
        return rms (x.l, 12000, 24000);
    };
    double last = bright (0, false) * 1.01;
    bool falling = true;
    for (int a = 1; a <= 4; ++a) { const double v = bright ((float) a, false); falling = falling && v < last; last = v; }
    CHECK (falling, "Analog 1 to 4: each darker than the one before");
    CHECK (bright (0, true) < 0.2 * bright (0, false), "LoFi: narrow band");
}

static void testChorus()
{
    Rig r;
    r.only (1);
    r.set (1, hOn, 1); r.set (1, hMix, 100); r.set (1, hRate, 2.0f); r.set (1, hDepth, 100); r.set (1, hTime, 6); r.set (1, hMode, 0); r.set (1, hGrit, 0);
    auto out = r.run (sine (96000, 440.0f));
    const double centre = tone (out.l, 440.0, 48000, 96000), ref = 0.25;
    CHECKV (centre < ref * 0.7, "the LFO sweeps the pitch of a steady tone", centre / ref);
    double diff = 0;
    for (int i = 48000; i < 96000; ++i) diff += std::abs (out.l[(size_t) i] - out.r[(size_t) i]);
    CHECK (diff < 1.0e-3, "Mono: both sides identical, like the pedal");

    Rig s;
    s.only (1); s.set (1, hOn, 1); s.set (1, hMix, 100); s.set (1, hMode, 1); s.set (1, hGrit, 0);
    auto st = s.run (sine (96000, 440.0f));
    double diff2 = 0;
    for (int i = 48000; i < 96000; ++i) diff2 += std::abs (st.l[(size_t) i] - st.r[(size_t) i]);
    CHECK (diff2 > 10.0, "Stereo: opposite LFO on the two sides");

    // the BBD band: about 6.6 kHz at the top
    auto band = [] (float hz) {
        Rig t; t.only (1); t.set (1, hOn, 1); t.set (1, hMix, 100); t.set (1, hDepth, 0); t.set (1, hGrit, 0); t.set (1, hMode, 1);
        auto x = t.run (sine (48000, hz, 0.1f));
        return rms (x.l, 24000, 48000);
    };
    CHECKV (db (band (14000.0f) / band (1000.0f)) < -20.0, "wet path stops above the Tone frequency", db (band (14000.0f) / band (1000.0f)));
}

/** Strongest frequency in the amplitude envelope of a signal, between lo and hi Hz. */
static double modulationHz (const Sig& x, int from, int to, double lo = 0.3, double hi = 12.0, double sr = 48000.0)
{
    Sig env;
    const int hop = 240;
    for (int i = from; i + hop < to; i += hop) env.push_back ((float) rms (x, i, i + hop));
    double mean = 0;
    for (auto v : env) mean += v;
    mean /= (double) env.size();
    double best = 0, bestHz = 0;
    for (double hz = lo; hz < hi; hz += 0.01)
    {
        double a = 0, b = 0;
        for (size_t i = 0; i < env.size(); ++i)
        {
            const double t = (double) i * hop / sr;
            a += (env[i] - mean) * std::cos (6.283185307 * hz * t);
            b += (env[i] - mean) * std::sin (6.283185307 * hz * t);
        }
        if (a * a + b * b > best) { best = a * a + b * b; bestHz = hz; }
    }
    return bestHz;
}

static void testRotary()
{
    auto spin = [] (int speed) {
        Rig t; t.only (1); t.set (1, rOn, 1); t.set (1, rMix, 100); t.set (1, rSpeed, (float) speed); t.set (1, rDrive, 0); t.set (1, rBal, 0);
        return t;
    };
    (void) spin;
    {
        Rig t; t.only (1); t.set (1, rOn, 1); t.set (1, rMix, 100); t.set (1, rSpeed, 2); t.set (1, rDrive, 0);
        auto o = t.run (sine (144000, 3000.0f));
        const double hz = modulationHz (o.l, 72000, 144000);
        CHECKV (std::abs (hz - 6.67) < 0.4, "tremolo: horn at 400 rpm", hz);
    }
    {
        Rig t; t.only (1); t.set (1, rOn, 1); t.set (1, rMix, 100); t.set (1, rSpeed, 1); t.set (1, rDrive, 0);
        auto o = t.run (sine (480000, 3000.0f));
        const double hz = modulationHz (o.l, 96000, 480000);
        CHECKV (std::abs (hz - 0.83) < 0.08, "chorale: horn at 50 rpm", hz);
    }
    {   // the drum, below the crossover
        Rig t; t.only (1); t.set (1, rOn, 1); t.set (1, rMix, 100); t.set (1, rSpeed, 2); t.set (1, rDrive, 0); t.set (1, rAccel, 25);
        auto o = t.run (sine (192000, 150.0f));
        const double hz = modulationHz (o.l, 96000, 192000);
        CHECKV (std::abs (hz - 5.67) < 0.4, "tremolo: drum at 340 rpm", hz);
    }
    {   // inertia: the horn does not jump to tremolo
        Rig t; t.only (1); t.set (1, rOn, 1); t.set (1, rMix, 100); t.set (1, rSpeed, 1);
        t.run (silence (9600));
        t.set (1, rSpeed, 2);
        t.run (sine (4800, 3000.0f));
        const float early = t.p.engine->ui.hornHz[1].load();
        t.run (sine (144000, 3000.0f));
        const float late = t.p.engine->ui.hornHz[1].load();
        CHECK (early < 2.0f && late > 6.3f, "rotors have inertia");
    }
    {   // the two microphones hear it at different moments
        Rig t; t.only (1); t.set (1, rOn, 1); t.set (1, rMix, 100); t.set (1, rSpeed, 2); t.set (1, rDrive, 0); t.set (1, rMic, 180);
        auto o = t.run (sine (96000, 3000.0f));
        double corr = 0, a = 0, b = 0;
        Sig el, er;
        for (int i = 48000; i + 240 < 96000; i += 240) { el.push_back ((float) rms (o.l, i, i + 240)); er.push_back ((float) rms (o.r, i, i + 240)); }
        double ml = 0, mr = 0;
        for (size_t i = 0; i < el.size(); ++i) { ml += el[i]; mr += er[i]; }
        ml /= (double) el.size(); mr /= (double) er.size();
        for (size_t i = 0; i < el.size(); ++i) { corr += (el[i] - ml) * (er[i] - mr); a += (el[i] - ml) * (el[i] - ml); b += (er[i] - mr) * (er[i] - mr); }
        CHECKV (corr / std::sqrt (a * b + 1.0e-12) < -0.5, "mics at 180 degrees: opposite amplitude", corr / std::sqrt (a * b + 1.0e-12));
    }
}

static void testFilters()
{
    auto level = [] (std::function<void (Rig&)> setup, float hz) {
        Rig t; t.only (1); t.set (1, fOn, 1); t.set (1, fMix, 100); t.set (1, fHpOn, 0); t.set (1, fLpOn, 0);
        setup (t);
        auto x = t.run (sine (48000, hz, 0.1f));
        return rms (x.l, 24000, 48000) / (0.1 * 0.7071);
    };
    auto hp = [] (Rig& t) { t.set (1, fHpOn, 1); t.set (1, fHpF, 1000); t.set (1, fHpQ, 0); };
    auto lp = [] (Rig& t) { t.set (1, fLpOn, 1); t.set (1, fLpF, 1000); t.set (1, fLpQ, 0); };
    auto bp = [] (Rig& t) { t.set (1, fBpOn, 1); t.set (1, fBpF, 1000); t.set (1, fBpQ, 4); t.set (1, fBpM, 100); };
    CHECKV (db (level (hp, 100)) < -30.0, "HPF 1 kHz at 12 dB: 100 Hz down by more than 30 dB", db (level (hp, 100)));
    CHECKV (std::abs (db (level (hp, 10000))) < 1.0, "HPF: passband flat", db (level (hp, 10000)));
    CHECKV (db (level (lp, 10000)) < -30.0, "LPF 1 kHz at 12 dB: 10 kHz down by more than 30 dB", db (level (lp, 10000)));
    CHECKV (std::abs (db (level (lp, 100))) < 1.0, "LPF: passband flat", db (level (lp, 100)));
    CHECKV (std::abs (db (level (bp, 1000))) < 1.0, "BPF: unity at the centre", db (level (bp, 1000)));
    CHECKV (db (level (bp, 100)) < -20.0 && db (level (bp, 10000)) < -20.0, "BPF: both skirts down", db (level (bp, 100)));
    auto hp24 = [] (Rig& t) { t.set (1, fHpOn, 1); t.set (1, fHpF, 1000); t.set (1, fHpQ, 0); t.set (1, fHpS, 1); };
    CHECKV (db (level (hp24, 100)) < -60.0, "HPF at 24 dB: twice the slope", db (level (hp24, 100)));
    auto reso = [] (Rig& t) { t.set (1, fLpOn, 1); t.set (1, fLpF, 1000); t.set (1, fLpQ, 80); };
    CHECKV (db (level (reso, 1000)) > 12.0, "Reso lifts the corner", db (level (reso, 1000)));
}

// ---------------------------------------------------------------- chain

static void testChainOrder()
{
    // tape after a filter, or a filter after tape: the same two blocks, different results. Here: a saturator-free check
    // with the delay and a low pass on the repeats would hide it, so use chorus then filter with mix.
    auto render = [] (std::vector<int> first) {
        Rig t; t.only (1);
        int o[kNumFx], i = 0, used = 0;
        for (int f : first) { o[i++] = f; used |= 1 << f; }
        for (int f = 0; f < kNumFx; ++f) if ((used & (1 << f)) == 0) o[i++] = f;
        t.p.setOrder (1, o);
        t.set (1, dOn, 1); t.set (1, dMix, 50); t.set (1, dFb, 0); t.set (1, dSync, 0); t.set (1, dTime, 200); t.set (1, dAnalog, 0);
        t.set (1, tOn, 1); t.set (1, tMix, 50); t.set (1, tMode, 0); t.set (1, tTime, 120); t.set (1, tDrive, 100); t.set (1, tWow, 0); t.set (1, tFlut, 0);
        return t.run (noise (48000, 0.3f));
    };
    auto a = render ({ fxDelay, fxTape }), b = render ({ fxTape, fxDelay });
    double d = 0;
    for (int i = 24000; i < 48000; ++i) d += std::abs (a.l[(size_t) i] - b.l[(size_t) i]);
    CHECKV (d > 100.0, "the order of the chain changes the sound", d);

    // reordering while audio plays: only a short dip, no click
    Rig t;
    t.only (1);
    t.run (sine (24000, 200.0f));
    int o[kNumFx] = { 5, 4, 3, 2, 1, 0 };
    t.p.setOrder (1, o);
    auto x = t.run (sine (24000, 200.0f));
    double jump = 0;
    for (int i = 1; i < 24000; ++i) jump = juce::jmax (jump, (double) std::abs (x.l[(size_t) i] - x.l[(size_t) i - 1]));
    CHECKV (jump < 0.03, "new order: no click", jump);
    int back[kNumFx];
    t.p.getOrder (1, back);
    CHECK (back[0] == 5 && back[5] == 0, "order round trip through the parameter");

    // tails ring on after the effect is switched off
    Rig r;
    r.only (1);
    r.set (1, dOn, 1); r.set (1, dMix, 100); r.set (1, dFb, 70); r.set (1, dSync, 0); r.set (1, dTime, 250); r.set (1, dAnalog, 0);
    r.run (impulse (9600, 100));
    r.set (1, dOn, 0);
    auto after = r.run (silence (48000));
    CHECK (peak (after.l, 9600, 48000) > 0.05, "Tails on: the repeats go on after the effect is off");
    Rig c;
    c.only (1);
    c.set (1, sTails, 0);
    c.set (1, dOn, 1); c.set (1, dMix, 100); c.set (1, dFb, 70); c.set (1, dSync, 0); c.set (1, dTime, 250); c.set (1, dAnalog, 0);
    c.run (impulse (9600, 100));
    c.set (1, dOn, 0);
    auto cut = c.run (silence (48000));
    CHECK (peak (cut.l, 960, 48000) < 1.0e-5, "Tails off: the repeats stop with the effect");
    // and a switched-off chain costs nothing once it is quiet: the output is exact again
    Rig q;
    q.only (1);
    auto in = noise (24000);
    auto out = q.run (in);
    const int lat = q.p.getLatencySamples();
    double worst = 0;
    for (int i = lat; i < 24000; ++i) worst = juce::jmax (worst, (double) std::abs (out.l[(size_t) i] - in.l[(size_t) (i - lat)]));
    CHECKV (db (worst) < -100.0, "all effects off: transparent", db (worst));
}

// ---------------------------------------------------------------- sequencer

static void laneSetup (Rig& r, int l, int destIndex, int target, int rate, int len, float amt)
{
    r.set (TailsProcessor::lid (l, lOn), 1);
    r.set (TailsProcessor::lid (l, lDest), (float) destIndex);
    r.set (TailsProcessor::lid (l, lTarget), (float) target);
    r.set (TailsProcessor::lid (l, lRate), (float) rate);
    r.set (TailsProcessor::lid (l, lLen), (float) len);
    r.set (TailsProcessor::lid (l, lAmt), amt);
}

static int destOf (int param)
{
    const auto& t = DestTable::get();
    for (int i = 0; i < t.count; ++i) if (t.param[(size_t) i] == param) return kNumGlobalDest + i;
    return -1;
}

/** The sequencer gates a filter's on switch: when a step is on the sine is filtered away. */
static Stereo gatedFilter (int blockSize, float bpm = 120.0f, bool freeSync = false)
{
    Rig r (blockSize);
    r.only (1);
    r.head.bpm = bpm;
    r.set (1, fMix, 100); r.set (1, fHpOn, 1); r.set (1, fHpF, 20000); r.set (1, fHpQ, 0); r.set (1, fHpS, 1); r.set (1, fLpOn, 0);
    laneSetup (r, 0, destOf (fOn), 1, kDiv14, 4, 100);
    auto& pat = r.p.engine->patterns[0];
    pat.step[0] = 1.0f; pat.step[1] = -1.0f; pat.step[2] = -1.0f; pat.step[3] = 1.0f;
    if (freeSync) { r.set ("g_sync", 1); r.set ("g_bpm", bpm); r.set ("g_run", 1); r.head.playing = false; }
    return r.run (sine (192000, 200.0f));
}

static void testSequencer()
{
    const auto a = gatedFilter (512);
    const int lat = 511;
    // at 120 BPM a quarter is 24000 samples: on, off, off, on
    auto level = [&] (const Stereo& s, int step) { return rms (s.l, step * 24000 + 6000 + lat, step * 24000 + 18000 + lat); };
    CHECK (level (a, 0) < 0.02 && level (a, 1) > 0.1 && level (a, 2) > 0.1 && level (a, 3) < 0.02, "a lane switches an effect on the beat");
    CHECK (level (a, 4) < 0.02 && level (a, 5) > 0.1, "the pattern loops at its Length");

    // independent of the buffer size
    for (int b : { 64, 128, 1024, 2048 })
    {
        const auto o = gatedFilter (b);
        double worst = 0;
        for (size_t i = 0; i < 192000; ++i) worst = juce::jmax (worst, (double) std::abs (o.l[i] - a.l[i]));
        CHECKV (worst < 1.0e-3, "same output with any buffer size", worst);
    }

    // follows the tempo
    const auto fast = gatedFilter (512, 240.0f);
    CHECK (rms (fast.l, 3000 + lat, 9000 + lat) < 0.02 && rms (fast.l, 15000 + lat, 21000 + lat) > 0.1, "DAW sync: follows the host tempo");

    // free clock, with the host stopped
    const auto free = gatedFilter (512, 120.0f, true);
    CHECK (level (free, 0) < 0.02 && level (free, 1) > 0.1, "FREE: runs on its own clock with the host stopped");

    // host stopped, DAW sync: lanes hold
    {
        Rig r;
        r.only (1); r.head.playing = false;
        laneSetup (r, 0, destOf (fOn), 1, kDiv14, 2, 100);
        r.p.engine->patterns[0].step[0] = 1.0f; r.p.engine->patterns[0].step[1] = -1.0f;
        r.run (silence (48000));
        CHECK (r.p.engine->ui.pos[0].load() == 0, "DAW sync with the host stopped: the lane holds");
    }

    // continuous destination, Amount and sign
    {
        Rig r;
        r.only (1);
        r.set (1, fOn, 1); r.set (1, fLpOn, 1); r.set (1, fHpOn, 0); r.set (1, fLpF, 800); r.set (1, fLpQ, 0);
        laneSetup (r, 0, destOf (fLpF), 1, kDiv14, 2, 100);
        r.p.engine->patterns[0].step[0] = 1.0f; r.p.engine->patterns[0].step[1] = -1.0f;
        auto o = r.run (sine (96000, 4000.0f));
        const double hi = rms (o.l, 6000 + lat, 18000 + lat), lo = rms (o.l, 30000 + lat, 42000 + lat);
        CHECKV (db (hi / lo) > 30.0, "a lane opens and closes a filter", db (hi / lo));
    }

    // Slew makes the step a ramp
    {
        auto steps = [] (float slew) {
            Rig r;
            r.only (1);
            laneSetup (r, 0, 0, 2, kDiv14, 2, 100);
            r.set (TailsProcessor::lid (0, lDest), (float) gdTonalGain);
            r.set (TailsProcessor::lid (0, lSlew), slew);
            r.set (TailsProcessor::lid (0, lAmt), 60);
            r.p.engine->patterns[0].step[0] = 0.0f; r.p.engine->patterns[0].step[1] = -1.0f;
            auto o = r.run (sine (72000, 500.0f));
            double jump = 0;
            Sig env;
            for (int i = 0; i + 240 < 72000; i += 240) env.push_back ((float) rms (o.l, i, i + 240));
            for (size_t i = 4; i < env.size(); ++i) jump = juce::jmax (jump, (double) std::abs (env[i] - env[i - 1]));
            return jump;
        };
        CHECK (steps (100) < steps (0) * 0.5, "Slew softens the step");
    }

    // Chance and the patterns do not depend on anything but the position
    {
        Rig r;
        r.only (1);
        laneSetup (r, 0, destOf (fOn), 1, kDiv116, 8, 100);
        r.set (TailsProcessor::lid (0, lDir), 3);
        for (int i = 0; i < 8; ++i) r.p.engine->patterns[0].step[(size_t) i] = i % 2 == 0 ? 1.0f : -1.0f;
        r.run (silence (48000));
        CHECK (juce::isPositiveAndBelow (r.p.engine->ui.pos[0].load(), 8), "Random direction stays inside the Length");
    }

    // global destinations: Focus moved by a lane
    {
        Rig r;
        r.set (1, sGain, -60);
        laneSetup (r, 0, gdFocus, 2, kDiv14, 2, 100);
        r.p.engine->patterns[0].step[0] = 1.0f; r.p.engine->patterns[0].step[1] = -1.0f;
        auto o = r.run (sine (96000, 440.0f));
        CHECKV (rms (o.l, 6000 + lat, 18000 + lat) > 20.0 * rms (o.l, 30000 + lat, 42000 + lat), "a lane moves the Split Focus", rms (o.l, 6000 + lat, 18000 + lat));
    }

    // polymeter: two lanes of different lengths keep their own position
    {
        Rig r;
        laneSetup (r, 0, destOf (fOn), 1, kDiv14, 3, 100);
        laneSetup (r, 1, destOf (dOn), 1, kDiv14, 5, 100);
        r.run (silence (7 * 24000 + 1000));
        CHECK (r.p.engine->ui.pos[0].load() == 1 && r.p.engine->ui.pos[1].load() == 2, "lanes of different lengths drift against each other");
    }

    // reset
    {
        Rig r;
        laneSetup (r, 0, destOf (fOn), 1, kDiv14, 16, 100);
        r.run (silence (5 * 24000 + 1000));
        CHECK (r.p.engine->ui.pos[0].load() == 5, "lane position before reset");
        r.set ("g_reset", 1);
        r.run (silence (1024));
        r.set ("g_reset", 0);
        CHECK (r.p.engine->ui.pos[0].load() == 0, "Sequencer Reset goes back to step 1");
    }
}

// ---------------------------------------------------------------- MIDI map

static void sendMidi (Rig& r, const juce::MidiMessage& m)
{
    juce::MidiBuffer midi;
    midi.addEvent (m, 0);
    juce::AudioBuffer<float> b (2, 64);
    b.clear();
    r.p.processBlock (b, midi);
    r.head.ppq += 64.0 / 24000.0;
}

static void testMidiMap()
{
    Rig r;
    auto& map = r.p.midiMap;
    const int mix = r.p.indexOfParam (TailsProcessor::sid (1, cMix));
    CHECK (mix >= 0, "parameters have an index");

    // learn from a CC
    map.learnParam = mix;
    sendMidi (r, juce::MidiMessage::controllerEvent (3, 74, 127));
    CHECK (map.learnParam.load() == -1, "learn disarms after one message");
    const int slot = map.slotOf (mix);
    CHECK (slot >= 0 && map.slots[(size_t) slot].source.load() == msCC && map.slots[(size_t) slot].number.load() == 74 && map.slots[(size_t) slot].channel.load() == 3, "learn stores CC and channel");
    CHECKV (r.p.sf (1, cMix) > 99.0f, "the value reaches the engine in the same block", r.p.sf (1, cMix));

    sendMidi (r, juce::MidiMessage::controllerEvent (3, 74, 0));
    CHECKV (r.p.sf (1, cMix) < 1.0f, "CC 0 goes to the minimum", r.p.sf (1, cMix));
    sendMidi (r, juce::MidiMessage::controllerEvent (4, 74, 127));
    CHECKV (r.p.sf (1, cMix) < 1.0f, "another channel is ignored", r.p.sf (1, cMix));
    sendMidi (r, juce::MidiMessage::controllerEvent (3, 75, 127));
    CHECKV (r.p.sf (1, cMix) < 1.0f, "another CC is ignored", r.p.sf (1, cMix));

    // the host parameter catches up on the message thread
    sendMidi (r, juce::MidiMessage::controllerEvent (3, 74, 64));
    r.p.flushMidiMap();
    CHECKV (std::abs (r.p.state.getParameter (TailsProcessor::sid (1, cMix))->getValue() - 0.504f) < 0.01f, "the host sees the change", r.p.state.getParameter (TailsProcessor::sid (1, cMix))->getValue());

    // range
    auto& s = map.slots[(size_t) slot];
    s.lo = 0.2f; s.hi = 0.4f;
    sendMidi (r, juce::MidiMessage::controllerEvent (3, 74, 127));
    CHECKV (std::abs (r.p.sf (1, cMix) - 40.0f) < 0.5f, "Max limits the range", r.p.sf (1, cMix));
    s.lo = 1.0f; s.hi = 0.0f;
    sendMidi (r, juce::MidiMessage::controllerEvent (3, 74, 127));
    CHECKV (r.p.sf (1, cMix) < 1.0f, "Min above Max inverts", r.p.sf (1, cMix));

    // note: momentary and toggle, on a switch
    const int on = r.p.indexOfParam (TailsProcessor::sid (0, dOn));
    map.learnParam = on;
    sendMidi (r, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    CHECK (r.p.sf (0, dOn) > 0.5f, "note learn: momentary, down");
    sendMidi (r, juce::MidiMessage::noteOff (1, 60));
    CHECK (r.p.sf (0, dOn) < 0.5f, "note learn: momentary, up");
    map.slots[(size_t) map.slotOf (on)].mode = mmToggle;
    sendMidi (r, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    sendMidi (r, juce::MidiMessage::noteOff (1, 60));
    CHECK (r.p.sf (0, dOn) > 0.5f, "toggle: first press on");
    sendMidi (r, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
    sendMidi (r, juce::MidiMessage::noteOff (1, 60));
    CHECK (r.p.sf (0, dOn) < 0.5f, "toggle: second press off");

    // relative encoder
    const int focus = r.p.indexOfParam ("g_focus");
    auto& rel = map.slots[(size_t) map.firstFree()];
    rel.param = focus; rel.number = 20; rel.channel = 0; rel.mode = mmRelative; rel.lo = 0.0f; rel.hi = 1.0f; rel.source = msCC;
    const float before = r.p.state.getRawParameterValue ("g_focus")->load();
    sendMidi (r, juce::MidiMessage::controllerEvent (1, 20, 10));
    CHECK (r.p.state.getRawParameterValue ("g_focus")->load() > before, "relative encoder: up");
    sendMidi (r, juce::MidiMessage::controllerEvent (1, 20, 118));
    sendMidi (r, juce::MidiMessage::controllerEvent (1, 20, 118));
    CHECK (r.p.state.getRawParameterValue ("g_focus")->load() < before, "relative encoder: down");

    // pitch bend and pressure as sources
    auto& bend = map.slots[(size_t) map.firstFree()];
    bend.param = r.p.indexOfParam ("g_smooth"); bend.channel = 0; bend.mode = mmAbsolute; bend.lo = 0.0f; bend.hi = 1.0f; bend.source = msBend;
    sendMidi (r, juce::MidiMessage::pitchWheel (1, 16383));
    CHECKV (r.p.state.getRawParameterValue ("g_smooth")->load() > 99.0f, "pitch bend as a source", r.p.state.getRawParameterValue ("g_smooth")->load());

    // the map is saved with the session, by parameter id
    juce::MemoryBlock mb;
    r.p.getStateInformation (mb);
    Rig q;
    q.p.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (q.p.midiMap.slotOf (on) >= 0 && q.p.midiMap.slotOf (mix) >= 0, "the MIDI map comes back with the session");
    sendMidi (q, juce::MidiMessage::controllerEvent (3, 74, 0));
    CHECKV (q.p.sf (1, cMix) > 99.0f, "the restored mapping works, with its range", q.p.sf (1, cMix));
}

// ---------------------------------------------------------------- state, presets, robustness

static void testState()
{
    Rig r;
    r.set (0, cDecay, 42); r.set (1, tTime, 512); r.set ("g_focus", 33);
    int o[kNumFx] = { 3, 1, 5, 0, 4, 2 };
    r.p.setOrder (1, o);
    r.p.engine->patterns[3].step[7] = 0.625f;
    juce::MemoryBlock mb;
    r.p.getStateInformation (mb);
    Rig q;
    q.p.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (std::abs (q.p.sf (0, cDecay) - 42.0f) < 0.01f && std::abs (q.p.sf (1, tTime) - 512.0f) < 0.5f, "parameters come back");
    int back[kNumFx];
    q.p.getOrder (1, back);
    CHECK (back[0] == 3 && back[2] == 5 && back[5] == 2, "chain order comes back");
    CHECK (std::abs (q.p.engine->patterns[3].step[7].load() - 0.625f) < 0.001f, "sequencer steps come back");

    juce::MemoryBlock two;
    q.p.getStateInformation (two);
    CHECK (mb.getSize() > 1000 && std::abs ((long) two.getSize() - (long) mb.getSize()) < 64, "state is stable when saved again");

    // every rank is a real order
    for (int rank = 0; rank < 720; ++rank)
    {
        int a[kNumFx];
        orderFromRank (rank, a);
        if (rankFromOrder (a) != rank) { CHECK (false, "rank round trip"); break; }
    }
    ++passes;

    // table and parameters agree
    for (int p = 0; p < kNumP; ++p)
    {
        const auto& d = streamParams()[p];
        auto* prm = r.p.state.getParameter (TailsProcessor::sid (0, p));
        CHECK (prm != nullptr, "every table entry is a parameter");
        if (d.type == T::Float)
        {
            const float v = d.lo + 0.37f * (d.hi - d.lo);
            CHECKV (std::abs (toNorm (d, skewOf (d), v) - prm->convertTo0to1 (v)) < 1.0e-3f, "engine and host agree on the knob curve", p);
        }
    }
}

static void testPresets()
{
    Rig r;
    for (int i = 0; i < r.p.presets.numFactory(); ++i)
    {
        r.p.presets.load (i);
        r.syncIr();
        auto o = r.run (noise (48000, 0.1f));
        const double pk = peak (o.l);
        char msg[128];
        std::snprintf (msg, sizeof (msg), "preset %s stays in range", r.p.getProgramName (i).toRawUTF8());
        CHECKV (pk < 2.5 && std::isfinite (pk), msg, pk);
        std::snprintf (msg, sizeof (msg), "preset %s makes sound", r.p.getProgramName (i).toRawUTF8());
        CHECK (rms (o.l, 24000, 48000) > 1.0e-3, msg);
    }
    r.p.presets.load (2);
    CHECK (r.p.lf (0, lOn) > 0.5f, "preset with lanes switches them on");
    r.p.presets.load (0);
    CHECK (r.p.lf (0, lOn) < 0.5f, "Init clears the lanes");
    CHECK (std::abs (r.p.engine->patterns[0].step[0].load()) < 1.0e-6f, "Init clears the steps");
}

static void testRobust()
{
    // NaN at the input must not stay in the feedback paths
    Rig r;
    r.set (0, dOn, 1); r.set (1, dOn, 1); r.set (1, tOn, 1); r.set (1, cOn, 1);
    r.syncIr();
    auto in = noise (9600);
    in.l[100] = std::numeric_limits<float>::quiet_NaN();
    r.run (in);
    auto o = r.run (noise (48000));
    bool finite = true;
    for (auto v : o.l) finite = finite && std::isfinite (v);
    CHECK (finite, "a NaN at the input does not stay in the feedback paths");

    // sample rates
    for (double sr : { 44100.0, 96000.0, 192000.0 })
    {
        Rig t (512, sr);
        CHECK (t.p.getLatencySamples() < (int) (sr * 0.015), "latency stays near 10 ms at any rate");
        t.only (1);
        t.set (1, dOn, 1); t.set (1, dMix, 100); t.set (1, dFb, 0); t.set (1, dSync, 0); t.set (1, dTime, 100); t.set (1, dAnalog, 0);
        t.run (silence (9600));
        auto x = t.run (impulse ((int) (sr * 0.5), 0));
        CHECK (std::abs (firstAbove (x.l, 0.3f) - (t.p.getLatencySamples() + (int) (sr * 0.1))) <= 3, "delay time in milliseconds at any sample rate");
    }

    // odd blocks, zero-length included
    {
        Rig t;
        t.set (1, dOn, 1);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> b (2, 7);
        b.clear();
        t.p.processBlock (b, midi);
        juce::AudioBuffer<float> e (2, 0);
        t.p.processBlock (e, midi);
        ++passes;
    }

    // bypass keeps the latency, and stays sample-aligned
    {
        Rig t;
        auto in2 = noise (24000);
        Stereo out = in2;
        juce::MidiBuffer midi;
        for (int i = 0; i < 24000; i += 512)
        {
            float* ch[2] = { out.l.data() + i, out.r.data() + i };
            juce::AudioBuffer<float> buf (ch, 2, juce::jmin (512, 24000 - i));
            t.p.processBlockBypassed (buf, midi);
        }
        const int lat = t.p.getLatencySamples();
        double worst = 0;
        for (int i = lat; i < 24000; ++i) worst = juce::jmax (worst, (double) std::abs (out.l[(size_t) i] - in2.l[(size_t) (i - lat)]));
        CHECKV (db (worst) < -100.0, "bypass: the input, delayed by the latency", db (worst));
    }

    // mono
    {
        auto proc = std::make_unique<TailsProcessor>();
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::mono()); l.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (proc->setBusesLayout (l), "mono layout accepted");
        proc->prepareToPlay (48000, 512);
        juce::AudioBuffer<float> b (1, 512);
        b.clear();
        juce::MidiBuffer midi;
        proc->processBlock (b, midi);
        ++passes;
    }
}

/** Renders a test phrase through a preset: drums and a chord, for listening and for spectrograms. */
static void runRender (const juce::String& file, int preset, double seconds)
{
    Rig r (256);
    r.head.bpm = 96;
    if (preset >= 0) r.p.presets.load (preset);
    r.syncIr();
    const int n = (int) (seconds * 48000.0);
    auto in = silence (n);
    std::mt19937 g (7); std::normal_distribution<float> d;
    const double beat = 60.0 / 96.0 * 48000.0, step = beat / 4.0;
    const int kick[16] = { 1,0,0,0, 0,0,1,0, 0,0,1,0, 0,0,0,0 }, snare[16] = { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,1 }, hat[16] = { 1,0,1,1, 1,0,1,0, 1,1,1,0, 1,0,1,1 };
    const float notes[4][3] = { { 110.0f, 164.81f, 246.94f }, { 98.0f, 146.83f, 220.0f }, { 87.31f, 130.81f, 196.0f }, { 98.0f, 164.81f, 233.08f } };
    for (int i = 0; i < n; ++i)
    {
        float x = 0;
        const double pos = i / step;
        const int s = (int) pos % 16;
        const float t = (float) ((pos - std::floor (pos)) * step);    // samples since the step
        if (kick[s]) x += 0.7f * std::exp (-t / 5000.0f) * (float) std::sin (6.2831853 * (46.0 * t / 48000.0 + 12.0 * (1.0 - std::exp (-t / 900.0f))));
        if (snare[s]) x += 0.35f * std::exp (-t / 2200.0f) * d (g) + 0.2f * std::exp (-t / 1600.0f) * (float) std::sin (6.2831853 * 190.0 * t / 48000.0);
        if (hat[s]) { static float last = 0; const float w = d (g); x += 0.2f * std::exp (-t / 420.0f) * (w - last); last = w; }
        const int bar = (int) (i / (beat * 4.0)) % 4;
        const float env = juce::jlimit (0.0f, 1.0f, (float) std::fmod (i, beat * 4.0) / 6000.0f);
        float pad = 0;
        for (float f : notes[bar]) pad += (float) std::sin (6.2831853 * f * i / 48000.0) + 0.3f * (float) std::sin (6.2831853 * f * 2.003 * i / 48000.0);
        x += 0.08f * env * pad;
        in.l[(size_t) i] = in.r[(size_t) i] = x * 0.8f;
    }
    auto out = r.run (in);
    juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (file);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), 48000.0, 2, 24, {}, 0));
    const float* ch[2] = { out.l.data(), out.r.data() };
    w->writeFromFloatArrays (ch, 2, n);
    std::printf ("%s: peak %.3f\n", r.p.getProgramName (juce::jmax (0, preset)).toRawUTF8(), juce::jmax (peak (out.l), peak (out.r)));
}

static void testRealtime()
{
    Rig r (256);
    r.p.presets.load (2);        // lanes running
    r.syncIr();
    for (int s = 0; s < 2; ++s)
    {
        r.set (s, cOn, 1); r.set (s, tOn, 1); r.set (s, dOn, 1); r.set (s, hOn, 1); r.set (s, rOn, 1); r.set (s, fOn, 1);
    }
    auto& map = r.p.midiMap;
    auto& slot = map.slots[0];
    slot.param = r.p.indexOfParam ("to_f_lpf"); slot.number = 74; slot.source = msCC;
    r.syncIr();

    Sig l (256), rr (256);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    int order[kNumFx] = { 5, 3, 1, 0, 2, 4 };
    const auto msg = juce::MidiMessage::controllerEvent (1, 74, 90);
    allocationsSeen = 0;
    for (int b = 0; b < 4000; ++b)
    {
        for (int i = 0; i < 256; ++i) { l[(size_t) i] = 0.1f * (float) std::sin (i * 0.1f); rr[(size_t) i] = l[(size_t) i]; }
        float* ch[2] = { l.data(), rr.data() };
        juce::AudioBuffer<float> buf (ch, 2, 256);
        midi.clear();
        midi.addEvent (msg, 10);
        if (b == 1000) r.p.setOrder (1, order);       // on the test thread, like a host would from its message thread
        if (b == 2000) r.set (1, cIr, 4);             // the worker thread builds this one
        watchAllocations = true;
        r.p.processBlock (buf, midi);
        watchAllocations = false;
        r.head.ppq += 256.0 * r.head.bpm / (60.0 * r.sr);
    }
    CHECKV (allocationsSeen.load() == 0, "no allocation on the audio thread, IR swap and new order included", allocationsSeen.load());
}

static void reportCost()
{
    // worst realistic case: all twelve effects on, both long IRs, while the sequencer runs
    Rig r (128);
    for (int s = 0; s < 2; ++s)
    {
        r.set (s, cOn, 1); r.set (s, tOn, 1); r.set (s, dOn, 1); r.set (s, hOn, 1); r.set (s, rOn, 1); r.set (s, fOn, 1);
        r.set (s, cIr, 16);  r.set (s, cDamp, 40);
    }
    r.set (TailsProcessor::lid (0, lOn), 1); r.set (TailsProcessor::lid (0, lDest), (float) destOf (fLpF));
    r.syncIr();
    auto in = noise (480000);
    juce::AudioBuffer<float> b (2, 128);
    juce::MidiBuffer midi;
    double sec = 0.0, peak = 0.0;
    for (int i = 0; i + 128 <= 480000; i += 128)
    {
        b.copyFrom (0, 0, in.l.data() + i, 128); b.copyFrom (1, 0, in.r.data() + i, 128);
        const auto t0 = std::chrono::steady_clock::now();
        r.p.processBlock (b, midi);
        const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        sec += used;
        if (i >= 9600) peak = juce::jmax (peak, used);   // the first blocks only warm the caches
    }
    std::printf ("CPU: all 12 effects with the 9.4 s IR on both chains: %.1f %% of one core at 48 kHz, slowest block %.2f ms of 2.67\n", sec / 10.0 * 100.0, peak * 1000.0);
}

// ---------------------------------------------------------------- interface, driven by hand

static juce::MouseEvent mouse (juce::Component& c, int x, int y, juce::ModifierKeys mods, int downX, int downY)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (src, { (float) x, (float) y }, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, { (float) downX, (float) downY }, now, 1, x != downX || y != downY);
}

static void testInterface()
{
    Rig r;
    const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier), right (juce::ModifierKeys::rightButtonModifier);
    tails::ChainView chain (r.p);
    chain.setBounds (0, 0, 824, 128);
    int selS = -1, selF = -1;
    chain.onSelect = [&] (int s, int f) { selS = s; selF = f; };

    auto press = [&] (int x, int y) { chain.mouseDown (mouse (chain, x, y, left, x, y)); chain.mouseUp (mouse (chain, x, y, {}, x, y)); };
    const int x3 = 98 + 3 * 121 + 60, row2 = 68 + 40;
    press (x3, row2);
    CHECK (selS == 1 && selF == fxChorus, "a click on a plate selects that effect in that row");
    CHECK (r.p.sf (1, hOn) < 0.5f, "a click on the body does not switch it");
    press (98 + 3 * 121 + 26, 68 + 14);
    CHECK (r.p.sf (1, hOn) > 0.5f, "a click on the LED switches it");

    // drag the fifth plate of the tonal row to the front
    const int from = 98 + 4 * 121 + 55;
    chain.mouseDown (mouse (chain, from, row2, left, from, row2));
    chain.mouseDrag (mouse (chain, 120, row2, left, from, row2));
    chain.mouseUp (mouse (chain, 120, row2, {}, from, row2));
    int order[kNumFx];
    r.p.getOrder (1, order);
    CHECK (order[0] == fxRotary && order[1] == fxConv && order[5] == fxFilter, "dragging a plate reorders the chain");
    r.p.getOrder (0, order);
    CHECK (order[0] == fxConv, "the other chain stays as it was");

    tails::LaneList lanes (r.p);
    lanes.setBounds (0, 0, 172, 168);
    int sel = -1;
    lanes.onSelect = [&] (int l) { sel = l; };
    lanes.mouseDown (mouse (lanes, 100, 45, left, 100, 45));
    CHECK (sel == 2, "a click selects a lane");
    lanes.mouseDown (mouse (lanes, 27, 45, left, 27, 45));
    CHECK (r.p.lf (2, lOn) > 0.5f, "a click on its LED switches the lane");

    tails::StepView steps (r.p);
    steps.setBounds (0, 0, 320, 100);
    steps.setLane (2);
    steps.mouseDown (mouse (steps, 15, 0, left, 15, 0));
    CHECK (r.p.engine->patterns[2].step[1].load() > 0.95f, "drawing at the top sets the step to the maximum");
    steps.mouseDrag (mouse (steps, 25, 100, left, 15, 0));
    CHECK (r.p.engine->patterns[2].step[2].load() < -0.95f, "dragging draws the next step");
    steps.mouseDown (mouse (steps, 15, 10, right, 15, 10));
    CHECK (std::abs (r.p.engine->patterns[2].step[1].load()) < 0.001f, "right click clears a step");
    r.set (TailsProcessor::lid (2, lDest), (float) destOf (fOn));
    steps.mouseDown (mouse (steps, 35, 30, left, 35, 30));
    CHECK (r.p.engine->patterns[2].step[3].load() > 0.99f, "a switch destination snaps to on");

    // the two long menus must keep their index order when they are folded into submenus
    {
        tails::Control dest (r.p.state, TailsProcessor::lid (0, lDest), "D", tails::Control::Kind::Menu);
        auto* c = dynamic_cast<juce::AudioParameterChoice*> (r.p.state.getParameter (TailsProcessor::lid (0, lDest)));
        CHECK (dest.combo.getNumItems() == c->choices.size(), "destination menu: nothing lost in the submenus");
        bool same = true;
        for (int i = 0; i < c->choices.size(); ++i)
            same = same && dest.combo.getItemId (i) == i + 1;
        CHECK (same, "destination menu: submenus keep the parameter's order");
        tails::Control ir (r.p.state, TailsProcessor::sid (0, cIr), "I", tails::Control::Kind::Menu);
        bool sameIr = ir.combo.getNumItems() == kNumIrs;
        for (int i = 0; i < kNumIrs; ++i) sameIr = sameIr && ir.combo.getItemId (i) == i + 1;
        CHECK (sameIr, "IR menu: submenus keep the parameter's order");
    }

    // every page of the editor builds, at the extremes of the window
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
    auto* te = dynamic_cast<TailsEditor*> (ed.get());
    for (int s = 0; s < kStreams; ++s)
        for (int f = 0; f < kNumFx; ++f) { te->selectFx (s, f); te->tickForSnapshot(); }
    for (int l = 0; l < kLanes; ++l) te->selectLane (l);
    te->showMidi (true); te->tickForSnapshot(); te->showMidi (false);
    te->setSize (768, 480); te->setSize (2560, 1600);
    ++passes;
}

// ---------------------------------------------------------------- snapshot

static void runSnap (const juce::String& file, int page, int stream, int fx, int preset, float scale)
{
    juce::ScopedJuceInitialiser_GUI gui;
    Rig r (128);
    if (preset >= 0) r.p.presets.load (preset);
    r.syncIr();
    // something to look at: hits over a chord
    auto in = silence (96000);
    std::mt19937 g (5); std::normal_distribution<float> d;
    for (int i = 0; i < 96000; ++i)
    {
        const int hit = i % 12000;
        const float x = 0.5f * std::exp (-(float) hit / 200.0f) * d (g) + 0.06f * (float) std::sin (i * 0.03f) + 0.05f * (float) std::sin (i * 0.0451f);
        in.l[(size_t) i] = in.r[(size_t) i] = x;
    }
    r.run (in);
    r.p.flushMidiMap();
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
    auto* te = dynamic_cast<TailsEditor*> (ed.get());
    te->selectFx (stream, fx);
    if (preset >= 0) te->selectLane (0);
    if (page == 1)
    {
        auto& m = r.p.midiMap;
        auto put = [&] (int i, int src, int num, const juce::String& id, int mode) {
            auto& s = m.slots[(size_t) i];
            s.number = num; s.param = r.p.indexOfParam (id); s.mode = mode; s.source = src;
        };
        put (0, msCC, 74, "to_f_lpf", mmAbsolute); put (1, msCC, 1, "g_focus", mmAbsolute); put (2, msNote, 36, "tr_d_on", mmMomentary);
        put (3, msNote, 37, "to_r_speed", mmToggle); put (4, msPressure, 0, "tr_c_decay", mmAbsolute);
        te->showMidi (true);
    }
    te->tickForSnapshot();
    te->setSize (juce::roundToInt (1280 * scale), juce::roundToInt (800 * scale));
    te->tickForSnapshot();
    auto img = te->createComponentSnapshot (te->getLocalBounds(), true, 1.0f);
    juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (file);
    f.deleteFile();
    juce::FileOutputStream o (f);
    juce::PNGImageFormat().writeImageToStream (img, o);
    ed.reset();
}

int main (int argc, char** argv)
{
    if (argc > 1 && juce::String (argv[1]) == "--snap")
    {
        auto arg = [&] (int i, int def) { return argc > i ? juce::String (argv[i]).getIntValue() : def; };
        runSnap (argc > 2 ? argv[2] : "snap_tails.png", arg (3, 0), arg (4, 0), arg (5, 0), arg (6, -1), argc > 7 ? juce::String (argv[7]).getFloatValue() : 2.0f);
        return 0;
    }
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 1 && juce::String (argv[1]) == "--render")
    {
        runRender (argv[2], juce::String (argv[3]).getIntValue(), argc > 4 ? juce::String (argv[4]).getDoubleValue() : 20.0);
        return 0;
    }
    using Fn = void (*)();
    const std::vector<std::pair<const char*, Fn>> tests = {
        { "null", testNull }, { "split", testSplitSeparates }, { "convolution", testConvolution }, { "convolver", testConvolverAlignment }, { "user ir", testUserIr },
        { "tape", testTape }, { "delay", testDelay }, { "chorus", testChorus }, { "rotary", testRotary }, { "filters", testFilters },
        { "chain", testChainOrder }, { "sequencer", testSequencer }, { "midi map", testMidiMap }, { "state", testState },
        { "presets", testPresets }, { "robustness", testRobust }, { "interface", testInterface }, { "real time", testRealtime } };
    for (auto& t : tests)
    {
        const int before = failures;
        t.second();
        std::printf ("%-12s %s\n", t.first, failures == before ? "ok" : "FAILED");
        std::fflush (stdout);
    }
    reportCost();
    std::printf ("%d checks, %d failed\n", passes + failures, failures);
    return failures == 0 ? 0 : 1;
}
