#pragma once

// The hero: a dark glass screen with the EQ curve and draggable band nodes. After a listen it shows
// what the engineer heard (measured), where it wants to go (target, with a +/-3 dB tolerance tube)
// and what the current EQ does to it (after). Repaints only when something changed.

#include "MuziUI.h"

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal" // exact compares here are deliberate (change detection, identity checks)
#endif

namespace muziui
{
class GlassDisplay final : public juce::Component
{
public:
    explicit GlassDisplay(MuziConsoleAudioProcessor& p) : processor(p) { setOpaque(true); }

    // Called ~15x a second by the editor.
    void refresh()
    {
        const auto s = processor.readSettings();
        const auto st = processor.listenState();
        const auto ser = processor.suggestionSerial();
        bool dirty = s != lastSettings || st != lastState || ser != lastSerial
                  || std::abs(processor.sampleRateForDisplay() - lastRate) > 1.0;
        if (st == MuziConsoleAudioProcessor::ListenState::listening)
        {
            auto bands = processor.liveBands();
            if (bands != live) { live = std::move(bands); dirty = true; }
        }
        if (! dirty)
            return;
        lastSettings = s;
        lastState = st;
        lastRate = processor.sampleRateForDisplay();
        if (ser != lastSerial)
        {
            lastSerial = ser;
            sug = processor.suggestion();
        }
        rebuildCurves();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        using namespace palette;
        g.drawImageAt(background, 0, 0);
        const auto plot = plotArea();

        // Engineer layers: measured spectrum, target tube, predicted result.
        if (! measured.isEmpty())
        {
            juce::Path fill(measured);
            fill.lineTo(plot.getRight(), plot.getBottom());
            fill.lineTo(plot.getX(), plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill(juce::ColourGradient(muted.withAlpha(0.22f), 0.0f, plot.getY(), muted.withAlpha(0.02f), 0.0f, plot.getBottom(), false));
            g.fillPath(fill);
            g.setColour(muted.withAlpha(0.65f));
            g.strokePath(measured, juce::PathStrokeType(1.4f));
        }
        if (! targetTube.isEmpty())
        {
            g.setColour(gold.withAlpha(0.08f));
            g.fillPath(targetTube);
            juce::Path dashed;
            const float dashes[] { 5.0f, 4.0f };
            juce::PathStrokeType(1.4f).createDashedStroke(dashed, targetLine, dashes, 2);
            g.setColour(gold.withAlpha(0.85f));
            g.fillPath(dashed);
        }
        if (! ghost.isEmpty())
        {
            // The suggested EQ: solid gold, so it reads apart from the dashed target.
            g.setColour(gold.withAlpha(0.9f));
            g.strokePath(ghost, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        if (! after.isEmpty())
        {
            g.setColour(lmf.withAlpha(0.9f));
            g.strokePath(after, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // The EQ curve itself
        if (! eqCurve.isEmpty())
        {
            const auto zeroY = yForDb(0.0f);
            juce::Path fill(eqCurve);
            fill.lineTo(plot.getRight(), zeroY);
            fill.lineTo(plot.getX(), zeroY);
            fill.closeSubPath();
            g.setColour(text.withAlpha(0.07f));
            g.fillPath(fill);
            g.setColour(lastSettings.eqOn ? text : dim);
            g.strokePath(eqCurve, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Nodes
        for (int i = 0; i < nodeCount; ++i)
        {
            const auto n = node(i);
            if (! n.visible) continue;
            const auto pos = n.position;
            const auto r = (i == hover || i == dragging) ? 9.0f : 7.0f;
            const auto box = juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(pos);
            if (n.live)
            {
                g.setColour(n.colour.withAlpha(0.25f));
                g.fillEllipse(box.expanded(5.0f));
                g.setColour(n.colour);
                g.fillEllipse(box);
                g.setColour(glass);
                g.setFont(font(11.0f, Weight::bold));
                g.drawText(n.tag, box, juce::Justification::centred);
            }
            else
            {
                g.setColour(glass);
                g.fillEllipse(box);
                g.setColour(n.colour.withAlpha(0.8f));
                g.drawEllipse(box, 1.6f);
            }
            if (i == hover || i == dragging)
            {
                const auto label = n.readout;
                g.setFont(font(15.0f, Weight::semibold));
                const auto w = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), label) + 16;
                auto tag = juce::Rectangle<float>(static_cast<float>(w), 22.0f).withCentre(pos.translated(0.0f, pos.y > plot.getCentreY() ? -26.0f : 26.0f));
                tag = tag.constrainedWithin(plot);
                g.setColour(panelHi.withAlpha(0.95f));
                g.fillRoundedRectangle(tag, 5.0f);
                g.setColour(n.colour);
                g.drawRoundedRectangle(tag, 5.0f, 1.0f);
                g.setColour(text);
                g.drawText(label, tag, juce::Justification::centred);
            }
        }

        // Titles: readable even as a small store thumbnail.
        const auto bus = lastSettings.mode == 1;
        g.setFont(font(22.0f, Weight::bold));
        g.setColour(text);
        const juce::String modeText = bus ? "BUS" : "CHANNEL";
        g.drawText(modeText, plot.getX() + 14.0f, plot.getY() + 8.0f, 200.0f, 26.0f, juce::Justification::left);
        const auto modeW = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font(22.0f, Weight::bold), modeText));
        g.setFont(font(16.0f, Weight::semibold));
        g.setColour(muted);
        juce::String sub = juce::String(lastSettings.flavour == 0 ? "E-curve" : "G-curve");
        if (bus && lastSettings.busGuard) sub << juce::String::fromUTF8("  \xc2\xb7  guard \xc2\xb1" "2 dB");
        if (! lastSettings.eqOn) sub << juce::String::fromUTF8("  \xc2\xb7  EQ OUT");
        g.drawText(sub, plot.getX() + 22.0f + modeW, plot.getY() + 11.0f, 300.0f, 22.0f, juce::Justification::left);

        if (lastState == MuziConsoleAudioProcessor::ListenState::listening)
        {
            g.setColour(gold);
            g.setFont(font(16.0f, Weight::bold));
            g.drawText("LISTENING", plot.withTrimmedRight(14.0f).withTrimmedTop(10.0f).withHeight(22.0f), juce::Justification::right);
        }
        else if (sug.valid)
        {
            auto legend = plot.withTrimmedRight(14.0f).withTrimmedTop(10.0f).withHeight(20.0f);
            auto item = [&](juce::Colour c, const juce::String& t, bool dashed)
            {
                g.setFont(font(14.0f, Weight::semibold));
                const auto w = static_cast<float>(juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), t));
                auto r = legend.removeFromRight(w);
                g.setColour(text.withAlpha(0.8f));
                g.drawText(t, r, juce::Justification::centredLeft);
                auto sw = legend.removeFromRight(24.0f).reduced(4.0f, 9.0f);
                g.setColour(c);
                if (dashed) { g.fillRect(sw.removeFromLeft(6.0f)); sw.removeFromLeft(4.0f); g.fillRect(sw.removeFromLeft(6.0f)); }
                else g.fillRect(sw);
                legend.removeFromRight(12.0f);
            };
            item(lmf, previewing ? "after APPLY" : "after", false);
            if (previewing) item(gold.withAlpha(0.9f), "suggested EQ", false);
            item(gold, "target", true);
            item(muted, "heard", false);
        }
    }

    void resized() override
    {
        background = juce::Image(juce::Image::RGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true, juce::SoftwareImageType());
        juce::Graphics g(background);
        paintBackground(g);
        rebuildCurves();
    }

    // ---- interaction --------------------------------------------------------------
    void mouseMove(const juce::MouseEvent& e) override { setHover(nearest(e.position)); }
    void mouseExit(const juce::MouseEvent&) override { setHover(-1); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragging = nearest(e.position);
        if (dragging < 0) return;
        for (auto* p : nodeParams(dragging)) if (p) p->beginChangeGesture();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        const auto ids = nodeIds(dragging);
        auto* freq = processor.params.getParameter(ids.freq);
        const auto hz = juce::jlimit(ids.lo, ids.hi, hzForX(e.position.x));
        freq->setValueNotifyingHost(freq->convertTo0to1(hz));
        if (ids.gain != nullptr)
        {
            auto* gain = processor.params.getParameter(ids.gain);
            const auto db = juce::jlimit(muzi::range::gainLo, muzi::range::gainHi, std::round(dbForY(e.position.y) * 10.0f) / 10.0f);
            gain->setValueNotifyingHost(gain->convertTo0to1(db));
        }
        refresh();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging >= 0)
            for (auto* p : nodeParams(dragging)) if (p) p->endChangeGesture();
        dragging = -1;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const auto i = nearest(e.position);
        if (i < 0 || nodeIds(i).gain == nullptr) return;
        auto* gain = processor.params.getParameter(nodeIds(i).gain);
        gain->beginChangeGesture();
        gain->setValueNotifyingHost(gain->convertTo0to1(0.0f));
        gain->endChangeGesture();
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const auto i = nearest(e.position);
        const char* qId = i == 1 ? muzi::pid::lmfQ : i == 2 ? muzi::pid::hmfQ : nullptr;
        if (qId == nullptr) return;
        auto* q = processor.params.getParameter(qId);
        const auto current = q->convertFrom0to1(q->getValue());
        const auto next = juce::jlimit(muzi::range::qLo, muzi::range::qHi, current * std::pow(1.12f, w.deltaY * 4.0f));
        q->beginChangeGesture();
        q->setValueNotifyingHost(q->convertTo0to1(next));
        q->endChangeGesture();
    }

private:
    static constexpr float topDb = 18.0f, bottomDb = -30.0f;
    static constexpr int nodeCount = 6; // LF, LMF, HMF, HF, HPF, LPF

    juce::Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced(1.0f).withTrimmedLeft(30.0f).withTrimmedBottom(20.0f); }
    float xForHz(float hz) const
    {
        const auto p = plotArea();
        return p.getX() + p.getWidth() * std::log(hz / 20.0f) / std::log(1000.0f);
    }
    float hzForX(float x) const
    {
        const auto p = plotArea();
        return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, (x - p.getX()) / p.getWidth()));
    }
    float yForDb(float db) const
    {
        const auto p = plotArea();
        return p.getY() + p.getHeight() * (topDb - db) / (topDb - bottomDb);
    }
    float dbForY(float y) const
    {
        const auto p = plotArea();
        return topDb - (y - p.getY()) / p.getHeight() * (topDb - bottomDb);
    }

    void paintBackground(juce::Graphics& g)
    {
        using namespace palette;
        const auto all = getLocalBounds().toFloat();
        g.fillAll(panel);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0d1116), all.getCentreX(), all.getY(),
                                               glass, all.getCentreX(), all.getBottom(), false));
        g.fillRoundedRectangle(all, 10.0f);
        const auto p = plotArea();

        g.setFont(font(13.0f, Weight::semibold));
        for (const float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const auto x = xForHz(hz);
            g.setColour(edge.withAlpha(0.55f));
            g.drawVerticalLine(juce::roundToInt(x), p.getY(), p.getBottom());
            g.setColour(dim);
            g.drawText(hzLabel(hz), juce::Rectangle<float>(40.0f, 18.0f).withCentre({ x, p.getBottom() + 10.0f }), juce::Justification::centred);
        }
        for (float db = 12.0f; db >= -24.0f; db -= 6.0f)
        {
            const auto y = yForDb(db);
            g.setColour(db == 0.0f ? edge.brighter(0.4f) : edge.withAlpha(0.55f));
            g.drawHorizontalLine(juce::roundToInt(y), p.getX(), p.getRight());
            g.setColour(dim);
            g.drawText((db > 0 ? "+" : "") + juce::String(juce::roundToInt(db)), juce::Rectangle<float>(26.0f, 14.0f).withCentre({ p.getX() - 15.0f, y }),
                       juce::Justification::centredRight);
        }
        g.setColour(edge);
        g.drawRoundedRectangle(all.reduced(0.5f), 10.0f, 1.0f);
    }

    void rebuildCurves()
    {
        const auto p = plotArea();
        if (p.isEmpty()) return;
        const auto fs = juce::jmax(22050.0, processor.sampleRateForDisplay());
        const auto design = muzi::designEq(lastSettings, fs);

        eqCurve.clear();
        constexpr int points = 220;
        for (int i = 0; i <= points; ++i)
        {
            const auto hz = 20.0f * std::pow(1000.0f, static_cast<float>(i) / points);
            const auto y = yForDb(juce::jlimit(bottomDb - 2.0f, topDb + 2.0f, static_cast<float>(design.magnitudeDb(hz, fs))));
            i == 0 ? eqCurve.startNewSubPath(xForHz(hz), y) : eqCurve.lineTo(xForHz(hz), y);
        }

        measured.clear(); targetLine.clear(); targetTube.clear(); after.clear(); ghost.clear();
        previewing = false;
        auto smoothPath = [&](juce::Path& path, const std::vector<float>& f, auto valueAt)
        {
            for (std::size_t b = 0; b < f.size(); ++b)
            {
                const auto pt = juce::Point<float>(xForHz(f[b]), yForDb(juce::jlimit(bottomDb - 4.0f, topDb + 4.0f, valueAt(b))));
                b == 0 ? path.startNewSubPath(pt) : path.lineTo(pt);
            }
        };

        if (lastState == MuziConsoleAudioProcessor::ListenState::listening && ! live.empty())
        {
            const auto& f = muzi::CriticalBands::get().centre;
            std::vector<float> core;
            for (std::size_t b = 0; b < f.size() && b < live.size(); ++b)
                if (f[b] > 100.0f && f[b] < 5000.0f) core.push_back(live[b]);
            std::nth_element(core.begin(), core.begin() + static_cast<long>(core.size() / 2), core.end());
            const auto mid = core.empty() ? 0.0f : core[core.size() / 2];
            smoothPath(measured, f, [&](std::size_t b) { return live[b] - mid - 3.0f; });
        }
        else if (lastState == MuziConsoleAudioProcessor::ListenState::done && sug.valid)
        {
            const auto& f = sug.freqs;
            smoothPath(measured, f, [&](std::size_t b) { return sug.measuredDb[b]; });
            smoothPath(targetLine, f, [&](std::size_t b) { return sug.targetDb[b]; });
            for (std::size_t b = 0; b < f.size(); ++b)
            {
                const auto pt = juce::Point<float>(xForHz(f[b]), yForDb(sug.targetDb[b] + 3.0f));
                b == 0 ? targetTube.startNewSubPath(pt) : targetTube.lineTo(pt);
            }
            for (auto b = f.size(); b-- > 0;)
                targetTube.lineTo(xForHz(f[b]), yForDb(sug.targetDb[b] - 3.0f));
            targetTube.closeSubPath();
            // Before APPLY: preview the suggested EQ and what it would do. After APPLY they're the same as the live curve.
            const auto suggested = muzi::designEq(muzi::withMoves(lastSettings, sug.moves), fs);
            float maxDiff = 0.0f;
            for (int i = 0; i <= 60; ++i)
            {
                const auto hz = 20.0 * std::pow(1000.0, i / 60.0);
                maxDiff = juce::jmax(maxDiff, static_cast<float>(std::abs(suggested.magnitudeDb(hz, fs) - design.magnitudeDb(hz, fs))));
            }
            previewing = maxDiff > 0.05f;
            const auto& shown = previewing ? suggested : design;
            if (previewing)
                for (int i = 0; i <= 220; ++i)
                {
                    const auto hz = 20.0f * std::pow(1000.0f, static_cast<float>(i) / 220.0f);
                    const auto y = yForDb(juce::jlimit(bottomDb - 2.0f, topDb + 2.0f, static_cast<float>(suggested.magnitudeDb(hz, fs))));
                    i == 0 ? ghost.startNewSubPath(xForHz(hz), y) : ghost.lineTo(xForHz(hz), y);
                }
            smoothPath(after, f, [&](std::size_t b) { return sug.measuredDb[b] + static_cast<float>(shown.magnitudeDb(f[b], fs)); });
            // The trim also shifts what's heard, but the display compares shapes, so it's left out.
        }
    }

    struct NodeIds { const char* gain; const char* freq; float lo, hi; };
    static NodeIds nodeIds(int i)
    {
        using namespace muzi;
        switch (i)
        {
            case 0: return { pid::lfDb, pid::lfHz, range::lfLo, range::lfHi };
            case 1: return { pid::lmfDb, pid::lmfHz, range::lmfLo, range::lmfHi };
            case 2: return { pid::hmfDb, pid::hmfHz, range::hmfLo, range::hmfHi };
            case 3: return { pid::hfDb, pid::hfHz, range::hfLo, range::hfHi };
            case 4: return { nullptr, pid::hpfHz, range::hpfLo, range::hpfHi };
            default: return { nullptr, pid::lpfHz, range::lpfLo, range::lpfHi };
        }
    }
    std::array<juce::RangedAudioParameter*, 2> nodeParams(int i)
    {
        const auto ids = nodeIds(i);
        return { processor.params.getParameter(ids.freq), ids.gain ? processor.params.getParameter(ids.gain) : nullptr };
    }

    struct Node { bool visible; bool live; juce::Point<float> position; juce::Colour colour; juce::String tag, readout; };
    Node node(int i) const
    {
        const auto& s = lastSettings;
        const auto fmtHz = [](float hz) { return hz < 1000.0f ? juce::String(juce::roundToInt(hz)) + " Hz" : juce::String(hz / 1000.0f, 2) + " kHz"; };
        const auto fmtDb = [](float db) { return (db > 0.04f ? "+" : "") + juce::String(db, 1) + " dB"; };
        switch (i)
        {
            case 0: return { true, std::abs(s.lfDb) > 0.04f, { xForHz(s.lfHz), yForDb(s.lfDb) }, palette::lf, "L", "LF  " + fmtHz(s.lfHz) + "  " + fmtDb(s.lfDb) };
            case 1: return { true, std::abs(s.lmfDb) > 0.04f, { xForHz(s.lmfHz), yForDb(s.lmfDb) }, palette::lmf, "LM", "LMF  " + fmtHz(s.lmfHz) + "  " + fmtDb(s.lmfDb) + "  Q " + juce::String(s.lmfQ, 2) };
            case 2: return { true, std::abs(s.hmfDb) > 0.04f, { xForHz(s.hmfHz), yForDb(s.hmfDb) }, palette::hmf, "HM", "HMF  " + fmtHz(s.hmfHz) + "  " + fmtDb(s.hmfDb) + "  Q " + juce::String(s.hmfQ, 2) };
            case 3: return { true, std::abs(s.hfDb) > 0.04f, { xForHz(s.hfHz), yForDb(s.hfDb) }, palette::hf, "H", "HF  " + fmtHz(s.hfHz) + "  " + fmtDb(s.hfDb) };
            case 4: return { s.hpfOn, true, { xForHz(s.hpfHz), yForDb(0.0f) }, palette::filter, "", "HPF  " + fmtHz(s.hpfHz) };
            default: return { s.lpfOn, true, { xForHz(s.lpfHz), yForDb(0.0f) }, palette::filter, "", "LPF  " + fmtHz(s.lpfHz) };
        }
    }

    int nearest(juce::Point<float> at) const
    {
        int best = -1; float bestD = 16.0f;
        for (int i = 0; i < nodeCount; ++i)
        {
            const auto n = node(i);
            if (! n.visible) continue;
            const auto d = n.position.getDistanceFrom(at);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void setHover(int i)
    {
        if (i == hover) return;
        hover = i;
        setMouseCursor(i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    MuziConsoleAudioProcessor& processor;
    juce::Image background;
    muzi::Settings lastSettings { -1 };
    MuziConsoleAudioProcessor::ListenState lastState = MuziConsoleAudioProcessor::ListenState::idle;
    int lastSerial = -1;
    double lastRate = 0.0;
    muzi::Suggestion sug;
    std::vector<float> live;
    juce::Path eqCurve, measured, targetLine, targetTube, after, ghost;
    bool previewing = false;
    int hover = -1, dragging = -1;
};
}

#if defined(__GNUC__) || defined(__clang__)
 #pragma GCC diagnostic pop
#endif
