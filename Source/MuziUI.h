#pragma once

// Muzi Console look: palette, typeface and the LookAndFeel (knobs, buttons, combo boxes).

#include "PluginProcessor.h"
#include "../Shared/epsilon_ui/EpsilonHeader.h"
#include "../Shared/epsilon_ui/EpsilonSignature.h"

#include <BinaryData.h>

namespace muziui
{
namespace palette
{
// Epsilon Sounds design system v1 (Muzi tier). Band colours stay: they identify the EQ bands.
inline const epsilon_ui::Palette brand = epsilon_ui::palette (epsilon_ui::Tier::Muzi);
inline const juce::Colour background = brand.bg;
inline const juce::Colour panel      = brand.surface;
inline const juce::Colour panelHi    = brand.raised;
inline const juce::Colour glass      = brand.bg.darker (0.3f);
inline const juce::Colour edge       = brand.line;
inline const juce::Colour text       = brand.ink;
inline const juce::Colour muted      = brand.inkMuted;
inline const juce::Colour dim        = epsilon_ui::faint (brand);
inline const juce::Colour gold       = brand.live;   // brand + the engineer
inline const juce::Colour lf         { 0xffff9f43 };
inline const juce::Colour lmf        { 0xff2ed3b7 };
inline const juce::Colour hmf        { 0xff9b8cff };
inline const juce::Colour hf         { 0xffff5e78 };
inline const juce::Colour filter     { 0xff7aa7ff };
inline const juce::Colour dyn        { 0xffc6f36b };
inline const juce::Colour hot        = brand.meterClip;
}

enum class Weight { medium, semibold, bold };

inline juce::Font font(float height, Weight weight = Weight::medium)
{
    // Epsilon Dial, the Epsilon Sounds house typeface, embedded so the panel looks the same everywhere.
    static const auto medium = juce::Typeface::createSystemTypefaceFor(
        BinaryData::EpsilonDialRegular_ttf, BinaryData::EpsilonDialRegular_ttfSize);
    static const auto semibold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::EpsilonDialBold_ttf, BinaryData::EpsilonDialBold_ttfSize);
    static const auto bold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::EpsilonDialBold_ttf, BinaryData::EpsilonDialBold_ttfSize);
    const auto& face = weight == Weight::bold ? bold : weight == Weight::semibold ? semibold : medium;
    return juce::Font(juce::FontOptions(face).withHeight(height));
}

inline juce::String hzLabel(float hz)
{
    if (hz >= 1000.0f) return juce::String(hz / 1000.0f, hz >= 10000.0f ? 0 : 1) + "k";
    return juce::String(juce::roundToInt(hz));
}

//==============================================================================
// Knob properties (set on the Slider):
//   "accent"  colour of the value arc when the control is live
//   "bipolar" arc grows from the centre (gain knobs); the arc lights only when away from 0
//   "big"     larger value text
//   "fromEnd" arc grows from the right end (thresholds)
class MuziLook final : public juce::LookAndFeel_V4
{
public:
    MuziLook()
    {
        using namespace palette;
        setColour(juce::ComboBox::backgroundColourId, glass);
        setColour(juce::ComboBox::outlineColourId, edge);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::arrowColourId, muted);
        setColour(juce::PopupMenu::backgroundColourId, panelHi);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, edge);
        setColour(juce::PopupMenu::highlightedTextColourId, text);
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, edge);
        setColour(juce::TextEditor::backgroundColourId, glass);
        setColour(juce::TextEditor::textColourId, text);
        setColour(juce::TextEditor::focusedOutlineColourId, edge);
        setColour(juce::CaretComponent::caretColourId, text);
        setColour(juce::TooltipWindow::backgroundColourId, panelHi);
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, edge);
        setColour(juce::ScrollBar::thumbColourId, edge);
    }

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& f) override
    {
        return font(f.getHeight(), f.isBold() ? Weight::bold : Weight::medium).getTypefacePtr();
    }

    // Epsilon Sounds signature coin-edge knob (static layers cached)
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float value, float, float, juce::Slider& slider) override
    {
        epsilon_ui::signature::knob(g, juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height).reduced(4.0f),
                                    value, slider, epsilon_ui::Tier::Muzi);
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(font(static_cast<bool>(slider.getProperties()["big"]) ? 16.0f : 14.0f, Weight::semibold));
        label->setJustificationType(juce::Justification::centred);
        label->setMinimumHorizontalScale(0.8f);
        label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        label->setColour(juce::Label::outlineWhenEditingColourId, palette::edge);
        return label;
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto area = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        g.setColour(palette::glass);
        g.fillRoundedRectangle(area, 6.0f);
        g.setColour(box.isMouseOver(true) ? palette::dim : palette::edge);
        g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);
        juce::Path arrow;
        const auto ax = static_cast<float>(width) - 14.0f, ay = static_cast<float>(height) * 0.5f;
        arrow.addTriangle(ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour(palette::muted);
        g.fillPath(arrow);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(box.getLocalBounds().reduced(10, 1).withTrimmedRight(14));
        label.setFont(font(16.0f, Weight::semibold));
        label.setJustificationType(juce::Justification::centredLeft);
    }

    juce::Font getPopupMenuFont() override { return font(16.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(14.0f, Weight::bold); }

    // Toggle buttons light in their "accent" colour when on; plain buttons are flat panels.
    // Epsilon Sounds signature keycap; toggles carry the champagne lume bar when on
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool over, bool down) override
    {
        epsilon_ui::signature::keycap(g, button, over, down, epsilon_ui::Tier::Muzi);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        using namespace palette;
        const auto accent = b.getProperties().contains("accent")
                              ? juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(b.getProperties()["accent"]))) : gold;
        g.setFont(font(juce::jmin(15.0f, static_cast<float>(b.getHeight()) * 0.52f), Weight::bold));
        g.setColour(! b.isEnabled() ? dim : b.getToggleState() ? accent : muted);
        // toggles leave room for the keycap's lume bar
        const bool toggle = b.getClickingTogglesState() || b.getToggleState();
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0).withTrimmedBottom(toggle ? 5 : 0), juce::Justification::centred, 1);
    }

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                       int thumbStart, int thumbSize, bool over, bool) override
    {
        const auto thumb = vertical ? juce::Rectangle<int>(x + w / 2 - 2, thumbStart, 4, thumbSize)
                                    : juce::Rectangle<int>(thumbStart, y + h / 2 - 2, thumbSize, 4);
        g.setColour(over ? palette::dim : palette::edge);
        g.fillRoundedRectangle(thumb.toFloat(), 2.0f);
    }
};

inline void setAccent(juce::Component& c, juce::Colour colour)
{
    c.getProperties().set("accent", static_cast<juce::int64>(colour.getARGB()));
}
}
