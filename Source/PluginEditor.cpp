#include "PluginEditor.h"

//==============================================================================
namespace
{
    class PlateLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        PlateLookAndFeel()
        {
            setColour (juce::PopupMenu::backgroundColourId,             stui::card());
            setColour (juce::PopupMenu::textColourId,                   stui::ink());
            setColour (juce::PopupMenu::headerTextColourId,             stui::ink3());
            setColour (juce::PopupMenu::highlightedBackgroundColourId,  stui::ink());
            setColour (juce::PopupMenu::highlightedTextColourId,        stui::paper());
        }

        juce::Font getPopupMenuFont() override { return stui::sans (15.0f); }

        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            g.fillAll (stui::card());
            g.setColour (stui::hair());
            g.drawRect (0, 0, width, height, 1);
        }
    };

    PlateLookAndFeel& plateLaf()
    {
        static PlateLookAndFeel laf;
        return laf;
    }

    juce::String formatValue (double v, int decimals, const juce::String& suffix)
    {
        juce::String s;
        if (decimals <= 0) s = juce::String ((juce::int64) std::llround (v));
        else               s = juce::String (v, decimals);
        return s + suffix;
    }

    constexpr float kFadeTime = 0.22f;   // page and chord transitions
}

//==============================================================================
ChoiceBox::ChoiceBox (juce::String captionText)
    : caption (std::move (captionText))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ChoiceBox::setItems (const juce::StringArray& newItems, const juce::StringArray& newIds, int selectedIndex)
{
    items = newItems;
    ids   = newIds;
    sel   = items.isEmpty() ? 0 : juce::jlimit (0, items.size() - 1, selectedIndex);
    repaint();
}

void ChoiceBox::setSelectedIndex (int idx, bool notify)
{
    if (items.isEmpty())
    {
        if (sel != 0) { sel = 0; repaint(); }
        return;
    }
    idx = juce::jlimit (0, items.size() - 1, idx);
    if (idx == sel) return;
    sel = idx;
    repaint();
    if (notify && onChange) onChange();
}

juce::String ChoiceBox::getSelectedId() const
{
    if (ids.size() == items.size() && juce::isPositiveAndBelow (sel, ids.size()))
        return ids[sel];
    return juce::String (sel);
}

void ChoiceBox::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    const bool hasCaption = caption.isNotEmpty();
    const juce::String text = items.isEmpty() ? juce::String ("--")
                                              : items[juce::jlimit (0, items.size() - 1, sel)];

    g.setColour (hover ? stui::field() : stui::card());
    g.fillRect (b);
    g.setColour (hover ? stui::ink3() : stui::hair());
    g.drawRect (b, 1.0f);

    auto inner = b.reduced (10, 0);

    if (hasCaption && inlineCaption)
    {
        // caption in its own left column, value right after it: one line, so
        // the control fits a 24-30 px band inside a voice card
        const auto capFont = stui::capsFont (9.0f, 0.15f);
        const int capW = (int) std::ceil (capFont.getStringWidthFloat (caption)) + 8;
        auto cap = inner.removeFromLeft (juce::jmin (capW, inner.getWidth() / 2));
        stui::drawCaps (g, caption, cap, stui::ink3(), juce::Justification::centredLeft, 9.0f, 0.15f);
        inner.removeFromLeft (2);
    }
    else if (hasCaption)
    {
        stui::drawCaps (g, caption, inner.removeFromTop (16), stui::ink3(),
                        juce::Justification::centredLeft, 9.0f, 0.15f);
    }

    auto chev = inner.removeFromRight (14);

    if (trailing.isNotEmpty())
    {
        const auto tf = stui::capsFont (8.2f, 0.20f);
        const int tw = (int) std::ceil (tf.getStringWidthFloat (trailing)) + 4;
        auto tag = inner.removeFromRight (juce::jmin (tw, inner.getWidth() / 2));
        stui::drawCaps (g, trailing, tag, stui::ink3().withAlpha (0.85f),
                        juce::Justification::centredRight, 8.2f, 0.20f);
        inner.removeFromRight (8);
    }

    g.setColour (stui::ink3());
    {
        const float x = (float) chev.getCentreX();
        const float y = (float) chev.getCentreY();
        juce::Path p;
        p.startNewSubPath (x - 4.2f, y - 2.2f);
        p.lineTo (x, y + 2.4f);
        p.lineTo (x + 4.2f, y - 2.2f);
        g.strokePath (p, juce::PathStrokeType (1.3f));
    }

    g.setColour (hover ? stui::ink() : stui::ink());
    if (hasCaption && inlineCaption)
    {
        g.setFont (stui::sans (14.0f));
        g.drawFittedText (text, inner, juce::Justification::centredLeft, 1, 0.80f);
    }
    else
    {
        g.setFont (stui::sans (hasCaption ? 15.0f : 15.5f));
        g.drawFittedText (text, inner.reduced (0, hasCaption ? 4 : 0),
                          juce::Justification::centredLeft, 1, 0.82f);
    }
}

void ChoiceBox::mouseDown (const juce::MouseEvent&)
{
    int selected = sel;
    if (menuBuilder)
        menuBuilder (items, ids, selected);

    if (items.isEmpty()) return;
    sel = juce::jlimit (0, items.size() - 1, selected);
    repaint();

    juce::PopupMenu menu;
    juce::String lastGroup;
    for (int i = 0; i < items.size(); ++i)
    {
        if (groupOf)
        {
            const auto grp = groupOf (i);
            if (grp.isNotEmpty() && grp != lastGroup)
            {
                menu.addSectionHeader (grp);
                lastGroup = grp;
            }
        }
        menu.addItem (i + 1, items[i], true, i == sel);
    }

    menu.setLookAndFeel (&plateLaf());

    auto opts = juce::PopupMenu::Options()
                    .withTargetComponent (this)
                    .withMinimumWidth (juce::jmax (getWidth(), 240));

    juce::Component::SafePointer<ChoiceBox> safe (this);
    menu.showMenuAsync (opts, [safe] (int result)
    {
        if (safe == nullptr || result <= 0) return;
        safe->setSelectedIndex (result - 1, true);
    });
}

void ChoiceBox::mouseEnter (const juce::MouseEvent&) { hover = true;  repaint(); }
void ChoiceBox::mouseExit  (const juce::MouseEvent&) { hover = false; repaint(); }

//==============================================================================
MiniButton::MiniButton (juce::String text, bool isToggle)
    : label (std::move (text)), toggle (isToggle)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void MiniButton::setOn (bool shouldBeOn, bool notify)
{
    if (on == shouldBeOn) return;
    on = shouldBeOn;
    repaint();
    if (notify && onClick) onClick();
}

void MiniButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const bool filled = (toggle && on) || solidFace;

    if (filled)
    {
        g.setColour (stui::ink());
        g.fillRect (b);
    }
    else
    {
        g.setColour (hover ? stui::field() : stui::card());
        g.fillRect (b);
        g.setColour (hover ? stui::ink3() : stui::hair());
        g.drawRect (b, 1.0f);
    }

    auto fg = filled ? stui::paper() : (hover ? stui::ink() : stui::ink2());
    if (! isEnabled()) fg = fg.withAlpha (0.30f);
    g.setColour (fg);

    auto area = getLocalBounds().reduced (4, 0);
    const float gh = (float) juce::jmin (getHeight(), 14);

    if (glyph != Glyph::none)
    {
        auto glyphArea = label.isEmpty() ? area
                                         : area.removeFromLeft (juce::jmin (area.getWidth(), (int) (gh + 6)));
        const float x = (float) glyphArea.getCentreX();
        const float y = (float) glyphArea.getCentreY();
        juce::Path p;

        switch (glyph)
        {
            case Glyph::play:
                p.addTriangle (x - 3.2f, y - 4.4f, x - 3.2f, y + 4.4f, x + 4.4f, y);
                g.fillPath (p);
                break;
            case Glyph::stop:
                g.fillRect (juce::Rectangle<float> (x - 3.4f, y - 3.4f, 6.8f, 6.8f));
                break;
            case Glyph::plus:
                g.drawLine (x - 4.0f, y, x + 4.0f, y, 1.2f);
                g.drawLine (x, y - 4.0f, x, y + 4.0f, 1.2f);
                break;
            case Glyph::up:
                p.addTriangle (x, y - 4.0f, x - 4.2f, y + 3.0f, x + 4.2f, y + 3.0f);
                g.strokePath (p, juce::PathStrokeType (1.2f));
                break;
            case Glyph::down:
                p.addTriangle (x, y + 4.0f, x - 4.2f, y - 3.0f, x + 4.2f, y - 3.0f);
                g.strokePath (p, juce::PathStrokeType (1.2f));
                break;
            case Glyph::cross:
                g.drawLine (x - 3.6f, y - 3.6f, x + 3.6f, y + 3.6f, 1.15f);
                g.drawLine (x - 3.6f, y + 3.6f, x + 3.6f, y - 3.6f, 1.15f);
                break;
            case Glyph::none:
                break;
        }
    }

    if (label.isNotEmpty())
    {
        if (compact)
        {
            g.setFont (stui::sans (12.5f, true));
            g.drawFittedText (label, area, juce::Justification::centred, 1, 0.80f);
        }
        else
        {
            g.setFont (stui::capsFont (9.8f, 0.16f));
            g.drawFittedText (label, area, juce::Justification::centred, 1, 0.72f);
        }
    }

    // armed: the button is being fired by the bar clock, not by a click
    if (armed)
    {
        g.setColour (stui::ball (0));
        g.fillEllipse ((float) getWidth() - 10.0f, 6.0f, 4.0f, 4.0f);
    }
}

void MiniButton::mouseEnter (const juce::MouseEvent&) { hover = true;  repaint(); }
void MiniButton::mouseExit  (const juce::MouseEvent&) { hover = false; down = false; repaint(); }

void MiniButton::mouseDown (const juce::MouseEvent&) { down = true; }

void MiniButton::mouseUp (const juce::MouseEvent& e)
{
    if (! down) return;
    down = false;
    if (! getLocalBounds().contains (e.getPosition())) return;

    if (toggle) on = ! on;
    repaint();
    if (onClick) onClick();
}

//==============================================================================
ParamStrip::ParamStrip (juce::String label, double lo_, double hi_, int decimals_, juce::String suffix_)
    : labelText (std::move (label)), suffix (std::move (suffix_)), lo (lo_), hi (hi_), value (lo_), decimals (decimals_)
{
    setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
}

void ParamStrip::setValue (double v, bool notify)
{
    v = juce::jlimit (lo, hi, v);
    if (std::abs (v - value) < 1.0e-9) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void ParamStrip::setTicks (const juce::StringArray& labels)
{
    tickLabels = labels;
    repaint();
}

void ParamStrip::setDetent (double at)
{
    detent = juce::jlimit (lo, hi, at);
    hasDetent = true;
    repaint();
}

void ParamStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    const bool stepped = ! tickLabels.isEmpty();

    juce::String shown = formatValue (value, decimals, suffix);
    if (stepped)
        shown = tickLabels[juce::jlimit (0, tickLabels.size() - 1, (int) std::lround (value))];

    juce::Rectangle<int> track;

    if (framed)
    {
        // every cell on the page shares one rhythm: caption on the top line,
        // readout + track on the bottom line
        g.setColour (hover ? stui::field() : stui::card());
        g.fillRect (b);
        g.setColour (hover ? stui::ink3() : stui::hair());
        g.drawRect (b, 1.0f);
        b.reduce (10, 0);

        auto top = b.removeFromTop (17);
        stui::drawCaps (g, labelText, top, stui::ink3(), juce::Justification::centredLeft, 9.0f, 0.15f);

        auto val = b.removeFromRight (juce::jmin (54, juce::jmax (34, b.getWidth() * 44 / 100)));
        g.setColour (stui::ink2());
        g.setFont (stui::mono (13.0f));
        g.drawText (shown, val, juce::Justification::centredRight, false);

        track = b.reduced (1, 0);
    }
    else
    {
        if (b.getHeight() >= 26)
        {
            // stacked, unframed: caption on the top line, track + readout on
            // the bottom line -- the same rhythm as a framed page cell, which
            // leaves the track enough room for five RATE ticks
            auto top = b.removeFromTop (juce::jmax (10, b.getHeight() - 16));
            stui::drawCaps (g, labelText, top, stui::ink3(), juce::Justification::centredLeft, 8.0f, 0.15f);

            auto val = b.removeFromRight (juce::jmin (42, juce::jmax (28, b.getWidth() * 33 / 100)));
            g.setColour (stui::ink2());
            g.setFont (stui::mono (10.5f));
            g.drawText (shown, val, juce::Justification::centredRight, false);

            b.removeFromRight (6);                 // air between track and readout
            track = b.reduced (1, 0);
        }
        else
        {
            auto left = b.removeFromLeft (juce::jmax (40, b.getWidth() * 36 / 100));
            stui::drawCaps (g, labelText, left, stui::ink3(), juce::Justification::centredLeft, 8.0f, 0.15f);
            auto right = b.removeFromRight (juce::jmax (30, b.getWidth() * 30 / 100));
            g.setColour (stui::ink2());
            g.setFont (stui::mono (10.5f));
            g.drawText (shown, right, juce::Justification::centredRight, false);
            b.removeFromRight (5);
            track = b.reduced (1, 0);
        }
    }

    // stepped strips keep the end ticks off the very edge, otherwise the dot
    // at 1/1 reads as an arrow head glued to the start of the track
    if (stepped) track = track.reduced (3, 0);

    trackArea = track;

    const float frac = (float) juce::jlimit (0.0, 1.0, (value - lo) / juce::jmax (1.0e-9, hi - lo));
    const float y = (float) track.getCentreY();
    const float x0 = (float) track.getX();
    const float x1 = (float) track.getRight();
    const float xv = x0 + frac * (x1 - x0);

    g.setColour (hover ? stui::hair() : stui::hairSoft());
    g.drawLine (x0, y, x1, y, 1.0f);

    const float xFrom = hasDetent
                          ? x0 + (float) juce::jlimit (0.0, 1.0, (detent - lo) / juce::jmax (1.0e-9, hi - lo)) * (x1 - x0)
                          : x0;

    g.setColour (stui::ink());
    g.drawLine (xFrom, y, xv, y, 1.9f);

    if (stepped)
    {
        const int n = tickLabels.size();
        const int cur = juce::jlimit (0, n - 1, (int) std::lround (value));
        for (int i = 0; i < n; ++i)
        {
            const float fx = x0 + (n > 1 ? (float) i / (float) (n - 1) : 0.5f) * (x1 - x0);
            g.setColour (i == cur ? stui::ink() : stui::hair());
            g.drawLine (fx, y - 4.0f, fx, y + 4.0f, i == cur ? 1.4f : 1.0f);
        }
    }
    else if (hasDetent)
    {
        // slack -- normal -- tight
        const float xd = xFrom;
        g.setColour (stui::ink3());
        g.drawLine (xd, y - 4.5f, xd, y + 4.5f, 1.2f);
        g.setColour (stui::hair());
        g.drawLine (x0, y - 2.5f, x0, y + 2.5f, 1.0f);
        g.drawLine (x1, y - 2.5f, x1, y + 2.5f, 1.0f);
    }

    g.setColour (stui::ink());
    g.fillEllipse (xv - 3.4f, y - 3.4f, 6.8f, 6.8f);
    g.setColour (stui::card());
    g.fillEllipse (xv - 1.2f, y - 1.2f, 2.4f, 2.4f);
}

void ParamStrip::applyX (int x)
{
    if (trackArea.getWidth() <= 1) return;
    const double frac = juce::jlimit (0.0, 1.0, (double) (x - trackArea.getX()) / (double) trackArea.getWidth());
    double v = lo + frac * (hi - lo);
    if (! tickLabels.isEmpty()) v = std::round (v);
    setValue (v, true);
}

void ParamStrip::mouseDown (const juce::MouseEvent& e) { applyX (e.getPosition().x); }
void ParamStrip::mouseDrag (const juce::MouseEvent& e) { applyX (e.getPosition().x); }

void ParamStrip::mouseDoubleClick (const juce::MouseEvent&)
{
    if (hasDetent) setValue (detent, true);
}

void ParamStrip::mouseEnter (const juce::MouseEvent&) { hover = true;  repaint(); }
void ParamStrip::mouseExit  (const juce::MouseEvent&) { hover = false; repaint(); }

void ParamStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const double d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? (double) w.deltaY : (double) w.deltaX;
    if (std::abs (d) < 1.0e-5) return;

    // one notch per event; a stepped strip moves one stop, a continuous one
    // moves 2 % of its travel -- fine enough to trim, coarse enough to feel
    const double notch = std::abs (d) > 1.0 ? std::abs (d) : 1.0;
    const double unit  = tickLabels.isEmpty() ? (hi - lo) / 50.0 : 1.0;
    double next = value + (d > 0.0 ? 1.0 : -1.0) * notch * unit;
    if (! tickLabels.isEmpty()) next = std::round (next);
    setValue (next, true);
}

//==============================================================================
DragValue::DragValue (juce::String cap, double lo_, double hi_, double step_, int decimals_)
    : caption (std::move (cap)), lo (lo_), hi (hi_), step (step_), value (lo_), decimals (decimals_)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void DragValue::setValue (double v, bool notify)
{
    v = juce::jlimit (lo, hi, v);
    if (std::abs (v - value) < 1.0e-9) return;
    value = v;
    repaint();
    if (notify && onChange) onChange (value);
}

void DragValue::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.setColour (hover ? stui::field() : stui::card());
    g.fillRect (b);
    g.setColour (hover ? stui::ink3() : stui::hair());
    g.drawRect (b, 1.0f);

    b.reduce (10, 0);
    stui::drawCaps (g, caption, b.removeFromTop (16), stui::ink3(), juce::Justification::centredLeft, 9.0f, 0.18f);
    g.setColour (stui::ink());
    g.setFont (stui::mono (22.0f, true));
    g.drawText (formatValue (value, decimals, juce::String()), b, juce::Justification::centredLeft, false);
}

void DragValue::mouseDown (const juce::MouseEvent& e)
{
    dragStart = value;
    dragY = e.getPosition().y;
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    const double dy = (double) (dragY - e.getPosition().y);
    const double raw = dragStart + dy * step * 0.45;
    setValue (step > 0.0 ? std::round (raw / step) * step : raw, true);
}

void DragValue::mouseEnter (const juce::MouseEvent&) { hover = true;  repaint(); }
void DragValue::mouseExit  (const juce::MouseEvent&) { hover = false; repaint(); }

void DragValue::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const double d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? (double) w.deltaY : (double) w.deltaX;
    if (std::abs (d) < 1.0e-5 || step <= 0.0) return;

    const double notch = std::abs (d) > 1.0 ? std::abs (d) : 1.0;
    setValue (value + (d > 0.0 ? 1.0 : -1.0) * notch * step, true);
}

//==============================================================================
StageView::StageView (StringTheoryProcessor& p) : proc (p)
{
    setPaintingIsUnclipped (true);
    setOpaque (true);
}

void StageView::grab()
{
    const juce::SpinLock::ScopedTryLockType sl (proc.world.lock);
    if (! sl.isLocked()) return;

    frame.voiceCount = proc.world.voiceCount;
    frame.chordStep  = proc.world.chordStep.load();
    frame.nowSec     = (float) proc.world.currentNow();
    frame.rippleCount = juce::jlimit (0, st::kMaxRipples, proc.world.rippleCount);
    for (int i = 0; i < frame.rippleCount; ++i)
        frame.ripples[(size_t) i] = proc.world.ripples[(size_t) i];

    for (int i = 0; i < st::kNumStrings; ++i)
    {
        frame.flash[(size_t) i]      = proc.world.flash[(size_t) i];
        frame.flashVoice[(size_t) i] = proc.world.flashVoice[(size_t) i];
        frame.pitch[(size_t) i]      = proc.world.voicing[
            st::stringSlot (proc.world.stringMap.load(), i)];
    }

    for (int i = 0; i < st::kMaxVoices; ++i)
    {
        const auto& b = proc.world.balls[(size_t) i];
        auto& f = frame.balls[(size_t) i];
        // the harmony voice has no ball: it only ever shows as string sweeps
        f.active = b.active && i < proc.world.voiceCount && ! b.harmony;
        f.mute = b.mute;
        f.solo = b.solo;
        f.x = (float) b.x;
        f.y = (float) b.y;
        f.pulse = b.pulse;
        f.tc = b.trailCount;
        f.th = b.trailHead;
        for (int k = 0; k < st::BallState::kTrail; ++k)
        {
            f.tx[k] = b.trailX[(size_t) k];
            f.ty[k] = b.trailY[(size_t) k];
        }
    }
}

bool StageView::tick (double dt)
{
    const int pIdx = proc.world.progression.load();
    const int step = juce::jlimit (0, 3, proc.world.chordStep.load());

    const auto nameOf = [pIdx, step]
    {
        const auto& progs = st::progressions();
        const auto& prog = progs[(size_t) juce::jlimit (0, (int) progs.size() - 1, pIdx)];
        return st::chordName (prog.steps[(size_t) juce::jlimit (0, 3, step)]);
    };

    if (lastChordStep < 0 || lastProgression < 0)
    {
        chordNow = nameOf();
        lastChordStep = step;
        lastProgression = pIdx;
        chordFade = 1.0f;
    }
    else if (step != lastChordStep || pIdx != lastProgression)
    {
        chordPrev = chordNow;
        chordNow  = nameOf();
        lastChordStep = step;
        lastProgression = pIdx;
        chordFade = 0.0f;
    }

    if (chordFade < 1.0f)
        chordFade = juce::jmin (1.0f, chordFade + (float) (dt / 0.42));

    // ---- idle watchdog ------------------------------------------------------
    // The stage only changes while the balls move or a pluck is still ringing.
    // Once every pluck has rung out there is literally nothing to draw, so the
    // stage stops repainting and an idle instrument costs no GUI time at all.
    bool live = proc.isRunning() || chordFade < 1.0f;

    if (! live)
    {
        const juce::SpinLock::ScopedTryLockType sl (proc.world.lock);
        if (! sl.isLocked())
        {
            live = true;                       // can't tell -> keep drawing
        }
        else
        {
            const float t = (float) proc.world.currentNow();
            for (int i = 0; i < proc.world.rippleCount && ! live; ++i)
                if (t - proc.world.ripples[(size_t) i].birth <= st::kRippleLife) live = true;

            for (int i = 0; i < st::kNumStrings && ! live; ++i)
                if (proc.world.flash[(size_t) i] > 0.01f) live = true;
        }
    }

    if (live) frozenTicks = 0;
    else if (frozenTicks < 1000) ++frozenTicks;

    return frozenTicks < 6 || chordFade < 1.0f;
}

void StageView::paintStrings (juce::Graphics& g, juce::Point<float> c, float radius)
{
    constexpr int n = st::kSectors;
    constexpr float pi = juce::MathConstants<float>::pi;
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    const float ten = juce::jlimit (0.0f, 1.0f, proc.world.tension.load());

    // A plucked string, not a ripple on water. The ball deflects a section of
    // the string; that section swings in and out around its rest position and
    // settles. TENSION decides how much it can move and how quickly it calms:
    // a tight string is stiff (small, quick), a slack one swings wide and long.
    const float soft = st::slackOf (ten);

    const float ampBase = 0.062f * (0.12f + 0.88f * soft);  // fraction of radius at full strength
    const float width   = 0.62f + 0.26f * soft;             // rad, half-width of the deflected span
    const float tauVis  = 0.95f - 0.28f * ten;              // seconds before it has settled
    const float hzBase  = 2.60f + 3.20f * ten;              // tighter string buzzes faster

    // the ring of sector angles is shared by all six strings
    float ca[n], sa[n];
    for (int s = 0; s < n; ++s)
    {
        const float a = -pi + twoPi * (float) s / (float) n;
        ca[s] = std::cos (a);
        sa[s] = std::sin (a);
    }

    juce::Path path;

    for (int i = 0; i < st::kNumStrings; ++i)
    {
        // the inner strings are shorter, so the same absolute swing would look
        // far more violent on them -- even it out, but never below 70 %
        static const std::array<float, st::kNumStrings> szK = []
        {
            std::array<float, st::kNumStrings> k { };
            for (int s = 0; s < st::kNumStrings; ++s)
                k[(size_t) s] = (float) std::pow ((double) st::kStringR[s]
                                                  / (double) st::kStringR[st::kNumStrings - 1], 0.80);
            return k;
        }();

        // Everything that depends on time only -- the decay, the swing rate,
        // the fade -- is evaluated once per pluck here instead of once per
        // sector. That is 96x fewer exp()/sin() calls per string per frame.
        struct Excitation { float angle, invWidth, k; };
        Excitation ex[8];
        int exCount = 0;

        for (int r = 0; r < frame.rippleCount && exCount < 8; ++r)
        {
            const auto& rp = frame.ripples[(size_t) r];
            if (rp.stringIdx != i) continue;

            const float age = frame.nowSec - rp.birth;
            if (age < 0.0f || age > st::kRippleLife) continue;

            const float hz  = hzBase + 0.28f * (float) i;
            const float ph  = twoPi * hz * age;
            const float osc = std::sin (ph) + 0.22f * std::sin (2.0f * ph + 0.9f);

            const float atk  = juce::jmin (1.0f, age / 0.035f);   // no pop on the way in
            const float tail = juce::jlimit (0.0f, 1.0f, 1.0f - age / st::kRippleLife);

            ex[exCount++] = { rp.angle, 1.0f / juce::jmax (1.0e-4f, width),
                              rp.strength * ampBase * szK[(size_t) i] * osc * atk
                              * (tail * tail) * std::exp (-age / tauVis) };
        }

        float pts[2 * n];
        for (int s = 0; s < n; ++s)
        {
            const float a = -pi + twoPi * (float) s / (float) n;

            float dr = 0.0f;
            for (int e = 0; e < exCount; ++e)
            {
                // angular distance to the pluck point, wrapped into [-pi, pi]
                float dth = a - ex[e].angle;
                if (dth >  pi) dth -= twoPi;
                if (dth < -pi) dth += twoPi;

                // the pluck point is pulled furthest; the rest of the string
                // follows in a soft straight-ish slope, exactly like a finger
                // lifting a string off its line
                const float u = juce::jlimit (0.0f, 1.0f, 1.0f - std::abs (dth) * ex[e].invWidth);
                dr += ex[e].k * (u * u * (3.0f - 2.0f * u));
            }

            // soft saturation instead of a hard clamp -- a clamp flattens the
            // crest and leaves a corner, which reads as a polygon rather than water
            constexpr float lim = 0.030f;
            const float shaped = lim * std::tanh (dr / lim);

            const float rr = radius * ((float) st::kStringR[i] + shaped);
            pts[2 * s]     = c.x + rr * ca[s];
            pts[2 * s + 1] = c.y + rr * sa[s];
        }

        // closed Catmull-Rom: smooth, water-like curve through every sector
        path.clear();
        path.startNewSubPath (pts[0], pts[1]);
        for (int s = 0; s < n; ++s)
        {
            const int i0 = ((s - 1 + n) % n) * 2;
            const int i1 = s * 2;
            const int i2 = ((s + 1) % n) * 2;
            const int i3 = ((s + 2) % n) * 2;

            const float c1x = pts[i1]     + (pts[i2] - pts[i0]) / 6.0f;
            const float c1y = pts[i1 + 1] + (pts[i2 + 1] - pts[i0 + 1]) / 6.0f;
            const float c2x = pts[i2]     - (pts[i3] - pts[i1]) / 6.0f;
            const float c2y = pts[i2 + 1] - (pts[i3 + 1] - pts[i1 + 1]) / 6.0f;

            path.cubicTo (c1x, c1y, c2x, c2y, pts[i2], pts[i2 + 1]);
        }
        path.closeSubPath();

        // one flat stroke: the struck string simply picks up the voice colour
        // for a moment, no halo, no second offset line
        const float fl = juce::jlimit (0.0f, 1.0f, frame.flash[(size_t) i]);
        juce::Colour col = stui::ink().withAlpha (0.72f);
        if (fl > 0.01f)
            col = col.interpolatedWith (stui::ball (frame.flashVoice[(size_t) i]), 0.20f * fl)
                     .withAlpha (0.72f + 0.22f * fl);

        g.setColour (col);
        g.strokePath (path, juce::PathStrokeType (1.1f + 0.9f * fl, juce::PathStrokeType::mitered,
                                                  juce::PathStrokeType::rounded));
    }
}

void StageView::paintBalls (juce::Graphics& g, juce::Point<float> c, float radius)
{
    const float k = radius / 358.0f;
    const float br = 9.2f * k;

    bool anySolo = false;
    for (int i = 0; i < frame.voiceCount; ++i)
        if (frame.balls[(size_t) i].solo && ! frame.balls[(size_t) i].mute)
            anySolo = true;

    for (int i = 0; i < frame.voiceCount; ++i)
    {
        const auto& f = frame.balls[(size_t) i];
        if (! f.active) continue;

        float alpha = 1.0f;
        if (f.mute) alpha = 0.28f;
        else if (anySolo && ! f.solo) alpha = 0.22f;

        const auto col = stui::ball (i);
        const float px = c.x + f.x / (float) st::kBoundaryR * radius;
        const float py = c.y + f.y / (float) st::kBoundaryR * radius;
        const float brad = br * (1.0f + 0.20f * juce::jlimit (0.0f, 1.0f, f.pulse));

        // flat: one solid disc per voice, no trail, no glow, no specular dot
        g.setColour (col.withAlpha (alpha));
        g.fillEllipse (px - brad, py - brad, brad * 2.0f, brad * 2.0f);
    }
}

void StageView::paintChordPlate (juce::Graphics& g)
{
    const int x0 = (int) (cx - radius * 0.86f) + 18;
    const int y0 = (int) (cy - radius * 0.50f) + 16;

    stui::drawCaps (g, "CHORD", { x0, y0, 150, 15 }, stui::ink3(),
                    juce::Justification::centredLeft, 10.0f, 0.20f);

    const auto nameRect = juce::Rectangle<int> (x0 - 2, y0 + 14, 170, 56);

    // cross-fade: the old symbol slides up and out, the new one rises in
    const float f = juce::jlimit (0.0f, 1.0f, chordFade);
    g.setFont (stui::serif (42.0f));

    if (f < 1.0f && chordPrev.isNotEmpty() && chordPrev != chordNow)
    {
        g.setColour (stui::ink().withAlpha (1.0f - f));
        g.drawText (chordPrev, nameRect.translated (0, (int) (-8.0f * f)),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (stui::ink().withAlpha (0.25f + 0.75f * f));
    g.drawText (chordNow, nameRect.translated (0, (int) (10.0f * (1.0f - f))),
                juce::Justification::centredLeft, false);

    const int step = juce::jlimit (0, 3, frame.chordStep);
    for (int i = 0; i < 4; ++i)
    {
        const juce::Rectangle<float> dot ((float) x0 + 2.0f + 20.0f * i,
                                          (float) y0 + 80.0f, 6.0f, 6.0f);
        if (i == step) { g.setColour (stui::ink()); g.fillRect (dot); }
        else           { g.setColour (stui::hair()); g.drawRect (dot, 1.0f); }
    }

    stui::drawCaps (g, "STRINGS", { x0, y0 + 102, 150, 13 }, stui::ink3(),
                    juce::Justification::centredLeft, 9.2f, 0.18f);

    for (int i = 0; i < st::kNumStrings; ++i)
    {
        const int y = y0 + 122 + i * 25;
        g.setColour (stui::ink3());
        g.setFont (stui::mono (9.6f));
        g.drawText (juce::String (st::kNumStrings - i), x0 - 6, y, 14, 18,
                    juce::Justification::centredRight, false);

        const float fl = juce::jlimit (0.0f, 1.0f, frame.flash[(size_t) i]);
        g.setColour (fl > 0.06f ? stui::ball (frame.flashVoice[(size_t) i]).withAlpha (0.55f + 0.45f * fl)
                                : stui::ink().withAlpha (0.85f));
        g.setFont (stui::mono (15.0f));
        g.drawText (juce::MidiMessage::getMidiNoteName (frame.pitch[(size_t) i], true, true, 4),
                    x0 + 24, y, 70, 18, juce::Justification::centredLeft, false);
    }
}

void StageView::paint (juce::Graphics& g)
{
    grab();

    auto b = getLocalBounds().toFloat();
    g.setColour (stui::paper());
    g.fillRect (b);

    cx = b.getCentreX();
    cy = b.getCentreY();
    radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 10.0f;

    // hub
    g.setColour (stui::card());
    g.fillEllipse (cx - radius * 0.052f, cy - radius * 0.052f, radius * 0.104f, radius * 0.104f);
    g.setColour (stui::hair());
    g.drawEllipse (cx - radius * 0.052f, cy - radius * 0.052f, radius * 0.104f, radius * 0.104f, 1.0f);
    g.setColour (stui::ink3());
    g.fillEllipse (cx - 1.1f, cy - 1.1f, 2.2f, 2.2f);

    paintStrings (g, { cx, cy }, radius);

    // rim
    g.setColour (stui::ink());
    g.drawEllipse (cx - radius, cy - radius, radius * 2.0f, radius * 2.0f, 7.0f);

    // the chord plate is a label pasted onto the drawing: it needs a paper
    // ground, otherwise the strings run straight through the type
    {
        const juce::Rectangle<float> plate (cx - radius * 0.86f, cy - radius * 0.50f, 178.0f, 288.0f);
        g.setColour (stui::card());
        g.fillRect (plate);
        g.setColour (stui::hair());
        g.drawRect (plate, 1.0f);
    }

    paintBalls (g, { cx, cy }, radius);
    paintChordPlate (g);
}

//==============================================================================
VoiceCard::VoiceCard (StringTheoryProcessor& p, int voiceIndex)
    : proc (p), index (voiceIndex)
{
    juce::StringArray octs { "-2", "-1", "0", "+1", "+2" };
    octaveBox.setItems (octs, {}, 2);
    octaveBox.setInlineCaption (true);

    juce::StringArray chans;
    for (int c = 1; c <= 16; ++c) chans.add (juce::String (c).paddedLeft ('0', 2));
    channelBox.setItems (chans, {}, 0);
    channelBox.setInlineCaption (true);

    // one patch per voice -- the menu keeps the LOW / MID / HIGH families so a
    // colour is still found the way a patch library is browsed. Each name
    // carries its length (S / M / L) so "short pluck" and "long drone" can be
    // told apart before it is selected.
    juce::StringArray patches;
    for (int i = 0; i < st::timbreCount(); ++i)
        patches.add (juce::String (st::timbreAt (i).name) + " · " + st::timbreDurTag (i));
    timbreBox.setItems (patches, {}, 0);
    timbreBox.setInlineCaption (true);
    timbreBox.groupOf = [] (int i) { return juce::String (st::timbreFamily (i)); };

    delButton.setGlyph (MiniButton::Glyph::cross);
    muteButton.setCompact (true);
    soloButton.setCompact (true);

    rateStrip.setTicks ({ "1/1", "1/2", "1/4", "1/8", "1/16" });
    rateStrip.setFramed (false);
    probStrip.setFramed (false);
    volStrip.setFramed (false);

    addAndMakeVisible (timbreBox);
    addAndMakeVisible (octaveBox);
    addAndMakeVisible (channelBox);
    addAndMakeVisible (rateStrip);
    addAndMakeVisible (randButton);
    addAndMakeVisible (probStrip);
    addAndMakeVisible (volStrip);
    addAndMakeVisible (chordButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (soloButton);
    addAndMakeVisible (delButton);

    timbreBox.onChange = [this] { proc.setVoiceTimbre (index, timbreBox.getSelectedIndex()); };
    octaveBox.onChange = [this] { proc.setVoiceOctave (index, octaveBox.getSelectedIndex() - 2); };
    channelBox.onChange = [this] { proc.setVoiceChannel (index, channelBox.getSelectedIndex() + 1); };
    rateStrip.onChange = [this] (double v) { proc.setVoiceRate (index, (int) std::lround (v)); };
    randButton.onClick = [this]
    {
        proc.setVoiceRandomRate (index, randButton.isOn());
    };
    probStrip.onChange  = [this] (double v) { proc.setVoiceRand (index, v / 100.0); };
    volStrip.onChange   = [this] (double v) { proc.setVoiceGain (index, v / 100.0); };
    chordButton.onClick = [this] { proc.setVoiceFollow (index, chordButton.isOn()); };
    muteButton.onClick  = [this] { proc.setVoiceMute (index, muteButton.isOn()); };
    soloButton.onClick  = [this] { proc.setVoiceSolo (index, soloButton.isOn()); };
    delButton.onClick   = [this] { if (! harmony && onDelete) onDelete (index); };

    // ---- the harmony voice (always voice 1) --------------------------------
    //  No ball, so there is no probability of a string being missed and no
    //  chord-follow switch to turn off: it always follows the progression. The
    //  two controls it gives up make room for a read-out of the chord it is
    //  sounding right now.
    harmony = st::StrumWorld::isHarmonyVoice (voiceIndex);
    if (harmony)
    {
        probStrip.setVisible (false);
        chordButton.setVisible (false);
        delButton.setVisible (false);

        // on the chord voice the same strip means something else: there is no
        // ball to speed up, the division decides how often the whole chord is
        // rolled -- so say so, instead of reusing the ball-speed label.
        rateStrip.setCaption ("ROLL");
    }
}

void VoiceCard::refresh()
{
    if (harmony)
    {
        const auto ch = proc.world.currentChordText();
        if (ch != chordShown) { chordShown = ch; repaint(); }
    }

    const juce::SpinLock::ScopedTryLockType sl (proc.world.lock);
    if (! sl.isLocked()) return;
    if (index >= proc.world.voiceCount) return;

    const auto& b = proc.world.balls[(size_t) index];
    const int tIdx = juce::jlimit (0, st::timbreCount() - 1, b.timbre);
    timbreBox.setSelectedIndex (tIdx);
    timbreBox.setTrailingText (st::timbreFamily (tIdx));
    octaveBox.setSelectedIndex (b.octave + 2);
    channelBox.setSelectedIndex (juce::jlimit (0, 15, b.channel - 1));
    rateStrip.setValue (b.rate);
    randButton.setOn (b.randomRate);
    probStrip.setValue (b.rand * 100.0);
    volStrip.setValue (b.gain * 100.0);
    chordButton.setOn (b.follow);
    muteButton.setOn (b.mute);
    soloButton.setOn (b.solo);
}

void VoiceCard::resized()
{
    auto b = getLocalBounds();
    b.removeFromLeft (12);
    b.removeFromRight (12);
    b.removeFromTop (6);

    // band A -- identity + patch. The patch is the headline of a voice now, so
    // it gets the full remaining width instead of a caption in a global row
    auto a = b.removeFromTop (26);
    a.removeFromLeft (22);                          // "V1" is painted, not a control
    a.removeFromLeft (8);
    timbreBox.setBounds (a);

    b.removeFromTop (4);

    // band B -- the three performance strips, stacked so every track keeps a
    // usable length. 26 px is the narrowest height at which ParamStrip still
    // prints its caption on a line of its own.
    auto s = b.removeFromTop (26);
    constexpr int gap = 12;
    const int w = s.getWidth();
    // the rate cell shares its slot with the R toggle: random-rate arming sits
    // right next to the division it re-rolls
    auto rateCell = s.removeFromLeft (w * 33 / 100);
    rateStrip.setBounds (rateCell.removeFromLeft (rateCell.getWidth() - 22));
    rateCell.removeFromLeft (4);
    randButton.setBounds (rateCell.removeFromLeft (18));
    s.removeFromLeft (gap);
    const auto mid = s.removeFromLeft (w * 33 / 100);
    if (harmony) chordCell = mid; else      probStrip.setBounds (mid);
    s.removeFromLeft (gap);
    volStrip.setBounds (s);

    b.removeFromTop (4);

    // band C -- plumbing: register, channel, chord follow, mute / solo / delete
    auto c = b.removeFromTop (24);
    octaveBox.setBounds (c.removeFromLeft (88));
    c.removeFromLeft (8);
    channelBox.setBounds (c.removeFromLeft (112));
    c.removeFromLeft (8);

    if (harmony)
    {
        // no delete, no chord switch: mute / solo anchor the right edge so the
        // band does not read as a row that ran out of controls
        soloButton.setBounds (c.removeFromRight (20));
        c.removeFromRight (6);
        muteButton.setBounds (c.removeFromRight (20));
        return;
    }

    chordButton.setBounds (c.removeFromLeft (46));
    c.removeFromLeft (8);
    muteButton.setBounds (c.removeFromLeft (20));
    c.removeFromLeft (6);
    soloButton.setBounds (c.removeFromLeft (20));
    c.removeFromLeft (6);
    delButton.setBounds (c.removeFromLeft (18));
}

void VoiceCard::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (stui::card());
    g.fillRect (b);

    // a whisper of the voice colour: card, spine and ball read as one object
    g.setColour (stui::ball (index).withAlpha (0.030f));
    g.fillRect (b);

    g.setColour (hover ? stui::ink3() : stui::hair());
    g.drawRect (b, 1.0f);

    // identity bar
    g.setColour (stui::ball (index));
    g.fillRect (juce::Rectangle<float> (1.0f, 1.0f, 4.0f, b.getHeight() - 2.0f));

    // hairline dividers keep RATE / PROB / VOL from reading as one long row
    {
        g.setColour (stui::hair());
        const auto firstCell = harmony ? chordCell : probStrip.getBounds();
        const auto volCell   = volStrip.getBounds();
        for (const auto* r : { &firstCell, &volCell })
            g.drawVerticalLine (r->getX() - 6, (float) r->getY() - 1.0f, (float) r->getBottom() - 1.0f);
    }

    // the harmony voice prints the chord it is sounding where the other cards
    // carry their probability slider -- it is the one thing this voice *is*
    if (harmony && ! chordCell.isEmpty())
    {
        auto cell = chordCell;
        auto top = cell.removeFromTop (juce::jmax (10, cell.getHeight() - 16));
        stui::drawCaps (g, "CHORD", top, stui::ink3(), juce::Justification::centredLeft, 8.0f, 0.15f);
        g.setColour (stui::ink());
        g.setFont (stui::serif (14.0f));
        g.drawText (chordShown, cell, juce::Justification::centredLeft, false);
    }

    g.setColour (stui::ink().withAlpha (0.85f));
    g.setFont (stui::capsFont (11.0f, 0.08f, true));
    g.drawText ("V" + juce::String (index + 1), 13, 6, 26, 26, juce::Justification::centredLeft, false);
}

void VoiceCard::mouseEnter (const juce::MouseEvent&) { hover = true;  repaint(); }
void VoiceCard::mouseExit  (const juce::MouseEvent&) { hover = false; repaint(); }

//==============================================================================
StringTheoryEditor::StringTheoryEditor (StringTheoryProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), stage (p)
{
    setLookAndFeel (&plateLaf());
    setResizable (false, false);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (stage);
    addAndMakeVisible (bpmBox);
    addAndMakeVisible (playButton);
    addAndMakeVisible (syncButton);
    addAndMakeVisible (progressionBox);
    addAndMakeVisible (paceBox);
    addAndMakeVisible (retuneButton);
    addAndMakeVisible (retuneBarsBox);
    addAndMakeVisible (tensionStrip);
    addAndMakeVisible (reverbStrip);
    addAndMakeVisible (delayStrip);
    addAndMakeVisible (fbStrip);
    addAndMakeVisible (outStrip);
    addAndMakeVisible (divStrip);
    addAndMakeVisible (internalButton);
    addAndMakeVisible (midiOutBox);
    addAndMakeVisible (addButton);
    addAndMakeVisible (pageUpButton);
    addAndMakeVisible (pageDownButton);

    midiOutBox.setCaption ("MIDI OUT");
    midiOutBox.menuBuilder = [this] (juce::StringArray& items, juce::StringArray& ids, int& sel)
    {
        buildMidiMenu (items, ids, sel);
    };
    {
        juce::StringArray items, ids;
        int sel = 0;
        buildMidiMenu (items, ids, sel);
        midiOutBox.setItems (items, ids, sel);
    }

    // --- content -------------------------------------------------------------
    juce::StringArray progs;
    for (auto& pr : st::progressions()) progs.add (pr.label);
    progressionBox.setItems (progs, {}, juce::jlimit (0, progs.size() - 1, proc.world.progression.load()));
    progressionBox.groupOf = [] (int i)
    {
        const auto& p = st::progressions();
        if (! juce::isPositiveAndBelow (i, (int) p.size())) return juce::String();
        return juce::String (p[(size_t) i].family);
    };

    // RETUNE: re-deal the six chord tones across the strings. EVERY decides how
    // often the deal is repeated -- HOLD means only on the button.
    retuneBarsBox.setItems ({ "HOLD", "2 BAR", "4 BAR", "8 BAR", "16 BAR" }, {}, 0);
    retuneButton.setCompact (false);

    // PACE: harmonic rhythm -- how many bars a chord holds before the
    // progression steps on. HOLD freezes the harmony; the bar clock (and with
    // it EVERY) keeps ticking either way.
    paceBox.setItems ({ "HOLD", "1 BAR", "2 BAR", "4 BAR", "8 BAR" }, {}, 1);

    divStrip.setTicks ({ "1/16", "1/8", "1/8 D", "1/4", "1/2" });
    tensionStrip.setDetent (50.0);

    playButton.setGlyph (MiniButton::Glyph::play);
    addButton.setGlyph (MiniButton::Glyph::plus);
    pageUpButton.setGlyph (MiniButton::Glyph::up);
    pageDownButton.setGlyph (MiniButton::Glyph::down);
    pageUpButton.setCompact (true);
    pageDownButton.setCompact (true);

    // --- callbacks -----------------------------------------------------------
    bpmBox.onChange = [this] (double v) { proc.world.bpm.store (v); };
    playButton.onClick = [this] { proc.setPlaying (! proc.isRunning()); refreshAll(); };
    syncButton.onClick = [this] { proc.world.synced.store (syncButton.isOn()); };
    progressionBox.onChange = [this] { proc.world.progression.store (progressionBox.getSelectedIndex()); };
    paceBox.onChange = [this]
    {
        static const int bars[5] = { 0, 1, 2, 4, 8 };
        proc.world.paceBars.store (bars[juce::jlimit (0, 4, paceBox.getSelectedIndex())]);
        refreshAll();
    };
    tensionStrip.onChange = [this] (double v) { proc.world.tension.store ((float) (v / 100.0)); };
    retuneButton.onClick = [this] { proc.retuneStrings(); };
    retuneBarsBox.onChange = [this]
    {
        static const int bars[5] = { 0, 2, 4, 8, 16 };
        proc.setRetuneBars (bars[juce::jlimit (0, 4, retuneBarsBox.getSelectedIndex())]);
        refreshAll();
        stage.repaint();
    };
    reverbStrip.onChange = [this] (double v) { proc.reverbMix.store ((float) (v / 100.0)); };
    delayStrip.onChange  = [this] (double v) { proc.delayMix.store ((float) (v / 100.0)); };
    fbStrip.onChange     = [this] (double v) { proc.delayFb.store ((float) (v / 100.0)); };
    outStrip.onChange    = [this] (double v) { proc.master.store ((float) (v / 100.0)); };
    divStrip.onChange    = [this] (double v) { proc.delayDiv.store ((int) std::lround (v)); };
    internalButton.onClick = [this] { proc.world.internalSynth.store (internalButton.isOn()); };
    addButton.onClick = [this]
    {
        if (proc.voiceCount() < st::kMaxVoices)
        {
            proc.addVoice();
            rebuildPager();
            setPage ((proc.voiceCount() - 1) / st::kVoicePage);
            repaint();
        }
    };
    pageUpButton.onClick   = [this] { setPage (page - 1); };
    pageDownButton.onClick = [this] { setPage (page + 1); };

    midiOutBox.onChange = [this]
    {
        proc.setMidiRoute (midiOutBox.getSelectedId());
    };

    // --- voice cards ---------------------------------------------------------
    for (int i = 0; i < st::kMaxVoices; ++i)
    {
        cards[(size_t) i] = std::make_unique<VoiceCard> (proc, i);
        auto* c = cards[(size_t) i].get();
        c->onDelete = [this] (int idx)
        {
            proc.removeVoice (idx);
            setPage (juce::jmin (page, juce::jmax (0, (proc.voiceCount() - 1) / st::kVoicePage)));
            rebuildPager();
        };
        addAndMakeVisible (c);
        c->setVisible (false);
    }

    // --- initial values ------------------------------------------------------
    bpmBox.setValue (proc.world.bpm.load());
    syncButton.setOn (proc.world.synced.load());
    internalButton.setOn (proc.world.internalSynth.load());
    reverbStrip.setValue (proc.reverbMix.load() * 100.0);
    delayStrip.setValue (proc.delayMix.load() * 100.0);
    fbStrip.setValue (proc.delayFb.load() * 100.0);
    outStrip.setValue (proc.master.load() * 100.0);
    divStrip.setValue ((double) proc.delayDiv.load());
    tensionStrip.setValue (proc.world.tension.load() * 100.0);

    setSize (800, contentBottom());
    rebuildPager();
    setPage (0);
    refreshAll();

    startTimerHz (60);
}

StringTheoryEditor::~StringTheoryEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

int StringTheoryEditor::contentBottom() const
{
    const int voices = proc.voiceCount();
    const int vis = juce::jlimit (0, st::kVoicePage, voices - page * st::kVoicePage);
    const int rows = juce::jmax (1, (vis + 1) / 2);
    return stui::kCardY + rows * (stui::kCardH + stui::kCardGapY)
           - stui::kCardGapY + stui::kCardBotPad;
}

void StringTheoryEditor::rebuildPager()
{
    const int voices = proc.voiceCount();
    const int pages = juce::jmax (1, (voices + st::kVoicePage - 1) / st::kVoicePage);
    page = juce::jlimit (0, pages - 1, page);

    addButton.setEnabled (voices < st::kMaxVoices);
    resized();
    const int wantH = contentBottom();
    if (getHeight() != wantH) setSize (800, wantH);
    repaint();
}

void StringTheoryEditor::setPage (int p)
{
    const int voices = proc.voiceCount();
    const int pages = juce::jmax (1, (voices + st::kVoicePage - 1) / st::kVoicePage);
    page = juce::jlimit (0, pages - 1, p);

    for (int i = 0; i < st::kMaxVoices; ++i)
    {
        const bool visible = (i >= page * st::kVoicePage) && (i < juce::jmin (voices, (page + 1) * st::kVoicePage));
        cards[(size_t) i]->setVisible (visible);
    }

    const bool multi = pages > 1;
    pageUpButton.setVisible (multi);
    pageDownButton.setVisible (multi);
    pageUpButton.setEnabled (page > 0);
    pageDownButton.setEnabled (page < pages - 1);

    pageFade = 0.0f;                     // cards fade in on the new page
    resized();
    const int wantH = contentBottom();
    if (getHeight() != wantH) setSize (800, wantH);
    repaint();
}

void StringTheoryEditor::refreshAll()
{
    const bool running = proc.isRunning();
    playButton.setActiveFace (running);
    playButton.setGlyph (running ? MiniButton::Glyph::stop : MiniButton::Glyph::play);
    playButton.setText (running ? "STOP" : "PLAY");

    bpmBox.setValue (proc.world.bpm.load());
    syncButton.setOn (proc.world.synced.load());
    internalButton.setOn (proc.world.internalSynth.load());
    tensionStrip.setValue (proc.world.tension.load() * 100.0);

    reverbStrip.setValue (proc.reverbMix.load() * 100.0);
    delayStrip.setValue (proc.delayMix.load() * 100.0);
    fbStrip.setValue (proc.delayFb.load() * 100.0);
    outStrip.setValue (proc.master.load() * 100.0);
    divStrip.setValue ((double) juce::jlimit (0, 4, proc.delayDiv.load()));
    progressionBox.setSelectedIndex (juce::jlimit (0, st::progressionCount() - 1, proc.world.progression.load()));

    {
        static const int bars[5] = { 0, 1, 2, 4, 8 };
        const int pace = juce::jlimit (0, 8, proc.world.paceBars.load());
        int idx = 1;
        for (int i = 0; i < 5; ++i) if (bars[i] == pace) idx = i;
        paceBox.setSelectedIndex (idx);
    }

    {
        static const int bars[5] = { 0, 2, 4, 8, 16 };
        const int every = juce::jlimit (0, 16, proc.world.retuneBars.load());
        int idx = 0;
        for (int i = 0; i < 5; ++i) if (bars[i] == every) idx = i;
        retuneBarsBox.setSelectedIndex (idx);
        retuneButton.setArmed (every > 0);
    }

    {
        const uint32_t map = proc.world.stringMap.load();
        if (map != lastMapShown)
        {
            lastMapShown = map;
            stage.repaint();
        }
    }

    if (midiOutBox.getSelectedId() != proc.getMidiRoute())
    {
        juce::StringArray items, ids;
        int sel = 0;
        buildMidiMenu (items, ids, sel);
        midiOutBox.setItems (items, ids, sel);
    }

    for (int i = 0; i < st::kMaxVoices; ++i)
        if (cards[(size_t) i]->isVisible())
            cards[(size_t) i]->refresh();

    if (proc.voiceCount() != lastVoiceCount)
    {
        lastVoiceCount = proc.voiceCount();
        rebuildPager();
        setPage (page);
        repaint();
    }
}

void StringTheoryEditor::buildMidiMenu (juce::StringArray& items, juce::StringArray& ids, int& selected) const
{
    items.clear();
    ids.clear();

    items.add ("OFF");      ids.add ("OFF");
    items.add ("HOST BUS"); ids.add ("HOST");

    for (auto& d : juce::MidiOutput::getAvailableDevices())
    {
        if (d.identifier == "OFF" || d.identifier == "HOST") continue;
        items.add (d.name);
        ids.add (d.identifier);
    }

    selected = juce::jmax (0, ids.indexOf (proc.getMidiRoute()));
}

void StringTheoryEditor::timerCallback()
{
    ++tick;
    proc.flushMidi();

    const bool stageLive = stage.tick (1.0 / 60.0);

    if (pageFade < 1.0f)
    {
        pageFade = juce::jmin (1.0f, pageFade + 1.0f / 60.0f / kFadeTime);
        for (auto& c : cards)
            c->setAlpha (0.35f + 0.65f * pageFade);
    }

    if (tick % 6 == 0) refreshAll();

    // a parked stage costs nothing: no plucks to draw, no repaint
    if (stageLive) stage.repaint();
}

bool StringTheoryEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        proc.setPlaying (! proc.isRunning());
        refreshAll();
        return true;
    }
    return false;
}

void StringTheoryEditor::paint (juce::Graphics& g)
{
    // publish the window's scale before anything measures type: children are
    // painted after their parent, so every font factory used below (and in
    // every child) sees a scale that is already correct.
    //
    // Component::getDesktopScaleFactor() is NOT the value we need -- it only
    // mirrors the global scale factor, which stays at 1.0 unless someone sets
    // it. The renderer actually scales by the peer's platform factor
    // (AffineTransform::scale (peer->getPlatformScaleFactor())), so that is
    // the number the glyph rasteriser multiplies the font height by.
    auto s = 1.0f;
    if (auto* p = getPeer())
        s = (float) p->getPlatformScaleFactor();
    stui::setUiScale (s);

    g.fillAll (stui::paper());

    // title block
    g.setColour (stui::ink());
    g.setFont (stui::serif (31.0f).withExtraKerningFactor (0.22f));
    g.drawText ("STRING THEORY", 30, 8, 460, 40, juce::Justification::centredLeft, false);

    stui::drawCaps (g, "STRUM ATMOSPHERE GENERATOR", { 31, 46, 380, 14 }, stui::ink3(),
                    juce::Justification::centredLeft, 9.0f, 0.26f);

    g.setColour (stui::hair());
    g.fillRect (stui::kMargin, 66, stui::kContentW, 1);

    // section rules
    g.setColour (stui::hairSoft());
    g.fillRect (stui::kMargin, stui::kRuleY, stui::kContentW, 1);

    stui::drawCaps (g, "VOICES  " + juce::String (proc.voiceCount()),
                    { stui::kMargin, stui::kHeadY + 2, 200, 20 }, stui::ink3(),
                    juce::Justification::centredLeft, 9.4f, 0.18f);
}

void StringTheoryEditor::resized()
{
    const int M = stui::kMargin;
    const int W = stui::kContentW;

    stage.setBounds (M, stui::kStageY, W, stui::kStageH);

    // row 1 -- tempo / transport / progression / tension
    //  tempo | transport | progression | PACE | RETUNE | EVERY | tension
    //  (progression and tension give up width to the harmonic-rhythm control
    //   and the two string controls)
    bpmBox.setBounds (M, stui::kRow1Y, 60, stui::kRowH);
    playButton.setBounds (98, stui::kRow1Y, 56, stui::kRowH);
    syncButton.setBounds (162, stui::kRow1Y, 52, stui::kRowH);
    progressionBox.setBounds (222, stui::kRow1Y, 148, stui::kRowH);
    paceBox.setBounds (378, stui::kRow1Y, 80, stui::kRowH);
    retuneButton.setBounds (466, stui::kRow1Y, 80, stui::kRowH);
    retuneBarsBox.setBounds (554, stui::kRow1Y, 88, stui::kRowH);
    tensionStrip.setBounds (650, stui::kRow1Y, 120, stui::kRowH);

    // row 2 -- effects, six equal cells
    int x = M;
    const int fxW[6] = { 116, 116, 116, 116, 116, 120 };
    juce::Component* fx[6] = { &reverbStrip, &delayStrip, &fbStrip, &divStrip, &outStrip, &internalButton };
    for (int i = 0; i < 6; ++i)
    {
        fx[i]->setBounds (x, stui::kRow2Y, fxW[i], stui::kRowH);
        x += fxW[i] + stui::kGutter;
    }

    // voice grid
    const int cardW = 364, cardH = stui::kCardH, gapX = 12, gapY = stui::kCardGapY;
    if (cards[0] != nullptr)
    {
        for (int i = 0; i < st::kMaxVoices; ++i)
        {
            const int local = i % st::kVoicePage;
            const int col = local % 2;
            const int row = local / 2;
            cards[(size_t) i]->setBounds (M + col * (cardW + gapX),
                                          stui::kCardY + row * (cardH + gapY), cardW, cardH);
        }
    }

    // voices header: page indicator, pager, add
    addButton.setBounds (M + W - 132, stui::kHeadY - 2, 132, 22);
    pageDownButton.setBounds (M + W - 132 - 12 - 22, stui::kHeadY - 1, 22, 20);
    pageUpButton.setBounds (M + W - 132 - 12 - 22 - 6 - 22, stui::kHeadY - 1, 22, 20);

    midiOutBox.setBounds (M + W - 190, 10, 190, 48);
}
