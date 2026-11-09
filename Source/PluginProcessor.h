#pragma once

#include <JuceHeader.h>

#include "ConsoleEngine.h"
#include "Engineer.h"
#include "ParamIDs.h"

#include <mutex>

class MuziConsoleAudioProcessor : public juce::AudioProcessor
{
public:
    MuziConsoleAudioProcessor();
    ~MuziConsoleAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Muzi Console"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int size) override;

    muzi::Settings readSettings() const noexcept;
    muzi::Meters& meters() noexcept { return engine.meters(); }
    double sampleRateForDisplay() const noexcept { return currentRate.load(); }

    // ---- Listen & Suggest (message thread) --------------------------------------
    enum class ListenState { idle, listening, done };
    static constexpr double listenSeconds = 10.0;

    void startListening();
    void stopListening();               // finishes early with what was heard (if enough)
    ListenState listenState() const noexcept { return state.load(); }
    double listenProgress() const noexcept { return progress.load(); }   // active seconds heard
    std::vector<float> liveBands() const;

    muzi::Role role() const noexcept;
    muzi::Genre genre() const noexcept;
    void setRoleAndGenre(muzi::Role, muzi::Genre); // re-suggests from the last listen, no need to listen again
    muzi::Suggestion suggestion() const;
    int suggestionSerial() const noexcept { return serial.load(); }

    void applySuggestion();
    bool canUndo() const noexcept { return ! undoValues.empty(); }
    void undoSuggestion();

    juce::AudioProcessorValueTreeState params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    float raw(const char* id) const noexcept { return rawParams.at(id)->load(std::memory_order_relaxed); }
    void setParam(const juce::String& id, float value);

    class Worker;
    friend class Worker;
    void workerTick();
    void finishListening();

    muzi::Engine engine;
    std::map<juce::String, std::atomic<float>*> rawParams;
    std::atomic<double> currentRate { 44100.0 };

    std::unique_ptr<Worker> worker;
    std::atomic<ListenState> state { ListenState::idle };
    std::atomic<bool> startRequest { false }, stopRequest { false };
    std::atomic<double> progress { 0.0 };
    std::atomic<int> serial { 0 };
    muzi::Listener listener;     // worker thread only
    std::uint64_t readPos = 0;   // worker thread only
    std::vector<float> scratch;  // worker thread only
    double lastLiveUpdate = 0.0;

    mutable std::mutex resultLock;
    muzi::Profile lastProfile;
    muzi::Suggestion lastSuggestion;
    std::vector<float> live;

    std::vector<std::pair<juce::String, float>> undoValues;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MuziConsoleAudioProcessor)
};
