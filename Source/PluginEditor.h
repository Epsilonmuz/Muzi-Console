#pragma once

#include "PluginProcessor.h"

namespace muziui { class MainPanel; class MuziLook; }

class MuziConsoleAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    static constexpr int baseWidth = 1100, baseHeight = 720;

    explicit MuziConsoleAudioProcessorEditor(MuziConsoleAudioProcessor&);
    ~MuziConsoleAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::unique_ptr<muziui::MuziLook> look;
    std::unique_ptr<muziui::MainPanel> panel;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MuziConsoleAudioProcessorEditor)
};
