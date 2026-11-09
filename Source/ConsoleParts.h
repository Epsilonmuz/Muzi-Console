#pragma once

// Panel parts: knobs with labels, the meters and the engineer panel.

#include "MuziUI.h"

namespace muziui
{
//==============================================================================
struct Knob
{
    Knob(juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& name, juce::Colour accent,
         bool bipolar = false, bool big = false)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        // Set on the slider itself: the value box can be created before the panel's LookAndFeel reaches it.
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxTextColourId, palette::text);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 18);
        slider.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
        setAccent(slider, accent);
        slider.getProperties().set("bipolar", bipolar);
        slider.getProperties().set("big", big);
        slider.setPopupDisplayEnabled(false, false, nullptr);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, slider);
        if (auto* p = state.getParameter(id))
            slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        label.setText(name, juce::dontSendNotification);
        label.setFont(font(13.0f, Weight::bold));
        label.setColour(juce::Label::textColourId, palette::muted);
        label.setJustificationType(juce::Justification::centred);
        label.setInterceptsMouseClicks(false, false);
    }

    void addTo(juce::Component& parent) { parent.addAndMakeVisible(slider); parent.addAndMakeVisible(label); }
    void setVisible(bool v) { slider.setVisible(v); label.setVisible(v); }
    void setBounds(juce::Rectangle<int> area)
    {
        label.setBounds(area.removeFromTop(16));
        slider.setBounds(area);
    }
    void setDimmed(bool d) { slider.setAlpha(d ? 0.45f : 1.0f); }

    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

struct Toggle
{
    Toggle(juce::AudioProcessorValueTreeState& state, const char* id, const juce::String& text, juce::Colour accent)
    {
        button.setButtonText(text);
        button.setClickingTogglesState(true);
        setAccent(button, accent);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, id, button);
    }
    juce::TextButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

// Two buttons driving a two-choice parameter (Channel/Bus, E/G).
class Segmented final : public juce::Component
{
public:
    Segmented(juce::AudioProcessorValueTreeState& state, const char* id, juce::StringArray names, juce::Colour accent)
        : attachment(*state.getParameter(id), [this](float v) { show(juce::roundToInt(v)); }, nullptr)
    {
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = buttons.add(new juce::TextButton(names[i]));
            setAccent(*b, accent);
            b->onClick = [this, i] { attachment.setValueAsCompleteGesture(static_cast<float>(i)); };
            addAndMakeVisible(b);
        }
        attachment.sendInitialUpdate();
    }

    void setTooltip(const juce::String& t) { for (auto* b : buttons) b->setTooltip(t); }

    void resized() override
    {
        auto area = getLocalBounds();
        const auto w = area.getWidth() / juce::jmax(1, buttons.size());
        for (auto* b : buttons) b->setBounds(area.removeFromLeft(w).reduced(2, 0));
    }

private:
    void show(int index)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState(i == index, juce::dontSendNotification);
    }
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};

//==============================================================================
class MeterStrip final : public juce::Component, private juce::Timer
{
public:
    explicit MeterStrip(MuziConsoleAudioProcessor& p) : processor(p) { startTimerHz(30); }

    void paint(juce::Graphics& g) override
    {
        using namespace palette;
        auto area = getLocalBounds().toFloat();
        const auto columnW = area.getWidth() / 3.0f;
        drawColumn(g, area.removeFromLeft(columnW).reduced(4.0f, 0.0f), "IN", inDb, false);
        drawColumn(g, area.removeFromLeft(columnW).reduced(4.0f, 0.0f), "GR", grDb, true);
        drawColumn(g, area.reduced(4.0f, 0.0f), "OUT", outDb, false);
    }

private:
    void timerCallback() override
    {
        auto& m = processor.meters();
        const auto in = gainToDb(m.inPeak.exchange(0.0f)), out = gainToDb(m.outPeak.exchange(0.0f));
        const auto gr = juce::jmin(m.compGrDb.exchange(0.0f), 0.0f) + juce::jmin(m.gateGrDb.exchange(0.0f), 0.0f);
        const auto fall = 1.2f;
        inDb = juce::jmax(in, inDb - fall);
        outDb = juce::jmax(out, outDb - fall);
        grDb = gr < grDb ? gr : juce::jmin(0.0f, grDb + fall);
        if (std::abs(inDb - shownIn) + std::abs(outDb - shownOut) + std::abs(grDb - shownGr) > 0.2f)
        {
            shownIn = inDb; shownOut = outDb; shownGr = grDb;
            repaint();
        }
    }

    static float gainToDb(float g) { return g > 1.0e-5f ? 20.0f * std::log10(g) : -100.0f; }

    void drawColumn(juce::Graphics& g, juce::Rectangle<float> r, const char* name, float db, bool reduction)
    {
        using namespace palette;
        g.setFont(font(11.0f, Weight::bold));
        g.setColour(muted);
        g.drawText(name, r.removeFromBottom(16.0f).expanded(6.0f, 0.0f), juce::Justification::centred);
        auto bar = r.withSizeKeepingCentre(10.0f, r.getHeight());
        g.setColour(glass);
        g.fillRoundedRectangle(bar, 3.0f);
        if (reduction)
        {
            const auto t = juce::jlimit(0.0f, 1.0f, -db / 20.0f);
            if (t > 0.002f)
            {
                g.setColour(dyn);
                g.fillRoundedRectangle(bar.withHeight(bar.getHeight() * t), 3.0f);
            }
        }
        else
        {
            const auto t = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
            if (t > 0.002f)
            {
                const auto level = bar.withTrimmedTop(bar.getHeight() * (1.0f - t));
                g.setColour(db > -0.5f ? hot : db > -12.0f ? gold : lmf);
                g.fillRoundedRectangle(level, 3.0f);
            }
            const auto y18 = bar.getY() + bar.getHeight() * (1.0f - 42.0f / 66.0f);
            g.setColour(text.withAlpha(0.35f));
            g.drawHorizontalLine(juce::roundToInt(y18), bar.getX() - 3.0f, bar.getRight() + 3.0f);
        }
    }

    MuziConsoleAudioProcessor& processor;
    float inDb = -100.0f, outDb = -100.0f, grDb = 0.0f;
    float shownIn = 1.0f, shownOut = 1.0f, shownGr = 1.0f;
};

//==============================================================================
class NotesView final : public juce::Component
{
public:
    void setNotes(const juce::String& headlineText, const std::vector<std::string>& lines, bool good)
    {
        headline = headlineText;
        notes.clear();
        for (const auto& l : lines) notes.add(juce::String::fromUTF8(l.c_str()));
        ok = good;
        layoutText();
        repaint();
    }

    void layoutText()
    {
        const auto w = static_cast<float>(juce::jmax(40, getWidth() - 4));
        layout.clear();
        float y = 0.0f;
        auto addBlock = [&](const juce::String& t, juce::Font f, juce::Colour c, float indent)
        {
            juce::AttributedString a;
            a.append(t, f, c);
            a.setWordWrap(juce::AttributedString::byWord);
            juce::TextLayout tl;
            tl.createLayout(a, w - indent);
            layout.push_back({ std::move(tl), y, indent });
            y += layout.back().text.getHeight() + 6.0f;
        };
        if (headline.isNotEmpty())
            addBlock(headline, font(16.0f, Weight::bold), ok ? palette::gold : palette::muted, 0.0f);
        for (const auto& n : notes)
            addBlock(n, font(14.5f), palette::text.withAlpha(0.88f), 12.0f);
        contentHeight = juce::roundToInt(y);
    }

    int getContentHeight() const { return contentHeight; }
    void resized() override { layoutText(); }

    void paint(juce::Graphics& g) override
    {
        for (const auto& b : layout)
        {
            if (b.indent > 0.0f)
            {
                g.setColour(palette::gold.withAlpha(0.8f));
                g.fillEllipse(2.0f, b.y + 7.0f, 4.0f, 4.0f);
            }
            b.text.draw(g, juce::Rectangle<float>(b.indent, b.y, static_cast<float>(getWidth()) - b.indent, b.text.getHeight()));
        }
    }

private:
    struct Block { juce::TextLayout text; float y, indent; };
    std::vector<Block> layout;
    juce::String headline;
    juce::StringArray notes;
    bool ok = false;
    int contentHeight = 0;
};

//==============================================================================
class EngineerPanel final : public juce::Component
{
public:
    explicit EngineerPanel(MuziConsoleAudioProcessor& p) : processor(p)
    {
        for (int i = 0; i < static_cast<int>(muzi::Role::count); ++i)
            roleBox.addItem(muzi::roleName(static_cast<muzi::Role>(i)), i + 1);
        for (int i = 0; i < static_cast<int>(muzi::Genre::count); ++i)
            genreBox.addItem(muzi::genreName(static_cast<muzi::Genre>(i)), i + 1);
        roleBox.setSelectedId(static_cast<int>(processor.role()) + 1, juce::dontSendNotification);
        genreBox.setSelectedId(static_cast<int>(processor.genre()) + 1, juce::dontSendNotification);
        auto changed = [this]
        {
            processor.setRoleAndGenre(static_cast<muzi::Role>(roleBox.getSelectedId() - 1),
                                      static_cast<muzi::Genre>(genreBox.getSelectedId() - 1));
        };
        roleBox.onChange = changed;
        genreBox.onChange = changed;
        roleBox.setTooltip("What's on this channel - the engineer aims for a different target for each");
        genreBox.setTooltip("Shifts the target: Trap/Drill more sub, Drill darker, R&B glossier top, Boom Bap dustier");

        listen.setButtonText("LISTEN");
        setAccent(listen, palette::gold);
        listen.onClick = [this]
        {
            if (processor.listenState() == MuziConsoleAudioProcessor::ListenState::listening)
                processor.stopListening();
            else
                processor.startListening();
        };
        apply.setButtonText("APPLY");
        setAccent(apply, palette::gold);
        apply.onClick = [this] { processor.applySuggestion(); refresh(); };
        undo.setButtonText("UNDO");
        undo.onClick = [this] { processor.undoSuggestion(); refresh(); };

        viewport.setViewedComponent(&notes, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(6);

        for (juce::Component* c : { static_cast<juce::Component*>(&roleBox), static_cast<juce::Component*>(&genreBox),
                                    static_cast<juce::Component*>(&listen), static_cast<juce::Component*>(&apply),
                                    static_cast<juce::Component*>(&undo), static_cast<juce::Component*>(&viewport) })
            addAndMakeVisible(c);
        refresh();
    }

    void refresh()
    {
        using State = MuziConsoleAudioProcessor::ListenState;
        // Follow role/genre changes made elsewhere (session reload, another editor).
        if (roleBox.getSelectedId() != static_cast<int>(processor.role()) + 1)
            roleBox.setSelectedId(static_cast<int>(processor.role()) + 1, juce::dontSendNotification);
        if (genreBox.getSelectedId() != static_cast<int>(processor.genre()) + 1)
            genreBox.setSelectedId(static_cast<int>(processor.genre()) + 1, juce::dontSendNotification);
        const auto st = processor.listenState();
        const auto prog = processor.listenProgress();
        const auto ser = processor.suggestionSerial();

        if (st == State::listening)
        {
            listen.setButtonText("LISTENING  " + juce::String(prog, 1) + " / " + juce::String(juce::roundToInt(MuziConsoleAudioProcessor::listenSeconds)) + " s  -  STOP");
            listen.setToggleState(true, juce::dontSendNotification);
            if (st != lastState)
                notes.setNotes("Listening...", { prog < 0.1 ? "Press play in your session - play the busiest part of the track."
                                                              : "Keep it playing. It stops by itself after 10 seconds of sound." }, true);
        }
        else
        {
            listen.setButtonText(st == State::done ? "LISTEN AGAIN" : "LISTEN");
            listen.setToggleState(false, juce::dontSendNotification);
        }

        if (ser != lastSerial || (st != lastState && st != State::listening))
        {
            lastSerial = ser;
            if (st == State::done)
            {
                const auto s = processor.suggestion();
                notes.setNotes(juce::String::fromUTF8(s.headline.c_str()), s.notes, s.valid);
            }
            else if (st == State::idle)
                notes.setNotes("Listen & Suggest", { "Pick what's on this channel and the genre.",
                                                     "Press LISTEN and play the track - the engineer hears 10 seconds, compares it to its target and proposes settings with a reason for each.",
                                                     "Nothing changes until you press APPLY, and UNDO puts everything back." }, false);
        }
        if (st == State::listening && std::abs(prog - lastProgress) > 0.05)
            notes.setNotes("Listening...", { prog < 0.1 ? "Press play in your session - play the busiest part of the track."
                                                         : "Keep it playing. It stops by itself after 10 seconds of sound." }, true);
        lastProgress = prog;
        lastState = st;

        apply.setEnabled(st == State::done && processor.suggestion().valid);
        undo.setEnabled(processor.canUndo());
        notes.setSize(viewport.getMaximumVisibleWidth(), juce::jmax(notes.getContentHeight(), 10));
        repaint(progressArea);
    }

    void paint(juce::Graphics& g) override
    {
        using namespace palette;
        auto area = getLocalBounds().toFloat();
        g.setColour(panel);
        g.fillRoundedRectangle(area, 10.0f);
        g.setColour(edge);
        g.drawRoundedRectangle(area.reduced(0.5f), 10.0f, 1.0f);

        g.setFont(font(18.0f, Weight::bold));
        g.setColour(gold);
        g.drawText("ENGINEER", 16, 10, 140, 24, juce::Justification::left);
        g.setFont(font(14.0f, Weight::semibold));
        g.setColour(muted);
        g.drawText("listen & suggest", 100, 12, 180, 22, juce::Justification::left);

        if (processor.listenState() == MuziConsoleAudioProcessor::ListenState::listening)
        {
            const auto t = static_cast<float>(juce::jlimit(0.0, 1.0, processor.listenProgress() / MuziConsoleAudioProcessor::listenSeconds));
            const auto bar = progressArea.toFloat();
            g.setColour(glass);
            g.fillRoundedRectangle(bar, 2.0f);
            g.setColour(gold);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * t), 2.0f);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(14, 0);
        area.removeFromTop(42);
        auto row = area.removeFromTop(30);
        roleBox.setBounds(row.removeFromLeft(row.getWidth() / 2 - 4));
        row.removeFromLeft(8);
        genreBox.setBounds(row);
        area.removeFromTop(10);
        listen.setBounds(area.removeFromTop(38));
        progressArea = area.removeFromTop(8).withTrimmedTop(4);
        area.removeFromBottom(12);
        auto buttons = area.removeFromBottom(32);
        apply.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2 - 4));
        buttons.removeFromLeft(8);
        undo.setBounds(buttons);
        area.removeFromBottom(10);
        area.removeFromTop(6);
        viewport.setBounds(area);
        notes.setSize(viewport.getMaximumVisibleWidth(), 10);
        notes.layoutText();
        notes.setSize(viewport.getMaximumVisibleWidth(), juce::jmax(notes.getContentHeight(), 10));
    }

private:
    MuziConsoleAudioProcessor& processor;
    juce::ComboBox roleBox, genreBox;
    juce::TextButton listen, apply, undo;
    juce::Viewport viewport;
    NotesView notes;
    juce::Rectangle<int> progressArea;
    MuziConsoleAudioProcessor::ListenState lastState = MuziConsoleAudioProcessor::ListenState::idle;
    int lastSerial = -1;
    double lastProgress = -1.0;
};
}
