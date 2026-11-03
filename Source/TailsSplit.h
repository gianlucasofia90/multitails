#pragma once
// Splits the input into a transient and a tonal stream, in time and frequency, with an STFT: a bin goes to the transient
// stream when its energy jumps above its recent history, and stays there for the Trans Decay. The tonal stream is the input
// minus the transient stream, so with no effects the two always add back to the exact input. L and R share one mask,
// which keeps the stereo image still. The latency is one window.
#include "TailsCore.h"
#include "TailsConv.h"
#include <memory>
#include <vector>

namespace tails
{
class Splitter
{
public:
    struct Settings { bool on = true; float focus = 0.0f, smooth = 0.3f, decayMs = 40.0f; int source = 0; };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        n = 512;
        while (n * 1.4 < 512.0 * sr / 48000.0) n *= 2;         // the same window in milliseconds at 96 kHz and up
        hop = n / 4;
        fft = std::make_unique<RealFFT> (n);
        bins = n / 2 + 1;
        win.resize ((size_t) n);
        for (int i = 0; i < n; ++i) win[(size_t) i] = std::sin (kPi * ((float) i + 0.5f) / (float) n);   // sqrt-Hann both ways
        olaGain = 2.0f / 4.0f * 1.0f;                           // sum of win^2 over a hop of n/4 is 2
        for (auto* v : { &slow, &mask, &smoothed, &energy, &prefix, &wreL, &wimL, &wreR, &wimR, &target })
            v->assign ((size_t) bins + 1, 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            in[c].assign ((size_t) n, 0.0f);
            ola[c].assign ((size_t) n * 2, 0.0f);
            dry[c].assign ((size_t) n * 2, 0.0f);
            frame[c].assign ((size_t) n, 0.0f);
        }
        reset();
    }

    int latency() const noexcept { return n - 1; }

    void reset() noexcept
    {
        for (auto* v : { &slow, &mask, &smoothed }) std::fill (v->begin(), v->end(), 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            std::fill (in[c].begin(), in[c].end(), 0.0f);
            std::fill (ola[c].begin(), ola[c].end(), 0.0f);
            std::fill (dry[c].begin(), dry[c].end(), 0.0f);
        }
        pos = 0; count = 0; gain = -1.0f;
    }

    /** Input in l, r. dl, dr: the input, delayed by latency(). tl, tr: transient. sl, sr_: tonal. All delayed alike. */
    void process (const float* l, const float* r, float* dl, float* dr, float* tl, float* tr, float* sl, float* sr_, int count_,
                  const Settings& s) noexcept
    {
        settings = s;
        const int ring = n * 2;
        const float parallel = 0.5f * (1.0f + s.focus);
        for (int i = 0; i < count_; ++i)
        {
            in[0][(size_t) (pos % n)] = l[i];
            in[1][(size_t) (pos % n)] = r[i];
            const int at = pos % ring;
            dry[0][(size_t) at] = l[i];
            dry[1][(size_t) at] = r[i];

            ++pos;
            if (++count == hop) { count = 0; analyse(); }
            if (pos >= ring * 1024) pos -= ring * 1024;     // both rings are divisors of this

            // the sample now leaving was fully covered by the frames that ended at or before this one
            const int out = (pos + ring - n) % ring;
            const float a = dry[0][(size_t) out], b = dry[1][(size_t) out];
            float ta = ola[0][(size_t) out], tb = ola[1][(size_t) out];
            ola[0][(size_t) out] = 0.0f; ola[1][(size_t) out] = 0.0f;

            const float tgt = s.on ? 1.0f : 0.0f;              // Split off: Focus is a plain crossfade between the two chains
            gain += 0.002f * (tgt - gain);
            if (gain < 0.0f) gain = tgt;
            ta = gain * ta + (1.0f - gain) * parallel * a;
            tb = gain * tb + (1.0f - gain) * parallel * b;

            dl[i] = a; dr[i] = b;
            tl[i] = ta; tr[i] = tb;
            sl[i] = a - ta; sr_[i] = b - tb;
        }
    }

private:
    struct Tuning { float refMs, spread, floorDb; };

    static Tuning tuning (int source) noexcept
    {
        // refMs: memory of the tonal reference. spread: width of the detector in bins. A vocal keeps moving, so its
        // reference is short and only large jumps count; drums are broadband, so the detector looks wide.
        static constexpr Tuning t[6] = { { 90, 6, 0 }, { 110, 10, -2 }, { 70, 5, -1 }, { 80, 4, 1 }, { 45, 5, 3 }, { 160, 7, 2 } };
        return t[std::clamp (source, 0, 5)];
    }

    void analyse() noexcept
    {
        const int wrapEnd = pos % n;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                frame[c][(size_t) i] = in[c][(size_t) ((wrapEnd + i) % n)] * win[(size_t) i];
        fft->forward (frame[0].data(), wreL.data(), wimL.data());
        fft->forward (frame[1].data(), wreR.data(), wimR.data());

        const float frameSec = (float) hop / (float) sr;
        const auto tune = tuning (settings.source);
        const float up = 1.0f - std::exp (-frameSec / (tune.refMs * 0.001f));
        const float down = 1.0f - std::exp (-frameSec / 0.012f);
        const float release = std::exp (-frameSec / std::max (0.002f, settings.decayMs * 0.001f));
        const float smoothA = 1.0f - std::exp (-frameSec / (0.001f + 0.02f * settings.smooth));

        // Focus moves the threshold of the jump, from everything counting to almost nothing counting
        const float f = settings.focus;
        const float threshDb = 6.0f + tune.floorDb - 9.0f * f;
        const float pushUp = std::clamp ((f - 0.8f) / 0.2f, 0.0f, 1.0f), pushDown = std::clamp ((-f - 0.8f) / 0.2f, 0.0f, 1.0f);
        constexpr float eps = 1.0e-10f;

        prefix[0] = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            energy[(size_t) k] = wreL[(size_t) k] * wreL[(size_t) k] + wimL[(size_t) k] * wimL[(size_t) k]
                               + wreR[(size_t) k] * wreR[(size_t) k] + wimR[(size_t) k] * wimR[(size_t) k];
            prefix[(size_t) k + 1] = prefix[(size_t) k] + energy[(size_t) k];
        }
        const int w = std::max (1, (int) tune.spread);
        // wide detector on the energy; the reference is compared over the same band
        float refAcc = 0.0f;
        for (int k = 0; k < bins; ++k)
        {
            const int lo = std::max (0, k - w), hi = std::min (bins - 1, k + w);
            const float now = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (float) (hi - lo + 1);
            const float ref = slow[(size_t) k];
            target[(size_t) k] = (10.0f * std::log10 ((now + eps) / (ref + eps)) - threshDb) / 6.0f;   // 0 at the threshold, 1 at 6 dB over
            refAcc = now;
            slow[(size_t) k] += (now > ref ? up : down) * (now - ref);
        }
        (void) refAcc;

        for (int k = 0; k < bins; ++k)
        {
            float m = std::clamp (target[(size_t) k], 0.0f, 1.0f);
            m = m + pushUp * (1.0f - m);
            m = m * (1.0f - pushDown);
            mask[(size_t) k] = std::max (m, mask[(size_t) k] * release);     // instant attack, Trans Decay on the way out
            smoothed[(size_t) k] += (mask[(size_t) k] > smoothed[(size_t) k] ? std::min (1.0f, smoothA * 4.0f) : smoothA) * (mask[(size_t) k] - smoothed[(size_t) k]);
        }

        for (int k = 0; k < bins; ++k)
        {
            const float g = smoothed[(size_t) k];
            wreL[(size_t) k] *= g; wimL[(size_t) k] *= g;
            wreR[(size_t) k] *= g; wimR[(size_t) k] *= g;
        }
        wimL[0] = wimR[0] = wimL[(size_t) bins - 1] = wimR[(size_t) bins - 1] = 0.0f;
        fft->inverse (wreL.data(), wimL.data(), frame[0].data());
        fft->inverse (wreR.data(), wimR.data(), frame[1].data());

        const int ring = n * 2;
        const int start = (pos - n + ring * 2048) % ring;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                ola[c][(size_t) ((start + i) % ring)] += frame[c][(size_t) i] * win[(size_t) i] * olaGain;
    }

    double sr = 48000.0;
    int n = 512, hop = 128, bins = 257, pos = 0, count = 0;
    float gain = -1.0f, olaGain = 0.5f;
    std::unique_ptr<RealFFT> fft;
    std::vector<float> win, slow, mask, smoothed, energy, prefix, wreL, wimL, wreR, wimR, target;
    std::vector<float> in[2], ola[2], dry[2], frame[2];
    Settings settings;
};
} // namespace tails
