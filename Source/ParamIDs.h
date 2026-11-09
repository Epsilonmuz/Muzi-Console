#pragma once

// Parameter IDs. Never rename these: saved sessions and automation depend on them.
namespace muzi::pid
{
constexpr const char* mode = "mode";
constexpr const char* flavour = "flavour";
constexpr const char* inTrim = "inTrim";
constexpr const char* outTrim = "outTrim";
constexpr const char* hpfOn = "hpfOn";
constexpr const char* hpfHz = "hpfHz";
constexpr const char* lpfOn = "lpfOn";
constexpr const char* lpfHz = "lpfHz";
constexpr const char* eqOn = "eqOn";
constexpr const char* lfDb = "lfDb";
constexpr const char* lfHz = "lfHz";
constexpr const char* lfBell = "lfBell";
constexpr const char* lmfDb = "lmfDb";
constexpr const char* lmfHz = "lmfHz";
constexpr const char* lmfQ = "lmfQ";
constexpr const char* hmfDb = "hmfDb";
constexpr const char* hmfHz = "hmfHz";
constexpr const char* hmfQ = "hmfQ";
constexpr const char* hfDb = "hfDb";
constexpr const char* hfHz = "hfHz";
constexpr const char* hfBell = "hfBell";
constexpr const char* busGuard = "busGuard";
constexpr const char* dynOn = "dynOn";
constexpr const char* dynPre = "dynPre";
constexpr const char* compThr = "compThr";
constexpr const char* compRatio = "compRatio";
constexpr const char* compRel = "compRel";
constexpr const char* compFast = "compFast";
constexpr const char* gateThr = "gateThr";
constexpr const char* gateRange = "gateRange";
constexpr const char* busThr = "busThr";
constexpr const char* busRatio = "busRatio";
constexpr const char* busAttack = "busAttack";
constexpr const char* busRelease = "busRelease";
constexpr const char* busMakeup = "busMakeup";
constexpr const char* busMix = "busMix";
constexpr const char* busScHpf = "busScHpf";
constexpr const char* drive = "drive";
}

namespace muzi::range
{
// Shared by the plugin parameters and the engineer, so suggestions always land inside the knobs.
constexpr float trimLo = -24.0f, trimHi = 24.0f;
constexpr float hpfLo = 16.0f, hpfHi = 350.0f;
constexpr float lpfLo = 3000.0f, lpfHi = 22000.0f;
constexpr float gainLo = -15.0f, gainHi = 15.0f;
constexpr float lfLo = 30.0f, lfHi = 450.0f;
constexpr float lmfLo = 200.0f, lmfHi = 2500.0f;
constexpr float hmfLo = 600.0f, hmfHi = 7000.0f;
constexpr float hfLo = 1500.0f, hfHi = 16000.0f;
constexpr float qLo = 0.5f, qHi = 2.5f;
constexpr float compThrLo = -40.0f, compThrHi = 0.0f;
constexpr float ratioLo = 1.0f, ratioHi = 20.0f;
constexpr float relLo = 0.1f, relHi = 4.0f;
constexpr float gateThrLo = -80.0f, gateThrHi = 0.0f;
constexpr float busThrLo = -30.0f, busThrHi = 0.0f;
}
