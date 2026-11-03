#pragma once
// Partitioned convolution without JUCE. Two tiers of uniform partitions: 64 samples for the head of the IR, 1024 for the
// rest. The wet signal sits 64 samples behind the input (1.3 ms at 48 kHz) and no latency is reported for it.
// Decay and Damp act per partition at run time, so they can be sequenced without rebuilding anything.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

namespace tails
{
/** Real FFT of even power-of-two size, on separate real and imaginary arrays so the loops vectorise. */
class RealFFT
{
public:
    explicit RealFFT (int size) : n (size), h (size / 2), wr ((size_t) h), wi ((size_t) h), zr ((size_t) h), zi ((size_t) h),
                                  rev ((size_t) h), tr ((size_t) h / 2 + 1), ti ((size_t) h / 2 + 1)
    {
        for (int i = 0; i < h; ++i)
        {
            const double a = -2.0 * 3.14159265358979323846 * i / h;
            wr[(size_t) i] = (float) std::cos (a);
            wi[(size_t) i] = (float) std::sin (a);
        }
        for (int i = 0; i <= h / 2; ++i)
        {
            const double a = -2.0 * 3.14159265358979323846 * i / n;
            tr[(size_t) i] = (float) std::cos (a);
            ti[(size_t) i] = (float) std::sin (a);
        }
        int bits = 0;
        while ((1 << bits) < h) ++bits;
        for (int i = 0; i < h; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if ((i >> b) & 1) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const noexcept { return n; }
    int bins() const noexcept { return h + 1; }

    /** in: n samples. re, im: n/2 + 1 bins. */
    void forward (const float* in, float* re, float* im) noexcept
    {
        for (int i = 0; i < h; ++i) { const int r = rev[(size_t) i]; zr[(size_t) r] = in[2 * i]; zi[(size_t) r] = in[2 * i + 1]; }
        complexFft (zr.data(), zi.data());
        re[0] = zr[0] + zi[0];  im[0] = 0;
        re[h] = zr[0] - zi[0];  im[h] = 0;
        for (int k = 1; k <= h / 2; ++k)
        {
            const int m = h - k;
            const float ar = zr[(size_t) k], ai = zi[(size_t) k], br = zr[(size_t) m], bi = -zi[(size_t) m];
            const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);
            const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
            const float cr = tr[(size_t) k], ci = ti[(size_t) k];
            const float or_ = di * cr + dr * ci, oi_ = di * ci - dr * cr;   // -i * W * d
            re[k] = er + or_;  im[k] = ei + oi_;
            re[m] = er - or_;  im[m] = -(ei - oi_);
        }
    }

    /** re, im: n/2 + 1 bins. out: n samples. Scaled so that inverse (forward (x)) == x. */
    void inverse (const float* re, const float* im, float* out) noexcept
    {
        for (int k = 0; k < h; ++k)
        {
            const int m = h - k;
            const float er = re[k] + re[m], ei = im[k] - im[m];     // X[k] + conj X[h - k]
            const float dr = re[k] - re[m], di = im[k] + im[m];     // X[k] - conj X[h - k]
            const float cr = k <= h / 2 ? tr[(size_t) k] : -tr[(size_t) m];
            const float ci = k <= h / 2 ? -ti[(size_t) k] : -ti[(size_t) m];   // conj of W^k
            const float wr_ = dr * cr - di * ci, wi_ = dr * ci + di * cr;
            const float zre = er - wi_, zim = ei + wr_;                       // Z[k] = E + i * W^-k * D
            // re and im are swapped on the way in and out, so the forward transform acts as the inverse
            const int r = rev[(size_t) k];
            zr[(size_t) r] = zim;
            zi[(size_t) r] = zre;
        }
        complexFft (zr.data(), zi.data());
        const float g = 0.5f / (float) h;
        for (int i = 0; i < h; ++i) { out[2 * i] = zi[(size_t) i] * g; out[2 * i + 1] = zr[(size_t) i] * g; }
    }

private:
    void complexFft (float* re, float* im) noexcept
    {
        for (int len = 2; len <= h; len <<= 1)
        {
            const int half = len / 2, step = h / len;
            for (int i = 0; i < h; i += len)
            {
                float* r0 = re + i;  float* i0 = im + i;
                float* r1 = r0 + half;  float* i1 = i0 + half;
                if (step == 1)
                {
                    const float* wre = wr.data();  const float* wim = wi.data();
                    for (int j = 0; j < half; ++j)
                    {
                        const float tre = r1[j] * wre[j] - i1[j] * wim[j], tim = r1[j] * wim[j] + i1[j] * wre[j];
                        r1[j] = r0[j] - tre;  i1[j] = i0[j] - tim;
                        r0[j] += tre;         i0[j] += tim;
                    }
                }
                else
                    for (int j = 0; j < half; ++j)
                    {
                        const float cr = wr[(size_t) (j * step)], ci = wi[(size_t) (j * step)];
                        const float tre = r1[j] * cr - i1[j] * ci, tim = r1[j] * ci + i1[j] * cr;
                        r1[j] = r0[j] - tre;  i1[j] = i0[j] - tim;
                        r0[j] += tre;         i0[j] += tim;
                    }
            }
        }
    }

    int n, h;
    std::vector<float> wr, wi, zr, zi;
    std::vector<int> rev;
    std::vector<float> tr, ti;
};

constexpr int kBlockA = 64, kBlockB = 1024, kPartsA = 15;
constexpr int kHeadSamples = kBlockA * kPartsA;          // 960: tier B takes over here (see Convolver::boundary)
constexpr int kBinsA = kBlockA + 1, kBinsB = kBlockB + 1;
constexpr double kMaxIrSeconds = 20.0;
constexpr int kPreviewPoints = 128;

/** Immutable, frequency-domain impulse response. Built on a worker thread, read by the audio thread. */
struct IrData
{
    int partsB = 0;
    std::array<std::vector<float>, 2> aRe, aIm, bRe, bIm;
    float t60 = 1.0f;
    bool shapable = true;                                 // Decay and Damp assume a falling tail; not for a reversed IR
    std::array<float, kPreviewPoints> preview {};         // 0..1, dB-scaled envelope
};

/** Windowed-sinc resampling for sample-rate conversion and for the Size control. ratio = output rate / input rate. */
inline std::vector<float> resampleIr (const float* in, int n, double ratio)
{
    if (std::abs (ratio - 1.0) < 1.0e-9) return std::vector<float> (in, in + n);
    const int out = (int) std::floor (n * ratio);
    std::vector<float> y ((size_t) std::max (out, 1), 0.0f);
    const double cut = std::min (1.0, ratio) * 0.98;
    const int half = (int) std::ceil (16.0 / cut);
    for (int i = 0; i < out; ++i)
    {
        const double t = i / ratio;
        const int c = (int) std::floor (t);
        double sum = 0.0;
        for (int k = std::max (0, c - half + 1); k <= std::min (n - 1, c + half); ++k)
        {
            const double d = t - k, x = 3.14159265358979323846 * d * cut;
            const double s = std::abs (x) < 1.0e-8 ? 1.0 : std::sin (x) / x;
            const double w = 0.5 + 0.5 * std::cos (3.14159265358979323846 * std::min (1.0, std::abs (d) / (double) half));
            sum += in[k] * s * w;
        }
        y[(size_t) i] = (float) (sum * cut);
    }
    return y;
}

/** RT60 from the backward-integrated energy, fitted between -5 dB and -35 dB (or -25 dB on a short or noisy file). */
inline float estimateT60 (const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    const size_t n = std::min (l.size(), r.size());
    if (n < 16) return 0.05f;
    std::vector<double> e (n);
    double acc = 0.0;
    for (size_t i = n; i-- > 0;) { acc += (double) l[i] * l[i] + (double) r[i] * r[i]; e[i] = acc; }
    if (acc <= 0.0) return 0.05f;
    auto at = [&] (double db) {
        const double target = acc * std::pow (10.0, db / 10.0);
        size_t lo = 0, hi = n - 1;
        while (lo < hi) { const size_t m = (lo + hi) / 2; if (e[m] > target) lo = m + 1; else hi = m; }
        return lo;
    };
    const size_t a = at (-5.0), b = at (-35.0), c = at (-25.0);
    const double rate = b + 1 < n ? 2.0 * (double) (b - a) : 3.0 * (double) (c - a);
    return (float) std::clamp (rate / sr, 0.05, 30.0);
}

/** Heavy: runs on a worker thread, or on the message thread when a session is restored. Input at the engine's sample rate. */
inline IrData* buildIr (std::vector<float> l, std::vector<float> r, double sr, bool reversed)
{
    auto* d = new IrData();
    const size_t cap = (size_t) (kMaxIrSeconds * sr);
    const size_t len = std::min (std::min (l.size(), r.size()), cap);
    l.resize (len); r.resize (len);
    d->t60 = estimateT60 (l, r, sr);
    d->shapable = ! reversed;
    if (reversed) { std::reverse (l.begin(), l.end()); std::reverse (r.begin(), r.end()); }

    // preview before partitioning
    {
        const size_t hop = std::max<size_t> (1, len / kPreviewPoints);
        for (int p = 0; p < kPreviewPoints; ++p)
        {
            float peak = 0.0f;
            const size_t from = (size_t) p * hop, to = std::min (len, from + hop);
            for (size_t i = from; i < to; ++i) peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
            d->preview[(size_t) p] = std::clamp (1.0f + 20.0f * std::log10 (peak + 1.0e-7f) / 60.0f, 0.0f, 1.0f);
        }
        const float top = *std::max_element (d->preview.begin(), d->preview.end());
        if (top > 0.0f) for (auto& v : d->preview) v /= top;
    }

    const int tailLen = (int) len - kHeadSamples;
    d->partsB = tailLen > 0 ? (tailLen + kBlockB - 1) / kBlockB : 0;
    RealFFT fa (kBlockA * 2), fb (kBlockB * 2);
    std::vector<float> time ((size_t) kBlockB * 2);
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& x = ch == 0 ? l : r;
        d->aRe[(size_t) ch].assign ((size_t) kPartsA * kBinsA, 0.0f);
        d->aIm[(size_t) ch].assign ((size_t) kPartsA * kBinsA, 0.0f);
        for (int p = 0; p < kPartsA; ++p)
        {
            std::fill (time.begin(), time.begin() + kBlockA * 2, 0.0f);
            for (int i = 0; i < kBlockA; ++i)
            {
                const size_t k = (size_t) p * kBlockA + (size_t) i;
                if (k < len) time[(size_t) i] = x[k];
            }
            fa.forward (time.data(), d->aRe[(size_t) ch].data() + p * kBinsA, d->aIm[(size_t) ch].data() + p * kBinsA);
        }
        d->bRe[(size_t) ch].assign ((size_t) d->partsB * kBinsB, 0.0f);
        d->bIm[(size_t) ch].assign ((size_t) d->partsB * kBinsB, 0.0f);
        for (int p = 0; p < d->partsB; ++p)
        {
            std::fill (time.begin(), time.end(), 0.0f);
            for (int i = 0; i < kBlockB; ++i)
            {
                const size_t k = (size_t) kHeadSamples + (size_t) p * kBlockB + (size_t) i;
                if (k < len) time[(size_t) i] = x[k];
            }
            fb.forward (time.data(), d->bRe[(size_t) ch].data() + (size_t) p * kBinsB, d->bIm[(size_t) ch].data() + (size_t) p * kBinsB);
        }
    }
    return d;
}

/** One stereo convolver: left input through the left IR, right input through the right IR. */
class Convolver
{
public:
    Convolver() : fa (kBlockA * 2), fb (kBlockB * 2) {}
    ~Convolver()
    {
        delete cur; delete next; delete pending.exchange (nullptr);
        for (auto& s : retired) delete s.exchange (nullptr);
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        capB = (int) (kMaxIrSeconds * sr / kBlockB) + 2;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[(size_t) ch];
            c.fdlAre.assign ((size_t) kPartsA * kBinsA, 0.0f); c.fdlAim.assign ((size_t) kPartsA * kBinsA, 0.0f);
            c.fdlBre.assign ((size_t) capB * kBinsB, 0.0f);    c.fdlBim.assign ((size_t) capB * kBinsB, 0.0f);
        }
        damp.resize (kBinsB);
        const double fc = 2200.0;
        for (int k = 0; k < kBinsB; ++k)
        {
            const double f = k * sr / (2.0 * kBlockB);
            damp[(size_t) k] = (float) (1.0 / (1.0 + (f / fc) * (f / fc)));
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : chans)
        {
            std::fill (c.inA.begin(), c.inA.end(), 0.0f); std::fill (c.inB.begin(), c.inB.end(), 0.0f);
            std::fill (c.outA.begin(), c.outA.end(), 0.0f); std::fill (c.outB.begin(), c.outB.end(), 0.0f);
            std::fill (c.fdlAre.begin(), c.fdlAre.end(), 0.0f); std::fill (c.fdlAim.begin(), c.fdlAim.end(), 0.0f);
            std::fill (c.fdlBre.begin(), c.fdlBre.end(), 0.0f); std::fill (c.fdlBim.begin(), c.fdlBim.end(), 0.0f);
        }
        posA = posB = 0; headA = headB = 0;
    }

    /** From any one non-audio thread at a time. The convolver takes ownership. */
    void post (IrData* d) noexcept { delete pending.exchange (d, std::memory_order_acq_rel); }

    /** Installs straight away, with no crossfade. Only while the audio thread is not running. */
    void installNow (IrData* d) noexcept { delete cur; cur = d; delete next; next = nullptr; delete pending.exchange (nullptr, std::memory_order_acq_rel); }

    /** Old responses wait here for a non-audio thread to delete them. */
    void collectGarbage() noexcept { for (auto& s : retired) delete s.exchange (nullptr, std::memory_order_acq_rel); }

    bool hasIr() const noexcept { return cur != nullptr; }
    float currentT60() const noexcept { return cur != nullptr ? cur->t60 : 0.0f; }

    /** decay: 0.05..1 of the file's own RT60. dampAmount: 0..1 extra loss above about 2 kHz. Replaces the input with the wet signal. */
    void process (float* l, float* r, int n, float decay, float dampAmount) noexcept
    {
        decayNow = decay; dampNow = dampAmount;
        auto& a = chans[0]; auto& b = chans[1];
        for (int i = 0; i < n; ++i)
        {
            a.inA[(size_t) (kBlockA + posA)] = l[i];  b.inA[(size_t) (kBlockA + posA)] = r[i];
            a.inB[(size_t) (kBlockB + posB)] = l[i];  b.inB[(size_t) (kBlockB + posB)] = r[i];
            l[i] = a.outA[(size_t) posA] + a.outB[(size_t) posB];
            r[i] = b.outA[(size_t) posA] + b.outB[(size_t) posB];
            ++posB;
            if (++posA == kBlockA)
            {
                posA = 0;
                if (posB == kBlockB) { posB = 0; boundary(); }
                runA();
            }
        }
    }

private:
    struct Chan
    {
        std::array<float, kBlockA * 2> inA {}, outA {}, timeA {};
        std::array<float, kBlockB * 2> inB {}, outB {}, timeB {};
        std::array<float, kBinsB> yr {}, yi {}, y2r {}, y2i {};
        std::array<float, kBlockB> incomingB {};
        std::vector<float> fdlAre, fdlAim, fdlBre, fdlBim;
    };

    void boundary() noexcept
    {
        if (next != nullptr)
        {   // the crossfade of the last 1024 samples is over
            bool parked = false;
            for (auto& s : retired)
            {
                IrData* expected = nullptr;
                if (s.compare_exchange_strong (expected, cur, std::memory_order_acq_rel)) { parked = true; break; }
            }
            if (parked || cur == nullptr) { cur = next; next = nullptr; }
        }
        if (next == nullptr && pending.load (std::memory_order_acquire) != nullptr)
        {
            bool room = false;
            for (auto& s : retired) if (s.load (std::memory_order_acquire) == nullptr) room = true;
            if (room) next = pending.exchange (nullptr, std::memory_order_acq_rel);
        }
        runB();
    }

    static void accumulate (float* yr, float* yi, const float* xr, const float* xi, const float* hr, const float* hi, int bins, float g) noexcept
    {
        for (int k = 0; k < bins; ++k)
        {
            yr[k] += g * (xr[k] * hr[k] - xi[k] * hi[k]);
            yi[k] += g * (xr[k] * hi[k] + xi[k] * hr[k]);
        }
    }

    void convolveA (Chan& c, size_t ch, const IrData& d, float* out) noexcept
    {
        std::fill (c.yr.begin(), c.yr.begin() + kBinsA, 0.0f);
        std::fill (c.yi.begin(), c.yi.begin() + kBinsA, 0.0f);
        for (int p = 0; p < kPartsA; ++p)
        {
            const int slot = (headA - p + kPartsA) % kPartsA;
            accumulate (c.yr.data(), c.yi.data(), c.fdlAre.data() + slot * kBinsA, c.fdlAim.data() + slot * kBinsA,
                        d.aRe[ch].data() + p * kBinsA, d.aIm[ch].data() + p * kBinsA, kBinsA, 1.0f);
        }
        fa.inverse (c.yr.data(), c.yi.data(), c.timeA.data());
        std::memcpy (out, c.timeA.data() + kBlockA, sizeof (float) * kBlockA);
    }

    void runA() noexcept
    {
        headA = (headA + 1) % kPartsA;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[ch];
            fa.forward (c.inA.data(), c.fdlAre.data() + headA * kBinsA, c.fdlAim.data() + headA * kBinsA);
            std::memcpy (c.inA.data(), c.inA.data() + kBlockA, sizeof (float) * kBlockA);
            if (cur != nullptr) convolveA (c, ch, *cur, c.outA.data());
            else std::fill (c.outA.begin(), c.outA.begin() + kBlockA, 0.0f);
            if (next != nullptr)
            {
                std::array<float, kBlockA> incoming;
                convolveA (c, ch, *next, incoming.data());
                for (int i = 0; i < kBlockA; ++i)
                {
                    const float x = (float) (posB + i) / (float) kBlockB;
                    c.outA[(size_t) i] += x * (incoming[(size_t) i] - c.outA[(size_t) i]);
                }
            }
        }
    }

    void convolveB (Chan& c, size_t ch, const IrData& d, float* out) noexcept
    {
        std::fill (c.yr.begin(), c.yr.end(), 0.0f);
        std::fill (c.yi.begin(), c.yi.end(), 0.0f);
        const bool useDamp = d.shapable && dampNow > 0.001f;
        if (useDamp) { std::fill (c.y2r.begin(), c.y2r.end(), 0.0f); std::fill (c.y2i.begin(), c.y2i.end(), 0.0f); }

        const double step = kBlockB / sr;
        const double t60 = std::max (0.05, (double) d.t60);
        const double extra = d.shapable ? 6.9078 / t60 * (1.0 / std::max (0.05, (double) decayNow) - 1.0) : 0.0;
        const double hiLoss = useDamp ? 3.0 * (double) dampNow * 6.9078 / std::max (0.05, t60 * decayNow) : 0.0;
        double t = (double) kHeadSamples / sr;
        float g = (float) std::exp (-extra * t), gh = (float) std::exp (-(extra + hiLoss) * t);
        const float stepG = (float) std::exp (-extra * step), stepH = (float) std::exp (-(extra + hiLoss) * step);

        const int parts = std::min (d.partsB, capB);
        for (int p = 0; p < parts; ++p)
        {
            if (g < 1.0e-5f) break;      // inaudible, and most of the cost of a short Decay goes away
            const size_t slot = (size_t) ((headB - p + capB) % capB) * kBinsB;
            const float* xr = c.fdlBre.data() + slot;  const float* xi = c.fdlBim.data() + slot;
            const float* hr = d.bRe[ch].data() + (size_t) p * kBinsB;  const float* hi = d.bIm[ch].data() + (size_t) p * kBinsB;
            accumulate (c.yr.data(), c.yi.data(), xr, xi, hr, hi, kBinsB, g);
            if (useDamp) accumulate (c.y2r.data(), c.y2i.data(), xr, xi, hr, hi, kBinsB, gh);
            g *= stepG; gh *= stepH;
        }
        if (useDamp)
            for (int k = 0; k < kBinsB; ++k)
            {
                const float m = damp[(size_t) k];
                c.yr[(size_t) k] = m * c.yr[(size_t) k] + (1.0f - m) * c.y2r[(size_t) k];
                c.yi[(size_t) k] = m * c.yi[(size_t) k] + (1.0f - m) * c.y2i[(size_t) k];
            }
        fb.inverse (c.yr.data(), c.yi.data(), c.timeB.data());
        std::memcpy (out, c.timeB.data() + kBlockB, sizeof (float) * kBlockB);
    }

    void runB() noexcept
    {
        headB = (headB + 1) % capB;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[ch];
            fb.forward (c.inB.data(), c.fdlBre.data() + (size_t) headB * kBinsB, c.fdlBim.data() + (size_t) headB * kBinsB);
            std::memcpy (c.inB.data(), c.inB.data() + kBlockB, sizeof (float) * kBlockB);
            if (cur != nullptr) convolveB (c, ch, *cur, c.outB.data());
            else std::fill (c.outB.begin(), c.outB.begin() + kBlockB, 0.0f);
            if (next != nullptr)
            {
                convolveB (c, ch, *next, c.incomingB.data());
                for (int i = 0; i < kBlockB; ++i)
                {
                    const float x = (float) i / (float) kBlockB;
                    c.outB[(size_t) i] += x * (c.incomingB[(size_t) i] - c.outB[(size_t) i]);
                }
            }
        }
    }

    double sr = 48000.0;
    int capB = 1, posA = 0, posB = 0, headA = 0, headB = 0;
    float decayNow = 1.0f, dampNow = 0.0f;
    RealFFT fa, fb;
    std::array<Chan, 2> chans;
    std::vector<float> damp;
    IrData* cur = nullptr;
    IrData* next = nullptr;
    std::atomic<IrData*> pending { nullptr };
    std::array<std::atomic<IrData*>, 4> retired {};
};
} // namespace tails
