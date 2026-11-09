// Headless check of the whole plugin: runs a real Listen & Suggest pass through processBlock,
// renders the editor to PNG (idle, after a listen, after Apply, bus mode) and times a full repaint.
// Usage: MuziSnapshot <output folder>

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <random>
#include <thread>

namespace
{
struct Pink
{
    std::mt19937 rng { 99 };
    std::normal_distribution<float> n { 0.0f, 1.0f };
    float b[7] {};
    float next()
    {
        const float w = n(rng);
        b[0] = 0.99886f * b[0] + w * 0.0555179f; b[1] = 0.99332f * b[1] + w * 0.0750759f; b[2] = 0.96900f * b[2] + w * 0.1538520f;
        b[3] = 0.86650f * b[3] + w * 0.3104856f; b[4] = 0.55000f * b[4] + w * 0.5329522f; b[5] = -0.7616f * b[5] - w * 0.0168980f;
        const float out = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362f;
        b[6] = w * 0.115926f;
        return out * 0.11f;
    }
};

void save(juce::Component& c, const juce::File& file)
{
    const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
    std::printf("wrote %s\n", file.getFullPathName().toRawUTF8());
}

void pump(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

// A "vocal" with problems the engineer should find: boxy at 450 Hz, harsh at 3.6 kHz, quiet.
void listenToTestVocal(MuziConsoleAudioProcessor& p, float level, double boxDb, double harshDb, bool lowEnd)
{
    const double fs = 48000.0;
    using namespace muzi;
    const auto box = design::peak(fs, 450.0, 1.4, boxDb), harsh = design::peak(fs, 3600.0, 3.0, harshDb);
    const auto hp = design::highPass2(fs, lowEnd ? 30.0 : 140.0, 0.7), lp = design::lowPass2(fs, lowEnd ? 16000.0 : 11000.0, 0.7);
    Biquad s1, s2, s3, s4;
    Pink pink;
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    p.startListening();
    pump(60);
    for (int block = 0; block < 1200 && p.listenState() != MuziConsoleAudioProcessor::ListenState::done; ++block)
    {
        for (int i = 0; i < 512; ++i)
        {
            auto x = pink.next();
            x = s1.process(box, x); x = s2.process(harsh, x); x = s3.process(hp, x); x = s4.process(lp, x);
            // syllable-ish envelope
            const auto t = static_cast<double>(block * 512 + i) / fs;
            const auto env = 0.55 + 0.45 * std::sin(2.0 * muzi::pi * 3.0 * t);
            buffer.setSample(0, i, level * x * static_cast<float>(env));
            buffer.setSample(1, i, level * x * static_cast<float>(env));
        }
        p.processBlock(buffer, midi);
        std::this_thread::sleep_for(std::chrono::microseconds(1500));
        if (block % 20 == 0) pump(1);
    }
    for (int i = 0; i < 100 && p.listenState() != MuziConsoleAudioProcessor::ListenState::done; ++i)
        pump(20);
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    MuziConsoleAudioProcessor processor;
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);
    processor.setRoleAndGenre(muzi::Role::vocal, muzi::Genre::trap);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    editor->setSize(MuziConsoleAudioProcessorEditor::baseWidth, MuziConsoleAudioProcessorEditor::baseHeight);
    pump(200);
    save(*editor, outDir.getChildFile("muzi-console-idle.png"));

    listenToTestVocal(processor, 0.12f, 7.0, 6.0, false);
    const auto sug = processor.suggestion();
    std::printf("listen state %d, valid %d: %s\n", static_cast<int>(processor.listenState()), sug.valid ? 1 : 0, sug.headline.c_str());
    for (const auto& n : sug.notes) std::printf("  - %s\n", n.c_str());
    pump(300);
    save(*editor, outDir.getChildFile("muzi-console-suggested.png"));

    processor.applySuggestion();
    pump(300);
    save(*editor, outDir.getChildFile("muzi-console-applied.png"));

    // Repaint cost of the full panel at 1x (software render, like an Intel Mac without GPU help).
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < 20; ++i)
            juce::ignoreUnused(editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f));
        std::printf("full panel repaint: %.1f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 20.0);
    }

    // Bus mode on a "mix"
    processor.undoSuggestion();
    if (auto* m = processor.params.getParameter(muzi::pid::mode)) m->setValueNotifyingHost(1.0f);
    processor.setRoleAndGenre(muzi::Role::mixBus, muzi::Genre::trap);
    listenToTestVocal(processor, 0.3f, 4.0, 3.0, true);
    pump(300);
    const auto bus = processor.suggestion();
    std::printf("bus: %s\n", bus.headline.c_str());
    for (const auto& n : bus.notes) std::printf("  - %s\n", n.c_str());
    processor.applySuggestion();
    pump(300);
    save(*editor, outDir.getChildFile("muzi-console-bus.png"));

    // Scaled down (smallest window) to check the labels stay readable
    editor->setSize(MuziConsoleAudioProcessorEditor::baseWidth * 3 / 4, MuziConsoleAudioProcessorEditor::baseHeight * 3 / 4);
    pump(200);
    save(*editor, outDir.getChildFile("muzi-console-small.png"));

    editor.reset();
    pump(100);
    return 0;
}
