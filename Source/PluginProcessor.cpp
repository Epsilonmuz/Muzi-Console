#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace muzi;

//==============================================================================
class MuziConsoleAudioProcessor::Worker final : public juce::Thread
{
public:
    explicit Worker(MuziConsoleAudioProcessor& p) : juce::Thread("Muzi Console Listen"), owner(p) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait(20);
            owner.workerTick();
        }
    }
private:
    MuziConsoleAudioProcessor& owner;
};

//==============================================================================
namespace
{
    juce::String hzText(float hz)
    {
        return hz < 1000.0f ? juce::String(juce::roundToInt(hz)) + " Hz"
                            : juce::String(hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
    }
    float hzFromText(const juce::String& t)
    {
        const auto v = t.getFloatValue();
        return t.containsIgnoreCase("k") ? v * 1000.0f : v;
    }
    juce::NormalisableRange<float> logRange(float lo, float hi)
    {
        return { lo, hi,
                 [](float a, float b, float t) { return a * std::pow(b / a, t); },
                 [](float a, float b, float v) { return std::log(v / a) / std::log(b / a); },
                 [](float a, float b, float v) { return juce::jlimit(a, b, v); } };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MuziConsoleAudioProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    const auto unit = [](juce::String suffix, int decimals)
    {
        return juce::AudioParameterFloatAttributes {}.withStringFromValueFunction(
            [suffix, decimals](float v, int) { return juce::String(v, decimals) + suffix; });
    };
    const auto signedDb = juce::AudioParameterFloatAttributes {}.withStringFromValueFunction(
        [](float v, int) { return (v > 0.04f ? "+" : "") + juce::String(v, 1) + " dB"; });
    const auto freq = juce::AudioParameterFloatAttributes {}
        .withStringFromValueFunction([](float v, int) { return hzText(v); })
        .withValueFromStringFunction([](const juce::String& t) { return hzFromText(t); });
    auto f = [&](const char* id, const char* name, juce::NormalisableRange<float> r, float def, juce::AudioParameterFloatAttributes a)
    { layout.add(std::make_unique<F>(juce::ParameterID { id, 1 }, name, r, def, a)); };
    auto b = [&](const char* id, const char* name, bool def)
    { layout.add(std::make_unique<B>(juce::ParameterID { id, 1 }, name, def)); };
    auto c = [&](const char* id, const char* name, juce::StringArray items, int def)
    { layout.add(std::make_unique<C>(juce::ParameterID { id, 1 }, name, items, def)); };

    c(pid::mode, "Mode", { "Channel", "Bus" }, 0);
    c(pid::flavour, "EQ Flavour", { "E", "G" }, 0);
    f(pid::inTrim, "Input Trim", { range::trimLo, range::trimHi, 0.1f }, 0.0f, signedDb);
    f(pid::outTrim, "Output", { range::trimLo, range::trimHi, 0.1f }, 0.0f, signedDb);

    b(pid::hpfOn, "HPF On", false);
    f(pid::hpfHz, "HPF", logRange(range::hpfLo, range::hpfHi), 80.0f, freq);
    b(pid::lpfOn, "LPF On", false);
    f(pid::lpfHz, "LPF", logRange(range::lpfLo, range::lpfHi), 18000.0f, freq);

    b(pid::eqOn, "EQ In", true);
    const juce::NormalisableRange<float> gain { range::gainLo, range::gainHi, 0.1f };
    f(pid::lfDb, "LF Gain", gain, 0.0f, signedDb);
    f(pid::lfHz, "LF Freq", logRange(range::lfLo, range::lfHi), 100.0f, freq);
    b(pid::lfBell, "LF Bell", false);
    f(pid::lmfDb, "LMF Gain", gain, 0.0f, signedDb);
    f(pid::lmfHz, "LMF Freq", logRange(range::lmfLo, range::lmfHi), 500.0f, freq);
    f(pid::lmfQ, "LMF Q", logRange(range::qLo, range::qHi), 1.0f, unit("", 2));
    f(pid::hmfDb, "HMF Gain", gain, 0.0f, signedDb);
    f(pid::hmfHz, "HMF Freq", logRange(range::hmfLo, range::hmfHi), 3000.0f, freq);
    f(pid::hmfQ, "HMF Q", logRange(range::qLo, range::qHi), 1.0f, unit("", 2));
    f(pid::hfDb, "HF Gain", gain, 0.0f, signedDb);
    f(pid::hfHz, "HF Freq", logRange(range::hfLo, range::hfHi), 10000.0f, freq);
    b(pid::hfBell, "HF Bell", false);
    b(pid::busGuard, "Bus Guard", true);

    b(pid::dynOn, "Dynamics In", true);
    b(pid::dynPre, "Dynamics Pre-EQ", false);
    f(pid::compThr, "Comp Threshold", { range::compThrLo, range::compThrHi, 0.1f }, 0.0f, unit(" dB", 1));
    f(pid::compRatio, "Comp Ratio", { range::ratioLo, range::ratioHi, 0.01f, 0.45f }, 4.0f, unit(":1", 1));
    f(pid::compRel, "Comp Release", logRange(range::relLo, range::relHi), 0.3f, unit(" s", 2));
    b(pid::compFast, "Comp Fast Attack", false);
    f(pid::gateThr, "Gate Threshold", { range::gateThrLo, range::gateThrHi, 0.1f }, -80.0f,
      juce::AudioParameterFloatAttributes {}.withStringFromValueFunction(
          [](float v, int) { return v <= -79.9f ? juce::String("Off") : juce::String(v, 1) + " dB"; }));
    f(pid::gateRange, "Gate Range", { 0.0f, 40.0f, 0.1f }, 40.0f, unit(" dB", 0));

    f(pid::busThr, "Bus Threshold", { range::busThrLo, range::busThrHi, 0.1f }, 0.0f, unit(" dB", 1));
    c(pid::busRatio, "Bus Ratio", { "2:1", "4:1", "10:1" }, 1);
    c(pid::busAttack, "Bus Attack", { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 3);
    c(pid::busRelease, "Bus Release", { "0.1 s", "0.3 s", "0.6 s", "1.2 s", "Auto" }, 4);
    f(pid::busMakeup, "Bus Make-up", { 0.0f, 15.0f, 0.1f }, 0.0f, unit(" dB", 1));
    f(pid::busMix, "Bus Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, unit("%", 0));
    b(pid::busScHpf, "Bus SC HPF", false);

    f(pid::drive, "Drive", { 0.0f, 100.0f, 1.0f }, 0.0f, unit("%", 0));
    return layout;
}

MuziConsoleAudioProcessor::MuziConsoleAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      params(*this, nullptr, "MuziConsole", makeLayout())
{
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            rawParams[withId->paramID] = params.getRawParameterValue(withId->paramID);

    params.state.setProperty("role", 0, nullptr);
    params.state.setProperty("genre", 1, nullptr);
    scratch.resize(16384);
    worker = std::make_unique<Worker>(*this);
    worker->startThread(juce::Thread::Priority::low);
}

MuziConsoleAudioProcessor::~MuziConsoleAudioProcessor()
{
    worker->stopThread(2000);
}

bool MuziConsoleAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void MuziConsoleAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    engine.setSettings(readSettings());
    engine.prepare(sampleRate, samplesPerBlock);
    setLatencySamples(0);
}

Settings MuziConsoleAudioProcessor::readSettings() const noexcept
{
    Settings s;
    s.mode = static_cast<int>(raw(pid::mode));
    s.flavour = static_cast<int>(raw(pid::flavour));
    s.inTrimDb = raw(pid::inTrim); s.outTrimDb = raw(pid::outTrim);
    s.hpfOn = raw(pid::hpfOn) > 0.5f; s.hpfHz = raw(pid::hpfHz);
    s.lpfOn = raw(pid::lpfOn) > 0.5f; s.lpfHz = raw(pid::lpfHz);
    s.eqOn = raw(pid::eqOn) > 0.5f;
    s.lfDb = raw(pid::lfDb); s.lfHz = raw(pid::lfHz); s.lfBell = raw(pid::lfBell) > 0.5f;
    s.lmfDb = raw(pid::lmfDb); s.lmfHz = raw(pid::lmfHz); s.lmfQ = raw(pid::lmfQ);
    s.hmfDb = raw(pid::hmfDb); s.hmfHz = raw(pid::hmfHz); s.hmfQ = raw(pid::hmfQ);
    s.hfDb = raw(pid::hfDb); s.hfHz = raw(pid::hfHz); s.hfBell = raw(pid::hfBell) > 0.5f;
    s.busGuard = raw(pid::busGuard) > 0.5f;
    s.dynOn = raw(pid::dynOn) > 0.5f; s.dynPreEq = raw(pid::dynPre) > 0.5f;
    s.compThrDb = raw(pid::compThr); s.compRatio = raw(pid::compRatio); s.compReleaseS = raw(pid::compRel);
    s.compFast = raw(pid::compFast) > 0.5f;
    s.gateThrDb = raw(pid::gateThr); s.gateRangeDb = raw(pid::gateRange);
    s.busThrDb = raw(pid::busThr); s.busRatio = static_cast<int>(raw(pid::busRatio));
    s.busAttack = static_cast<int>(raw(pid::busAttack)); s.busRelease = static_cast<int>(raw(pid::busRelease));
    s.busMakeupDb = raw(pid::busMakeup); s.busMix = raw(pid::busMix); s.busScHpf = raw(pid::busScHpf) > 0.5f;
    s.drive = raw(pid::drive);
    return s;
}

void MuziConsoleAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());
    engine.setSettings(readSettings());
    engine.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
}

//==============================================================================
// Listen & Suggest

void MuziConsoleAudioProcessor::startListening()
{
    stopRequest = false;
    startRequest = true;
}

void MuziConsoleAudioProcessor::stopListening()
{
    if (state.load() == ListenState::listening)
        stopRequest = true;
}

void MuziConsoleAudioProcessor::workerTick()
{
    if (startRequest.exchange(false))
    {
        listener.reset(currentRate.load());
        readPos = engine.tap().position();
        progress = 0.0;
        lastLiveUpdate = 0.0;
        engine.setListening(true);
        state = ListenState::listening;
    }
    if (state.load() != ListenState::listening)
        return;

    int n = 0;
    while ((n = engine.tap().pull(readPos, scratch.data(), static_cast<int>(scratch.size()))) > 0)
        listener.consume(scratch.data(), n);
    progress = listener.activeSeconds();

    if (listener.heardSeconds() - lastLiveUpdate > 0.2)
    {
        lastLiveUpdate = listener.heardSeconds();
        auto bands = listener.liveBandDb();
        const std::lock_guard<std::mutex> g(resultLock);
        live = std::move(bands);
    }

    if (listener.activeSeconds() >= listenSeconds || stopRequest.exchange(false))
        finishListening();
}

void MuziConsoleAudioProcessor::finishListening()
{
    engine.setListening(false);
    auto profile = listener.profile();
    auto result = muzi::suggest(profile, role(), genre(), readSettings());
    {
        const std::lock_guard<std::mutex> g(resultLock);
        lastProfile = std::move(profile);
        lastSuggestion = std::move(result);
        live.clear();
    }
    ++serial;
    state = ListenState::done;
}

std::vector<float> MuziConsoleAudioProcessor::liveBands() const
{
    const std::lock_guard<std::mutex> g(resultLock);
    return live;
}

Role MuziConsoleAudioProcessor::role() const noexcept
{
    return static_cast<Role>(juce::jlimit(0, static_cast<int>(Role::count) - 1, static_cast<int>(params.state.getProperty("role", 0))));
}

Genre MuziConsoleAudioProcessor::genre() const noexcept
{
    return static_cast<Genre>(juce::jlimit(0, static_cast<int>(Genre::count) - 1, static_cast<int>(params.state.getProperty("genre", 1))));
}

void MuziConsoleAudioProcessor::setRoleAndGenre(Role r, Genre g)
{
    params.state.setProperty("role", static_cast<int>(r), nullptr);
    params.state.setProperty("genre", static_cast<int>(g), nullptr);
    if (state.load() != ListenState::done)
        return;
    const std::lock_guard<std::mutex> lock(resultLock);
    lastSuggestion = muzi::suggest(lastProfile, r, g, readSettings());
    ++serial;
}

Suggestion MuziConsoleAudioProcessor::suggestion() const
{
    const std::lock_guard<std::mutex> g(resultLock);
    return lastSuggestion;
}

void MuziConsoleAudioProcessor::setParam(const juce::String& id, float value)
{
    if (auto* p = params.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

void MuziConsoleAudioProcessor::applySuggestion()
{
    const auto s = suggestion();
    if (! s.valid)
        return;
    undoValues.clear();
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*>(p))
            undoValues.push_back({ withId->paramID, withId->convertFrom0to1(withId->getValue()) });
    for (const auto& m : s.moves)
        setParam(m.id, m.value);
}

void MuziConsoleAudioProcessor::undoSuggestion()
{
    for (const auto& [id, v] : undoValues)
        setParam(id, v);
    undoValues.clear();
}

//==============================================================================
void MuziConsoleAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void MuziConsoleAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(params.state.getType()))
            params.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* MuziConsoleAudioProcessor::createEditor()
{
    return new MuziConsoleAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MuziConsoleAudioProcessor();
}
