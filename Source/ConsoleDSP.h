#pragma once

// Muzi Console — DSP building blocks (no JUCE, no allocation).
// Biquads, the two console EQ "flavours" (E and G), and helpers shared by the
// audio engine, the curve display and the Listen & Suggest engineer.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal" // exact compares here are deliberate (change detection, identity checks)
#endif

namespace muzi
{
constexpr double pi = 3.14159265358979323846;

inline float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float g, float floorDb = -120.0f) noexcept
{
    return g > 1.0e-6f ? 20.0f * std::log10(g) : floorDb;
}

//==============================================================================
struct Coeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static Coeffs identity() noexcept { return {}; }
    bool isIdentity() const noexcept { return b0 == 1.0 && b1 == 0.0 && b2 == 0.0 && a1 == 0.0 && a2 == 0.0; }

    double magnitude(double freq, double fs) const noexcept
    {
        const auto w = 2.0 * pi * freq / fs;
        const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return std::abs(num / den);
    }
};

namespace design
{
    inline Coeffs normalise(double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    inline double omega(double f, double fs) noexcept
    {
        return 2.0 * pi * std::clamp(f, 5.0, fs * 0.49) / fs;
    }

    inline Coeffs peak(double fs, double f, double q, double db) noexcept
    {
        if (std::abs(db) < 0.01) return Coeffs::identity();
        const auto A = std::pow(10.0, db / 40.0), w = omega(f, fs);
        const auto alpha = std::sin(w) / (2.0 * q), c = std::cos(w);
        return normalise(1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }

    inline Coeffs lowShelf(double fs, double f, double q, double db) noexcept
    {
        if (std::abs(db) < 0.01) return Coeffs::identity();
        const auto A = std::pow(10.0, db / 40.0), w = omega(f, fs);
        const auto alpha = std::sin(w) / (2.0 * q), c = std::cos(w), s = 2.0 * std::sqrt(A) * alpha;
        return normalise(A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
                         (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
    }

    inline Coeffs highShelf(double fs, double f, double q, double db) noexcept
    {
        if (std::abs(db) < 0.01) return Coeffs::identity();
        const auto A = std::pow(10.0, db / 40.0), w = omega(f, fs);
        const auto alpha = std::sin(w) / (2.0 * q), c = std::cos(w), s = 2.0 * std::sqrt(A) * alpha;
        return normalise(A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
                         (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }

    inline Coeffs highPass2(double fs, double f, double q) noexcept
    {
        const auto w = omega(f, fs), alpha = std::sin(w) / (2.0 * q), c = std::cos(w);
        return normalise((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    inline Coeffs lowPass2(double fs, double f, double q) noexcept
    {
        const auto w = omega(f, fs), alpha = std::sin(w) / (2.0 * q), c = std::cos(w);
        return normalise((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }

    inline Coeffs highPass1(double fs, double f) noexcept
    {
        const auto k = std::tan(omega(f, fs) / 2.0);
        const auto b0 = 1.0 / (1.0 + k);
        return { b0, -b0, 0.0, (k - 1.0) / (k + 1.0), 0.0 };
    }
}

//==============================================================================
// Transposed direct form II, double precision so very low cut-offs stay stable.
struct Biquad
{
    double s1 = 0.0, s2 = 0.0;
    void reset() noexcept { s1 = s2 = 0.0; }

    inline float process(const Coeffs& c, float in) noexcept
    {
        const double x = in;
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return static_cast<float>(y);
    }
};

//==============================================================================
// Everything the strip needs, in plain units. The plugin fills this from its parameters.
struct Settings
{
    int mode = 0;            // 0 Channel, 1 Bus
    int flavour = 0;         // 0 E (constant Q, gentle shelves), 1 G (proportional Q, shelves with a small bump)
    float inTrimDb = 0.0f, outTrimDb = 0.0f;

    bool hpfOn = false;  float hpfHz = 80.0f;     // 18 dB/oct
    bool lpfOn = false;  float lpfHz = 18000.0f;  // 12 dB/oct

    bool eqOn = true;
    float lfDb = 0.0f,  lfHz = 100.0f;  bool lfBell = false;
    float lmfDb = 0.0f, lmfHz = 500.0f, lmfQ = 1.0f;
    float hmfDb = 0.0f, hmfHz = 3000.0f, hmfQ = 1.0f;
    float hfDb = 0.0f,  hfHz = 10000.0f; bool hfBell = false;
    bool busGuard = true;    // Bus mode: EQ moves limited to +/-2 dB

    // Channel dynamics
    bool dynOn = true, dynPreEq = false;
    float compThrDb = 0.0f, compRatio = 4.0f, compReleaseS = 0.3f; bool compFast = false;
    float gateThrDb = -80.0f, gateRangeDb = 40.0f;

    // Bus compressor
    float busThrDb = 0.0f; int busRatio = 1;  // 0 2:1, 1 4:1, 2 10:1
    int busAttack = 3;                         // 0.1, 0.3, 1, 3, 10, 30 ms
    int busRelease = 4;                        // 0.1, 0.3, 0.6, 1.2 s, Auto
    float busMakeupDb = 0.0f, busMix = 100.0f; bool busScHpf = false;

    float drive = 0.0f;      // 0..100 %
};

inline bool operator==(const Settings& a, const Settings& b) noexcept
{
    return a.mode == b.mode && a.flavour == b.flavour && a.inTrimDb == b.inTrimDb && a.outTrimDb == b.outTrimDb
        && a.hpfOn == b.hpfOn && a.hpfHz == b.hpfHz && a.lpfOn == b.lpfOn && a.lpfHz == b.lpfHz && a.eqOn == b.eqOn
        && a.lfDb == b.lfDb && a.lfHz == b.lfHz && a.lfBell == b.lfBell && a.lmfDb == b.lmfDb && a.lmfHz == b.lmfHz
        && a.lmfQ == b.lmfQ && a.hmfDb == b.hmfDb && a.hmfHz == b.hmfHz && a.hmfQ == b.hmfQ && a.hfDb == b.hfDb
        && a.hfHz == b.hfHz && a.hfBell == b.hfBell && a.busGuard == b.busGuard && a.dynOn == b.dynOn
        && a.dynPreEq == b.dynPreEq && a.compThrDb == b.compThrDb && a.compRatio == b.compRatio
        && a.compReleaseS == b.compReleaseS && a.compFast == b.compFast && a.gateThrDb == b.gateThrDb
        && a.gateRangeDb == b.gateRangeDb && a.busThrDb == b.busThrDb && a.busRatio == b.busRatio
        && a.busAttack == b.busAttack && a.busRelease == b.busRelease && a.busMakeupDb == b.busMakeupDb
        && a.busMix == b.busMix && a.busScHpf == b.busScHpf && a.drive == b.drive;
}
inline bool operator!=(const Settings& a, const Settings& b) noexcept { return ! (a == b); }

constexpr float busGuardDb = 2.0f;
constexpr std::array<float, 3> busRatios { 2.0f, 4.0f, 10.0f };
constexpr std::array<float, 6> busAttacksMs { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };
constexpr std::array<float, 4> busReleasesS { 0.1f, 0.3f, 0.6f, 1.2f };

//==============================================================================
// The EQ section as seven biquads: HPF (1st + 2nd order = 18 dB/oct Butterworth),
// LPF (2nd order), then LF, LMF, HMF, HF.
struct EqDesign
{
    enum { hpf1, hpf2, lpf, lf, lmf, hmf, hf, count };
    std::array<Coeffs, count> c {};

    double magnitudeDb(double freq, double fs) const noexcept
    {
        double m = 1.0;
        for (const auto& k : c)
            if (! k.isIdentity())
                m *= k.magnitude(freq, fs);
        return 20.0 * std::log10(std::max(m, 1.0e-9));
    }
};

// The flavour laws. E: bell width stays put whatever the gain. G: the bell narrows as the gain
// rises (proportional Q), so small moves are broad and big moves are focused; its shelves carry a
// small opposite-direction bump below the turnover.
inline double bellQ(int flavour, float q, float gainDb) noexcept
{
    if (flavour == 0)
        return q * 0.85;
    return q * std::clamp(0.45 + 0.07 * std::abs(gainDb), 0.45, 1.6);
}
inline double shelfQ(int flavour) noexcept { return flavour == 0 ? 0.707 : 1.05; }

inline float guarded(const Settings& s, float db) noexcept
{
    return (s.mode == 1 && s.busGuard) ? std::clamp(db, -busGuardDb, busGuardDb) : db;
}

inline EqDesign designEq(const Settings& s, double fs) noexcept
{
    EqDesign d;
    if (s.hpfOn)
    {
        d.c[EqDesign::hpf1] = design::highPass1(fs, s.hpfHz);
        d.c[EqDesign::hpf2] = design::highPass2(fs, s.hpfHz, 1.0);
    }
    if (s.lpfOn)
        d.c[EqDesign::lpf] = design::lowPass2(fs, s.lpfHz, 0.707);

    if (s.eqOn)
    {
        const auto f = s.flavour;
        const auto lf = guarded(s, s.lfDb), lmf = guarded(s, s.lmfDb), hmf = guarded(s, s.hmfDb), hf = guarded(s, s.hfDb);
        d.c[EqDesign::lf]  = s.lfBell ? design::peak(fs, s.lfHz, bellQ(f, 0.9f, lf), lf)
                                      : design::lowShelf(fs, s.lfHz, shelfQ(f), lf);
        d.c[EqDesign::lmf] = design::peak(fs, s.lmfHz, bellQ(f, s.lmfQ, lmf), lmf);
        d.c[EqDesign::hmf] = design::peak(fs, s.hmfHz, bellQ(f, s.hmfQ, hmf), hmf);
        d.c[EqDesign::hf]  = s.hfBell ? design::peak(fs, s.hfHz, bellQ(f, 0.9f, hf), hf)
                                      : design::highShelf(fs, s.hfHz, shelfQ(f), hf);
    }
    return d;
}

//==============================================================================
// Static compressor curve (gain reduction in dB, <= 0) with a soft knee.
inline float compressorCurve(float levelDb, float thrDb, float ratio, float kneeDb) noexcept
{
    const auto over = levelDb - thrDb;
    const auto slope = 1.0f / ratio - 1.0f;
    if (2.0f * over <= -kneeDb) return 0.0f;
    if (2.0f * std::abs(over) <= kneeDb)
    {
        const auto x = over + kneeDb * 0.5f;
        return slope * x * x / (2.0f * kneeDb);
    }
    return slope * over;
}

inline float onePoleCoeff(double seconds, double fs) noexcept
{
    return seconds <= 0.0 ? 0.0f : static_cast<float>(std::exp(-1.0 / (seconds * fs)));
}

// Channel compressor auto make-up: half of the gain lost at a 0 dB peak.
inline float channelMakeupDb(float thrDb, float ratio) noexcept
{
    return 0.5f * (-compressorCurve(0.0f, thrDb, ratio, 6.0f));
}
}

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic pop
#endif
