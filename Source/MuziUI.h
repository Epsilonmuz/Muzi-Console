#pragma once

// Muzi Console look: palette, typeface and the LookAndFeel (knobs, buttons, combo boxes).

#include "PluginProcessor.h"

#include <BinaryData.h>

namespace muziui
{
namespace palette
{
inline const juce::Colour background { 0xff0c0d10 };
inline const juce::Colour panel      { 0xff15171c };
inline const juce::Colour panelHi    { 0xff1b1e24 };
inline const juce::Colour glass      { 0xff08090c };
inline const juce::Colour edge       { 0xff272b33 };
inline const juce::Colour text       { 0xfff3f0e8 };
inline const juce::Colour muted      { 0xff9c9a93 };
inline const juce::Colour dim        { 0xff5a5c62 };
inline const juce::Colour gold       { 0xffffc857 };   // brand + the engineer
inline const juce::Colour lf         { 0xffff9f43 };
inline const juce::Colour lmf        { 0xff2ed3b7 };
inline const juce::Colour hmf        { 0xff9b8cff };
inline const juce::Colour hf         { 0xffff5e78 };
inline const juce::Colour filter     { 0xff7aa7ff };
inline const juce::Colour dyn        { 0xffc6f36b };
inline const juce::Colour hot        { 0xffff4d4d };
}

enum class Weight { medium, semibold, bold };

inline juce::Font font(float height, Weight weight = Weight::medium)
{
    // Barlow Semi Condensed (SIL Open Font License), embedded so the panel looks the same everywhere.
    static const auto medium = juce::Typeface::createSystemTypefaceFor(
        BinaryData::BarlowSemiCondensedMedium_ttf, BinaryData::BarlowSemiCondensedMedium_ttfSize);
    static const auto semibold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize);
    static const auto bold = juce::Typeface::createSystemTypefaceFor(
        BinaryData::BarlowSemiCondensedBold_ttf, BinaryData::BarlowSemiCondensedBold_ttfSize);
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

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float value, float start, float end, juce::Slider& slider) override
    {
        using namespace palette;
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height));
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
        if (radius <= 4.0f)
            return;
        const auto centre = bounds.getCentre();
        const auto& props = slider.getProperties();
        const auto bipolar = static_cast<bool>(props["bipolar"]);
        const auto fromEnd = static_cast<bool>(props["fromEnd"]); // thresholds: the arc grows as the threshold drops
        const auto accent = props.contains("accent") ? juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(props["accent"]))) : text;
        const auto enabled = slider.isEnabled();
        const auto angle = start + value * (end - start);
        const auto from = bipolar ? (start + end) * 0.5f : fromEnd ? end : start;
        const auto live = enabled && (bipolar ? std::abs(value - 0.5f) > 0.004f : fromEnd ? value < 0.996f : value > 0.004f);
        const auto ring = radius - 1.5f;

        juce::Path rail, active;
        rail.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, start, end, true);
        active.addCentredArc(centre.x, centre.y, ring, ring, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        const juce::PathStrokeType stroke(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(edge);
        g.strokePath(rail, stroke);
        if (live)
        {
            g.setColour((bipolar ? accent : accent.withMultipliedSaturation(0.35f).withMultipliedBrightness(0.85f)));
            g.strokePath(active, stroke);
        }
        if (bipolar)
        {
            const auto top = centre.getPointOnCircumference(ring + 3.5f, (start + end) * 0.5f);
            g.setColour(dim);
            g.fillEllipse(juce::Rectangle<float>(2.4f, 2.4f).withCentre(top));
        }

        // Cap: a dark machined puck with a light catch
        const auto capR = ring - 6.0f;
        const auto cap = juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(cap.translated(0.0f, 2.5f).expanded(1.0f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff30333a), cap.getX(), cap.getY(),
                                               juce::Colour(0xff121317), cap.getRight(), cap.getBottom(), false));
        g.fillEllipse(cap);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        juce::Path catchLight;
        catchLight.addCentredArc(centre.x, centre.y, capR - 1.0f, capR - 1.0f, 0.0f, -2.2f, 0.6f, true);
        g.strokePath(catchLight, juce::PathStrokeType(1.2f));
        g.setColour(edge.brighter(0.25f));
        g.drawEllipse(cap, 1.0f);

        const auto p0 = centre.getPointOnCircumference(capR * 0.3f, angle);
        const auto p1 = centre.getPointOnCircumference(capR * 0.9f, angle);
        g.setColour(enabled ? (live && bipolar ? accent : text) : dim);
        g.drawLine(juce::Line<float>(p0, p1), 2.2f);
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
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        using namespace palette;
        const auto area = b.getLocalBounds().toFloat().reduced(0.5f);
        const auto accent = b.getProperties().contains("accent")
                              ? juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(b.getProperties()["accent"]))) : gold;
        const auto on = b.getToggleState();
        const auto radius = juce::jmin(6.0f, area.getHeight() * 0.5f);
        g.setColour(on ? accent.withAlpha(0.18f) : (down ? edge : glass));
        g.fillRoundedRectangle(area, radius);
        g.setColour(on ? accent.withAlpha(0.9f) : (over ? dim : edge));
        g.drawRoundedRectangle(area, radius, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        using namespace palette;
        const auto accent = b.getProperties().contains("accent")
                              ? juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(b.getProperties()["accent"]))) : gold;
        g.setFont(font(juce::jmin(15.0f, static_cast<float>(b.getHeight()) * 0.6f), Weight::bold));
        g.setColour(! b.isEnabled() ? dim : b.getToggleState() ? accent : muted);
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
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
