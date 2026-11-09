#include "PluginEditor.h"

#include "ConsoleParts.h"
#include "GlassDisplay.h"

namespace muziui
{
using namespace muzi;

//==============================================================================
// The whole panel, laid out at 1100 x 690 and scaled as a unit when the window is resized.
class MainPanel final : public juce::Component, private juce::Timer
{
public:
    explicit MainPanel(MuziConsoleAudioProcessor& p)
        : processor(p), state(p.params), glass(p), engineer(p), meters(p),
          mode(state, pid::mode, { "CHANNEL", "BUS" }, palette::gold),
          flavour(state, pid::flavour, { "E", "G" }, palette::gold),
          hpf(state, pid::hpfHz, "HPF", palette::filter), lpf(state, pid::lpfHz, "LPF", palette::filter),
          hpfOn(state, pid::hpfOn, "HPF IN", palette::filter), lpfOn(state, pid::lpfOn, "LPF IN", palette::filter),
          lfDb(state, pid::lfDb, "GAIN", palette::lf, true, true), lfHz(state, pid::lfHz, "FREQ", palette::lf),
          lmfDb(state, pid::lmfDb, "GAIN", palette::lmf, true, true), lmfHz(state, pid::lmfHz, "FREQ", palette::lmf), lmfQ(state, pid::lmfQ, "WIDTH", palette::lmf),
          hmfDb(state, pid::hmfDb, "GAIN", palette::hmf, true, true), hmfHz(state, pid::hmfHz, "FREQ", palette::hmf), hmfQ(state, pid::hmfQ, "WIDTH", palette::hmf),
          hfDb(state, pid::hfDb, "GAIN", palette::hf, true, true), hfHz(state, pid::hfHz, "FREQ", palette::hf),
          lfBell(state, pid::lfBell, "BELL", palette::lf), hfBell(state, pid::hfBell, "BELL", palette::hf),
          eqOn(state, pid::eqOn, "EQ IN", palette::text), busGuard(state, pid::busGuard, juce::String::fromUTF8("GUARD \xc2\xb1" "2"), palette::gold),
          dynOn(state, pid::dynOn, "DYN IN", palette::dyn), dynPre(state, pid::dynPre, "PRE-EQ", palette::dyn),
          compFast(state, pid::compFast, "FAST", palette::dyn),
          compThr(state, pid::compThr, "THRESH", palette::dyn), compRatio(state, pid::compRatio, "RATIO", palette::dyn),
          compRel(state, pid::compRel, "RELEASE", palette::dyn),
          gateThr(state, pid::gateThr, "GATE", palette::dyn), gateRange(state, pid::gateRange, "RANGE", palette::dyn),
          busThr(state, pid::busThr, "THRESH", palette::dyn), busRatio(state, pid::busRatio, "RATIO", palette::dyn),
          busAttack(state, pid::busAttack, "ATTACK", palette::dyn), busRelease(state, pid::busRelease, "RELEASE", palette::dyn),
          busMakeup(state, pid::busMakeup, "MAKE-UP", palette::dyn), busMix(state, pid::busMix, "MIX", palette::dyn),
          busScHpf(state, pid::busScHpf, "SC HPF", palette::dyn),
          inTrim(state, pid::inTrim, "TRIM", palette::gold, true), drive(state, pid::drive, "DRIVE", palette::hf),
          outTrim(state, pid::outTrim, "OUTPUT", palette::gold, true)
    {
        addAndMakeVisible(glass);
        addAndMakeVisible(engineer);
        addAndMakeVisible(meters);
        addAndMakeVisible(mode);
        addAndMakeVisible(flavour);
        mode.setTooltip("CHANNEL: one track - trim, filters, EQ, compressor and gate.  BUS: groups and the mix - gentle EQ and the glue compressor.");
        flavour.setTooltip("E: bell width stays the same at any gain; gentle shelves.  G: bells narrow as you boost (proportional Q); shelves with a small bump.");

        for (auto* k : allKnobs()) k->addTo(*this);
        for (auto* t : allToggles()) addAndMakeVisible(t->button);
        busGuard.button.setTooltip("Bus mode: EQ moves are held to +/-2 dB. Bigger problems belong on the channel.");
        dynPre.button.setTooltip("Dynamics before the EQ instead of after it");
        compFast.button.setTooltip("0.3 ms attack instead of 3 ms - catches the very front of the hit");
        busScHpf.button.setTooltip("Sidechain high-pass at 60 Hz so the sub doesn't pump the mix");
        drive.slider.setTooltip("Console colour: gentle 2nd and 3rd harmonics, level-matched");
        lmfQ.slider.setTooltip("Q - bell width. Higher is narrower. (Scroll on a node in the display too.)");
        hmfQ.slider.setTooltip("Q - bell width. Higher is narrower. (Scroll on a node in the display too.)");
        compThr.slider.getProperties().set("fromEnd", true);
        busThr.slider.getProperties().set("fromEnd", true);

        refreshAll();
        startTimerHz(15);
    }

    void paint(juce::Graphics& g) override
    {
        using namespace palette;
        g.fillAll(background);

        // Epsilon Sounds product mark (shared design system)
        epsilon_ui::drawProductMark(g, { 18.0f, 13.0f }, 1.0f, epsilon_ui::Tier::Muzi, "Console", {},
                                    [](float h, bool b) { return font(h, b ? Weight::bold : Weight::semibold); });
        g.setFont(font(12.0f, Weight::bold));
        g.setColour(dim);
        g.drawText("v0.1", getWidth() - 70, 22, 54, 16, juce::Justification::right);
        g.setFont(font(13.0f, Weight::bold));
        g.setColour(muted);
        g.drawText("CURVE", flavour.getX() - 52, flavour.getY(), 48, flavour.getHeight(), juce::Justification::right);

        for (const auto& s : sections)
        {
            g.setColour(panel);
            g.fillRoundedRectangle(s.area.toFloat(), 10.0f);
            g.setColour(edge);
            g.drawRoundedRectangle(s.area.toFloat().reduced(0.5f), 10.0f, 1.0f);
            g.setFont(font(15.0f, Weight::bold));
            g.setColour(text);
            g.drawText(s.title, s.area.getX() + 12, s.area.getY() + 6, 140, 22, juce::Justification::left);
        }

        // EQ band headers with their colour codes
        const std::array<std::pair<const char*, juce::Colour>, 4> bands { { { "LF", lf }, { "LMF", lmf }, { "HMF", hmf }, { "HF", hf } } };
        for (std::size_t i = 0; i < 4; ++i)
        {
            const auto r = bandHeaders[i];
            g.setColour(bands[i].second);
            g.fillRoundedRectangle(r.toFloat().withHeight(3.0f).reduced(18.0f, 0.0f), 1.5f);
            g.setFont(font(14.0f, Weight::bold));
            g.drawText(bands[i].first, r.withTrimmedTop(4), juce::Justification::centred);
        }
        {
            g.setFont(font(12.0f, Weight::bold));
            g.setColour(dim);
            for (const auto& [r, t] : dynCaptions)
                g.drawText(t, r, juce::Justification::left);
        }
    }

    void resized() override
    {
        mode.setBounds(300, 14, 220, 32);
        flavour.setBounds(610, 14, 104, 32);
        glass.setBounds(16, 62, 730, 314);
        engineer.setBounds(756, 62, 328, 314);

        sections.clear();
        const int top = 386, height = 318;
        auto filters = juce::Rectangle<int>(16, top, 128, height);
        auto eq = juce::Rectangle<int>(152, top, 512, height);
        auto dyn = juce::Rectangle<int>(672, top, 238, height);
        auto out = juce::Rectangle<int>(918, top, 166, height);
        sections.push_back({ filters, "FILTERS" });
        sections.push_back({ eq, "EQ" });
        sections.push_back({ dyn, "DYNAMICS" });
        sections.push_back({ out, "OUTPUT" });

        // Filters
        {
            auto a = filters.reduced(10, 0).withTrimmedTop(34).withTrimmedBottom(10);
            hpf.setBounds(a.removeFromTop(106));
            hpfOn.button.setBounds(a.removeFromTop(26).reduced(8, 0));
            a.removeFromTop(16);
            lpf.setBounds(a.removeFromTop(106));
            lpfOn.button.setBounds(a.removeFromTop(26).reduced(8, 0));
        }
        // EQ
        {
            auto header = eq.withHeight(32).reduced(10, 5);
            busGuard.button.setBounds(header.removeFromRight(82));
            header.removeFromRight(6);
            eqOn.button.setBounds(header.removeFromRight(64));
            auto a = eq.withTrimmedTop(34).withTrimmedBottom(8).reduced(4, 0);
            const int w = a.getWidth() / 4;
            std::array<std::array<Knob*, 3>, 4> cols { { { &lfDb, &lfHz, nullptr }, { &lmfDb, &lmfHz, &lmfQ },
                                                        { &hmfDb, &hmfHz, &hmfQ }, { &hfDb, &hfHz, nullptr } } };
            for (std::size_t i = 0; i < 4; ++i)
            {
                auto c = a.removeFromLeft(w).reduced(6, 0);
                bandHeaders[i] = c.removeFromTop(24);
                cols[i][0]->setBounds(c.removeFromTop(100));
                cols[i][1]->setBounds(c.removeFromTop(80).reduced(12, 0));
                auto third = c.removeFromTop(72);
                if (cols[i][2] != nullptr) cols[i][2]->setBounds(third.reduced(16, 0));
                else (i == 0 ? lfBell : hfBell).button.setBounds(third.withSizeKeepingCentre(70, 26).translated(0, 8));
            }
        }
        // Dynamics (two layouts; the timer shows the one for the current mode)
        {
            auto header = dyn.withHeight(32).reduced(10, 5);
            dynOn.button.setBounds(header.removeFromRight(66));
            auto a = dyn.withTrimmedTop(34).reduced(8, 0).withTrimmedBottom(10);
            dynCaptions.clear();
            lastBus = ! lastBus; // force captions to be re-titled for the current mode

            auto channel = a;
            dynCaptions.push_back({ channel.removeFromTop(14).withTrimmedLeft(6), "COMPRESSOR" });
            auto row = channel.removeFromTop(96);
            const int kw = row.getWidth() / 3;
            compThr.setBounds(row.removeFromLeft(kw));
            compRatio.setBounds(row.removeFromLeft(kw));
            compRel.setBounds(row);
            auto toggles = channel.removeFromTop(30).reduced(6, 2);
            compFast.button.setBounds(toggles.removeFromLeft(toggles.getWidth() / 2 - 3));
            toggles.removeFromLeft(6);
            dynPre.button.setBounds(toggles);
            channel.removeFromTop(8);
            dynCaptions.push_back({ channel.removeFromTop(14).withTrimmedLeft(6), "GATE / EXPANDER" });
            auto gateRow = channel.removeFromTop(96);
            gateThr.setBounds(gateRow.removeFromLeft(kw));
            gateRange.setBounds(gateRow.removeFromLeft(kw));

            auto bus = a;
            bus.removeFromTop(14);
            auto r1 = bus.removeFromTop(96);
            busThr.setBounds(r1.removeFromLeft(kw));
            busRatio.setBounds(r1.removeFromLeft(kw));
            busAttack.setBounds(r1);
            bus.removeFromTop(8);
            auto r2 = bus.removeFromTop(96);
            busRelease.setBounds(r2.removeFromLeft(kw));
            busMakeup.setBounds(r2.removeFromLeft(kw));
            busMix.setBounds(r2);
            busScHpf.button.setBounds(bus.removeFromTop(34).withSizeKeepingCentre(110, 26));
        }
        // Output
        {
            auto a = out.withTrimmedTop(34).withTrimmedBottom(10).reduced(6, 0);
            auto knobs = a.removeFromLeft(76);
            inTrim.setBounds(knobs.removeFromTop(90));
            drive.setBounds(knobs.removeFromTop(90));
            outTrim.setBounds(knobs.removeFromTop(90));
            a.removeFromLeft(4);
            meters.setBounds(a.reduced(0, 4));
        }
        refreshAll();
    }

private:
    void timerCallback() override { refreshAll(); }

    void refreshAll()
    {
        glass.refresh();
        engineer.refresh();
        const auto s = processor.readSettings();
        const bool bus = s.mode == 1;
        for (auto* k : { &compThr, &compRatio, &compRel, &gateThr, &gateRange }) k->setVisible(! bus);
        for (auto* t : { &compFast, &dynPre, &dynOn }) t->button.setVisible(! bus);
        for (auto* k : { &busThr, &busRatio, &busAttack, &busRelease, &busMakeup, &busMix }) k->setVisible(bus);
        busScHpf.button.setVisible(bus);
        busGuard.button.setVisible(bus);

        if (bus != lastBus)
        {
            lastBus = bus;
            if (dynCaptions.size() >= 1) dynCaptions.front().second = bus ? "GLUE COMPRESSOR" : "COMPRESSOR";
            if (dynCaptions.size() >= 2) dynCaptions[1].second = bus ? "" : "GATE / EXPANDER";
            if (sections.size() > 2) sections[2].title = bus ? "BUS COMP" : "DYNAMICS";
            repaint();
        }

        hpf.setDimmed(! s.hpfOn);
        lpf.setDimmed(! s.lpfOn);
        for (auto* k : { &lfDb, &lfHz, &lmfDb, &lmfHz, &lmfQ, &hmfDb, &hmfHz, &hmfQ, &hfDb, &hfHz }) k->setDimmed(! s.eqOn);
        for (auto* k : { &compThr, &compRatio, &compRel, &gateThr, &gateRange }) k->setDimmed(! s.dynOn);
        drive.setDimmed(s.drive < 0.5f);
    }

    std::vector<Knob*> allKnobs()
    {
        return { &hpf, &lpf, &lfDb, &lfHz, &lmfDb, &lmfHz, &lmfQ, &hmfDb, &hmfHz, &hmfQ, &hfDb, &hfHz,
                 &compThr, &compRatio, &compRel, &gateThr, &gateRange,
                 &busThr, &busRatio, &busAttack, &busRelease, &busMakeup, &busMix, &inTrim, &drive, &outTrim };
    }
    std::vector<Toggle*> allToggles()
    {
        return { &hpfOn, &lpfOn, &lfBell, &hfBell, &eqOn, &busGuard, &dynOn, &dynPre, &compFast, &busScHpf };
    }

    MuziConsoleAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    GlassDisplay glass;
    EngineerPanel engineer;
    MeterStrip meters;
    Segmented mode, flavour;
    Knob hpf, lpf;
    Toggle hpfOn, lpfOn;
    Knob lfDb, lfHz, lmfDb, lmfHz, lmfQ, hmfDb, hmfHz, hmfQ, hfDb, hfHz;
    Toggle lfBell, hfBell, eqOn, busGuard, dynOn, dynPre, compFast;
    Knob compThr, compRatio, compRel, gateThr, gateRange;
    Knob busThr, busRatio, busAttack, busRelease, busMakeup, busMix;
    Toggle busScHpf;
    Knob inTrim, drive, outTrim;

    struct Section { juce::Rectangle<int> area; juce::String title; };
    std::vector<Section> sections;
    std::array<juce::Rectangle<int>, 4> bandHeaders {};
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> dynCaptions;
    bool lastBus = false;
};
}

//==============================================================================
MuziConsoleAudioProcessorEditor::MuziConsoleAudioProcessorEditor(MuziConsoleAudioProcessor& p)
    : AudioProcessorEditor(&p)
{
    look = std::make_unique<muziui::MuziLook>();
    setLookAndFeel(look.get());
    panel = std::make_unique<muziui::MainPanel>(p);
    addAndMakeVisible(*panel);
    tooltips = std::make_unique<juce::TooltipWindow>(this, 600);

    setResizable(true, true);
    setResizeLimits(baseWidth * 3 / 4, baseHeight * 3 / 4, baseWidth * 2, baseHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(static_cast<double>(baseWidth) / baseHeight);
    setSize(baseWidth, baseHeight);
}

MuziConsoleAudioProcessorEditor::~MuziConsoleAudioProcessorEditor()
{
    tooltips.reset();
    panel.reset();
    setLookAndFeel(nullptr);
}

void MuziConsoleAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(muziui::palette::background);
}

void MuziConsoleAudioProcessorEditor::resized()
{
    const auto scale = static_cast<float>(getWidth()) / static_cast<float>(baseWidth);
    panel->setBounds(0, 0, baseWidth, baseHeight);
    panel->setTransform(juce::AffineTransform::scale(scale));
}
