#include "Engineer.h"
#include "ParamIDs.h"

#include <cstdio>
#include <numeric>
#include <utility>

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal" // exact compares here are deliberate 
#endif

namespace muzi
{
namespace
{
    double erbNumber(double hz) { return 21.4 * std::log10(0.00437 * hz + 1.0); }
    double erbToHz(double e) { return (std::pow(10.0, e / 21.4) - 1.0) / 0.00437; }

    std::string fmtHz(float hz)
    {
        char b[32];
        if (hz >= 1000.0f) std::snprintf(b, sizeof b, hz >= 10000.0f ? "%.0f kHz" : "%.1f kHz", hz / 1000.0f);
        else std::snprintf(b, sizeof b, "%.0f Hz", hz);
        return b;
    }
    std::string fmtDb(float db)
    {
        char b[32];
        std::snprintf(b, sizeof b, "%+.1f dB", db);
        return b;
    }
    std::string fmt(const char* f, double v)
    {
        char b[64];
        std::snprintf(b, sizeof b, f, v);
        return b;
    }
    float roundTo(float v, float step) { return std::round(v / step) * step; }
}

const char* roleName(Role r) noexcept
{
    static const char* names[] { "Vocal", "Kick", "808 / Sub", "Bass", "Snare / Clap", "Hats / Cymbals",
                                 "Guitar", "Keys / Synth", "Sample / Loop", "Mix Bus" };
    return names[static_cast<int>(r)];
}

const char* genreName(Genre g) noexcept
{
    static const char* names[] { "Neutral", "Trap", "Drill", "R&B", "Boom Bap", "Pop" };
    return names[static_cast<int>(g)];
}

const CriticalBands& CriticalBands::get()
{
    static const CriticalBands bands = []
    {
        CriticalBands b;
        const auto e0 = erbNumber(20.0), e1 = erbNumber(20000.0);
        const auto count = static_cast<int>(std::floor(e1 - e0));
        const auto step = (e1 - e0) / count;
        for (int i = 0; i < count; ++i)
        {
            const auto lo = erbToHz(e0 + step * i), hi = erbToHz(e0 + step * (i + 1));
            b.lo.push_back(static_cast<float>(lo));
            b.hi.push_back(static_cast<float>(hi));
            b.centre.push_back(static_cast<float>(std::sqrt(lo * hi)));
        }
        return b;
    }();
    return bands;
}

//==============================================================================
// Targets: per-octave level relative to pink noise, as (Hz, dB) points. These are starting
// points drawn from common practice for each source, not measurements of any particular record.
namespace
{
    using Curve = std::vector<std::pair<float, float>>;

    const Curve& roleCurve(Role r)
    {
        static const std::vector<Curve> curves {
            // Vocal
            { {20, -40}, {60, -28}, {100, -12}, {150, -3}, {250, 0}, {400, -1}, {800, -2}, {1500, -3}, {3000, -4},
              {5000, -7}, {8000, -10}, {12000, -14}, {16000, -20}, {20000, -30} },
            // Kick
            { {20, -10}, {35, -3}, {55, 0}, {80, -1}, {120, -5}, {250, -11}, {500, -16}, {1000, -18}, {2500, -16},
              {4000, -17}, {8000, -22}, {16000, -32}, {20000, -38} },
            // 808 / Sub
            { {20, -6}, {30, -1}, {45, 0}, {80, 0}, {130, -4}, {250, -12}, {500, -20}, {1000, -26}, {2000, -32},
              {5000, -40}, {10000, -48}, {20000, -60} },
            // Bass
            { {20, -14}, {40, -3}, {80, 0}, {150, -2}, {300, -6}, {600, -10}, {1000, -12}, {2000, -16}, {5000, -24},
              {10000, -32}, {20000, -45} },
            // Snare / Clap
            { {20, -40}, {60, -22}, {100, -10}, {200, -1}, {400, -3}, {800, -5}, {2000, -6}, {5000, -7}, {8000, -9},
              {12000, -13}, {16000, -18}, {20000, -26} },
            // Hats / Cymbals
            { {20, -50}, {200, -32}, {500, -18}, {1000, -10}, {3000, -4}, {6000, -1}, {10000, 0}, {14000, -2},
              {18000, -7}, {20000, -10} },
            // Guitar
            { {20, -40}, {80, -14}, {150, -4}, {300, -2}, {600, -3}, {1200, -3}, {2500, -4}, {5000, -7}, {8000, -11},
              {12000, -18}, {20000, -32} },
            // Keys / Synth
            { {20, -30}, {60, -12}, {120, -4}, {250, -2}, {500, -3}, {1000, -4}, {2000, -5}, {4000, -7}, {8000, -10},
              {14000, -15}, {20000, -24} },
            // Sample / Loop
            { {20, -12}, {40, -4}, {80, 0}, {200, -2}, {500, -4}, {1000, -6}, {2000, -8}, {4000, -10}, {8000, -13},
              {14000, -17}, {20000, -26} },
            // Mix bus (a modern urban mix: big lows, smooth mids, controlled top)
            { {20, -8}, {30, -2}, {50, 0}, {80, 0}, {150, -2}, {300, -5}, {600, -7}, {1000, -8}, {2000, -9},
              {4000, -11}, {8000, -14}, {12000, -17}, {16000, -22}, {20000, -32} },
        };
        return curves[static_cast<std::size_t>(r)];
    }

    float interp(const Curve& c, float hz)
    {
        if (hz <= c.front().first) return c.front().second;
        if (hz >= c.back().first) return c.back().second;
        for (std::size_t i = 1; i < c.size(); ++i)
            if (hz <= c[i].first)
            {
                const auto t = std::log(hz / c[i - 1].first) / std::log(c[i].first / c[i - 1].first);
                return c[i - 1].second + t * (c[i].second - c[i - 1].second);
            }
        return c.back().second;
    }

    // Smooth bump in dB centred (log) on a frequency range.
    float region(float hz, float lo, float hi, float db)
    {
        const auto c = std::sqrt(lo * hi), half = std::log2(hi / lo) * 0.5f + 0.5f;
        const auto d = std::abs(std::log2(hz / c)) / half;
        return d >= 1.0f ? 0.0f : db * 0.5f * (1.0f + std::cos(static_cast<float>(pi) * d));
    }
    float shelfUp(float hz, float corner, float db) // reaches db one octave above corner
    {
        const auto t = std::clamp(std::log2(hz / corner) + 0.5f, 0.0f, 1.0f);
        return db * t;
    }
    float shelfDown(float hz, float corner, float db)
    {
        const auto t = std::clamp(0.5f - std::log2(hz / corner), 0.0f, 1.0f);
        return db * t;
    }

    float genreOffset(Genre g, Role r, float hz)
    {
        const bool lowRole = r == Role::sub808 || r == Role::kick || r == Role::mixBus || r == Role::sample || r == Role::bass;
        switch (g)
        {
            case Genre::trap:    return (lowRole ? shelfDown(hz, 60, 2.0f) : 0.0f) + shelfUp(hz, 9000, 1.0f);
            case Genre::drill:   return (lowRole ? shelfDown(hz, 60, 2.0f) : 0.0f) + shelfUp(hz, 6000, -1.5f);
            case Genre::rnb:     return shelfUp(hz, 9000, 1.5f) + region(hz, 250, 500, -1.0f);
            case Genre::boomBap: return region(hz, 200, 600, 1.0f) + shelfUp(hz, 10000, -2.0f);
            case Genre::pop:     return region(hz, 1000, 3000, 1.0f);
            case Genre::neutral:
            case Genre::count:   break;
        }
        return 0.0f;
    }

    struct RoleRules
    {
        float hpfHz;        // 0 = leave the sub alone
        bool lowBoost;      // allowed to add low end
        float lowLo, lowHi; // where its weight lives
        float lowBoostHz; bool lowBoostBell;
        float presenceHz;   // 0 = no presence boost
        bool air;
        bool bodyBoost;     // allowed to add a little 200 Hz body
        float ratio, targetGr, releaseS; bool fast;
    };

    RoleRules rules(Role r)
    {
        switch (r)
        {
            case Role::vocal:  return { 80, false, 0, 0, 0, false, 2800, true, true, 4.0f, 5.0f, 0.25f, false };
            case Role::kick:   return { 25, true, 45, 90, 60, true, 3500, false, false, 4.0f, 3.0f, 0.10f, false };
            case Role::sub808: return { 0, true, 30, 70, 60, false, 0, false, false, 3.0f, 3.0f, 0.20f, false };
            case Role::bass:   return { 30, true, 50, 120, 90, false, 800, false, false, 4.0f, 4.0f, 0.20f, false };
            case Role::snare:  return { 80, false, 0, 0, 0, false, 4000, true, true, 4.0f, 4.0f, 0.15f, false };
            case Role::hats:   return { 400, false, 0, 0, 0, false, 0, true, false, 2.0f, 2.0f, 0.10f, true };
            case Role::guitar: return { 90, false, 0, 0, 0, false, 3000, true, true, 3.0f, 3.0f, 0.20f, false };
            case Role::keys:   return { 100, false, 0, 0, 0, false, 2500, true, false, 2.0f, 2.0f, 0.30f, false };
            case Role::sample: return { 40, true, 50, 100, 70, false, 0, true, false, 2.0f, 2.0f, 0.30f, false };
            case Role::mixBus: return { 0, true, 35, 80, 45, false, 0, true, false, 4.0f, 2.0f, 0.30f, false };
            case Role::count:  break;
        }
        return rules(Role::vocal);
    }

    std::string culpritFor(float hz)
    {
        if (hz < 120) return "the 808 or kick";
        if (hz < 500) return "bass, keys or samples in the low-mids";
        if (hz < 2000) return "the vocal or samples in the mids";
        if (hz < 6000) return "the vocal, snare or synth top end";
        return "the hats and cymbals";
    }
}

Settings withMoves(Settings s, const std::vector<Move>& moves)
{
    for (const auto& m : moves)
    {
        const auto& id = m.id;
        const auto v = m.value;
        const auto on = v > 0.5f;
        if (id == pid::mode) s.mode = static_cast<int>(v);
        else if (id == pid::flavour) s.flavour = static_cast<int>(v);
        else if (id == pid::inTrim) s.inTrimDb = v;
        else if (id == pid::outTrim) s.outTrimDb = v;
        else if (id == pid::hpfOn) s.hpfOn = on;
        else if (id == pid::hpfHz) s.hpfHz = v;
        else if (id == pid::lpfOn) s.lpfOn = on;
        else if (id == pid::lpfHz) s.lpfHz = v;
        else if (id == pid::eqOn) s.eqOn = on;
        else if (id == pid::lfDb) s.lfDb = v;
        else if (id == pid::lfHz) s.lfHz = v;
        else if (id == pid::lfBell) s.lfBell = on;
        else if (id == pid::lmfDb) s.lmfDb = v;
        else if (id == pid::lmfHz) s.lmfHz = v;
        else if (id == pid::lmfQ) s.lmfQ = v;
        else if (id == pid::hmfDb) s.hmfDb = v;
        else if (id == pid::hmfHz) s.hmfHz = v;
        else if (id == pid::hmfQ) s.hmfQ = v;
        else if (id == pid::hfDb) s.hfDb = v;
        else if (id == pid::hfHz) s.hfHz = v;
        else if (id == pid::hfBell) s.hfBell = on;
        else if (id == pid::busGuard) s.busGuard = on;
        else if (id == pid::dynOn) s.dynOn = on;
        else if (id == pid::dynPre) s.dynPreEq = on;
        else if (id == pid::compThr) s.compThrDb = v;
        else if (id == pid::compRatio) s.compRatio = v;
        else if (id == pid::compRel) s.compReleaseS = v;
        else if (id == pid::compFast) s.compFast = on;
        else if (id == pid::gateThr) s.gateThrDb = v;
        else if (id == pid::gateRange) s.gateRangeDb = v;
        else if (id == pid::busThr) s.busThrDb = v;
        else if (id == pid::busRatio) s.busRatio = static_cast<int>(v);
        else if (id == pid::busAttack) s.busAttack = static_cast<int>(v);
        else if (id == pid::busRelease) s.busRelease = static_cast<int>(v);
        else if (id == pid::busMakeup) s.busMakeupDb = v;
        else if (id == pid::busMix) s.busMix = v;
        else if (id == pid::busScHpf) s.busScHpf = on;
        else if (id == pid::drive) s.drive = v;
    }
    return s;
}

float targetAt(Role role, Genre genre, float hz)
{
    return interp(roleCurve(role), hz) + genreOffset(genre, role, hz);
}

//==============================================================================
Listener::Listener()
{
    window.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * static_cast<float>(pi) * i / fftSize);
    frame.assign(fftSize, 0.0f);
    work.resize(fftSize);
    bitReverse.resize(fftSize);
    for (std::size_t i = 0; i < fftSize; ++i)
    {
        std::size_t r = 0;
        for (int b = 0; b < fftOrder; ++b)
            if (i & (std::size_t { 1 } << b)) r |= std::size_t { 1 } << (fftOrder - 1 - b);
        bitReverse[i] = r;
    }
    twiddle.resize(fftSize / 2);
    for (int i = 0; i < fftSize / 2; ++i)
        twiddle[static_cast<std::size_t>(i)] = std::polar(1.0f, -2.0f * static_cast<float>(pi) * i / fftSize);
    frames.reserve(1024);
    blocks.reserve(8192);
    reset(48000.0);
}

void Listener::reset(double sampleRate)
{
    fs = sampleRate;
    active = heard = 0.0;
    frameFill = 0;
    std::fill(frame.begin(), frame.end(), 0.0f);
    frames.clear();
    blocks.clear();
    blockLen = std::max(1, static_cast<int>(fs * 0.01));
    blockFill = 0; blockPeak = 0.0f; blockSq = 0.0; maxPeak = 0.0f;

    const auto& bands = CriticalBands::get();
    binToBand.assign(fftSize / 2, -1);
    for (int k = 1; k < fftSize / 2; ++k)
    {
        const auto f = static_cast<float>(k * fs / fftSize);
        for (std::size_t b = 0; b < bands.lo.size(); ++b)
            if (f >= bands.lo[b] && f < bands.hi[b]) { binToBand[static_cast<std::size_t>(k)] = static_cast<int>(b); break; }
    }
}

void Listener::consume(const float* samples, int count)
{
    for (int i = 0; i < count; ++i)
    {
        const auto x = samples[i];
        const auto a = std::abs(x);
        blockPeak = std::max(blockPeak, a);
        blockSq += static_cast<double>(x) * x;
        maxPeak = std::max(maxPeak, a);
        if (++blockFill == blockLen)
        {
            const auto rms = static_cast<float>(std::sqrt(blockSq / blockLen));
            if (blocks.size() < 30000)
                blocks.push_back({ gainToDb(blockPeak), gainToDb(rms) });
            blockFill = 0; blockPeak = 0.0f; blockSq = 0.0;
        }

        frame[static_cast<std::size_t>(frameFill++)] = x;
        if (frameFill == fftSize)
        {
            analyseFrame();
            std::copy(frame.begin() + hop, frame.end(), frame.begin());
            frameFill = fftSize - hop;
        }
    }
    heard += count / fs;
}

void Listener::fft(std::vector<std::complex<float>>& a) const
{
    for (std::size_t i = 0; i < fftSize; ++i)
        if (i < bitReverse[i]) std::swap(a[i], a[bitReverse[i]]);
    for (std::size_t len = 2; len <= fftSize; len <<= 1)
    {
        const auto step = fftSize / len;
        for (std::size_t i = 0; i < fftSize; i += len)
            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto t = twiddle[j * step] * a[i + j + len / 2];
                a[i + j + len / 2] = a[i + j] - t;
                a[i + j] += t;
            }
    }
}

void Listener::analyseFrame()
{
    double sq = 0.0;
    for (int i = 0; i < fftSize; ++i)
    {
        const auto x = frame[static_cast<std::size_t>(i)];
        sq += static_cast<double>(x) * x;
        work[static_cast<std::size_t>(i)] = { x * window[static_cast<std::size_t>(i)], 0.0f };
    }
    const auto levelDb = gainToDb(static_cast<float>(std::sqrt(sq / fftSize)));
    if (levelDb > -60.0f)
        active += hop / fs;
    if (levelDb < -70.0f || frames.size() >= 4000)
        return;

    fft(work);
    const auto& bands = CriticalBands::get();
    Frame f { levelDb, std::vector<float>(bands.lo.size(), 0.0f) };
    for (std::size_t k = 1; k < fftSize / 2; ++k)
        if (const auto b = binToBand[k]; b >= 0)
            f.power[static_cast<std::size_t>(b)] += std::norm(work[k]);
    frames.push_back(std::move(f));
}

std::vector<float> Listener::liveBandDb() const
{
    return profile().bandDb;
}

Profile Listener::profile() const
{
    Profile p;
    p.activeSeconds = active;
    p.peakDb = gainToDb(maxPeak);
    const auto& bands = CriticalBands::get();
    const auto n = bands.lo.size();
    if (frames.empty())
        return p;

    // Only the loud, musical part counts: frames within 20 dB of the loudest.
    float loudest = -120.0f;
    for (const auto& f : frames) loudest = std::max(loudest, f.levelDb);
    std::vector<double> sum(n, 0.0);
    int used = 0;
    for (const auto& f : frames)
        if (f.levelDb > loudest - 20.0f)
        {
            for (std::size_t b = 0; b < n; ++b) sum[b] += f.power[b];
            ++used;
        }
    p.bandDb.resize(n);
    for (std::size_t b = 0; b < n; ++b)
    {
        const auto octaves = std::log2(bands.hi[b] / bands.lo[b]);
        p.bandDb[b] = static_cast<float>(10.0 * std::log10(sum[b] / std::max(used, 1) / octaves + 1.0e-20));
    }
    // Bands with no FFT bins (only possible at very low rates) borrow their neighbour.
    for (std::size_t b = 1; b < n; ++b)
        if (sum[b] <= 0.0) p.bandDb[b] = p.bandDb[b - 1];

    float loudBlock = -120.0f;
    for (const auto& b : blocks) loudBlock = std::max(loudBlock, b.rmsDb);
    double rmsSum = 0.0; int rmsCount = 0;
    for (const auto& b : blocks)
        if (b.rmsDb > loudBlock - 30.0f && b.rmsDb > -70.0f)
        {
            p.blockPeakDb.push_back(b.peakDb);
            rmsSum += std::pow(10.0, b.rmsDb / 10.0);
            ++rmsCount;
        }
    p.rmsDb = rmsCount > 0 ? static_cast<float>(10.0 * std::log10(rmsSum / rmsCount)) : -120.0f;
    p.valid = active >= 2.0 && used >= 4;
    return p;
}

//==============================================================================
namespace
{
    struct Peak { float hz = 0.0f, value = 0.0f, strength = 0.0f, widthOct = 1.0f; bool found = false; };
    // value: smoothed deviation at the peak (used to find and rank it); gain moves use the larger of
    // that and the unsmoothed one, so narrow build-ups aren't under-cut.

    // Bandwidth in octaves -> Q (the usual constant-bandwidth relation).
    float qForWidth(float oct) { return static_cast<float>(1.0 / (2.0 * std::sinh(std::log(2.0) / 2.0 * std::max(oct, 0.2f)))); }

    // Knob Q that gives the wanted effective Q under the flavour's law.
    float knobQ(int flavour, float wantedQ, float gainDb)
    {
        const auto unit = bellQ(flavour, 1.0f, gainDb);
        return std::clamp(roundTo(static_cast<float>(wantedQ / unit), 0.05f), range::qLo, range::qHi);
    }

    struct Analysis
    {
        const std::vector<float>& f;
        std::vector<float> dev, raw, prominence;
        std::vector<bool> ok;

        // How far each band sticks up above its own neighbourhood (half an octave to an octave
        // either side). This is what makes a resonance audible even where the target slopes.
        void computeProminence()
        {
            prominence.assign(f.size(), 0.0f);
            for (std::size_t b = 0; b < f.size(); ++b)
            {
                double sum = 0.0; int n = 0;
                for (std::size_t k = 0; k < f.size(); ++k)
                {
                    const auto d = std::abs(std::log2(f[k] / f[b]));
                    if (ok[k] && d >= 0.45f && d <= 1.0f) { sum += raw[k]; ++n; }
                }
                prominence[b] = n > 0 ? raw[b] - static_cast<float>(sum / n) : 0.0f;
            }
        }

        struct BellFit { float q = 1.0f, gain = 0.0f, base = 0.0f; };

        BellFit fitBell(std::size_t centre) const
        {
            BellFit best;
            double bestErr = 1.0e30;
            const auto f0 = f[centre];
            for (const double q : { 0.4, 0.55, 0.7, 0.85, 1.0, 1.2, 1.4, 1.7, 2.0, 2.5, 3.0, 4.0, 5.0 })
            {
                const auto c = design::peak(48000.0, f0, q, 10.0);
                double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
                for (std::size_t k = 0; k < f.size(); ++k)
                {
                    if (! ok[k] || std::abs(std::log2(f[k] / f0)) > 2.0f) continue;
                    const auto x = 20.0 * std::log10(c.magnitude(f[k], 48000.0)) / 10.0; // unit-height bell shape
                    const double y = raw[k];
                    n += 1; sx += x; sy += y; sxx += x * x; sxy += x * y;
                }
                if (n < 4) continue;
                const auto den = n * sxx - sx * sx;
                if (std::abs(den) < 1.0e-9) continue;
                const auto g = (n * sxy - sx * sy) / den, a = (sy - g * sx) / n;
                double err = 0.0;
                for (std::size_t k = 0; k < f.size(); ++k)
                {
                    if (! ok[k] || std::abs(std::log2(f[k] / f0)) > 2.0f) continue;
                    const auto x = 20.0 * std::log10(c.magnitude(f[k], 48000.0)) / 10.0;
                    const auto e = raw[k] - (a + g * x);
                    err += e * e;
                }
                if (g > 0.0 && err < bestErr) { bestErr = err; best = { static_cast<float>(q), static_cast<float>(g), static_cast<float>(a) }; }
            }
            return best;
        }

        static float widthForQ(float q) // octaves between the half-gain points of an RBJ bell
        {
            return static_cast<float>(2.0 / std::log(2.0) * std::asinh(1.0 / (2.0 * q)));
        }

        // Cuts: a band qualifies if it's above target, or if it's a resonance (prominent and at least
        // a little above target). Returns the score in value/strength.
        Peak resonance(float lo, float hi, float tol) const
        {
            Peak p;
            float bestScore = 0.0f;
            int best = -1;
            for (std::size_t b = 0; b < f.size(); ++b)
            {
                if (! ok[b] || f[b] < lo || f[b] > hi) continue;
                const auto resonant = prominence[b] >= tol + 0.5f && dev[b] >= 1.0f ? prominence[b] : 0.0f;
                const auto score = std::max(dev[b], resonant);
                if (best < 0 || score > bestScore) { bestScore = score; best = static_cast<int>(b); }
            }
            if (best < 0) return p;
            const auto bi = static_cast<std::size_t>(best);
            p.found = true;
            p.hz = f[bi];
            p.value = bestScore;
            p.strength = std::max(bestScore, std::max(raw[bi], prominence[bi] >= tol + 0.5f ? prominence[bi] : 0.0f));
            // Shape: fit a bell (gain, Q and a local baseline) to the curve two octaves either side.
            // The fitted Q becomes the suggested width; the fitted gain is the bump's real height.
            const auto fit = fitBell(bi);
            if (fit.gain > 0.5f)
            {
                p.widthOct = widthForQ(fit.q);
                // The fitted height is what the ear hears against the surroundings; it's capped at 3 dB
                // past the target so a bump in an otherwise thin source isn't over-cut.
                p.strength = std::max(p.value, std::min(fit.gain, raw[bi] + 3.0f));
            }
            else
                p.widthOct = 1.0f;
            return p;
        }

        Peak peak(float lo, float hi, float sign) const
        {
            Peak p;
            int best = -1;
            for (std::size_t b = 0; b < f.size(); ++b)
                if (ok[b] && f[b] >= lo && f[b] <= hi && (best < 0 || sign * dev[b] > sign * dev[static_cast<std::size_t>(best)]))
                    best = static_cast<int>(b);
            if (best < 0) return p;
            p.found = true;
            p.hz = f[static_cast<std::size_t>(best)];
            p.value = sign * dev[static_cast<std::size_t>(best)];
            p.strength = std::max(p.value, sign * raw[static_cast<std::size_t>(best)]);
            auto l = static_cast<std::size_t>(best), r = l;
            while (l > 0 && ok[l - 1] && sign * dev[l - 1] > p.value * 0.5f) --l;
            while (r + 1 < f.size() && ok[r + 1] && sign * dev[r + 1] > p.value * 0.5f) ++r;
            p.widthOct = std::max(1.0f / 3.0f, std::log2(f[r] / f[l]) + 1.0f / 6.0f);
            return p;
        }

        float average(float lo, float hi) const
        {
            double s = 0.0; int n = 0;
            for (std::size_t b = 0; b < f.size(); ++b)
                if (ok[b] && f[b] >= lo && f[b] <= hi) { s += dev[b]; ++n; }
            return n > 0 ? static_cast<float>(s / n) : 0.0f;
        }
    };
}

Suggestion suggest(const Profile& profile, Role role, Genre genre, const Settings& current)
{
    Suggestion s;
    if (! profile.valid)
    {
        s.headline = "Didn't hear enough - play the track (at least a few seconds) and listen again.";
        return s;
    }

    const auto& bands = CriticalBands::get();
    const auto n = bands.centre.size();
    const bool bus = current.mode == 1;
    const auto rr = rules(role);
    const auto flavour = current.flavour;
    auto add = [&s](const char* id, float v) { s.moves.push_back({ id, v }); };

    // ---- compare against the target -----------------------------------------
    std::vector<float> target(n), raw(n);
    float loudest = -300.0f;
    for (std::size_t b = 0; b < n; ++b)
    {
        target[b] = targetAt(role, genre, bands.centre[b]);
        loudest = std::max(loudest, profile.bandDb[b]);
    }
    // Align on the bands that matter for this source (the median is robust to one big resonance).
    std::vector<float> diffs;
    for (std::size_t b = 0; b < n; ++b)
        if (target[b] >= -12.0f && profile.bandDb[b] > loudest - 45.0f)
            diffs.push_back(profile.bandDb[b] - target[b]);
    if (diffs.empty())
        for (std::size_t b = 0; b < n; ++b) diffs.push_back(profile.bandDb[b] - target[b]);
    std::nth_element(diffs.begin(), diffs.begin() + static_cast<long>(diffs.size() / 2), diffs.end());
    const auto offset = diffs[diffs.size() / 2];

    Analysis a { bands.centre, std::vector<float>(n, 0.0f), {}, {}, std::vector<bool>(n, false) };
    for (std::size_t b = 0; b < n; ++b)
    {
        raw[b] = profile.bandDb[b] - offset - target[b];
        // Ignore bands where the source has nothing to say (more than 45 dB down) or far below its target.
        a.ok[b] = profile.bandDb[b] > loudest - 45.0f && target[b] > -30.0f;
    }
    for (std::size_t b = 0; b < n; ++b)
    {
        // Light smoothing across neighbouring critical bands
        const auto l = raw[b > 0 ? b - 1 : b], r = raw[b + 1 < n ? b + 1 : b];
        a.dev[b] = 0.25f * l + 0.5f * raw[b] + 0.25f * r;
    }
    a.raw = raw;
    a.computeProminence();

    s.freqs = bands.centre;
    s.targetDb = target;
    s.measuredDb.resize(n);
    for (std::size_t b = 0; b < n; ++b) s.measuredDb[b] = profile.bandDb[b] - offset;

    const float tol = bus ? 2.0f : 3.0f;
    const float maxCut = 9.0f, maxBoost = 6.0f;
    std::vector<std::pair<float, float>> busOverflow; // (hz, raw dB) of moves a bus shouldn't make
    auto busLimit = [&](float db, float hz)
    {
        if (! bus) return db;
        if (std::abs(db) > busGuardDb + 0.2f) busOverflow.push_back({ hz, db });
        return std::clamp(db, -busGuardDb, busGuardDb);
    };
    auto cutFor = [&](float v) { return -std::min(roundTo(0.75f * v, 0.5f), maxCut); };
    auto boostFor = [&](float v) { return std::min(roundTo(0.6f * std::abs(v), 0.5f), maxBoost); };

    // ---- mode, gain staging ---------------------------------------------------
    if (role == Role::mixBus && ! bus)
    {
        add(pid::mode, 1.0f);
        s.notes.push_back("Switched to BUS mode - this is a full mix.");
    }
    const bool asBus = bus || role == Role::mixBus;

    float levelOffset = current.inTrimDb;
    if (! asBus)
    {
        const auto trim = std::clamp(roundTo(-18.0f - profile.rmsDb, 0.5f), range::trimLo, range::trimHi);
        add(pid::inTrim, trim);
        levelOffset = trim;
        if (std::abs(trim) >= 1.0f)
            s.notes.push_back("Trim " + fmtDb(trim) + " so the channel averages about -18 dBFS - the level the console is built around.");
    }
    else if (profile.rmsDb + current.inTrimDb > -8.0f)
        s.notes.push_back("The bus is running hot (" + fmt("%.0f", profile.rmsDb + current.inTrimDb)
                          + " dBFS average). Pull the faders feeding it down a little for headroom.");

    // ---- filters ----------------------------------------------------------------
    float hpf = 0.0f;
    if (! asBus && rr.hpfHz > 0.0f)
    {
        hpf = rr.hpfHz;
        if (role == Role::vocal && (genre == Genre::trap || genre == Genre::drill)) hpf = 100.0f;
        add(pid::hpfOn, 1.0f);
        add(pid::hpfHz, hpf);
        s.notes.push_back("High-pass at " + fmtHz(hpf) + " - clears rumble that isn't part of this sound.");
    }
    else if (asBus && a.average(18.0f, 28.0f) > 6.0f)
    {
        hpf = 20.0f;
        add(pid::hpfOn, 1.0f);
        add(pid::hpfHz, hpf);
        s.notes.push_back("High-pass at 20 Hz - only takes out sub-sonic energy nobody can hear.");
    }
    else
    {
        add(pid::hpfOn, 0.0f);
        if (role == Role::sub808 || role == Role::kick || asBus)
            s.notes.push_back("No high-pass - the sub is the point here.");
    }
    add(pid::lpfOn, 0.0f);

    // ---- EQ -------------------------------------------------------------------
    add(pid::eqOn, 1.0f);
    if (asBus) add(pid::busGuard, 1.0f);
    int eqMoves = 0;

    // LF band
    {
        float db = 0.0f, hz = current.lfHz; bool bell = current.lfBell;
        const auto lo = std::max(35.0f, hpf * 1.6f);
        const auto excess = a.peak(lo, 250.0f, 1.0f);
        if (excess.found && excess.value >= tol)
        {
            const auto rawDb = cutFor(excess.strength);
            hz = std::clamp(roundTo(excess.hz, 1.0f), range::lfLo, range::lfHi);
            bell = ! (excess.hz < 90.0f && excess.widthOct > 1.0f);
            db = busLimit(rawDb, hz);
            s.notes.push_back("LF " + fmtDb(db) + " at " + fmtHz(hz) + " - low end is piling up there"
                              + (bell ? " (narrow bell, so the rest of the bottom stays)." : "."));
        }
        else if (rr.lowBoost)
        {
            const auto deficit = a.average(rr.lowLo, rr.lowHi);
            if (deficit <= -tol)
            {
                hz = rr.lowBoostHz; bell = rr.lowBoostBell;
                if (role == Role::kick && (genre == Genre::trap || genre == Genre::drill)) hz = 55.0f;
                db = busLimit(boostFor(deficit), hz);
                s.notes.push_back("LF " + fmtDb(db) + " at " + fmtHz(hz) + (bell ? " (bell)" : " (shelf)") + " - adds the weight this "
                                  + std::string(asBus ? "mix" : "sound") + " is missing.");
            }
        }
        if (db != 0.0f) ++eqMoves;
        add(pid::lfDb, db); add(pid::lfHz, hz); add(pid::lfBell, bell ? 1.0f : 0.0f);
    }

    // LMF band: subtractive first (mud and box), a touch of body only when it's thin.
    {
        float db = 0.0f, hz = current.lmfHz, q = current.lmfQ;
        const auto excess = role == Role::hats ? Peak {} : a.resonance(180.0f, 1000.0f, tol);
        if (excess.found && excess.value >= tol)
        {
            hz = std::clamp(roundTo(excess.hz, 5.0f), range::lmfLo, range::lmfHi);
            db = busLimit(cutFor(excess.strength), hz);
            q = knobQ(flavour, std::clamp(qForWidth(excess.widthOct), 0.7f, 3.0f), db);
            const char* what = excess.hz < 400.0f ? "mud" : "boxiness";
            s.notes.push_back("LMF " + fmtDb(db) + " at " + fmtHz(hz) + " - cuts the " + what + " sitting above target.");
        }
        else if (rr.bodyBoost && ! asBus && a.average(180.0f, 350.0f) < -4.0f)
        {
            hz = 220.0f;
            db = std::min(roundTo(0.5f * std::abs(a.average(180.0f, 350.0f)), 0.5f), 3.0f);
            q = knobQ(flavour, 0.8f, db);
            s.notes.push_back("LMF " + fmtDb(db) + " at 220 Hz - a little body; it sounds thin against the target.");
        }
        if (db != 0.0f) ++eqMoves;
        add(pid::lmfDb, db); add(pid::lmfHz, hz); add(pid::lmfQ, q);
    }

    // HMF band: tame harshness first; otherwise add presence where the role wants it.
    {
        float db = 0.0f, hz = current.hmfHz, q = current.hmfQ;
        const auto harsh = a.resonance(1500.0f, 7000.0f, tol);
        auto presence = rr.presenceHz;
        if (role == Role::vocal && genre == Genre::pop) presence = 1500.0f;
        if (harsh.found && harsh.value >= tol + 1.0f)
        {
            hz = std::clamp(roundTo(harsh.hz, 10.0f), range::hmfLo, range::hmfHi);
            db = busLimit(cutFor(harsh.strength), hz);
            q = knobQ(flavour, std::clamp(qForWidth(harsh.widthOct), 1.5f, 4.0f), db);
            s.notes.push_back("HMF " + fmtDb(db) + " at " + fmtHz(hz) + " - a focused cut on the harsh spot.");
        }
        else if (presence > 0.0f)
        {
            const auto deficit = a.average(presence / 1.4f, presence * 1.4f);
            if (deficit <= -tol)
            {
                hz = presence;
                db = busLimit(std::min(boostFor(deficit), 5.0f), hz);
                q = knobQ(flavour, 0.8f, db);
                s.notes.push_back("HMF " + fmtDb(db) + " at " + fmtHz(hz) + " - presence, so it reads through the beat.");
            }
        }
        if (db != 0.0f) ++eqMoves;
        add(pid::hmfDb, db); add(pid::hmfHz, hz); add(pid::hmfQ, q);
    }

    // HF band: air if it's dull and the role wants it, a shelf down if it's fizzy.
    {
        float db = 0.0f, hz = current.hfHz; bool bell = false;
        const auto top = a.average(8000.0f, 16000.0f);
        if (top >= tol)
        {
            hz = 8000.0f;
            db = busLimit(cutFor(top), hz);
            s.notes.push_back("HF " + fmtDb(db) + " shelf at 8 kHz - the top is brighter than the target.");
        }
        else if (rr.air && top <= -tol)
        {
            hz = genre == Genre::rnb ? 12000.0f : 10000.0f;
            db = busLimit(boostFor(top), hz);
            s.notes.push_back("HF " + fmtDb(db) + " shelf at " + fmtHz(hz) + " - air on top.");
        }
        if (db != 0.0f) ++eqMoves;
        add(pid::hfDb, db); add(pid::hfHz, hz); add(pid::hfBell, bell ? 1.0f : 0.0f);
    }

    if (eqMoves == 0)
        s.notes.push_back("EQ: already close to the target - no moves. Restraint is a move too.");

    // A bus EQ move bigger than ~2 dB means the problem lives on a channel.
    for (const auto& [hz, db] : busOverflow)
        s.notes.push_back("Around " + fmtHz(hz) + " the mix wants " + fmtDb(db) + " - too much for a bus. Fix it at the source: likely "
                          + culpritFor(hz) + ". (Bus move held to " + fmtDb(db > 0 ? busGuardDb : -busGuardDb) + ".)");

    // ---- dynamics ---------------------------------------------------------------
    auto averageGr = [&](float thr, float ratio, float knee)
    {
        if (profile.blockPeakDb.empty()) return 0.0f;
        double sum = 0.0;
        for (const auto p : profile.blockPeakDb)
            sum += -compressorCurve(p + levelOffset, thr, ratio, knee);
        return static_cast<float>(sum / static_cast<double>(profile.blockPeakDb.size()));
    };
    auto solveThreshold = [&](float ratio, float knee, float goal, float lo, float hi)
    {
        for (int it = 0; it < 40; ++it)
        {
            const auto mid = 0.5f * (lo + hi);
            (averageGr(mid, ratio, knee) > goal ? lo : hi) = mid;
        }
        return 0.5f * (lo + hi);
    };

    if (! asBus)
    {
        const auto thr = std::clamp(roundTo(solveThreshold(rr.ratio, 6.0f, rr.targetGr, range::compThrLo, range::compThrHi), 0.5f),
                                    range::compThrLo, range::compThrHi);
        add(pid::dynOn, 1.0f); add(pid::dynPre, 0.0f);
        add(pid::compThr, thr); add(pid::compRatio, rr.ratio); add(pid::compRel, rr.releaseS); add(pid::compFast, rr.fast ? 1.0f : 0.0f);
        s.notes.push_back("Comp " + fmt("%.0f:1", rr.ratio) + " at " + fmt("%.1f dB", thr) + " - about " + fmt("%.0f dB", rr.targetGr)
                          + " of gain reduction on the loud parts; " + (rr.fast ? "fast attack." : "3 ms attack keeps the front of each hit."));
    }
    else
    {
        const bool punchy = genre == Genre::trap || genre == Genre::drill || genre == Genre::boomBap;
        const int ratioIndex = punchy ? 1 : 0;
        const auto ratio = busRatios[static_cast<std::size_t>(ratioIndex)];
        const auto thr = std::clamp(roundTo(solveThreshold(ratio, 4.0f, 2.0f, range::busThrLo, range::busThrHi), 0.5f),
                                    range::busThrLo, range::busThrHi);
        const bool scHpf = a.average(30.0f, 80.0f) > 0.0f || genre == Genre::trap || genre == Genre::drill;
        add(pid::busThr, thr); add(pid::busRatio, static_cast<float>(ratioIndex));
        add(pid::busAttack, 5.0f); add(pid::busRelease, 4.0f);
        add(pid::busMakeup, 1.5f); add(pid::busMix, 100.0f); add(pid::busScHpf, scHpf ? 1.0f : 0.0f);
        s.notes.push_back(std::string("Bus comp ") + (punchy ? "4:1" : "2:1") + " at " + fmt("%.1f dB", thr)
                          + " for about 2 dB of glue. 30 ms attack lets the drums punch through, auto release follows the groove"
                          + (scHpf ? "; sidechain high-pass so the sub doesn't pump the mix." : "."));
    }

    // ---- console colour ---------------------------------------------------------
    if (role == Role::sub808 && (genre == Genre::trap || genre == Genre::drill))
    {
        add(pid::drive, 35.0f);
        s.notes.push_back("Drive 35% - harmonics so the 808 still reads on phone and laptop speakers.");
    }

    s.headline = std::string(roleName(role)) + " - " + genreName(genre) + ": " + std::to_string(eqMoves)
                 + (eqMoves == 1 ? " EQ move" : " EQ moves");
    s.valid = true;
    return s;
}
}
