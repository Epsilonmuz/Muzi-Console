// Muzi Console engine + engineer tests. No JUCE needed.
//   c++ -std=c++17 -O2 -ISource Tests/EngineTests.cpp Source/Engineer.cpp -o tests && ./tests

#include "ConsoleEngine.h"
#include "Engineer.h"
#include "ParamIDs.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <tuple>

using namespace muzi;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static float magDb(const Settings& s, double hz, double fs = 48000.0) { return static_cast<float>(designEq(s, fs).magnitudeDb(hz, fs)); }

struct Pink
{
    std::mt19937 rng { 1234 };
    std::normal_distribution<float> n { 0.0f, 1.0f };
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    float next()
    {
        const float w = n(rng);
        b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
        b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
        const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
        b6 = w * 0.115926f;
        return out * 0.11f;
    }
};

static std::vector<float> pinkWithBump(double fs, double seconds, double hz, double db, float level = 1.0f)
{
    Pink p;
    const auto c = design::peak(fs, hz, 1.5, db);
    Biquad bq;
    std::vector<float> x(static_cast<std::size_t>(fs * seconds));
    for (auto& v : x) v = level * (db != 0.0 ? bq.process(c, p.next()) : p.next());
    return x;
}

static float findMove(const Suggestion& s, const char* id, float fallback = -999.0f)
{
    for (const auto& m : s.moves)
        if (m.id == id) return m.value;
    return fallback;
}

static Profile listenTo(const std::vector<float>& x, double fs)
{
    Listener l;
    l.reset(fs);
    l.consume(x.data(), static_cast<int>(x.size()));
    return l.profile();
}

int main()
{
    const double fs = 48000.0;

    std::printf("EQ curves\n");
    {
        Settings s; s.lmfDb = 6.0f; s.lmfHz = 1000.0f; s.lmfQ = 1.0f;
        CHECK(std::abs(magDb(s, 1000.0) - 6.0f) < 0.1f, "E bell centre %.2f", magDb(s, 1000.0));
        s.flavour = 1;
        CHECK(std::abs(magDb(s, 1000.0) - 6.0f) < 0.1f, "G bell centre %.2f", magDb(s, 1000.0));

        // G: proportional Q (narrow at big gain, broad at small gain); E: constant width.
        auto relWidth = [&](int flavour, float gain)
        {
            Settings t; t.flavour = flavour; t.lmfHz = 1000.0f; t.lmfQ = 1.0f; t.lmfDb = gain;
            return magDb(t, 2000.0) / gain; // fraction of the boost left one octave away
        };
        CHECK(relWidth(1, 12.0f) < relWidth(0, 12.0f), "G big boost should be narrower than E (%.3f vs %.3f)", relWidth(1, 12.0f), relWidth(0, 12.0f));
        CHECK(relWidth(1, 3.0f) > relWidth(0, 3.0f), "G small boost should be broader than E (%.3f vs %.3f)", relWidth(1, 3.0f), relWidth(0, 3.0f));
        CHECK(std::abs(relWidth(0, 3.0f) - relWidth(0, 12.0f)) < 0.12f, "E width should stay roughly put");

        Settings h; h.hpfOn = true; h.hpfHz = 100.0f;
        CHECK(std::abs(magDb(h, 100.0) + 3.0f) < 0.5f, "HPF -3 dB point %.2f", magDb(h, 100.0));
        CHECK(std::abs(magDb(h, 50.0) + 18.1f) < 1.5f, "HPF 18 dB/oct: %.2f at an octave down", magDb(h, 50.0));
        CHECK(std::abs(magDb(h, 1000.0)) < 0.05f, "HPF passband %.3f", magDb(h, 1000.0));

        Settings shelf; shelf.flavour = 1; shelf.lfDb = 6.0f; shelf.lfHz = 100.0f;
        CHECK(magDb(shelf, 20.0) > 5.5f, "G low shelf reaches its gain %.2f", magDb(shelf, 20.0));
        CHECK(magDb(shelf, 300.0) < -0.05f, "G shelf has its small dip above the turnover %.2f", magDb(shelf, 300.0));

        Settings b; b.mode = 1; b.lfDb = 8.0f; b.hfDb = -9.0f;
        CHECK(magDb(b, 30.0) <= 2.05f, "bus guard holds boosts to 2 dB (%.2f)", magDb(b, 30.0));
        CHECK(magDb(b, 18000.0) >= -2.05f, "bus guard holds cuts to 2 dB (%.2f)", magDb(b, 18000.0));
        b.busGuard = false;
        CHECK(magDb(b, 30.0) > 7.0f, "guard off lets the full move through");
    }

    std::printf("Engine\n");
    auto render = [&](Engine& e, std::vector<float>& L, std::vector<float>& R)
    {
        float* ch[2] { L.data(), R.data() };
        const int block = 512;
        for (std::size_t i = 0; i < L.size(); i += block)
        {
            const int n = static_cast<int>(std::min<std::size_t>(block, L.size() - i));
            float* p[2] { ch[0] + i, ch[1] + i };
            e.process(p, 2, n);
        }
    };
    auto sine = [&](double hz, float amp, double seconds)
    {
        std::vector<float> x(static_cast<std::size_t>(fs * seconds));
        for (std::size_t i = 0; i < x.size(); ++i) x[i] = amp * static_cast<float>(std::sin(2.0 * pi * hz * i / fs));
        return x;
    };
    {
        Engine e; e.prepare(fs, 512);
        Settings s; s.dynOn = true; s.compRatio = 1.0f; // nothing engaged
        e.setSettings(s);
        auto L = sine(1000.0, 0.5f, 1.0), R = L, ref = L;
        render(e, L, R);
        float err = 0.0f;
        for (std::size_t i = 0; i < L.size(); ++i) err = std::max(err, std::abs(L[i] - ref[i]));
        CHECK(err < 1.0e-5f, "flat strip should null against the input (max error %g)", err);
    }
    {
        Engine e; e.prepare(fs, 512);
        Settings s; s.compThrDb = -20.0f; s.compRatio = 4.0f; s.compReleaseS = 0.3f;
        e.setSettings(s);
        auto L = sine(1000.0, 1.0f, 1.0), R = L;
        render(e, L, R);
        e.meters().compGrDb.store(0.0f);
        auto L2 = sine(1000.0, 1.0f, 0.2), R2 = L2;
        render(e, L2, R2);
        const auto gr = e.meters().compGrDb.load();
        CHECK(gr < -13.0f && gr > -16.5f, "channel comp 4:1 at -20 on a 0 dBFS sine: GR %.2f (expect about -15)", gr);
    }
    {
        Engine e; e.prepare(fs, 512);
        Settings s; s.mode = 1; s.busThrDb = -10.0f; s.busRatio = 1; s.busAttack = 2; s.busRelease = 4;
        e.setSettings(s);
        auto L = sine(1000.0, 1.0f, 2.0), R = L;
        render(e, L, R);
        e.meters().compGrDb.store(0.0f);
        auto L2 = sine(1000.0, 1.0f, 0.2), R2 = L2;
        render(e, L2, R2);
        const auto gr = e.meters().compGrDb.load();
        CHECK(gr < -6.5f && gr > -8.5f, "bus comp 4:1 at -10 on a 0 dBFS sine: GR %.2f (expect about -7.5)", gr);

        s.busMix = 0.0f; e.setSettings(s);
        auto L3 = sine(1000.0, 1.0f, 0.5), R3 = L3, ref = L3;
        render(e, L3, R3);
        float err = 0.0f;
        for (std::size_t i = 24000; i < L3.size(); ++i) err = std::max(err, std::abs(L3[i] - ref[i]));
        CHECK(err < 1.0e-4f, "bus mix 0%% is the dry signal (error %g)", err);
    }
    {
        Engine e; e.prepare(fs, 512);
        Settings s; s.gateThrDb = -40.0f; s.gateRangeDb = 40.0f; s.compRatio = 1.0f;
        e.setSettings(s);
        auto L = sine(200.0, 0.001f, 1.0), R = L; // -60 dBFS
        render(e, L, R);
        float peak = 0.0f;
        for (std::size_t i = 36000; i < L.size(); ++i) peak = std::max(peak, std::abs(L[i]));
        CHECK(gainToDb(peak) < -95.0f, "gate closes 40 dB on a -60 dBFS signal (%.1f dBFS)", gainToDb(peak));
    }
    {
        Engine e; e.prepare(fs, 512);
        Settings s; s.drive = 50.0f; s.compRatio = 1.0f;
        e.setSettings(s);
        auto L = sine(100.0, 0.25f, 1.0), R = L;
        render(e, L, R);
        double sq = 0.0; for (std::size_t i = 24000; i < L.size(); ++i) sq += L[i] * L[i];
        const auto rms = gainToDb(static_cast<float>(std::sqrt(sq / (L.size() - 24000))));
        CHECK(std::abs(rms - gainToDb(0.25f / std::sqrt(2.0f))) < 1.5f, "drive stays level-matched (%.2f dBFS)", rms);
    }
    {
        // Everything on, loud noise and silence: no NaN, no blow-ups.
        Engine e; e.prepare(fs, 512);
        Settings s; s.hpfOn = s.lpfOn = true; s.lfDb = 15; s.lmfDb = -15; s.hmfDb = 15; s.hfDb = 15; s.flavour = 1;
        s.compThrDb = -30; s.compRatio = 20; s.compFast = true; s.gateThrDb = -50; s.drive = 100;
        e.setSettings(s);
        std::mt19937 rng(7); std::uniform_real_distribution<float> u(-4.0f, 4.0f);
        std::vector<float> L(48000 * 2), R(L.size());
        for (std::size_t i = 0; i < L.size(); ++i) { L[i] = i < 48000 ? u(rng) : 0.0f; R[i] = L[i]; }
        render(e, L, R);
        bool finite = true; for (auto v : L) finite = finite && std::isfinite(v) && std::abs(v) < 100.0f;
        CHECK(finite, "stays finite under abuse");
        s.mode = 1; e.setSettings(s); render(e, L, R);
        finite = true; for (auto v : L) finite = finite && std::isfinite(v);
        CHECK(finite, "bus mode stays finite under abuse");
    }
    {
        // Speed: one minute of stereo, everything on, with a parameter sweep so the EQ keeps redesigning.
        Engine e; e.prepare(44100.0, 512);
        Settings s; s.hpfOn = true; s.lfDb = 4; s.lmfDb = -3; s.hmfDb = 2; s.hfDb = 3; s.flavour = 1; s.compThrDb = -20; s.drive = 30;
        std::vector<float> L(512), R(512);
        Pink p;
        const int blocks = static_cast<int>(60.0 * 44100.0 / 512);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            s.lmfHz = 300.0f + 200.0f * static_cast<float>(std::sin(b * 0.01));
            e.setSettings(s);
            for (int i = 0; i < 512; ++i) { L[static_cast<std::size_t>(i)] = p.next(); R[static_cast<std::size_t>(i)] = L[static_cast<std::size_t>(i)]; }
            float* ch[2] { L.data(), R.data() };
            e.process(ch, 2, 512);
        }
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  60 s stereo, all on + sweeping EQ: %.0f ms (%.0fx real time)\n", ms, 60000.0 / ms);
        CHECK(ms < 3000.0, "should run far faster than real time");
    }

    std::printf("Engineer\n");
    {
        Listener l; l.reset(fs);
        std::vector<float> silence(static_cast<std::size_t>(fs * 5), 0.0f);
        l.consume(silence.data(), static_cast<int>(silence.size()));
        Settings cur;
        const auto sug = suggest(l.profile(), Role::vocal, Genre::trap, cur);
        CHECK(! sug.valid, "silence gives no suggestion");
    }
    {
        // A vocal-ish source with a +8 dB build-up at 400 Hz: the engineer should cut there.
        auto x = pinkWithBump(fs, 8.0, 400.0, 8.0, 0.25f);
        // Give it a vocal-like shape: roll off the lows and the extreme top.
        Settings shape; shape.hpfOn = true; shape.hpfHz = 150.0f; shape.hfDb = -8.0f; shape.hfHz = 9000.0f;
        const auto d = designEq(shape, fs);
        std::array<Biquad, EqDesign::count> st {};
        for (auto& v : x) for (std::size_t k = 0; k < EqDesign::count; ++k) if (! d.c[k].isIdentity()) v = st[k].process(d.c[k], v);

        Settings cur;
        const auto sug = suggest(listenTo(x, fs), Role::vocal, Genre::neutral, cur);
        CHECK(sug.valid, "valid suggestion");
        const auto db = findMove(sug, pid::lmfDb), hz = findMove(sug, pid::lmfHz);
        CHECK(db <= -3.0f, "cuts the build-up (LMF %.1f dB)", db);
        CHECK(hz >= 300.0f && hz <= 550.0f, "cuts near 400 Hz (LMF at %.0f Hz)", hz);
        CHECK(findMove(sug, pid::hpfOn) == 1.0f && findMove(sug, pid::hpfHz) == 80.0f, "vocal gets an 80 Hz high-pass");
        std::printf("  %s\n", sug.headline.c_str());
        for (const auto& note : sug.notes) std::printf("    - %s\n", note.c_str());

        // Closed loop: apply the suggested EQ and listen again; the build-up should be much smaller.
        Settings applied;
        for (const auto& m : sug.moves)
        {
            if (m.id == pid::lmfDb) applied.lmfDb = m.value; else if (m.id == pid::lmfHz) applied.lmfHz = m.value;
            else if (m.id == pid::lmfQ) applied.lmfQ = m.value;
        }
        const auto fix = designEq(applied, fs);
        std::array<Biquad, EqDesign::count> st2 {};
        auto y = x;
        for (auto& v : y) for (std::size_t k = 0; k < EqDesign::count; ++k) if (! fix.c[k].isIdentity()) v = st2[k].process(fix.c[k], v);
        const auto before = listenTo(x, fs), after = listenTo(y, fs);
        const auto& bands = CriticalBands::get();
        auto bumpHeight = [&](const Profile& p)
        {
            float atBump = -300, around = 0; int n = 0;
            for (std::size_t b = 0; b < bands.centre.size(); ++b)
            {
                if (bands.centre[b] > 350 && bands.centre[b] < 460) atBump = std::max(atBump, p.bandDb[b]);
                if ((bands.centre[b] > 1500 && bands.centre[b] < 2500)) { around += p.bandDb[b]; ++n; }
            }
            return atBump - around / std::max(n, 1);
        };
        CHECK(bumpHeight(after) < bumpHeight(before) - 3.0f, "applying the move flattens the bump (%.1f -> %.1f dB)", bumpHeight(before), bumpHeight(after));
    }
    {
        // Same problem on the mix bus: moves stay within 2 dB and the engineer points at the channel.
        auto x = pinkWithBump(fs, 8.0, 300.0, 9.0, 0.3f);
        Settings cur; cur.mode = 1;
        const auto sug = suggest(listenTo(x, fs), Role::mixBus, Genre::trap, cur);
        bool within = true;
        for (const char* id : { pid::lfDb, pid::lmfDb, pid::hmfDb, pid::hfDb })
            within = within && std::abs(findMove(sug, id, 0.0f)) <= busGuardDb + 1.0e-3f;
        CHECK(within, "bus moves stay within +/-2 dB");
        bool flagged = false;
        for (const auto& n : sug.notes) flagged = flagged || n.find("too much for a bus") != std::string::npos;
        CHECK(flagged, "bus flags a problem that belongs on a channel");
        CHECK(findMove(sug, pid::inTrim) == -999.0f, "bus mode leaves the trim alone");
        CHECK(findMove(sug, pid::busThr) > range::busThrLo, "bus comp threshold suggested");
        std::printf("  %s\n", sug.headline.c_str());
        for (const auto& note : sug.notes) std::printf("    - %s\n", note.c_str());
    }
    {
        // Restraint: a smooth source with no resonances gets no mid cuts.
        auto x = pinkWithBump(fs, 8.0, 1000.0, 0.0, 0.25f);
        Settings cur;
        const auto sug = suggest(listenTo(x, fs), Role::keys, Genre::neutral, cur);
        CHECK(findMove(sug, pid::lmfDb) == 0.0f && findMove(sug, pid::hmfDb) <= 0.0f,
              "no invented mid cuts on a smooth source (LMF %.1f, HMF %.1f)", findMove(sug, pid::lmfDb), findMove(sug, pid::hmfDb));
    }
    {
        // A resonance on a sloping part of the target is still found (prominence check).
        auto x = pinkWithBump(fs, 8.0, 450.0, 7.0, 0.12f);
        const auto hp = design::highPass2(fs, 140.0, 0.7);
        Biquad st;
        for (auto& v : x) v = st.process(hp, v);
        Settings cur;
        const auto sug = suggest(listenTo(x, fs), Role::vocal, Genre::trap, cur);
        const auto hz = findMove(sug, pid::lmfHz);
        CHECK(findMove(sug, pid::lmfDb) <= -2.5f && hz > 360.0f && hz < 560.0f, "finds the 450 Hz box (LMF %.1f dB at %.0f Hz)",
              findMove(sug, pid::lmfDb), hz);
    }
    {
        // The suggested bell width follows the problem's width (fitted, then converted through the flavour law).
        for (const auto& [bumpQ, lo, hi] : { std::tuple<double, float, float> { 0.7, 0.5f, 0.95f }, { 1.4, 1.1f, 1.8f } })
            for (int fl : { 0, 1 })
            {
                Pink p; const auto c = design::peak(fs, 600.0, bumpQ, 8.0); Biquad bq;
                std::vector<float> y(static_cast<std::size_t>(fs * 8.0));
                for (auto& v : y) v = 0.2f * bq.process(c, p.next());
                Settings cur; cur.flavour = fl;
                const auto sug = suggest(listenTo(y, fs), Role::keys, Genre::neutral, cur);
                const auto db = findMove(sug, pid::lmfDb), q = findMove(sug, pid::lmfQ);
                const auto eff = static_cast<float>(bellQ(fl, q, db));
                CHECK(eff >= lo && eff <= hi && db <= -3.0f, "bump Q %.1f, flavour %s -> cut %.1f dB, effective Q %.2f", bumpQ, fl ? "G" : "E", db, eff);
            }
    }
    {
        // 808 in trap: keep the sub (no high-pass) and add drive for small speakers.
        std::vector<float> x(static_cast<std::size_t>(fs * 6));
        for (std::size_t i = 0; i < x.size(); ++i)
        {
            const auto t = static_cast<double>(i % 24000) / fs; // a hit every half second
            x[i] = static_cast<float>(0.7 * std::exp(-t * 3.0) * std::sin(2.0 * pi * 43.6 * t));
        }
        Settings cur;
        const auto sug = suggest(listenTo(x, fs), Role::sub808, Genre::trap, cur);
        CHECK(sug.valid, "808 suggestion valid");
        CHECK(findMove(sug, pid::hpfOn) == 0.0f, "no high-pass on the 808");
        CHECK(findMove(sug, pid::drive) == 35.0f, "drive for harmonics");
    }
    {
        // Quiet vocal: trim brings it to about -18 dBFS average.
        auto x = pinkWithBump(fs, 6.0, 1000.0, 0.0, 0.05f);
        const auto prof = listenTo(x, fs);
        Settings cur;
        const auto sug = suggest(prof, Role::vocal, Genre::rnb, cur);
        const auto trim = findMove(sug, pid::inTrim);
        CHECK(std::abs(prof.rmsDb + trim + 18.0f) < 0.6f, "trim to -18 dBFS (rms %.1f, trim %.1f)", prof.rmsDb, trim);

        // The suggested compressor should land near its gain-reduction goal when it actually runs.
        Settings run;
        for (const auto& m : sug.moves)
        {
            if (m.id == pid::inTrim) run.inTrimDb = m.value; else if (m.id == pid::compThr) run.compThrDb = m.value;
            else if (m.id == pid::compRatio) run.compRatio = m.value; else if (m.id == pid::compRel) run.compReleaseS = m.value;
        }
        Engine e; e.prepare(fs, 480); e.setSettings(run);
        double grSum = 0.0; int grCount = 0;
        for (std::size_t i = 0; i + 480 <= x.size(); i += 480)
        {
            std::vector<float> L(x.begin() + static_cast<long>(i), x.begin() + static_cast<long>(i + 480)), R = L;
            float* ch[2] { L.data(), R.data() };
            e.meters().compGrDb.store(0.0f);
            e.process(ch, 2, 480);
            if (i > 48000) { grSum += e.meters().compGrDb.load(); ++grCount; }
        }
        const auto avgGr = static_cast<float>(grSum / grCount);
        CHECK(avgGr < -2.5f && avgGr > -8.0f, "suggested comp gives roughly its 5 dB goal (measured %.1f dB)", avgGr);
    }

    std::printf("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
