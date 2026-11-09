#pragma once

// Muzi Console — the audio engine. Header-only, no JUCE, no allocation after prepare(), zero latency.
//
// Channel: trim > filters > [dynamics if pre-EQ] > EQ > [dynamics] > drive > output
// Bus:     trim > filters > EQ > bus compressor (with mix) > drive > output

#include "ConsoleDSP.h"

#include <atomic>
#include <cstdint>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal" // exact compares here are deliberate (change detection, identity checks)
#endif

namespace muzi
{
constexpr int maxChannels = 2;

//==============================================================================
// Lock-free single-producer/single-consumer tap that feeds the Listen & Suggest engineer.
class ListenTap
{
public:
    static constexpr std::size_t size = 1u << 17;

    ListenTap() : ring(size, 0.0f) {}

    void push(const float* mono, int n) noexcept
    {
        auto w = written.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            ring[(w + static_cast<std::uint64_t>(i)) & (size - 1)] = mono[i];
        written.store(w + static_cast<std::uint64_t>(n), std::memory_order_release);
    }

    // Reader side (worker thread). Returns the number of samples copied; skips ahead if it fell behind.
    int pull(std::uint64_t& readPos, float* dest, int maxCount) const noexcept
    {
        const auto w = written.load(std::memory_order_acquire);
        if (w - readPos > size - 4096)
            readPos = w - (size - 4096);
        const auto available = static_cast<int>(std::min<std::uint64_t>(w - readPos, static_cast<std::uint64_t>(maxCount)));
        for (int i = 0; i < available; ++i)
            dest[i] = ring[(readPos + static_cast<std::uint64_t>(i)) & (size - 1)];
        readPos += static_cast<std::uint64_t>(available);
        return available;
    }

    std::uint64_t position() const noexcept { return written.load(std::memory_order_acquire); }

private:
    std::vector<float> ring;
    std::atomic<std::uint64_t> written { 0 };
};

//==============================================================================
struct Meters
{
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<float> compGrDb { 0.0f }, gateGrDb { 0.0f }; // most negative since last read

    static void raise(std::atomic<float>& a, float v) noexcept
    {
        auto cur = a.load(std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    static void lower(std::atomic<float>& a, float v) noexcept
    {
        auto cur = a.load(std::memory_order_relaxed);
        while (v < cur && ! a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
};

//==============================================================================
class Engine
{
public:
    void prepare(double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        mono.assign(static_cast<std::size_t>(std::max(maxBlock, 1)), 0.0f);
        chunk = std::max(8, static_cast<int>(std::lround(32.0 * fs / 48000.0)));
        reset();
        current = target;
        smoothed = toVector(target);
        eq = designEq(target, fs);
        updateStatic(target);
        prepared = true;
    }

    void reset() noexcept
    {
        for (auto& bank : filters)
            for (auto& b : bank)
                b.reset();
        for (auto& b : scFilter) b.reset();
        for (auto& d : dcState) d = {};
        compEnv = 0.0f; gateEnvDb = -120.0f; gateGain = 0.0f; gateOpen = false;
        busFast = busSlow = 0.0f;
    }

    // Called at the start of each block from the audio thread.
    void setSettings(const Settings& s) noexcept { target = s; }
    const Settings& settings() const noexcept { return target; }

    void setListening(bool on) noexcept { listening.store(on, std::memory_order_release); }
    bool isListening() const noexcept { return listening.load(std::memory_order_acquire); }
    const ListenTap& tap() const noexcept { return listenTap; }
    Meters& meters() noexcept { return meterValues; }
    double sampleRate() const noexcept { return fs; }

    void process(float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (! prepared || numSamples <= 0)
            return;
        numChannels = std::min(numChannels, maxChannels);

        // Input peak + the raw signal for the engineer (before trim and processing).
        float inPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                sum += channels[ch][i];
                inPeak = std::max(inPeak, std::abs(channels[ch][i]));
            }
            mono[static_cast<std::size_t>(i)] = numChannels > 0 ? sum / static_cast<float>(numChannels) : 0.0f;
        }
        Meters::raise(meterValues.inPeak, inPeak);
        if (listening.load(std::memory_order_relaxed))
            listenTap.push(mono.data(), numSamples);

        float compMin = 0.0f, gateMin = 0.0f, outPeak = 0.0f;

        for (int start = 0; start < numSamples; start += chunk)
        {
            const int n = std::min(chunk, numSamples - start);
            smoothTowardsTarget(n);

            for (int i = start; i < start + n; ++i)
            {
                inGain += (inGainTarget - inGain) * gainSlew;
                outGain += (outGainTarget - outGain) * gainSlew;
                driveMix += (driveTarget - driveMix) * gainSlew;

                float x[maxChannels] {};
                for (int ch = 0; ch < numChannels; ++ch)
                    x[ch] = channels[ch][i] * inGain;

                // Filters
                for (int ch = 0; ch < numChannels; ++ch)
                    for (int k = EqDesign::hpf1; k <= EqDesign::lpf; ++k)
                        if (! eq.c[static_cast<std::size_t>(k)].isIdentity())
                            x[ch] = filters[static_cast<std::size_t>(ch)][static_cast<std::size_t>(k)].process(eq.c[static_cast<std::size_t>(k)], x[ch]);

                if (current.mode == 0)
                {
                    if (current.dynOn && current.dynPreEq)
                        channelDynamics(x, numChannels, compMin, gateMin);
                    runEq(x, numChannels);
                    if (current.dynOn && ! current.dynPreEq)
                        channelDynamics(x, numChannels, compMin, gateMin);
                }
                else
                {
                    runEq(x, numChannels);
                    busCompressor(x, numChannels, compMin);
                }

                if (driveMix > 1.0e-4f)
                    for (int ch = 0; ch < numChannels; ++ch)
                        x[ch] = consoleDrive(x[ch], ch);

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const auto y = x[ch] * outGain;
                    channels[ch][i] = y;
                    outPeak = std::max(outPeak, std::abs(y));
                }
            }
        }

        Meters::raise(meterValues.outPeak, outPeak);
        Meters::lower(meterValues.compGrDb, compMin);
        Meters::lower(meterValues.gateGrDb, gateMin);
    }

    // The EQ actually running (smoothed), for the display.
    EqDesign currentDesign() const noexcept { return designEq(target, fs); }

private:
    // ---- parameter smoothing ------------------------------------------------
    static constexpr int numSmoothed = 13;
    static std::array<float, numSmoothed> toVector(const Settings& s) noexcept
    {
        return { s.hpfHz, s.lpfHz, s.lfDb, s.lfHz, s.lmfDb, s.lmfHz, s.lmfQ, s.hmfDb, s.hmfHz, s.hmfQ, s.hfDb, s.hfHz, 0.0f };
    }
    static void fromVector(Settings& s, const std::array<float, numSmoothed>& v) noexcept
    {
        s.hpfHz = v[0]; s.lpfHz = v[1]; s.lfDb = v[2]; s.lfHz = v[3]; s.lmfDb = v[4]; s.lmfHz = v[5]; s.lmfQ = v[6];
        s.hmfDb = v[7]; s.hmfHz = v[8]; s.hmfQ = v[9]; s.hfDb = v[10]; s.hfHz = v[11];
    }
    static bool isFrequency(int i) noexcept { return i == 0 || i == 1 || i == 3 || i == 5 || i == 8 || i == 11; }

    void smoothTowardsTarget(int n) noexcept
    {
        const auto goal = toVector(target);
        const auto a = 1.0f - std::exp(-static_cast<float>(n) / static_cast<float>(0.02 * fs));
        bool moved = false;
        for (int i = 0; i < numSmoothed; ++i)
        {
            auto& v = smoothed[static_cast<std::size_t>(i)];
            const auto g = goal[static_cast<std::size_t>(i)];
            if (v == g) continue;
            if (isFrequency(i))
            {
                const auto lv = std::log(std::max(v, 1.0f)), lg = std::log(std::max(g, 1.0f));
                const auto next = lv + (lg - lv) * a;
                v = std::abs(next - lg) < 1.0e-4f ? g : std::exp(next);
            }
            else
            {
                v += (g - v) * a;
                if (std::abs(v - g) < 1.0e-3f) v = g;
            }
            moved = true;
        }

        // Switches and modes change immediately; continuous values glide.
        const bool switched = current.mode != target.mode || current.flavour != target.flavour
                           || current.hpfOn != target.hpfOn || current.lpfOn != target.lpfOn || current.eqOn != target.eqOn
                           || current.lfBell != target.lfBell || current.hfBell != target.hfBell || current.busGuard != target.busGuard;
        auto next = target;
        fromVector(next, smoothed);
        current = next;
        if (moved || switched || ! designed)
        {
            eq = designEq(current, fs);
            designed = true;
        }
        updateStatic(target);
    }

    void updateStatic(const Settings& s) noexcept
    {
        inGainTarget = dbToGain(s.inTrimDb);
        outGainTarget = dbToGain(s.outTrimDb);
        driveTarget = std::clamp(s.drive / 100.0f, 0.0f, 1.0f);
        gainSlew = 1.0f - onePoleCoeff(0.015, fs);

        compAttack = onePoleCoeff(s.compFast ? 0.0003 : 0.003, fs);
        compRelease = onePoleCoeff(std::max(0.05f, s.compReleaseS), fs);
        compMakeup = channelMakeupDb(s.compThrDb, s.compRatio);

        gateDetectRelease = onePoleCoeff(0.06, fs);
        gateOpenCoeff = onePoleCoeff(0.0005, fs);
        gateCloseCoeff = onePoleCoeff(0.15, fs);

        busAtt = onePoleCoeff(busAttacksMs[static_cast<std::size_t>(std::clamp(s.busAttack, 0, 5))] / 1000.0, fs);
        busAuto = s.busRelease >= 4;
        busRel = onePoleCoeff(busAuto ? 0.1f : busReleasesS[static_cast<std::size_t>(std::clamp(s.busRelease, 0, 3))], fs);
        busSlowAtt = onePoleCoeff(0.25, fs);
        busSlowRel = onePoleCoeff(1.2, fs);
        busRatio = busRatios[static_cast<std::size_t>(std::clamp(s.busRatio, 0, 2))];
        busMakeupGain = dbToGain(s.busMakeupDb);
        busWet = std::clamp(s.busMix / 100.0f, 0.0f, 1.0f);
        if (! scDesigned || scFs != fs)
        {
            scCoeffs = design::highPass2(fs, 60.0, 0.707);
            scDesigned = true; scFs = fs;
        }
    }

    // ---- sections -------------------------------------------------------------
    void runEq(float* x, int numChannels) noexcept
    {
        for (int ch = 0; ch < numChannels; ++ch)
            for (int k = EqDesign::lf; k <= EqDesign::hf; ++k)
                if (! eq.c[static_cast<std::size_t>(k)].isIdentity())
                    x[ch] = filters[static_cast<std::size_t>(ch)][static_cast<std::size_t>(k)].process(eq.c[static_cast<std::size_t>(k)], x[ch]);
    }

    void channelDynamics(float* x, int numChannels, float& compMin, float& gateMin) noexcept
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max(peak, std::abs(x[ch]));
        const auto levelDb = gainToDb(peak, -120.0f);

        // Gate (off when the threshold is at its floor or the range is zero)
        float gateDb = 0.0f;
        if (target.gateThrDb > -79.9f && target.gateRangeDb > 0.05f)
        {
            gateEnvDb = levelDb > gateEnvDb ? levelDb : gateEnvDb * gateDetectRelease + levelDb * (1.0f - gateDetectRelease);
            if (! gateOpen && gateEnvDb > target.gateThrDb) gateOpen = true;
            else if (gateOpen && gateEnvDb < target.gateThrDb - 4.0f) gateOpen = false;
            const auto want = gateOpen ? 0.0f : -target.gateRangeDb;
            const auto c = want > gateGain ? gateOpenCoeff : gateCloseCoeff;
            gateGain = gateGain * c + want * (1.0f - c);
            gateDb = gateGain;
        }
        else
        {
            gateGain = 0.0f; gateOpen = true;
        }

        // Compressor
        const auto gr = compressorCurve(levelDb, target.compThrDb, std::max(1.0f, target.compRatio), 6.0f);
        compEnv = gr < compEnv ? compEnv * compAttack + gr * (1.0f - compAttack)
                               : compEnv * compRelease + gr * (1.0f - compRelease);
        const auto makeup = target.compRatio > 1.01f ? compMakeup : 0.0f;
        const auto g = dbToGain(compEnv + makeup + gateDb);
        for (int ch = 0; ch < numChannels; ++ch)
            x[ch] *= g;
        compMin = std::min(compMin, compEnv);
        gateMin = std::min(gateMin, gateDb);
    }

    void busCompressor(float* x, int numChannels, float& compMin) noexcept
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto d = x[ch];
            if (target.busScHpf)
                d = scFilter[static_cast<std::size_t>(ch)].process(scCoeffs, d);
            peak = std::max(peak, std::abs(d));
        }
        const auto gr = compressorCurve(gainToDb(peak, -120.0f), target.busThrDb, busRatio, 4.0f);

        busFast = gr < busFast ? busFast * busAtt + gr * (1.0f - busAtt) : busFast * busRel + gr * (1.0f - busRel);
        float env = busFast;
        if (busAuto)
        {
            // Program-dependent release: short peaks recover fast, sustained squeeze recovers slowly.
            busSlow = busFast < busSlow ? busSlow * busSlowAtt + busFast * (1.0f - busSlowAtt)
                                        : busSlow * busSlowRel + busFast * (1.0f - busSlowRel);
            env = std::min(busFast, busSlow);
        }
        const auto wetGain = dbToGain(env) * busMakeupGain;
        const auto g = (1.0f - busWet) + busWet * wetGain;
        for (int ch = 0; ch < numChannels; ++ch)
            x[ch] *= g;
        compMin = std::min(compMin, env);
    }

    // Console colour: gentle asymmetric saturation (2nd + 3rd harmonics), level-matched, DC-blocked.
    float consoleDrive(float in, int ch) noexcept
    {
        const auto pre = 1.0f + 3.0f * driveMix;
        const auto u = in * pre;
        const auto shaped = std::tanh(u + 0.12f * u * u) / pre;
        // DC blocker (the even-order term adds a little offset)
        auto& d = dcState[static_cast<std::size_t>(ch)];
        const auto y = shaped - d.x1 + 0.9995f * d.y1;
        d.x1 = shaped; d.y1 = y;
        return in + (y - in) * std::min(1.0f, driveMix * 2.0f);
    }

    double fs = 48000.0;
    bool prepared = false, designed = false;
    int chunk = 32;
    Settings target, current;
    std::array<float, numSmoothed> smoothed {};
    EqDesign eq;
    std::array<std::array<Biquad, EqDesign::count>, maxChannels> filters {};
    std::array<Biquad, maxChannels> scFilter {};
    Coeffs scCoeffs; bool scDesigned = false; double scFs = 0.0;
    struct Dc { float x1 = 0.0f, y1 = 0.0f; };
    std::array<Dc, maxChannels> dcState {};

    float inGain = 1.0f, outGain = 1.0f, driveMix = 0.0f;
    float inGainTarget = 1.0f, outGainTarget = 1.0f, driveTarget = 0.0f, gainSlew = 0.01f;

    float compEnv = 0.0f, compAttack = 0.0f, compRelease = 0.0f, compMakeup = 0.0f;
    float gateEnvDb = -120.0f, gateGain = 0.0f, gateDetectRelease = 0.0f, gateOpenCoeff = 0.0f, gateCloseCoeff = 0.0f;
    bool gateOpen = false;

    float busFast = 0.0f, busSlow = 0.0f, busAtt = 0.0f, busRel = 0.0f, busSlowAtt = 0.0f, busSlowRel = 0.0f;
    float busRatio = 4.0f, busMakeupGain = 1.0f, busWet = 1.0f;
    bool busAuto = true;

    std::vector<float> mono;
    std::atomic<bool> listening { false };
    ListenTap listenTap;
    Meters meterValues;
};
}

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic pop
#endif
