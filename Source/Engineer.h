#pragma once

// Muzi Console — the Listen & Suggest engineer (no JUCE).
//
// Listener: runs on a worker thread, never on the audio thread. It reads the raw track,
// measures its spectrum on critical bands (ERB scale) and its level over time.
// suggest(): compares that against a target for the chosen role and genre and proposes
// console settings, each with a plain-language reason. Everything is rule-based and explainable.

#include "ConsoleDSP.h"

#include <complex>
#include <string>
#include <vector>

namespace muzi
{
enum class Role { vocal, kick, sub808, bass, snare, hats, guitar, keys, sample, mixBus, count };
enum class Genre { neutral, trap, drill, rnb, boomBap, pop, count };

const char* roleName(Role) noexcept;
const char* genreName(Genre) noexcept;

// Critical bands: one band per ERB from 20 Hz to 20 kHz.
struct CriticalBands
{
    std::vector<float> lo, hi, centre;
    static const CriticalBands& get();
};

struct Profile
{
    bool valid = false;
    double activeSeconds = 0.0;
    std::vector<float> bandDb;      // power per octave, dB (pink noise reads flat)
    std::vector<float> blockPeakDb; // 10 ms peak levels of the active audio, dBFS
    float rmsDb = -120.0f;          // average level of the active audio, dBFS
    float peakDb = -120.0f;
};

class Listener
{
public:
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder, hop = fftSize / 2;

    Listener();
    void reset(double sampleRate);
    void consume(const float* samples, int count);

    double activeSeconds() const noexcept { return active; }
    double heardSeconds() const noexcept { return heard; }
    // Band levels so far (for the live display while listening).
    std::vector<float> liveBandDb() const;
    Profile profile() const;

private:
    void analyseFrame();
    void fft(std::vector<std::complex<float>>& data) const;

    double fs = 48000.0, active = 0.0, heard = 0.0;
    std::vector<float> window, frame;
    int frameFill = 0;
    std::vector<int> binToBand;
    std::vector<std::complex<float>> work;
    std::vector<std::size_t> bitReverse;
    std::vector<std::complex<float>> twiddle;

    struct Frame { float levelDb; std::vector<float> power; };
    std::vector<Frame> frames;

    int blockLen = 480, blockFill = 0;
    float blockPeak = 0.0f; double blockSq = 0.0;
    struct Block { float peakDb, rmsDb; };
    std::vector<Block> blocks;
    float maxPeak = 0.0f;
};

struct Move
{
    std::string id;
    float value;
};

struct Suggestion
{
    bool valid = false;
    std::string headline;
    std::vector<Move> moves;
    std::vector<std::string> notes;
    // For the display: band centres, the measured spectrum and the target, aligned to each other.
    std::vector<float> freqs, measuredDb, targetDb;
};

// current: the strip as it is now (mode, flavour and bus trim are read from it).
Suggestion suggest(const Profile& profile, Role role, Genre genre, const Settings& current);

// The strip as it would be with the suggestion applied (used to preview it before APPLY).
Settings withMoves(Settings s, const std::vector<Move>& moves);

// The target curve for a role and genre at a frequency (dB re pink, per octave).
float targetAt(Role role, Genre genre, float hz);
}
