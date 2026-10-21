#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <array>
#include <cmath>
#include <functional>

#include "PluginProcessor.h"

//==============================================================================
//  Visual direction -- "scientific plate", flat edition
//
//  Flat ivory paper, graphite hairlines, one editorial serif for chord symbols
//  and letterspaced caps for every label. Everything sits on an 8 px grid with
//  30 px outer margins, so the page reads as a single sheet: header, plate,
//  transport, progression, effects, voices. The only saturated colour is the
//  six voice balls.
//==============================================================================
namespace stui
{
    inline juce::Colour paper()      { return juce::Colour (0xfff7f4ed); }
    inline juce::Colour paperShade() { return juce::Colour (0xfff1ece1); }
    inline juce::Colour card()       { return juce::Colour (0xfffbf9f4); }
    inline juce::Colour ink()        { return juce::Colour (0xff1d1c19); }
    inline juce::Colour ink2()       { return juce::Colour (0xff6a665d); }
    inline juce::Colour ink3()       { return juce::Colour (0xff9c968a); }
    inline juce::Colour hair()       { return juce::Colour (0xffd6cfc1); }
    inline juce::Colour hairSoft()   { return juce::Colour (0xffe6e0d4); }
    inline juce::Colour field()      { return juce::Colour (0xffefeadf); }

    inline juce::Colour ball (int i)
    {
        // Six voices, six inks: one hue family, matched lightness, no neon.
        // They are used for the balls, the voice-card spine and the string flash,
        // so they have to sit quietly on ivory and still be told apart at 9 px.
        //  The hues are spaced ~55-60 deg apart: the old teal sat only 24 deg
        //  from the verdigris, and the two balls looked like the same voice.
        static const juce::Colour c[6] =
        {
            juce::Colour (0xffc4165c),   // rose        ~340
            juce::Colour (0xff2a5ba8),   // indigo      ~216
            juce::Colour (0xffcc8a14),   // ochre       ~38
            juce::Colour (0xff11805a),   // verdigris   ~160
            juce::Colour (0xff74509e),   // violet      ~270
            juce::Colour (0xff4c8430),   // moss        ~95
        };
        return c[(size_t) (((i % 6) + 6) % 6)];
    }

    //----------------------------------------------------------------------
    //  High-DPI crispness -- sizes are snapped to whole physical pixels.
    //
    //  A plugin window is often handed a fractional scale (1.25 on a 125%
    //  display, 1.5 on 150%). At 9.8 x 1.5 = 14.7 device pixels the
    //  rasteriser cannot hint the glyphs and smears the stems: that is the
    //  soft type you see inside the VST while the standalone stays sharp.
    //  So every size below is inverted through the current scale, giving the
    //  logical size whose physical result is integral (14.7 -> 15 px -> 10.0).
    //
    //  The scale is published once per paint by the editor: free helpers
    //  such as drawCaps() have no component of their own to ask.
    //----------------------------------------------------------------------
    inline float& uiScaleRef()       { static float s = 1.0f; return s; }
    inline void   setUiScale (float s) { uiScaleRef() = juce::jmax (0.5f, s); }
    inline float  uiScale()          { return uiScaleRef(); }

    inline float snapFontSize (float logicalSize, float scale)
    {
        if (scale <= 0.0f || logicalSize <= 0.0f)
            return logicalSize;

        const float physical = logicalSize * scale;
        return juce::jmax (1.0f, std::round (physical) / scale);
    }

    inline juce::Font sans (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Segoe UI", snapFontSize (h, uiScale()),
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font serif (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Georgia", snapFontSize (h, uiScale()),
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font mono (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions ("Consolas", snapFontSize (h, uiScale()),
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    inline juce::Font capsFont (float h, float tracking = 0.16f, bool bold = false)
    {
        return sans (h, bold).withExtraKerningFactor (tracking);
    }

    // A glyph rasterised at a half-pixel offset is smeared across two device
    // pixels and reads soft however good the type is. Layout coordinates are
    // whole logical numbers, but odd ones land on .5 at 1.25 / 1.5 scaling, so
    // every text origin is pulled back onto the device grid before drawing.
    inline float snapToPixel (float logicalValue)
    {
        const auto s = uiScale();
        return std::round (logicalValue * s) / s;
    }

    inline void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area,
                          juce::Colour colour, juce::Justification just = juce::Justification::centredLeft,
                          float size = 8.4f, float tracking = 0.16f)
    {
        g.setColour (colour);
        g.setFont (capsFont (size, tracking));

        auto r = area.toFloat();
        r.setX (snapToPixel (r.getX()));
        r.setY (snapToPixel (r.getY()));
        g.drawText (text, r, just, false);
    }

    // layout constants -- one grid for the whole page
    constexpr int kMargin   = 30;
    constexpr int kGutter   = 8;
    constexpr int kContentW = 740;

    //  Vertical rhythm. Everything below the stage is positioned off these,
    //  so the page can be tightened in exactly one place. Text sizes are
    //  deliberately NOT derived from them: squeezing the page must never
    //  shrink the type.
    //
    //  The stage is square and as wide as the content column, so the dial's
    //  ring runs right out to the margin on all four sides -- the page reads
    //  as one circle with an equal gutter around it. The height that bought
    //  is taken out of the control strip below instead (tighter rows, three
    //  bands instead of four in each voice card).
    constexpr int kStageY   = 74;
    constexpr int kStageH   = 740;
    constexpr int kRowH     = 46;                   // one control row
    constexpr int kRow1Y    = 822;                  // tempo / chord / tension
    constexpr int kRow2Y    = 874;                  // effects
    constexpr int kRuleY    = 932;                  // hairline + VOICES header
    constexpr int kHeadY    = 934;
    constexpr int kCardY    = 958;                  // first voice card
    constexpr int kCardH    = 96;
    constexpr int kCardGapY = 4;
    constexpr int kCardBotPad = 14;
}

//==============================================================================
//  A flat "choose from a list" control that matches the paper plate.
//==============================================================================
class ChoiceBox : public juce::Component
{
public:
    explicit ChoiceBox (juce::String captionText = {});

    void setItems (const juce::StringArray& newItems,
                   const juce::StringArray& newIds = {},
                   int selectedIndex = 0);
    void setSelectedIndex (int idx, bool notify = false);
    int  getSelectedIndex() const { return sel; }
    juce::String getSelectedId() const;
    void setCaption (const juce::String& c) { caption = c; repaint(); }

    // caption on the same line as the value instead of above it -- the only
    // way to get a labelled box into a card band that is 24-30 px tall
    void setInlineCaption (bool shouldInline) { inlineCaption = shouldInline; repaint(); }

    // faint caps tag at the right edge (the patch family: LOW / MID / HIGH) --
    // it fills the long box with information instead of air
    void setTrailingText (const juce::String& t) { if (t == trailing) return; trailing = t; repaint(); }

    std::function<void()> onChange;
    // fills items / ids / selection right before the menu opens (MIDI devices
    // are enumerated on demand, never on a timer)
    std::function<void (juce::StringArray& items, juce::StringArray& ids, int& selected)> menuBuilder;
    // optional section header for entry i (chord families, timbre families)
    std::function<juce::String (int index)> groupOf;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String caption, shown, trailing;
    juce::StringArray items, ids;
    int sel = 0;
    bool hover = false;
    bool inlineCaption = false;
};

//==============================================================================
//  Flat text / glyph button. Toggle buttons invert to a solid ink face.
//==============================================================================
class MiniButton : public juce::Component
{
public:
    enum class Glyph { none, play, stop, plus, up, down, cross };

    explicit MiniButton (juce::String text, bool isToggle = false);

    void setOn (bool shouldBeOn, bool notify = false);
    bool isOn() const { return on; }
    void setText (juce::String t) { label = std::move (t); repaint(); }
    void setGlyph (Glyph g) { glyph = g; repaint(); }
    void setCompact (bool c) { compact = c; repaint(); }
    void setActiveFace (bool solid) { solidFace = solid; repaint(); }

    // a small dot in the corner: this button is on a timer, not just a click
    void setArmed (bool a) { if (armed == a) return; armed = a; repaint(); }

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String label;
    Glyph glyph = Glyph::none;
    bool toggle = false, on = false, hover = false, down = false;
    bool compact = false, solidFace = false, armed = false;
};

//==============================================================================
//  Inline "LABEL ----o---- value" strip. Two-line when tall enough.
//==============================================================================
class ParamStrip : public juce::Component
{
public:
    ParamStrip (juce::String label, double lo, double hi, int decimals, juce::String suffix = {});
    void setValue (double v, bool notify = false);
    double getValue() const { return value; }
    void setCaption (const juce::String& c) { labelText = c; repaint(); }

    // stepped mode: the strip snaps to N stops and prints labels[i] instead of
    // a number (1/1 1/2 1/4 1/8 1/16, 1/16 1/8 1/8 D 1/4 1/2 ...)
    void setTicks (const juce::StringArray& labels);

    // centre-detent drawing: the fill grows out from `detent` instead of the
    // left end (TENSION reads left = slack, centre = normal, right = tight)
    void setDetent (double at);

    // framed = the strip owns its own 1 px cell border (page grid);
    // unframed = it lives inside a card and only draws label + track
    void setFramed (bool f) { framed = f; repaint(); }

    std::function<void (double)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void applyX (int x);
    juce::String labelText, suffix;
    double lo = 0.0, hi = 1.0, value = 0.0;
    double detent = 0.0;
    bool hasDetent = false;
    int decimals = 0;
    bool hover = false;
    bool framed = true;
    juce::StringArray tickLabels;
    juce::Rectangle<int> trackArea;
};

//==============================================================================
//  Caption + big number, vertical drag. Used for BPM.
//==============================================================================
class DragValue : public juce::Component
{
public:
    DragValue (juce::String caption, double lo, double hi, double step, int decimals);
    void setValue (double v, bool notify = false);
    double getValue() const { return value; }

    std::function<void (double)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::String caption;
    double lo = 0, hi = 0, step = 1, value = 0;
    int decimals = 0;
    bool hover = false;
    double dragStart = 0;
    int dragY = 0;
};

//==============================================================================
//  The stage: rim, six vibrating strings, balls, chord plate.
//==============================================================================
class StageView : public juce::Component
{
public:
    explicit StageView (StringTheoryProcessor&);
    void paint (juce::Graphics&) override;

    // advances the little UI animations (chord cross-fade) and reports whether
    // the stage still has something to draw -- the editor only repaints when it
    // does, which is what keeps the idle CPU near zero
    bool tick (double dt);

private:
    struct FrameBall
    {
        bool active = false;
        bool mute = false, solo = false;
        float x = 0.0f, y = 0.0f;
        float pulse = 0.0f;
        float tx[st::BallState::kTrail] { };
        float ty[st::BallState::kTrail] { };
        int tc = 0, th = 0;
    };

    struct Frame
    {
        int voiceCount = 0;
        std::array<FrameBall, st::kMaxVoices> balls { };
        std::array<st::Ripple, st::kMaxRipples> ripples { };
        int   rippleCount = 0;
        float nowSec = 0.0f;
        std::array<float, st::kNumStrings> flash { };
        std::array<int, st::kNumStrings> flashVoice { };
        std::array<int, st::kNumStrings> pitch { };
        int chordStep = 0;
    };

    void grab();
    void paintStrings (juce::Graphics&, juce::Point<float> c, float radius);
    void paintBalls (juce::Graphics&, juce::Point<float> c, float radius);
    void paintChordPlate (juce::Graphics&);

    StringTheoryProcessor& proc;
    Frame frame;
    float cx = 0.0f, cy = 0.0f, radius = 100.0f;

    // chord change animation
    juce::String chordNow, chordPrev;
    float  chordFade = 1.0f;      // 0 -> 1 after a chord change
    int    lastChordStep = -1;
    int    lastProgression = -1;

    // idle watchdog: counts frames in which the stage has nothing left to draw
    int   frozenTicks = 0;
};

//==============================================================================
//  One voice: its own patch, octave, MIDI channel, rate, humanize, level,
//  chord follow, mute / solo / delete. Three bands: colour, performance,
//  plumbing.
//==============================================================================
class VoiceCard : public juce::Component
{
public:
    VoiceCard (StringTheoryProcessor&, int voiceIndex);
    void refresh();

    std::function<void (int)> onDelete;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    StringTheoryProcessor& proc;
    int index = 0;
    //  voice 1 is the harmony voice: it has no ball, it rolls the chord, and it
    //  cannot be deleted. Its card trades PROB and CHORD for a live chord read-out.
    bool harmony = false;
    juce::Rectangle<int> chordCell;
    juce::String chordShown;

    bool hover = false;
    float hoverAmt = 0.0f;

    ChoiceBox timbreBox { "TIMBRE" };          // this voice's own patch
    ChoiceBox octaveBox { "OCT" };
    ChoiceBox channelBox { "MIDI CH" };
    ParamStrip rateStrip { "RATE", 0, (double) (st::kNumRates - 1), 0 };
    //  R -- random rate. Armed, the voice re-rolls its own division every
    //  couple of beats, so the strum lurches slow -> fast -> slow on its own.
    MiniButton randButton { "R", true };
    //  PROB, not RAND: the control is a probability (chance a crossed string
    //  actually sounds), and PROB is the abbreviation a player already reads
    //  in a sequencer. It also matches RATE / VOL at four characters.
    ParamStrip probStrip { "PROB", 0, 100, 0, "%" };
    ParamStrip volStrip  { "VOL", 0, 100, 0, "%" };
    MiniButton chordButton { "CHORD", true };
    MiniButton muteButton { "M" , true };
    MiniButton soloButton { "S", true };
    MiniButton delButton { "" };
};

//==============================================================================
class StringTheoryEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit StringTheoryEditor (StringTheoryProcessor&);
    ~StringTheoryEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void refreshAll();
    void rebuildPager();
    void setPage (int p);
    void buildMidiMenu (juce::StringArray& items, juce::StringArray& ids, int& selected) const;
    int  contentBottom() const;

    StringTheoryProcessor& proc;

    StageView stage;

    DragValue bpmBox { "BPM", 40, 180, 1, 0 };
    MiniButton playButton { "PLAY" };
    MiniButton syncButton { "SYNC", true };
    ChoiceBox progressionBox { "PROGRESSION" };
    ChoiceBox paceBox { "PACE" };           // harmonic rhythm: bars per chord
    MiniButton retuneButton { "RETUNE" };
    ChoiceBox retuneBarsBox  { "EVERY" };
    uint32_t  lastMapShown = 0xffffffffu;   // repaint the stage when the strings change hands
    ParamStrip tensionStrip { "TENSION", 0, 100, 0, "%" };

    ParamStrip reverbStrip { "REVERB", 0, 90, 0, "%" };
    ParamStrip delayStrip  { "DELAY", 0, 70, 0, "%" };
    ParamStrip fbStrip     { "FEEDBACK", 0, 80, 0, "%" };
    ParamStrip outStrip    { "OUT", 0, 100, 0, "%" };
    ParamStrip divStrip    { "DELAY DIV", 0, 4, 0 };
    MiniButton internalButton { "SYNTH", true };

    ChoiceBox midiOutBox;
    MiniButton addButton { "ADD VOICE" };
    MiniButton pageUpButton { "" };
    MiniButton pageDownButton { "" };

    std::array<std::unique_ptr<VoiceCard>, st::kMaxVoices> cards;
    int page = 0;
    int tick = 0;
    int lastVoiceCount = -1;

    // page transition
    float pageFade = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StringTheoryEditor)
};
