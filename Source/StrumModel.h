#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <random>
#include <vector>

#include "StringSynth.h"

//==============================================================================
//  String Theory -- physical model (no audio / no GUI dependency)
//
//  Stage geometry: a circular rim; six concentric strings sit between the rim
//  and the hub. Every voice owns a ball that lives inside the rim:
//
//    * balls spawn at the hub and travel outward, crossing string 6 -> string 1
//      (one full strum), then bounce off the rim and cross back 1 -> 6
//      (the reverse strum). The alternation is exactly a real strumming hand:
//      down-stroke / up-stroke.
//    * each crossing fires that string's pitch, optionally re-voiced by the
//      current chord (chord-follow) or left at open tuning (pedal / drone).
//    * every string is a 1-D wave loop (96 sectors). A crossing drops a smooth
//      raised-cosine lobe into the loop, the wave equation splits it into two
//      soft ripples that run around the ring and meet again -- water, not a
//      sawtooth. A fraction of the energy bleeds into the neighbouring strings.
//    * balls travel at exactly the speed implied by their RATE division and the
//      tempo, and collide elastically with each other.
//
//  TENSION scales the whole string: a tight string carries a faster, shallower
//  ripple and decays quickly; a slack string swings wide and rings long.
//
//  All numbers are in normalized stage units (rim radius = 1.0).
//==============================================================================

namespace st
{
    constexpr int    kNumStrings = 6;
    constexpr int    kMaxVoices  = 12;
    constexpr int    kSectors    = 72;
    constexpr int    kVoicePage  = 6;          // cards per page in the editor
    constexpr int    kMaxRipples = 96;
    constexpr int    kRipplesPerString = 4;    // keeps the surface legible, not a blur
    constexpr float  kRippleLife = 2.10f;      // seconds a pluck stays visible

    //==========================================================================
    //  TENSION. The knob is not a symmetric affair: an instrument is already
    //  under real tension, so 100 % must read as almost rigid, 50 % as barely
    //  slack, and only near 0 % does the string start behaving like water.
    //  Returns 0 (rigid) .. 1 (fully slack).
    //==========================================================================
    inline float slackOf (float tension) noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, tension);
        return 0.02f + 0.98f * std::pow (1.0f - t, 2.0f);
    }

    // one pluck = one deflected span at `angle` on string `stringIdx`; it swings
    // in and out around the rest position and settles, like a real plucked string
    struct Ripple    {
        int   stringIdx = 0;
        float angle     = 0.0f;   // radians, -pi .. pi
        float birth     = 0.0f;   // world seconds
        float strength  = 0.0f;
        int   voice     = 0;
    };

    constexpr double kTwoPi      = 6.283185307179586;
    constexpr double kBoundaryR  = 0.985;
    constexpr double kBallR      = 0.0205;

    // distance a ball covers during one stroke (hub -> rim)
    constexpr double kStrokeSpan = 0.935;

    // innermost (string 6, lowest) -> outermost (string 1, highest).
    // six strings, evenly spaced between the hub and the rim.
    inline constexpr double kStringR[kNumStrings] = { 0.260, 0.384, 0.508, 0.632, 0.756, 0.880 };

    // open tuning E2 A2 D3 G3 B3 E4
    inline constexpr int kOpenTuning[kNumStrings] = { 40, 45, 50, 55, 59, 64 };

    //==========================================================================
    //  Sweep rate. A ball sweeps the six strings once per stroke; the rate is
    //  written the way a player reads it -- 1/1, 1/2, 1/4, 1/8, 1/16 -- and is
    //  locked to the tempo, then trimmed by one ladder step so the eye can
    //  follow a single string being struck instead of a blur.
    //==========================================================================
    constexpr int kNumRates = 5;
    constexpr int kDefaultRate = 3;             // 1/8

    // Half speed == one rung down the RATE ladder (every step is a 2:1 ratio).
    // Motion only: the string amplitude and note velocity are normalised by
    // speedNorm() below, so the feel of a pluck is untouched.
    constexpr double kMotionTrim = 0.50;

    inline const char* rateLabel (int i) noexcept
    {
        static const char* const l[kNumRates] = { "1/1", "1/2", "1/4", "1/8", "1/16" };
        return l[(size_t) juce::jlimit (0, kNumRates - 1, i)];
    }

    // length of one stroke in beats
    inline double rateBeats (int i) noexcept
    {
        static const double b[kNumRates] = { 4.0, 2.0, 1.0, 0.5, 0.25 };
        return b[(size_t) juce::jlimit (0, kNumRates - 1, i)];
    }

    // stage radii per second -- already trimmed
    inline double rateSpeed (int i, double bpm) noexcept
    {
        const double beats = rateBeats (i);
        const double secs  = beats * 60.0 / juce::jmax (20.0, bpm);
        const double raw   = kMotionTrim * kStrokeSpan / juce::jmax (0.02, secs);

        // The eye gives up on a fast ball long before the ear gives up on a
        // fast strum. A second pass was needed: the first compression still
        // left 1/16 crossing the plate about four times a second, which read
        // as a blur rather than as "a busy stretch of strumming". Above a slow,
        // readable pace the speed is now compressed hard -- 1/16 stays the
        // busiest division but moves at about half the speed a straight
        // 1/seconds curve would give it (0.98 instead of 1.87 radii/s at
        // 60 BPM), which is a quarter of the old ratio to 1/1.
        constexpr double knee = 0.60;
        const double v = raw <= knee ? raw : knee + (raw - knee) * 0.30;
        return juce::jlimit (0.10, 3.20, v);
    }

    // musical liveliness, 0..1, independent of the trim: 1/16 reads as 1 at any
    // tempo, 1/1 as a slow crawl. Drives pluck strength and velocity.
    inline double speedNorm (double speed) noexcept
    {
        return juce::jlimit (0.0, 1.0, speed / (kMotionTrim * 3.2));
    }

    // kept for state restore of older patches
    inline int rateFromSpeed (double v) noexcept
    {
        int best = kDefaultRate;
        double bd = 1.0e9;
        for (int i = 0; i < kNumRates; ++i)
        {
            const double d = std::abs (kMotionTrim * kStrokeSpan / rateBeats (i) * 1.2 - v);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }

    //==========================================================================
    //  Harmony
    //==========================================================================
    enum class Quality { maj, min, maj7, min7, dom7, sus2, sus4, add9, min9, maj9, quart };

    inline const char* pitchClassName (int pc) noexcept
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B" };
        return names[((pc % 12) + 12) % 12];
    }

    inline const char* qualitySuffix (Quality q) noexcept
    {
        switch (q)
        {
            case Quality::maj:   return "";
            case Quality::min:   return "m";
            case Quality::maj7:  return "maj7";
            case Quality::min7:  return "m7";
            case Quality::dom7:  return "7";
            case Quality::sus2:  return "sus2";
            case Quality::sus4:  return "sus4";
            case Quality::add9:  return "add9";
            case Quality::min9:  return "m9";
            case Quality::maj9:  return "maj9";
            case Quality::quart: return "5";
        }
        return "";
    }

    inline void chordIntervals (Quality q, int* out, int& n) noexcept
    {
        switch (q)
        {
            case Quality::maj:   { const int v[] = { 0, 4, 7 };           n = 3; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::min:   { const int v[] = { 0, 3, 7 };           n = 3; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::maj7:  { const int v[] = { 0, 4, 7, 11 };       n = 4; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::min7:  { const int v[] = { 0, 3, 7, 10 };       n = 4; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::dom7:  { const int v[] = { 0, 4, 7, 10 };       n = 4; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::sus2:  { const int v[] = { 0, 2, 7 };           n = 3; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::sus4:  { const int v[] = { 0, 5, 7 };           n = 3; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::add9:  { const int v[] = { 0, 4, 7, 14 };       n = 4; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::min9:  { const int v[] = { 0, 3, 7, 10, 14 };   n = 5; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::maj9:  { const int v[] = { 0, 4, 7, 11, 14 };   n = 5; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
            case Quality::quart: { const int v[] = { 0, 5, 10 };          n = 3; for (int i = 0; i < n; ++i) out[i] = v[i]; return; }
        }
        n = 0;
    }

    struct ChordStep { int rootPc; Quality quality; };

    struct Progression
    {
        const char* family;                 // menu section: OPEN / MINOR / MODAL ...
        const char* label;                  // shown in the CHORD PROGRESSION menu
        std::array<ChordStep, 4> steps;     // one chord per bar
    };

    inline const std::vector<Progression>& progressions()
    {
        static const std::vector<Progression> p =
        {
            // ---- OPEN : sus / add9 / quartal, no thirds where possible -------
            { "OPEN",   "C - Isus2-IV-Isus4-IV", { { { 0, Quality::sus2 },  { 5, Quality::maj },  { 0, Quality::sus4 },  { 5, Quality::maj } } } },
            { "OPEN",   "D - Isus2-Vsus4-IV",          { { { 2, Quality::sus2 },  { 9, Quality::sus4 }, { 2, Quality::sus2 },  { 7, Quality::maj } } } },
            { "OPEN",   "G - Iadd9-IV-vi-I",     { { { 7, Quality::add9 },  { 0, Quality::maj },  { 4, Quality::min },   { 7, Quality::add9 } } } },
            { "OPEN",   "A - quartal drone",                        { { { 9, Quality::quart }, { 2, Quality::quart }, { 4, Quality::quart }, { 9, Quality::quart } } } },
            { "OPEN",   "E - Isus4-Isus2-bVII-IV", { { { 4, Quality::sus4 }, { 4, Quality::sus2 }, { 2, Quality::maj },   { 9, Quality::maj } } } },

            // ---- MINOR -------------------------------------------------------
            { "MINOR",  "A - i-VI-III-VII",        { { { 9, Quality::min },  { 5, Quality::maj },  { 0, Quality::maj },  { 7, Quality::maj } } } },
            { "MINOR",  "E - i-VII-VI-V",          { { { 4, Quality::min },  { 2, Quality::maj },  { 0, Quality::maj },  { 11, Quality::maj } } } },
            { "MINOR",  "D - i-VI-iv-V",           { { { 2, Quality::min },  { 10, Quality::maj }, { 7, Quality::min },  { 9, Quality::maj } } } },
            { "MINOR",  "C - i-VI-III-V",          { { { 0, Quality::min },  { 8, Quality::maj },  { 3, Quality::maj },  { 7, Quality::maj } } } },
            { "MINOR",  "F# - i-iv-VII-III",       { { { 6, Quality::min },  { 11, Quality::min }, { 4, Quality::maj },  { 9, Quality::maj } } } },

            // ---- MODAL -------------------------------------------------------
            { "MODAL",  "D dorian - i-IV-i-VII",   { { { 2, Quality::min },  { 7, Quality::maj },  { 2, Quality::min },  { 0, Quality::maj } } } },
            { "MODAL",  "G mixo - I-VII-IV-I",      { { { 7, Quality::maj },  { 5, Quality::maj },  { 0, Quality::maj },  { 7, Quality::maj } } } },
            { "MODAL",  "E phryg - i-II-iv-i",     { { { 4, Quality::min },  { 5, Quality::maj },  { 9, Quality::min },  { 4, Quality::min } } } },
            { "MODAL",  "C lydian - I-II-V-I",     { { { 0, Quality::maj },  { 2, Quality::maj },  { 7, Quality::maj },  { 0, Quality::maj } } } },
            { "MODAL",  "A aeolian - i-iv-v-i",    { { { 9, Quality::min },  { 2, Quality::min },  { 4, Quality::min },  { 9, Quality::min } } } },

            // ---- CINEMA : wide, slow, one big move per bar --------------------
            { "CINEMA", "C - i-VI-III-VII",        { { { 0, Quality::min },  { 8, Quality::maj },  { 3, Quality::maj },  { 10, Quality::maj } } } },
            { "CINEMA", "D - i-VI-III-VII",        { { { 2, Quality::min },  { 10, Quality::maj }, { 5, Quality::maj },  { 0, Quality::maj } } } },
            { "CINEMA", "E - i-iv-VI-V",           { { { 4, Quality::min },  { 9, Quality::min },  { 0, Quality::maj },  { 11, Quality::maj } } } },
            { "CINEMA", "Bb - I-V-vi-IV",          { { { 10, Quality::maj }, { 5, Quality::maj },  { 0, Quality::min },  { 3, Quality::maj } } } },

            // ---- DRIFT : static, suspended, almost no functional motion -------
            { "DRIFT",  "C - Imaj9-IVmaj9-vi9",          { { { 0, Quality::maj9 }, { 5, Quality::maj9 }, { 9, Quality::min9 }, { 0, Quality::maj9 } } } },
            { "DRIFT",  "A - i9-VImaj7-iv9",             { { { 9, Quality::min9 }, { 5, Quality::maj7 }, { 2, Quality::min9 }, { 9, Quality::min9 } } } },
            { "DRIFT",  "D - Isus2-IIsus4-Isus2",        { { { 2, Quality::sus2 }, { 4, Quality::sus4 }, { 2, Quality::sus2 }, { 7, Quality::maj } } } },
            { "DRIFT",  "F - Imaj7-ii9-IVmaj9",          { { { 5, Quality::maj7 }, { 7, Quality::min9 }, { 10, Quality::maj9 }, { 5, Quality::maj7 } } } },
            { "DRIFT",  "G - Iadd9-vi7-IVadd9",          { { { 7, Quality::add9 }, { 4, Quality::min7 }, { 0, Quality::add9 },  { 2, Quality::maj } } } },

            // ---- POP ---------------------------------------------------------
            { "POP",    "C - I-V-vi-IV",           { { { 0, Quality::maj },  { 7, Quality::maj },  { 9, Quality::min },  { 5, Quality::maj } } } },
            { "POP",    "G - I-V-IV-I",            { { { 7, Quality::maj },  { 2, Quality::maj },  { 0, Quality::maj },  { 7, Quality::maj } } } },
            { "POP",    "D - I-IV-vi-V",           { { { 2, Quality::maj },  { 7, Quality::maj },  { 11, Quality::min }, { 9, Quality::maj } } } },
        };
        return p;
    }

    inline int progressionCount() { return (int) progressions().size(); }
    inline const char* progressionFamily (int i)
    {
        const auto& p = progressions();
        return p[(size_t) juce::jlimit (0, (int) p.size() - 1, i)].family;
    }

    inline juce::String chordName (const ChordStep& s)
    {
        return juce::String (pitchClassName (s.rootPc)) + qualitySuffix (s.quality);
    }

    // Ascending six-string voicing of a chord, one chord tone per string.
    inline void makeVoicing (const ChordStep& step, int octave, int* out) noexcept
    {
        int iv[5] = { 0, 0, 0, 0, 0 };
        int n = 0;
        chordIntervals (step.quality, iv, n);
        if (n <= 0) { n = 3; iv[0] = 0; iv[1] = 4; iv[2] = 7; }

        int pcs[5] = { 0, 0, 0, 0, 0 };
        for (int i = 0; i < n; ++i) pcs[i] = ((step.rootPc + iv[i]) % 12 + 12) % 12;

        auto isTone = [&] (int midi)
        {
            for (int i = 0; i < n; ++i)
                if ((((midi % 12) + 12) % 12) == pcs[i]) return true;
            return false;
        };

        //  out[0] must be the ROOT. It used to be "whatever chord tone happens
        //  to sit first at or above E2", which for a C chord is the third and
        //  for a Csus2 the fifth -- so the bass voice, which is built from the
        //  bottom of this array, never played the root at all and the harmony
        //  had no floor under it.
        int cur = step.rootPc;              // 0..11
        while (cur < 40) cur += 12;         // lowest root the bass string can hold
        out[0] = cur;
        for (int s = 1; s < kNumStrings; ++s)
        {
            int nxt = out[s - 1] + 1;
            while (! isTone (nxt) && nxt < 108) ++nxt;
            out[s] = nxt;
        }

        for (int s = 0; s < kNumStrings; ++s)
            out[s] = juce::jlimit (0, 127, out[s] + 12 * octave);
    }

    //==========================================================================
    //  String assignment -- which chord tone sits on which string.
    //
    //  Six strings, and a hand never plays them in tuning order: it jumps, comes
    //  back, holds a tone twice. Handing out the chord tones in ascending order
    //  (the obvious thing, and what the instrument used to do) turns every sweep
    //  into a scale run instead.
    //
    //  So each string is given a rung of the chord-tone ladder -- the six
    //  ascending voicing notes -- and the six rungs are drawn at random, repeats
    //  allowed, then scored for melodic shape: thirds and fourths are free, wide
    //  leaps cost, seconds cost a little, three steps in the same direction cost
    //  a lot. The best draw of a few thousand wins, and if the caller passes the
    //  map it is replacing, that exact answer is ruled out, so RETUNE always
    //  changes something.
    //==========================================================================
    inline uint32_t packStringMap (const int* slots) noexcept
    {
        uint32_t v = 0;
        for (int i = 0; i < kNumStrings; ++i)
            v |= (uint32_t) (juce::jlimit (0, 7, slots[i])) << (3 * i);
        return v;
    }

    inline int stringSlot (uint32_t packed, int stringIdx) noexcept
    {
        return (int) ((packed >> (3 * juce::jlimit (0, kNumStrings - 1, stringIdx))) & 7u);
    }

    // how singable the six pitches are when the strings are played in order
    inline float melodicCost (const int* notes, uint32_t packed) noexcept
    {
        int slot[kNumStrings];
        bool used[kNumStrings] = { false, false, false, false, false, false };
        int distinct = 0;

        for (int i = 0; i < kNumStrings; ++i)
        {
            slot[i] = juce::jlimit (0, kNumStrings - 1, stringSlot (packed, i));
            if (! used[slot[i]]) { used[slot[i]] = true; ++distinct; }
        }

        float c = 0.0f;
        int dir = 0, run = 1;

        for (int i = 1; i < kNumStrings; ++i)
        {
            const int a = notes[slot[i - 1]];
            const int b = notes[slot[i]];
            const int d = std::abs (b - a);

            if      (d == 0)  c += 0.80f;      // a held tone is fine, twice
            else if (d >= 10) c += 3.0f + 0.40f * (float) (d - 10);
            else if (d >= 8)  c += 1.40f;
            else if (d >= 6)  c += 0.45f;
            else if (d >= 3)  c += 0.0f;       // thirds and fourths: the good ones
            else              c += 0.55f;      // seconds

            if (d > 0)
            {
                const int sgn = (b > a) ? 1 : -1;
                //  A sweep IS a scale run -- that is what a hand does across
                //  six strings, and punishing it hard (it used to cost 2.2) was
                //  what forced every map into a low-high-low fold with only
                //  three or four distinct pitches. Only real monotony is
                //  discouraged now, and RETUNE keeps it from repeating.
                if (sgn == dir) { if (++run >= 5) c += 0.25f * (float) (run - 4); }
                else            { dir = sgn; run = 1; }
            }
        }

        // a line that only touches three of the six voicing notes has stopped
        // spelling the chord
        if (distinct < 4) c += (float) (4 - distinct) * 1.2f;
        return c;
    }

    inline uint32_t makeStringMap (const int* notes, uint32_t seed, uint32_t avoid = 0u) noexcept
    {
        constexpr int kDraws = 1536;

        uint32_t rng = seed | 1u;
        auto step = [&rng]() noexcept
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return rng;
        };
        //  A draw is a *permutation* of the six rungs: every string gets its
        //  own pitch, so a sweep never sounds the same note three times. It
        //  starts from the tuning order -- low on the inside, high on the
        //  outside, the way a real instrument is strung, which is what makes a
        //  sweep read as a line -- and then swaps one or two neighbouring rungs
        //  to break the monotony without losing that line.
        auto draw = [&step]() noexcept
        {
            int slot[kNumStrings] = { 0, 1, 2, 3, 4, 5 };
            const int swaps = 1 + (int) (step() % 2u);              // 1 or 2 swaps
            for (int k = 0; k < swaps; ++k)
            {
                const int a = (int) (step() % (uint32_t) (kNumStrings - 1));
                const int b = juce::jmin (kNumStrings - 1, a + 1 + (int) (step() % 2u));
                std::swap (slot[a], slot[b]);
            }
            // two swaps can cancel out and hand back the plain ladder, which
            // would defeat RETUNE; force one kink in that case
            bool ladder = true;
            for (int i = 0; i < kNumStrings; ++i)
                if (slot[i] != i) ladder = false;
            if (ladder) std::swap (slot[2], slot[3]);
            return packStringMap (slot);
        };

        float best = 1.0e9f;
        uint32_t bestPacked = 0u;

        for (int i = 0; i < kDraws; ++i)
        {
            const uint32_t p = draw();
            if (p == avoid) continue;

            const float c = melodicCost (notes, p);
            if (c < best) { best = c; bestPacked = p; }
        }

        // reservoir draw among the near-best, so the same chord does not always
        // come back in the same order. The coin has its own generator: sharing
        // one with the draws would desynchronise the two passes.
        const float cut = best + 1.0f;
        uint32_t coin = (seed ^ 0xa5a5a5a5u) | 1u;
        auto flip = [&coin]() noexcept
        {
            coin ^= coin << 13; coin ^= coin >> 17; coin ^= coin << 5;
            return coin;
        };

        rng = seed | 1u;
        uint32_t chosen = 0u;
        int seen = 0;

        for (int i = 0; i < kDraws; ++i)
        {
            const uint32_t p = draw();
            if (p == avoid) continue;
            if (melodicCost (notes, p) > cut) continue;

            ++seen;
            if ((flip() >> 8) % (uint32_t) seen == 0u) chosen = p;
        }

        return seen > 0 ? chosen : bestPacked;
    }

    //==========================================================================
    //  Balls / voices
    //==========================================================================
    struct BallState
    {
        bool   active  = false;
        //  A harmony voice is a voice without a ball: it never travels, never
        //  touches a string and never collides. It rolls the whole current
        //  chord on the tempo clock instead (see StrumWorld::rollHarmony).
        //  Voice 1 is always a harmony voice and cannot be removed.
        bool   harmony = false;
        double x = 0.0, y = 0.0;
        double vx = 0.0, vy = 0.0;

        double speed   = 2.10;     // stage radii / s, derived from rate + tempo
        int    rate    = kDefaultRate;
        // R (random rate): when armed, the voice rests on a slow division and
        // now and then jumps straight to a fast one for a short burst --
        // calm, then a flurry of quick strumming, then calm again.
        bool   randomRate  = false;
        double rateJumpIn  = 0.0;  // seconds until the next rate step
        double rand    = 1.00;     // chance that a string crossing actually sounds
        float  gain    = 1.00f;    // voice level, 0 = silent
        int    octave  = 0;        // -2..+2
        int    timbre  = 0;        // index into st::timbres() -- this voice's patch
        int    channel = 1;        // 1..16
        bool   follow  = true;     // follow the chord progression
        bool   mute    = false;
        bool   solo    = false;
        float  pulse   = 0.0f;     // visual kick when the ball touches a string

        std::array<double, kNumStrings> lastHit { { -9, -9, -9, -9, -9, -9 } };

        static constexpr int kTrail = 18;
        std::array<float, kTrail> trailX { };
        std::array<float, kTrail> trailY { };
        int    trailCount = 0;
        int    trailHead  = 0;
        double trailAcc   = 0.0;
    };

    struct StrumEvent
    {
        int   sampleOffset = 0;
        int   voice        = 0;
        int   stringIdx    = 0;
        int   midi         = 60;
        int   channel      = 1;
        int   timbre       = 0;     // the voice's own patch
        int   slot         = 0;     // harmony: chord tone 0..5, so the six tones
                                    // of one chord never merge into one pad
        float velocity     = 0.7f;
    };

    //==========================================================================
    //  World
    //==========================================================================
    class StrumWorld
    {
    public:
        juce::SpinLock lock;

        // --- voice data (guarded by lock) ------------------------------------
        std::array<BallState, kMaxVoices> balls;
        int  voiceCount = 4;
        bool anySolo    = false;

        // --- ripples (guarded by lock) ---------------------------------------
        //  A pluck is stored as an EVENT, not as a damped field: the drawn
        //  displacement is a Gaussian-windowed travelling wave
        //      dr = strength * exp(-d*age) * cos(k*dth - w*age) * exp(-dth^2/2s^2)
        //  evaluated per sector at paint time. Cheap, always smooth, and it
        //  cannot smear into a lumpy ring the way a finite-difference field does.
        std::array<Ripple, kMaxRipples> ripples { };
        int  rippleCount = 0;
        std::array<float, kNumStrings> flash { };
        std::array<int,   kNumStrings> flashVoice { };

        // --- transport (atomics) ---------------------------------------------
        std::atomic<bool>   playing       { false };
        std::atomic<bool>   synced        { false };
        std::atomic<bool>   internalSynth { true };
        std::atomic<double> bpm           { 60.0 };
        std::atomic<double> hostBpm       { 0.0 };
        std::atomic<int>    progression   { 0 };
        std::atomic<int>    chordStep     { 0 };
        std::atomic<int>    hostBeat      { -1 };   // >=0 when the host supplies a bar clock
        std::atomic<double> hostPpq       { -1.0 }; // the same clock in beats, fractional
                                                    // -- the quantiser needs the
                                                    // position *inside* the beat
        std::atomic<float>  tension       { 0.50f }; // 0 = slack, 0.5 = normal, 1 = tight

        // ---- quantisation ---------------------------------------------------
        //  The balls cross strings whenever geometry says so, which is musical
        //  luck: three voices firing at unrelated instants reads as scatter,
        //  and a crossing near a chord change can land on the far side of it.
        //  So every onset is pulled towards a beat grid -- but only *forwards*:
        //  an onset that has already passed its nearest grid point is left
        //  alone, because a note cannot be moved back in time. Half the notes
        //  therefore keep their human placement while the other half snaps,
        //  which is what keeps this from sounding like a drum machine.
        //    grid: 0 = off, 1 = 1/4, 2 = 1/8, 3 = 1/16 (in beats)
        //    amount: how far towards the grid an onset travels, 0..1
        //
        //  Amount is 1.0, which sounds stronger than it is: an onset that has
        //  already passed its nearest grid point cannot travel at all, so only
        //  about half of them ever move. Those that do land exactly on the
        //  line; the rest keep their human placement. At 0.85 the moved half
        //  stopped just short of the line and the grid was barely audible.
        std::atomic<int>   quantGrid   { 3 };        // 1/16 by default
        std::atomic<float> quantAmount { 1.00f };

        static double quantiseGridBeats (int grid) noexcept
        {
            switch (juce::jlimit (0, 3, grid))
            {
                case 1:  return 1.00;    // quarter note
                case 2:  return 0.50;    // eighth
                case 3:  return 0.25;    // sixteenth
                default: return 0.0;     // off
            }
        }

        // which chord tone each string carries (packed permutation, 3 bits per
        // string), and how often it is re-drawn: 0 = hold, else bars
        std::atomic<uint32_t> stringMap   { 0u };
        std::atomic<int>      retuneBars  { 0 };

        // harmonic rhythm: how many bars a chord holds before the progression
        // advances -- 1 / 2 / 4 / 8, or 0 = HOLD (harmony frozen)
        std::atomic<int>      paceBars    { 1 };

        // restore an assignment from a saved patch
        void adoptStringMap (uint32_t packed)
        {
            //  Saves written before the map became a true permutation can hold
            //  repeats -- three strings on one pitch, which is exactly the
            //  stutter this instrument should not make. Those maps are re-dealt
            //  rather than trusted.
            int used[kNumStrings] = { 0, 0, 0, 0, 0, 0 };
            bool perm = true;
            for (int i = 0; i < kNumStrings; ++i)
            {
                const int s = stringSlot (packed, i);
                if (s < 0 || s >= kNumStrings) { perm = false; break; }
                ++used[s];
            }
            for (int k = 0; k < kNumStrings; ++k)
                if (used[k] != 1) perm = false;

            if (! perm) { retuneNow(); mapInited = true; return; }

            stringMap.store (packed, std::memory_order_relaxed);
            mapInited = true;
        }

        // re-assign the six chord tones across the strings right now
        void retuneNow()
        {
            retuneRng = retuneRng * 1664525u + 1013904223u;
            const uint32_t avoid = stringMap.load (std::memory_order_relaxed);
            stringMap.store (makeStringMap (voicing, retuneRng, avoid),
                             std::memory_order_relaxed);
        }

        StrumWorld() { reset(); }

        void prepare (double sr) { sampleRate = sr > 0 ? sr : 48000.0; }

        //----------------------------------------------------------------------
        void reset()
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            for (int i = 0; i < kMaxVoices; ++i)
            {
                auto& b = balls[(size_t) i];
                b = BallState();
                b.active  = i < voiceCount;
                applyPreset (b, i, bpm.load());
                if (! b.harmony) spawn (b, i);
            }
            for (auto& r : ripples) r = Ripple();
            rippleCount = 0;
            flash.fill (0.0f);
            flashVoice.fill (0);
            anySolo = false;
            now = 0.0;
            beatPos = 0.0;
            pendingCount = 0;
            chordStep.store (0);
            chordAge = 0.0;
            lastProgression = -1;
            refreshVoicing();

            if (! mapInited)
            {
                mapInited = true;
                retuneNow();            // never open on a plain scale run
            }
            barIndex = 0;
            lastRetuneBar = -1;
            lastHostBar = -1;
            freeBar = 0;
            rollAcc = 0.0;
            rollAngle = -1.5708f;
            wasRunning = false;
        }

        //----------------------------------------------------------------------
        //  Factory voicing.
        //
        //  Voice 1 is the HARMONY voice: no ball, no travelling, it simply rolls
        //  the current chord on the tempo clock -- the harmonic floor of the
        //  instrument. Voices 2..4 are the three balls, one per register, with
        //  rates that interleave instead of stacking:
        //
        //      V1  HARMONY      ENSEMBLE    1/1   --    the chord, low and wide
        //      V2  DEEP DRONE   1/1   ball        bass bed (patch is -12)
        //      V3  PAD          1/4   ball        body
        //      V4  SHIMMER      1/8   ball        air (patch is +12)
        //
        //  The harmony rolls once a bar, so it lands exactly when the chord
        //  changes; the balls then strum across it at three different speeds.
        //----------------------------------------------------------------------
        struct VoicePreset { const char* patch; int rate; int octave; };

        static bool isHarmonyVoice (int i) noexcept { return i == 0; }

        static const VoicePreset& defaultVoice (int i) noexcept
        {
            static const VoicePreset p[kMaxVoices] =
            {
                { "ENSEMBLE",      0,  0 },   // V1 harmony (no ball)
                { "DEEP DRONE",    0,  0 },   // V2 bass
                { "PAD",           2,  0 },   // V3 body
                { "SHIMMER",       2,  0 },   // V4 air (1/4, not 1/8: the top
                                              // was the densest layer of all)
                { "CELLO",         1,  1 },
                { "OCTAVE DRONE",  1,  0 },
                { "GLASS",         3,  0 },
                { "CHOIR",         2,  0 },
                { "BASS DRONE",    0,  0 },
                { "BOWED DRONE",   1,  0 },
                { "AIR",           3,  0 },
                { "HARMONIC DRONE",2,  0 },
            };
            return p[(size_t) (((i % kMaxVoices) + kMaxVoices) % kMaxVoices)];
        }

        static void applyPreset (BallState& b, int i, double bpmNow)
        {
            const auto& v = defaultVoice (i);
            b.harmony = isHarmonyVoice (i);
            b.timbre = timbreIndexOf (v.patch);
            b.rate   = juce::jlimit (0, kNumRates - 1, v.rate);
            b.octave = juce::jlimit (-2, 2, v.octave);
            b.speed  = rateSpeed (b.rate, bpmNow);
            b.channel = 1 + (i % 16);
            b.follow  = true;               // a harmony voice always follows
            // Density: a voice that sounds on every crossing, four at once, is
            // a wall. Leave gaps by default -- the resting string is what makes
            // the next one read as an event.
            b.rand    = 0.72;
        }

        void spawn (BallState& b, int index)
        {
            // everyone leaves the hub at a slightly different angle -> the
            // first pass is never a pile-up.
            const double a = kTwoPi * (double) index / (double) juce::jmax (1, voiceCount)
                             + rand01() * 0.35;
            b.x = std::cos (a) * 0.050;
            b.y = std::sin (a) * 0.050;
            b.vx = std::cos (a) * b.speed;
            b.vy = std::sin (a) * b.speed;
            b.trailCount = 0;
            b.trailHead  = 0;
            b.trailAcc   = 0.0;
            b.pulse = 0.0f;
            for (int k = 0; k < kNumStrings; ++k) b.lastHit[(size_t) k] = -9.0;
        }

        void setVoiceCount (int n)
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            // voice 1 is the harmony voice and is always there, so the smallest
            // possible instrument is one chord voice plus one ball
            n = juce::jlimit (2, kMaxVoices, n);
            if (n > voiceCount)
            {
                for (int i = voiceCount; i < n; ++i)
                {
                    auto& b = balls[(size_t) i];
                    b = BallState();
                    b.active  = true;
                    applyPreset (b, i, bpm.load());
                    if (! b.harmony) spawn (b, i);
                }
            }
            for (int i = 0; i < kMaxVoices; ++i)
                balls[(size_t) i].active = i < n;
            balls[0].harmony = true;
            voiceCount = n;
        }

        //----------------------------------------------------------------------
        //  Physics + note generation. Called from the audio thread.
        //----------------------------------------------------------------------
        void update (double dt, int numSamples, StrumEvent* events, int& eventCount)
        {
            eventCount = 0;
            if (dt <= 0.0 || numSamples <= 0) return;

            const double sr = sampleRate;
            const int steps = juce::jlimit (1, 64, (int) std::ceil (dt / 0.002));
            const double sub = dt / (double) steps;

            // solo bookkeeping
            anySolo = false;
            for (int i = 0; i < voiceCount; ++i)
                if (balls[(size_t) i].active && balls[(size_t) i].solo && ! balls[(size_t) i].mute)
                    anySolo = true;

            // ---- tempo -------------------------------------------------------
            const int hostBar = hostBeat.load (std::memory_order_relaxed);
            const double hostVal = hostBpm.load (std::memory_order_relaxed);
            const bool hostClock = synced.load (std::memory_order_relaxed) && hostBar >= 0;
            const double useBpm = (synced.load (std::memory_order_relaxed) && hostVal > 1.0)
                                    ? hostVal : bpm.load (std::memory_order_relaxed);

            // chord clock ------------------------------------------------------
            //  The harmonic rhythm -- paceBars -- decides how many bars a chord
            //  holds: 1 / 2 / 4 / 8, or 0 = HOLD. The bar clock itself always
            //  ticks (barIndex drives the EVERY retune) even when the harmony
            //  is frozen.
            const int pace = juce::jlimit (0, 8, paceBars.load (std::memory_order_relaxed));

            if (hostClock)
            {
                const long long bar = hostBar / 4;              // absolute bar
                if (bar != lastHostBar)
                {
                    lastHostBar = bar;
                    ++barIndex;
                    if (pace > 0)
                    {
                        const int step = (int) ((bar / pace) % 4);
                        if (step != chordStep.load (std::memory_order_relaxed))
                        {
                            chordStep.store (step, std::memory_order_relaxed);
                            refreshVoicing();
                        }
                    }
                }
            }
            else if (playing.load (std::memory_order_relaxed))
            {
                chordAge += dt * useBpm / 60.0;   // beats
                if (chordAge >= 4.0)
                {
                    chordAge -= 4.0;
                    ++freeBar;
                    ++barIndex;
                    if (pace > 0)
                    {
                        const int step = (int) ((freeBar / pace) % 4);
                        if (step != chordStep.load (std::memory_order_relaxed))
                        {
                            chordStep.store (step, std::memory_order_relaxed);
                            refreshVoicing();
                        }
                    }
                }
            }

            // a new progression must take effect on the very next crossing,
            // otherwise "follow the chord" feels broken for up to a bar
            if (progression.load (std::memory_order_relaxed) != lastProgression)
                refreshVoicing();

            // ---- timed re-assignment ---------------------------------------
            //  A four-bar cycle only becomes audible if the strings actually
            //  change hands when the cycle turns over.
            const int every = retuneBars.load (std::memory_order_relaxed);
            if (every > 0 && barIndex > 0 && barIndex != lastRetuneBar
                && (barIndex % every) == 0)
            {
                lastRetuneBar = barIndex;
                retuneNow();
            }

            const bool running = playing.load (std::memory_order_relaxed) || hostClock;

            // the moment the instrument starts, the chord voice must already be
            // sounding -- waiting a whole bar for the first roll reads as broken
            if (running && ! wasRunning) rollAcc = 1.0e3;
            wasRunning = running;

            // where this block starts on the beat clock: the host's own
            // position when there is one, otherwise the free-running counter,
            // which is advanced in step with the chord clock so the grid lines
            // and the chord changes are the same lines
            // A host that reports a bar clock but no ppq (hostPpq < 0) would
            // otherwise pin the quantiser to beat 0 forever, and every note it
            // held back would wait for a line that never arrives. Fall back to
            // the free-running counter in that case.
            const double ppqNow  = hostPpq.load (std::memory_order_relaxed);
            const bool   havePpq = hostClock && ppqNow >= 0.0;
            const double beat0   = havePpq ? ppqNow : beatPos;

            for (int s = 0; s < steps; ++s)
            {
                const int baseSample = juce::jlimit (0, numSamples - 1,
                                                     (int) std::round ((double) s * sub * sr));
                const double beatNow = beat0 + (double) s * sub * useBpm / 60.0;
                const double beatEnd = beatNow + sub * useBpm / 60.0;
                if (running)
                {
                    // the chord voice rolls the whole chord on the tempo clock;
                    // the balls then cross the strings on top of it
                    rollHarmony (sub, baseSample, numSamples, events, eventCount, useBpm, beatNow);
                    stepBalls (sub, baseSample, numSamples, events, eventCount, useBpm, beatNow);
                    flushPending (beatEnd, baseSample, numSamples, events, eventCount);
                }

                now += sub;
                // keep the fallback clock running even when the host claims a
                // bar clock -- it is only unused when a real ppq is there
                if (running && ! havePpq) beatPos += sub * useBpm / 60.0;
            }

            // a stopped instrument must not deliver notes it still owes
            if (! running) pendingCount = 0;

            pruneRipples();
            decayFlash (dt);
        }

        double currentNow() const { return now; }

        // the chord symbol the harmony voice is sounding right now -- the card
        // prints it, so the voice never has to be guessed at
        juce::String currentChordText() const
        {
            const auto& progs = progressions();
            const int p = juce::jlimit (0, (int) progs.size() - 1,
                                        progression.load (std::memory_order_relaxed));
            const int step = juce::jlimit (0, 3, chordStep.load (std::memory_order_relaxed));
            return chordName (progs[(size_t) p].steps[(size_t) step]);
        }

        int voicing[kNumStrings] { 40, 45, 50, 55, 59, 64 };

        // the note actually heard on a string for a given voice.
        //  role 0 = low voice, 1 = middle voice, 2 = air voice. Each takes its
        //  own slice of the six-note voicing, so four voices do not all sound
        //  the same six pitches in different octaves -- the low voice owns the
        //  bottom of the chord (root and fifth), the middle voice the middle,
        //  the air voice the top where the colour tones live.
        int noteFor (int stringIdx, int octave, bool follow, int role = 1) const noexcept
        {
            const int i = juce::jlimit (0, kNumStrings - 1, stringIdx);

            // the strings are not handed the chord tones in order: `stringMap`
            // says which tone landed on this string (see makeStringMap)
            int slot = juce::jlimit (0, kNumStrings - 1,
                                     stringSlot (stringMap.load (std::memory_order_relaxed), i));

            if (follow)
            {
                //  Fold the six rungs into this voice's slice *evenly*.
                //  Clamping did not do that: with the low voice limited to
                //  0..2, four of the six strings were clamped onto rung 2 and
                //  sounded the same note. Spreading 0..5 across the slice keeps
                //  the order and puts at most two strings on one pitch.
                const int lo = role == 0 ? 0 : (role == 2 ? 3 : 1);
                const int hi = role == 0 ? 2 : (role == 2 ? 5 : 4);
                slot = lo + (int) std::lround ((double) (slot * (hi - lo))
                                               / (double) (kNumStrings - 1));
                slot = juce::jlimit (lo, hi, slot);
            }

            // The top of the air voice carries a colour tone instead of a plain
            // chord tone: an 11th, a 9th or a 13th -- whichever the chord does
            // not already contain. That suspended shimmer is what keeps an
            // ambient voicing from sounding like a bare triad.
            if (follow && role == 2 && slot >= 4)
            {
                const auto& progs = progressions();
                const int pIdx = juce::jlimit (0, (int) progs.size() - 1,
                                               progression.load (std::memory_order_relaxed));
                const int stepIdx = juce::jlimit (0, 3, chordStep.load (std::memory_order_relaxed));
                const int rootPc = progs[(size_t) pIdx].steps[(size_t) stepIdx].rootPc;

                static const int colour[3] = { 5, 2, 9 };   // 11th, 9th, 13th
                int wantPc = -1;
                for (int c = 0; c < 3 && wantPc < 0; ++c)
                {
                    const int pc = ((rootPc + colour[c]) % 12 + 12) % 12;
                    bool usable = true;
                    for (int k = 0; k < kNumStrings; ++k)
                    {
                        const int chordPc = (((voicing[k] % 12) + 12) % 12);
                        if (chordPc == pc) usable = false;            // already in the chord
                        //  A colour tone a semitone or a major seventh from a
                        //  chord tone is an avoid note -- the 11th over a major
                        //  third (F against E) is the classic one, and it turns
                        //  a shimmer into mud.
                        const int iv = std::abs (pc - chordPc);
                        if (iv == 1 || iv == 11) usable = false;
                    }
                    if (usable) wantPc = pc;
                }

                if (wantPc >= 0)
                {
                    int v = voicing[slot];
                    while ((((v % 12) + 12) % 12) != wantPc && v < 116) ++v;
                    return juce::jlimit (0, 127, v + 12 * octave);
                }
            }

            const int base = follow ? voicing[slot] : kOpenTuning[slot];
            return juce::jlimit (0, 127, base + 12 * octave);
        }

    private:
        bool     mapInited = false;
        long long barIndex = 0;
        long long lastRetuneBar = -1;
        long long lastHostBar = -1;    // last bar seen on the host clock
        long long freeBar = 0;         // bar counter when running without a host
        uint32_t retuneRng = 0x9e3779b9u;

        // absolute position in beats, counted the same way the chord clock is
        // (see chordAge), so the grid lines fall on the bar and on the chord
        // changes instead of drifting against them
        double beatPos = 0.0;

        // onsets waiting for their quantised moment. The gain is remembered so
        // the velocity can be rescaled at release time: a note held for up to
        // a division must not arrive at the old level after the fader moved.
        struct PendingEvent { StrumEvent ev; double dueBeat = 0.0; float gainAtEmit = 1.0f; };
        static constexpr int kMaxPending = 96;
        std::array<PendingEvent, kMaxPending> pending;
        int pendingCount = 0;

        //  Re-roll the RATE for an R-armed voice.
        //
        //  Two states, not five. The voice rests on a slow division; when the
        //  hand speeds up it jumps *straight* to a fast one -- 1/1 -> 1/16,
        //  no ladder in between -- holds there just long enough to be heard,
        //  then falls back to a slow division and rests again. So the shape is
        //  calm -> a burst of quick strumming -> calm.
        //
        //  Randomness: which slow division it rests on, whether the burst is
        //  1/16 or 1/8, and +/-20 % on every hold.
        //
        //  The callers re-derive the speed (or the roll interval) from `rate`
        //  on every step, so this is the only place that needs to touch it.
        void rollRandomRate (BallState& b, double bpmNow)
        {
            // beats a division is held for, indexed by division. A 1/16 burst
            // is already very busy -- it only has to be glimpsed -- while 1/8
            // is sparse enough to need a longer stay before it reads as a
            // change of pace.
            static const double slowHold[kNumRates] = { 7.0, 5.5, 4.0, 0.0, 0.0 };
            static const double fastHold[kNumRates] = { 0.0, 0.0, 0.0, 2.4, 0.9 };

            const bool fast = b.rate >= 3;

            if (fast)
            {
                // settle back down: always onto a slow division, never a
                // step-by-step descent that would only sound like a stutter
                const double r = rand01();
                b.rate = r < 0.55 ? 0 : (r < 0.82 ? 1 : 2);
                b.rateJumpIn = slowHold[(size_t) b.rate];
            }
            else
            {
                // speed up: straight to the top, now and then to 1/8
                b.rate = rand01() < 0.80 ? 4 : 3;
                b.rateJumpIn = fastHold[(size_t) b.rate];
            }

            b.rateJumpIn *= (0.80 + rand01() * 0.45) * 60.0 / juce::jmax (20.0, bpmNow);
        }

        //----------------------------------------------------------------------
        //  How far, in beats, this onset should be pushed to reach a grid line.
        //
        //  Forward only: a note cannot be un-played, so the only direction
        //  available is later. "Nearest line, but never backwards" was the
        //  first attempt, and it fails exactly where it matters: a ball on a
        //  steady RATE crosses a string at a fixed phase, so every onset lands
        //  on the same side of its line and the grid never moves anything.
        //
        //  Snapping to the *next* line instead puts every onset on a line. The
        //  price is up to one division of delay (a 16th = 250 ms at 60 BPM),
        //  which a pad absorbs and a pluck merely leans on.
        double quantiseDelta (double beatAt) const
        {
            const int g = juce::jlimit (0, 3, quantGrid.load (std::memory_order_relaxed));
            const float amt = juce::jlimit (0.0f, 1.0f,
                                            quantAmount.load (std::memory_order_relaxed));
            const double grid = quantiseGridBeats (g);
            if (grid <= 0.0 || amt <= 0.0f) return 0.0;

            const double q = std::ceil (beatAt / grid - 1.0e-9) * grid;
            return (q - beatAt) * (double) amt;
        }

        //  Hand an onset to the output: now if its moment has come, otherwise
        //  hold it until the clock reaches dueBeat.
        void emitAt (const StrumEvent& evIn, double dueBeat, double beatNow,
                     int sampleOffset, int numSamples, StrumEvent* events, int& eventCount)
        {
            if (dueBeat > beatNow + 1.0e-9 && pendingCount < kMaxPending)
            {
                auto& p = pending[(size_t) pendingCount];
                p.ev = evIn;
                p.dueBeat = dueBeat;
                const int v = juce::jlimit (0, kMaxVoices - 1, evIn.voice);
                p.gainAtEmit = juce::jlimit (0.001f, 1.0f, (float) balls[(size_t) v].gain);
                ++pendingCount;
                return;
            }
            if (eventCount >= 128) return;
            StrumEvent ev = evIn;
            ev.sampleOffset = juce::jlimit (0, juce::jmax (0, numSamples - 1), sampleOffset);
            events[eventCount++] = ev;
        }

        void flushPending (double beatNow, int baseSample, int numSamples,
                           StrumEvent* events, int& eventCount)
        {
            int kept = 0;
            for (int i = 0; i < pendingCount; ++i)
            {
                auto& p = pending[(size_t) i];
                if (p.dueBeat <= beatNow + 1.0e-9 || eventCount >= 128)
                {
                    const int v = juce::jlimit (0, kMaxVoices - 1, p.ev.voice);
                    const auto& vb = balls[(size_t) v];
                    const float gNow = juce::jlimit (0.0f, 1.0f, (float) vb.gain);

                    // a voice that was muted, silenced or deleted while this
                    // note was waiting must not speak with its old level
                    if (gNow > 0.001f && ! (vb.mute && ! anySolo)
                        && eventCount < 128)
                    {
                        p.ev.velocity = juce::jlimit (0.0f, 1.0f,
                                                      p.ev.velocity * (gNow / p.gainAtEmit));
                        p.ev.sampleOffset = juce::jlimit (0, juce::jmax (0, numSamples - 1), baseSample);
                        events[eventCount++] = p.ev;
                    }
                }
                else
                {
                    // a loop or a jump can strand a note behind the playhead --
                    // never hold it longer than half a beat
                    if (beatNow - p.dueBeat > 0.5) continue;
                    if (kept != i) pending[(size_t) kept] = p;
                    ++kept;
                }
            }
            pendingCount = kept;
        }

        //----------------------------------------------------------------------
        void refreshVoicing()
        {
            const int pIdx = juce::jlimit (0, progressionCount() - 1,
                                           progression.load (std::memory_order_relaxed));
            lastProgression = pIdx;
            const auto& prog = progressions()[(size_t) pIdx];
            const int step = juce::jlimit (0, 3, chordStep.load (std::memory_order_relaxed));
            makeVoicing (prog.steps[(size_t) step], 0, voicing);
        }

        static int sectorOf (double x, double y)
        {
            double a = std::atan2 (y, x);                 // -pi .. pi
            int s = (int) ((a + juce::MathConstants<double>::pi) / kTwoPi * (double) kSectors);
            s %= kSectors;
            if (s < 0) s += kSectors;
            return s;
        }

        // A pluck is recorded as an event. The editor turns it into a standing
        // deflection at paint time: the string is pushed aside, then vibrates
        // around its line and settles -- no travelling ripple.
        void pluckWave (int idx, double theta, float amp, int voice)
        {
            pushRipple (idx, (float) theta, amp, voice);

            // sympathetic: the neighbouring strings answer, softer and a touch later
            for (int d = -1; d <= 1; d += 2)
            {
                const int j = idx + d;
                if (j < 0 || j >= kNumStrings) continue;
                pushRipple (j, (float) theta + 0.09f * (float) d, amp * 0.22f, voice);
            }

            flash[(size_t) idx]      = 1.0f;
            flashVoice[(size_t) idx] = voice;
        }

        void pushRipple (int idx, float angle, float amp, int voice)
        {
            Ripple nr;
            nr.stringIdx = juce::jlimit (0, kNumStrings - 1, idx);
            nr.angle     = angle;
            nr.birth     = (float) now;
            nr.strength  = juce::jlimit (0.0f, 2.5f, amp);
            nr.voice     = voice;

            // a strummed string can pick up dozens of plucks per second; keeping
            // only the newest few keeps the drawn surface readable instead of
            // muddying into a many-petalled flower
            int same = 0, oldest = -1;
            float oldestBirth = 1.0e9f;
            for (int i = 0; i < rippleCount; ++i)
            {
                if (ripples[(size_t) i].stringIdx != nr.stringIdx) continue;
                ++same;
                if (ripples[(size_t) i].birth < oldestBirth)
                {
                    oldestBirth = ripples[(size_t) i].birth;
                    oldest = i;
                }
            }

            if (same >= kRipplesPerString && oldest >= 0)
            {
                ripples[(size_t) oldest] = nr;
                return;
            }

            if (rippleCount >= kMaxRipples) return;
            ripples[(size_t) rippleCount++] = nr;
        }

        void pruneRipples()
        {
            int w = 0;
            for (int i = 0; i < rippleCount; ++i)
                if ((float) now - ripples[(size_t) i].birth <= kRippleLife)
                    ripples[(size_t) w++] = ripples[(size_t) i];
            rippleCount = w;
        }

        // striplight decay for the momentary colour tint, independent of physics rate
        void decayFlash (double dt)
        {
            const float k = (float) std::exp (-dt / 0.20);
            for (auto& f : flash) f *= k;
        }

        static void normaliseSpeed (BallState& b, double target)
        {
            const double m = std::sqrt (b.vx * b.vx + b.vy * b.vy);
            if (m < 1.0e-9)
            {
                b.vx = target;
                b.vy = 0.0;
                return;
            }
            const double k = target / m;
            b.vx *= k;
            b.vy *= k;
        }

        //----------------------------------------------------------------------
        //  The harmony voice: no ball, no collision, no string crossing. Every
        //  RATE division it rolls the whole current chord from the bottom string
        //  up -- one strum, six tones, a few tens of milliseconds apart. Because
        //  the voicing is read at fire time, a roll that lands on a chord change
        //  already carries the new harmony.
        //
        //  Each tone is given its own `slot`, so the pad engine keeps six
        //  separate layers instead of gliding one note up the chord.
        //----------------------------------------------------------------------
        void rollHarmony (double dt, int baseSample, int numSamples,
                          StrumEvent* events, int& eventCount, double bpmNow,
                          double beatNow)
        {
            auto& b = balls[0];
            if (! b.active || ! b.harmony) return;

            // R armed: the roll re-rolls its own division every few beats
            if (b.randomRate)
            {
                b.rateJumpIn -= dt;
                if (b.rateJumpIn <= 0.0) rollRandomRate (b, bpmNow);
            }

            const double baseInterval = juce::jmax (0.06, rateBeats (b.rate) * 60.0
                                                                / juce::jmax (20.0, bpmNow));
            const double interval = baseInterval * rollJitter;
            const double wait = juce::jlimit (0.0, interval, interval - rollAcc);
            if (dt < wait) { rollAcc += dt; return; }

            // the roll landed `wait` seconds into this step
            rollAcc = dt - wait;
            const int at = baseSample + (int) std::round (wait * sampleRate);

            // Re-draw the drift for the next roll: a hand never repeats its
            // spacing exactly, and locking to the bar makes every chord change
            // arrive with the same mechanical sweep on top of it.
            rollJitter = 0.91 + rand01() * 0.18;

            const float ten = juce::jlimit (0.0f, 1.0f, tension.load (std::memory_order_relaxed));
            const bool audible = b.gain > 0.001f && ! b.mute && (! anySolo || b.solo);
            const int octave = juce::jlimit (-2, 2, b.octave);

            // a hand sweeping the strings: the farther the interval, the lazier
            // the roll, but never longer than a 16th at any sane tempo
            const double span = juce::jlimit (0.040, 0.130, interval * 0.34);
            const int spanSamples = juce::jmax (1, (int) (span * sampleRate));

            // Quantise the hand, not the individual strings: the six tones of
            // one roll keep their sweep, the whole gesture moves to the grid.
            const double beatAt = beatNow + wait * bpmNow / 60.0;
            const double shift  = quantiseDelta (beatAt);
            const double beatsPerSec = bpmNow / 60.0;
            // VOL is a plain gain: 50 % must be -6 dB, the same on the chord
            // voice as on a ball voice, or the two cannot be balanced by ear
            const float amp = 0.54f * (1.30f - 0.62f * ten)
                              * juce::jlimit (0.0f, 1.0f, b.gain);

            // Alternate the stroke: down, up, down... A real hand does, and one
            // direction repeated every time is what made each roll sound like
            // the same block being dropped in again.
            const bool up = rollUp;
            rollUp = ! rollUp;

            for (int k = 0; k < kNumStrings; ++k)
            {
                const int s = up ? k : (kNumStrings - 1 - k);
                const int pos = up ? k : (kNumStrings - 1 - k);   // position in the stroke

                // the visual: one strum line at a slowly rotating angle, so the
                // chord voice leaves a sweep across all six strings. The drawn
                // string is always swept, even when the tone is left out.
                pluckWave (s, (double) rollAngle + 0.030 * (double) s, amp, 0);

                if (! audible || eventCount >= 128) continue;

                // A hand rarely catches all six strings: skip an inner one now
                // and then, so the chord arrives as a shape rather than a slab.
                if (pos > 0 && pos < kNumStrings - 1 && rand01() < 0.22) continue;

                StrumEvent e;
                const int atSample = juce::jlimit (0, juce::jmax (0, numSamples - 1),
                                                   at + pos * spanSamples / kNumStrings);
                const double beatTone = beatNow + ((double) (atSample - baseSample)
                                                   / juce::jmax (1.0, sampleRate)) * beatsPerSec;
                e.sampleOffset = atSample;
                e.voice    = 0;
                e.stringIdx = s;
                e.midi     = juce::jlimit (0, 127, voicing[s] + 12 * octave);
                e.channel  = b.channel;
                e.timbre   = b.timbre;
                e.slot     = s;
                // Velocity follows the stroke: the middle of a sweep is where
                // the hand presses hardest, the ends are featherweight.
                const float bow = std::sin (juce::MathConstants<float>::pi
                                            * (float) (pos + 1) / (float) (kNumStrings + 1));
                e.velocity = juce::jlimit (0.0f, 1.0f,
                                           (0.46f + 0.38f * bow + 0.08f * (float) rand01())
                                           * juce::jlimit (0.0f, 1.0f, b.gain));
                emitAt (e, beatTone + shift, beatNow, atSample, numSamples, events, eventCount);
            }

            b.pulse = 1.0f;

            // the hand drifts to a new angle on every roll, so six rolls walk
            // the sweep right round the plate
            rollAngle += 1.04f;
            while (rollAngle > 3.14159265f) rollAngle -= 6.28318531f;
        }

        void stepBalls (double dt, int baseSample, int numSamples,
                        StrumEvent* events, int& eventCount, double bpmNow,
                        double beatNow)
        {
            const int n = voiceCount;
            const float ten = juce::jlimit (0.0f, 1.0f, tension.load (std::memory_order_relaxed));

            for (int i = 0; i < n; ++i)
            {
                auto& b = balls[(size_t) i];
                if (! b.active || b.harmony) continue;

                // R armed: the ball re-rolls its own division every few beats;
                // the speed line below turns the new rate into motion at once
                if (b.randomRate)
                {
                    b.rateJumpIn -= dt;
                    if (b.rateJumpIn <= 0.0) rollRandomRate (b, bpmNow);
                }

                // RATE is a division of the tempo, so the ball must actually
                // travel at that speed -- this is what was missing before.
                b.speed = rateSpeed (b.rate, bpmNow);
                normaliseSpeed (b, b.speed);

                const double prevR = std::sqrt (b.x * b.x + b.y * b.y);
                b.x += b.vx * dt;
                b.y += b.vy * dt;
                double r = std::sqrt (b.x * b.x + b.y * b.y);
                if (r < 1.0e-9) { r = 1.0e-9; b.x = 1.0e-9; }

                // ---- the band beyond the last string --------------------------
                //  The outermost string sits at 0.88, the rim at 0.96: a ball
                //  that grazes the wall can orbit inside that band for bars
                //  without crossing anything -- a voice that simply goes quiet.
                //  Only this band pulls: a ball in it is drawn slowly inwards as
                //  if the plate curved up at its edge, so its orbit bends back
                //  onto the strings. The rest of the plate stays flat, the
                //  bounce off the rim stays a plain mirror, and the speed is
                //  renormalised every step, so RATE still owns the pace.
                r = std::sqrt (b.x * b.x + b.y * b.y);
                if (r > kStringR[kNumStrings - 1])
                {
                    constexpr double pull = 2.4;               // radii / s^2
                    b.vx -= pull * (b.x / r) * dt;
                    b.vy -= pull * (b.y / r) * dt;
                    const double sp = std::sqrt (b.vx * b.vx + b.vy * b.vy);
                    if (sp > 1.0e-9) { b.vx *= b.speed / sp; b.vy *= b.speed / sp; }
                    r = std::sqrt (b.x * b.x + b.y * b.y);
                }

                // ---- string crossings ----------------------------------------
                const bool outward = r > prevR;
                for (int k = 0; k < kNumStrings; ++k)
                {
                    const int idx = outward ? k : (kNumStrings - 1 - k);
                    const double radius = kStringR[idx];
                    const bool crossed = outward ? (prevR < radius && r >= radius)
                                                 : (prevR > radius && r <= radius);
                    if (! crossed) continue;

                    const double span = r - prevR;
                    const double frac = std::abs (span) > 1.0e-12 ? (radius - prevR) / span : 0.0;
                    const double px = b.x - b.vx * dt * (1.0 - frac);
                    const double py = b.y - b.vy * dt * (1.0 - frac);
                    const double theta = std::atan2 (py, px);

                    // the hand is never perfectly even -- a few ms of drift
                    const double jitterMs = (rand01() * 2.0 - 1.0) * 3.0;
                    int off = baseSample + (int) std::round ((frac * dt + jitterMs * 0.001) * sampleRate);
                    off = juce::jlimit (0, juce::jmax (0, numSamples - 1), off);

                    // slack string swings wider, tight string barely moves.
                    // VOL is a plain gain here too -- see the chord voice.
                    const float ampScale = 1.30f - 0.62f * ten;
                    const float amp = (0.34f + 0.30f * (float) speedNorm (b.speed))
                                      * ampScale * juce::jlimit (0.0f, 1.0f, b.gain);
                    pluckWave (idx, theta, amp, i);
                    b.pulse = 1.0f;

                    // STRUM RAND: the chance that this crossing actually speaks
                    const bool passRoll = rand01() <= juce::jlimit (0.0, 1.0, b.rand);
                    const bool audible = passRoll && b.gain > 0.001f && ! b.mute && (! anySolo || b.solo)
                                         && now - b.lastHit[(size_t) idx] > 0.045;
                    if (audible && eventCount < 128)
                    {
                        b.lastHit[(size_t) idx] = now;

                        float vel = 0.50f + 0.28f * (float) speedNorm (b.speed)
                                    + 0.18f * (float) rand01() * (float) b.rand;
                        vel = juce::jlimit (0.22f, 1.0f, vel * (1.06f - 0.18f * ten));
                        vel *= juce::jlimit (0.0f, 1.0f, b.gain);   // plain gain

                        StrumEvent e;
                        e.sampleOffset = off;
                        e.voice        = i;
                        e.stringIdx    = idx;
                        // the patch's register decides which slice of the chord
                        // this voice owns -- a LOW patch takes the bottom of the
                        // voicing, a HIGH patch the colour tones on top. Read
                        // the family, not octaveShift: a low patch can sit in
                        // its own octave and still be the bass part.
                        const char* fam = timbreAt (b.timbre).family;
                        const int role = fam[0] == 'L' ? 0 : (fam[0] == 'H' ? 2 : 1);
                        e.midi         = noteFor (idx, b.octave, b.follow, role);
                        e.channel      = b.channel;
                        e.timbre       = b.timbre;
                        e.velocity     = vel;

                        // pulled towards the grid, but never moved backwards
                        const double beatAt = beatNow + ((double) (off - baseSample)
                                                         / juce::jmax (1.0, sampleRate)) * (bpmNow / 60.0);
                        emitAt (e, beatAt + quantiseDelta (beatAt), beatNow,
                                off, numSamples, events, eventCount);
                    }
                }

                // ---- rim reflection -------------------------------------------
                const double limit = kBoundaryR - kBallR;
                r = std::sqrt (b.x * b.x + b.y * b.y);
                if (r > limit)
                {
                    const double nx = b.x / r, ny = b.y / r;
                    b.x = nx * limit;
                    b.y = ny * limit;
                    const double vn = b.vx * nx + b.vy * ny;
                    b.vx -= 2.0 * vn * nx;
                    b.vy -= 2.0 * vn * ny;

                    // a small angular deviation keeps the paths from collapsing
                    // into one identical diameter -> slowly rotating patterns
                    if (b.rand > 0.0 && std::abs (vn) > 0.05)
                    {
                        const double a = (rand01() * 2.0 - 1.0) * 0.11 * b.rand;
                        const double ca = std::cos (a), sa = std::sin (a);
                        const double tx = b.vx * ca - b.vy * sa;
                        const double ty = b.vx * sa + b.vy * ca;
                        b.vx = tx; b.vy = ty;
                    }
                }

                // ---- trail -----------------------------------------------------
                b.pulse *= 0.90f;
                b.trailAcc += dt;
                if (b.trailAcc >= 1.0 / 90.0)
                {
                    b.trailAcc = 0.0;
                    b.trailX[(size_t) b.trailHead] = (float) (b.x / kBoundaryR);
                    b.trailY[(size_t) b.trailHead] = (float) (b.y / kBoundaryR);
                    b.trailHead = (b.trailHead + 1) % BallState::kTrail;
                    b.trailCount = juce::jmin (b.trailCount + 1, BallState::kTrail);
                }
            }

            // ---- ball / ball collisions (equal mass, elastic) -----------------
            for (int i = 0; i < n; ++i)
            {
                for (int j = i + 1; j < n; ++j)
                {
                    auto& a = balls[(size_t) i];
                    auto& c = balls[(size_t) j];
                    if (! a.active || ! c.active || a.harmony || c.harmony) continue;

                    double dx = c.x - a.x, dy = c.y - a.y;
                    double d = std::sqrt (dx * dx + dy * dy);
                    const double minD = 2.0 * kBallR;
                    if (d >= minD || d < 1.0e-9) continue;

                    const double nx = dx / d, ny = dy / d;
                    const double push = (minD - d) * 0.5;
                    a.x -= nx * push; a.y -= ny * push;
                    c.x += nx * push; c.y += ny * push;

                    const double van = a.vx * nx + a.vy * ny;
                    const double vcn = c.vx * nx + c.vy * ny;
                    if (vcn - van < 0.0)
                    {
                        a.vx += (vcn - van) * nx; a.vy += (vcn - van) * ny;
                        c.vx += (van - vcn) * nx; c.vy += (van - vcn) * ny;
                    }
                }
            }
        }

        double rand01()
        {
            // xorshift32 -- deterministic, no allocation, audio-thread friendly
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            return (double) (rngState & 0x00ffffff) / 16777216.0;
        }

        double sampleRate = 48000.0;
        double now        = 0.0;
        double chordAge   = 0.0;

        // harmony clock
        double rollAcc   = 0.0;      // seconds since the last roll
        float  rollAngle = -1.5708f; // where on the ring the sweep lands
        bool   rollUp    = true;     // the hand alternates up and down strokes
        double rollJitter = 1.0;     // small drift so rolls never lock to the bar
        bool   wasRunning = false;   // primes the first roll on PLAY
        int    lastProgression = -1;
        juce::uint32 rngState = 0x9e3779b9u;
    };
}
