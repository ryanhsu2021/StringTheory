#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

//==============================================================================
//  StringSynth -- the internal sound engine for String Theory
//
//  Two engines sit behind one interface, because the instrument is not only a
//  guitar:
//
//    * PLUCK  -- Karplus-Strong delay loop. A short filtered noise burst is the
//                plectrum; the loop filter + loop gain set colour and decay.
//    * PAD    -- three detuned partials through a slow attack / release
//                envelope, for the atmospheric side of the instrument.
//
//  Timbres are classified by REGISTER -- the way a patch library is actually
//  browsed when you are looking for a colour, not a model name. Every voice
//  picks its own patch, so the table is the only shared state:
//
//    LOW     SUB PAD  DEEP DRONE  BASS DRONE  OCTAVE DRONE  LOW CHOIR  CELLO
//    MID     STEEL  ELECTRIC  ENSEMBLE  PAD  CHOIR  HARMONIC DRONE  BOWED DRONE
//    HIGH    GLASS  BELL  AIR  SHIMMER  GLASS DRONE  SKY DRONE
//
//  Every patch also carries `sub` / `oct` companion layers one octave down and
//  up, which is what makes a LOW patch read as low and a HIGH patch shimmer.
//  TENSION (0..1) thins the excitation, shortens the decay and slows the pad
//  attack on a tight string; a slack string rings long and detunes wide.
//==============================================================================

namespace st
{
    struct TimbreParams
    {
        const char* family;       // menu section: LOW / MID / HIGH
        const char* name;
        int   kind;               // 0 = plucked string, 1 = pad

        // pluck
        float exciteBright;       // lowpass on the noise burst (0 dark .. 1 bright)
        float loopBright;         // loop filter coefficient (0 dark .. 1 open)
        float decay;              // seconds to -60 dB
        float amp;                // output trim
        float pluckPos;           // excitation comb position 0..0.5

        // pad
        float attack;             // seconds
        float release;            // seconds
        float detune;             // cents of spread between the partials
        float h2, h3;             // upper partial weights
        float tremRate, tremDepth;

        // register / layers
        int   octaveShift;        // -12 low, 0 mid, +12 high
        float subLevel;           // companion one octave down
        float octLevel;           // companion one octave up
    };

    //  Menu order == table order: three contiguous register sections
    //  (LOW / MID / HIGH), each running long bed -> medium swell -> short
    //  accent. That is how an ambient patch library is browsed: pick the
    //  register, then pick the length. The S / M / L tag after each name is
    //  derived from the numbers themselves (timbreDurTag), never hand-set.
    inline const std::array<TimbreParams, 33>& timbres()
    {
        static const std::array<TimbreParams, 33> t =
        {{
            // family  name          kind excite loop  decay  amp  pos   atk   rel   det   h2    h3    trRt trDp  oct  sub   octL
            //  ---- LOW ----------------------------------------------------------
            //  HOLD: a drone is now one continuous note per voice, not a stack
            //  of retriggered copies -- the ball's next string glides the pad
            //  instead of adding another one. So the hold can be a proper
            //  sustained length again (7-14 s, the whole patch about 10-21 s,
            //  i.e. two to five bars at 60 BPM) without the low end turning to
            //  mud: whatever the strum density, one voice only ever has one
            //  pad per layer.
            { "LOW",  "SUB PAD",     1,  0.00f, 0.00f, 10.0f, 0.66f, 0.00f, 2.00f,  3.0f,  7.0f, 0.55f, 0.18f, 0.09f, 0.10f,   0, 0.45f, 0.00f },
            { "LOW",  "DEEP DRONE",  1,  0.00f, 0.00f, 12.0f, 0.60f, 0.00f, 2.40f,  3.6f,  5.0f, 0.28f, 0.12f, 0.05f, 0.14f,   0, 0.45f, 0.00f },
            { "LOW",  "BASS DRONE",  1,  0.00f, 0.00f, 14.0f, 0.62f, 0.00f, 2.60f,  4.0f,  4.0f, 0.20f, 0.09f, 0.04f, 0.12f,   0, 0.50f, 0.00f },
            { "LOW",  "OCTAVE DRONE",1,  0.00f, 0.00f, 12.0f, 0.58f, 0.00f, 2.20f,  3.4f,  6.0f, 0.34f, 0.16f, 0.06f, 0.12f,   0, 0.30f, 0.34f },
            { "LOW",  "LOW CHOIR",   1,  0.00f, 0.00f,  8.0f, 0.62f, 0.00f, 1.20f,  2.6f,  9.0f, 0.72f, 0.30f, 0.21f, 0.09f,   0, 0.40f, 0.00f },
            //  Medium low: swells and longer plucks -- a phrase-length bed
            //  without committing to a full drone.
            { "LOW",  "CELLO",       0,  0.26f, 0.34f,  5.00f,0.86f, 0.26f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.34f, 0.00f },
            { "LOW",  "SUB SWELL",   1,  0.00f, 0.00f,  4.0f, 0.66f, 0.00f, 0.80f,  1.20f, 6.0f, 0.45f, 0.18f, 0.12f, 0.08f,   0, 0.45f, 0.00f },
            { "LOW",  "BASS PLUCK",  0,  0.30f, 0.40f,  3.40f,0.84f, 0.28f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.22f, 0.05f },
            //  Short low: the points on top of the bed -- a struck string that
            //  is gone in one or two beats, so a dense strum reads as texture
            //  instead of a wall of low end.
            { "LOW",  "SUB PLUCK",   0,  0.18f, 0.30f,  1.60f,0.82f, 0.30f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.30f, 0.00f },
            { "LOW",  "WOOD BASS",   0,  0.34f, 0.22f,  0.90f,0.80f, 0.42f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.15f, 0.06f },

            //  ---- MID ---------------------------------------------------------
            //  Long mid: the harmonic heart -- ensemble pads and bowed drones
            //  that carry the chord.
            { "MID",  "ENSEMBLE",    1,  0.00f, 0.00f,  8.0f, 0.58f, 0.00f, 1.10f,  2.6f,  6.0f, 0.52f, 0.26f, 0.16f, 0.08f,   0, 0.22f, 0.06f },
            { "MID",  "PAD",         1,  0.00f, 0.00f,  9.0f, 0.62f, 0.00f, 0.90f,  3.0f,  9.0f, 0.45f, 0.22f, 0.18f, 0.10f,   0, 0.26f, 0.10f },
            { "MID",  "CHOIR",       1,  0.00f, 0.00f,  7.0f, 0.58f, 0.00f, 0.55f,  2.6f,  7.0f, 0.62f, 0.34f, 0.24f, 0.09f,   0, 0.20f, 0.04f },
            { "MID",  "HARMONIC DRONE",1, 0.00f,0.00f, 10.0f, 0.56f, 0.00f, 1.60f,  3.2f,  8.0f, 0.50f, 0.28f, 0.07f, 0.11f,   0, 0.16f, 0.08f },
            { "MID",  "BOWED DRONE", 1,  0.00f, 0.00f, 10.0f, 0.56f, 0.00f, 1.40f,  3.6f, 12.0f, 0.66f, 0.34f, 0.11f, 0.14f,   0, 0.14f, 0.06f },
            //  Medium mid: swells and plucks long enough to carry a phrase,
            //  short enough to breathe between the strums.
            { "MID",  "STEEL",       0,  0.72f, 0.66f,  3.00f,0.85f, 0.18f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.12f, 0.10f },
            { "MID",  "ELECTRIC",    0,  0.50f, 0.46f,  4.20f,0.80f, 0.24f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.18f, 0.00f },
            { "MID",  "AMBER SWELL", 1,  0.00f, 0.00f,  4.5f, 0.60f, 0.00f, 0.70f,  1.40f, 8.0f, 0.50f, 0.24f, 0.14f, 0.09f,   0, 0.18f, 0.10f },
            { "MID",  "HARP",        0,  0.74f, 0.62f,  4.00f,0.78f, 0.16f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.10f, 0.22f },
            //  Short mid: accents. PIZZ is the bowed-string family's own short
            //  articulation -- the thumpy pluck a string section makes when
            //  the bows lift.
            { "MID",  "NYLON",       0,  0.62f, 0.58f,  1.90f,0.84f, 0.22f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.10f, 0.14f },
            { "MID",  "PIZZ",        0,  0.42f, 0.28f,  0.70f,0.82f, 0.38f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.10f, 0.04f },
            { "MID",  "MUTED",       0,  0.40f, 0.16f,  0.55f,0.78f, 0.46f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.06f, 0.05f },
            { "MID",  "KALIMBA",     0,  0.55f, 0.30f,  1.10f,0.80f, 0.34f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,   0, 0.08f, 0.20f },

            //  ---- HIGH --------------------------------------------------------
            //  Long high: air and glass pads that sit above the mix.
            { "HIGH", "AIR",         1,  0.00f, 0.00f,  9.0f, 0.52f, 0.00f, 1.60f,  3.2f, 15.0f, 0.30f, 0.36f, 0.11f, 0.16f,  12, 0.00f, 0.30f },
            { "HIGH", "SHIMMER",     1,  0.00f, 0.00f, 10.0f, 0.50f, 0.00f, 2.00f,  3.4f, 18.0f, 0.24f, 0.30f, 0.14f, 0.18f,  12, 0.00f, 0.42f },
            { "HIGH", "GLASS DRONE", 1,  0.00f, 0.00f, 11.0f, 0.50f, 0.00f, 2.00f,  3.6f, 16.0f, 0.26f, 0.34f, 0.12f, 0.16f,  12, 0.00f, 0.34f },
            { "HIGH", "SKY DRONE",   1,  0.00f, 0.00f, 12.0f, 0.48f, 0.00f, 2.40f,  3.8f, 20.0f, 0.20f, 0.28f, 0.09f, 0.20f,  12, 0.00f, 0.46f },
            //  Medium high: metallic plucks and a slow glow.
            { "HIGH", "GLASS",       0,  0.95f, 0.92f,  4.60f,0.60f, 0.08f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,  12, 0.10f, 0.34f },
            { "HIGH", "CHIME",       0,  0.90f, 0.86f,  3.60f,0.60f, 0.10f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,  12, 0.00f, 0.26f },
            { "HIGH", "GLOW",        1,  0.00f, 0.00f,  4.0f, 0.50f, 0.00f, 0.90f,  1.30f, 14.0f, 0.28f, 0.30f, 0.13f, 0.15f,  12, 0.00f, 0.36f },
            //  Short high: bells and harmonics -- the sparkle accents.
            { "HIGH", "BELL",        0,  0.86f, 0.74f,  2.30f,0.72f, 0.05f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,  12, 0.08f, 0.26f },
            { "HIGH", "HARMONIC",    0,  0.88f, 0.90f,  2.20f,0.62f, 0.12f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,  12, 0.00f, 0.30f },
            { "HIGH", "MUSIC BOX",   0,  0.92f, 0.80f,  1.00f,0.58f, 0.06f, 0.00f, 0.00f,  0.0f, 0.00f, 0.00f, 0.00f, 0.00f,  12, 0.00f, 0.18f },
        }};
        return t;
    }

    inline int timbreCount() { return (int) timbres().size(); }

    inline const TimbreParams& timbreAt (int i)
    {
        return timbres()[(size_t) juce::jlimit (0, timbreCount() - 1, i)];
    }

    inline const char* timbreFamily (int i) { return timbreAt (i).family; }
    inline bool timbreIsPad (int i)         { return timbreAt (i).kind == 1; }

    // How long the patch holds, for the menu annotation. Derived from the
    // numbers themselves -- there is no second list to keep in step:
    //   a pluck is its decay; a pad is attack + hold + release.
    //   S under ~2.5 s, M under ~7 s, L beyond that.
    inline const char* timbreDurTag (int i)
    {
        const auto& t = timbreAt (i);
        const double total = t.kind == 1 ? (double) t.attack + (double) t.decay + (double) t.release
                                         : (double) t.decay * 1.04;
        return total < 2.5 ? "S" : (total < 7.0 ? "M" : "L");
    }

    // index of a patch by name -- -1 when the name is not in the table
    inline int timbreIndexOf (const char* name, int fallback = 0)
    {
        for (size_t i = 0; i < timbres().size(); ++i)
            if (std::strcmp (timbres()[i].name, name) == 0) return (int) i;
        return fallback;
    }

    class StringSynth
    {
    public:
        static constexpr int kMaxPlucks = 72;
        //  a harmony voice holds six chord tones x up to three layers, so the
        //  pool has to be able to hold a whole chord plus every ball
        static constexpr int kMaxPads   = 56;

        void prepare (double sampleRate)
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            const int maxLen = juce::jlimit (64, 8192, (int) (sr / 30.0) + 8);
            for (auto& p : plucks)
            {
                p.line.assign ((size_t) maxLen, 0.0f);
                p.active = false;
            }
            for (auto& p : pads) p.active = false;
            rngState = 0x1234567u;

            // one shared sine wavetable -- cheaper than sinf() per partial
            for (int i = 0; i <= kTabSize; ++i)
                sineTab[(size_t) i] = (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                        * (double) i / (double) kTabSize);
        }

        void reset()
        {
            for (auto& p : plucks) { p.active = false; std::fill (p.line.begin(), p.line.end(), 0.0f); }
            for (auto& p : pads) p.active = false;
        }

        // tension: 0 = slack, 0.5 = normal, 1 = tight
        // owner: which voice struck it (pads only merge within one voice)
        // slot : which chord tone of that voice -- a harmony roll gives every
        //        tone its own slot, so six tones stay six tones
        void trigger (int midi, float vel, int timbreIdx, float pan, float tension = 0.5f,
                      int owner = -1, int slot = 0)
        {
            const auto& tp = timbreAt (timbreIdx);
            const float ten = juce::jlimit (0.0f, 1.0f, tension);

            // TENSION only means something for a vibrating string. A struck /
            // modelled string follows it fully; a pad or a choir is a synth
            // layer, so it tracks the same gesture at roughly half strength.
            // the editor already folds the knob's softness curve into ,
            // so this is a straight tightness value here
            const float react  = (tp.kind == 1) ? 0.45f : 1.0f;
            const float tenEff = 0.5f + (ten - 0.5f) * react;

            const int root = midi + tp.octaveShift;

            if (tp.kind == 1)
            {
                triggerPad (root, vel, tp, pan, tenEff, owner, 0, slot);
                if (tp.subLevel > 0.01f) triggerPad (root - 12, vel * tp.subLevel, tp, pan, tenEff, owner, -1, slot);
                if (tp.octLevel > 0.01f) triggerPad (root + 12, vel * tp.octLevel, tp, pan, tenEff, owner, +1, slot);
            }
            else
            {
                triggerPluck (root, vel, tp, pan, tenEff);
                if (tp.subLevel > 0.01f) triggerPluck (root - 12, vel * tp.subLevel, tp, pan, tenEff);
                if (tp.octLevel > 0.01f) triggerPluck (root + 12, vel * tp.octLevel, tp, pan, tenEff);
            }
        }

        // Fade out everything one voice is still holding. Called when the
        // voice's patch changes: a drone is 10-21 s long, so without this the
        // old timbre keeps ringing behind the new one for several bars and the
        // switch is barely audible.
        void releaseOwned (int owner, float fadeSec = 0.40f)
        {
            const int rel = juce::jmax (1, (int) std::lround ((double) fadeSec * sr));

            for (auto& p : pads)
            {
                if (! p.active || p.owner != owner) continue;

                p.rel  = juce::jmin (p.rel, rel);
                if (p.pos < p.life - p.rel) p.pos = p.life - p.rel;   // into the release now
                p.life = juce::jmin (p.life, p.pos + p.rel);
            }
        }

        // Render `num` samples starting at `start` into the stereo mix.
        void render (float* left, float* right, int start, int num)
        {
            if (num <= 0) return;

            // nothing sounding -> the whole segment is silence. This is the
            // common case when the transport is parked, and it means the audio
            // thread does no work at all rather than scanning 120 slots.
            if (! anyActive()) return;

            for (int s = 0; s < num; ++s)
            {
                float l = 0.0f, r = 0.0f;

                for (auto& p : plucks)
                {
                    if (! p.active) continue;

                    double pos = (double) p.idx - p.delay;
                    while (pos < 0.0) pos += (double) p.len;
                    int i0 = (int) pos;
                    if (i0 >= p.len) i0 %= p.len;
                    const int i1 = (i0 + 1) % p.len;
                    const float frac = (float) (pos - std::floor (pos));
                    const float x = p.line[(size_t) i0] * (1.0f - frac) + p.line[(size_t) i1] * frac;

                    p.lp += p.bright * (x - p.lp);
                    float y = p.lp * p.g;

                    const float dc = y - p.dcIn + 0.9985f * p.dcOut;
                    p.dcIn = y;
                    p.dcOut = dc;
                    y = dc;

                    p.line[(size_t) p.idx] = y;
                    p.idx = (p.idx + 1) % p.len;

                    if (--p.life <= 0) { p.active = false; continue; }

                    p.lvl = p.lvl * 0.9995f + std::abs (y) * 0.0005f;

                    l += y * p.gl;
                    r += y * p.gr;
                }

                for (auto& p : pads)
                {
                    if (! p.active) continue;

                    // legato slide: retune one step per sample until it lands
                    if (p.glideLeft > 0)
                    {
                        for (int h = 0; h <= p.nH; ++h)
                            p.inc[(size_t) h] += p.incStep[(size_t) h];

                        if (--p.glideLeft == 0)
                            for (int h = 0; h <= p.nH; ++h)
                                p.inc[(size_t) h] = p.incTo[(size_t) h];
                    }

                    float sum = 0.0f;
                    // only the partials the patch actually uses -- a table read
                    // without interpolation, since every pad partial is under
                    // 2 kHz and 2048 points per cycle is far more than enough
                    for (int h = 0; h <= p.nH; ++h)
                    {
                        double ph = p.ph[(size_t) h] + p.inc[(size_t) h];
                        if (ph >= (double) kTabSize) ph -= (double) kTabSize;
                        p.ph[(size_t) h] = ph;
                        sum += sineTab[(size_t) ph] * p.ampH[(size_t) h];
                    }

                    // envelope: attack -> hold (slow fade) -> release. The
                    // release starts from exactly the level the hold ended on
                    // (0.60), otherwise every drone swells back up just before
                    // it fades out
                    float env;
                    if (p.pos < p.atk)                env = (float) p.pos / (float) juce::jmax (1, p.atk);
                    else if (p.pos > p.life - p.rel)  env = 0.60f * (float) juce::jmax (0, p.life - p.pos) / (float) juce::jmax (1, p.rel);
                    else                              env = 1.0f - 0.40f * (float) (p.pos - p.atk) / (float) juce::jmax (1, p.life - p.atk - p.rel);

                    // 4 ms one-pole: a re-struck drone tops the envelope back up
                    // from wherever it had sagged to, and this turns that step
                    // into a short rise instead of a click
                    p.envSm += (env - p.envSm) * 0.005f;

                    p.tremPh += p.tremInc;
                    if (p.tremPh >= (double) kTabSize) p.tremPh -= (double) kTabSize;
                    const float trem = 1.0f + p.tremDepth * lookup (p.tremPh);

                    ++p.pos;
                    if (p.pos >= p.life)
                    {
                        // half a millisecond of fade at the very end, so a pad
                        // that runs out is never a hard cut
                        p.envSm *= 0.90f;
                        if (p.envSm < 1.0e-4f) { p.active = false; continue; }
                    }

                    // level and pan follow the new strike without a step
                    p.gainSm += (p.gain - p.gainSm) * 0.004f;
                    p.gl     += (p.glT  - p.gl)     * 0.004f;
                    p.gr     += (p.grT  - p.gr)     * 0.004f;

                    const float y = sum * p.envSm * trem * p.gainSm;
                    p.lvl = p.lvl * 0.9995f + std::abs (y) * 0.0005f;

                    l += y * p.gl;
                    r += y * p.gr;
                }

                left[(size_t) (start + s)]  += l;
                right[(size_t) (start + s)] += r;
            }
        }

        int activeCount() const
        {
            int n = 0;
            for (auto& p : plucks) if (p.active) ++n;
            for (auto& p : pads)   if (p.active) ++n;
            return n;
        }

        bool anyActive() const
        {
            for (auto& p : plucks) if (p.active) return true;
            for (auto& p : pads)   if (p.active) return true;
            return false;
        }

    private:
        static constexpr int kTabSize = 2048;

        float lookup (double phase) const
        {
            const int i0 = (int) phase & (kTabSize - 1);
            const int i1 = (i0 + 1) & (kTabSize - 1);
            const float f = (float) (phase - std::floor (phase));
            return sineTab[(size_t) i0] * (1.0f - f) + sineTab[(size_t) i1] * f;
        }

        float randBipolar()
        {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 17;
            rngState ^= rngState << 5;
            return (float) ((double) (rngState & 0x00ffffffu) / 8388608.0 - 1.0);
        }

        static void panGains (float pan, float& gl, float& gr)
        {
            const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            gl = std::cos (a);
            gr = std::sin (a);
        }

        //----------------------------------------------------------------------
        // a tight string sits a little sharp, a slack one a little flat --
        // +-22 cents across the whole travel of the TENSION control
        static double freqOf (int midi, float ten) noexcept
        {
            const double cents = (double) (ten - 0.5f) * 44.0;
            return 440.0 * std::pow (2.0, ((double) (midi - 69) + cents / 100.0) / 12.0);
        }

        void triggerPluck (int midi, float vel, const TimbreParams& tp, float pan, float ten)
        {
            midi = juce::jlimit (21, 120, midi);

            Pluck* target = nullptr;
            for (auto& p : plucks) if (! p.active) { target = &p; break; }
            if (target == nullptr)
            {
                float best = 1.0e9f;
                for (auto& p : plucks)
                    if (p.lvl < best) { best = p.lvl; target = &p; }
            }
            if (target == nullptr) return;

            auto& p = *target;
            const double freq = freqOf (midi, ten);
            const double delay = juce::jlimit (4.0, (double) p.line.size() - 4.0, sr / juce::jmax (20.0, freq));

            p.len   = (int) std::ceil (delay) + 3;
            p.len   = juce::jmin (p.len, (int) p.line.size());
            p.delay = delay;
            p.idx   = 0;
            p.active = true;
            p.note  = midi;
            p.pan   = juce::jlimit (-1.0f, 1.0f, pan);
            panGains (p.pan, p.gl, p.gr);

            // a tight string is stiffer: brighter loop, shorter ring
            const float decay  = tp.decay  * (0.58f + 0.92f * ten);
            const float bright = juce::jlimit (0.02f, 0.985f, tp.loopBright * (0.45f + 1.05f * ten));

            p.g      = (float) std::pow (0.001, 1.0 / (double) juce::jmax (0.05f, decay) / sr);
            p.bright = bright;
            p.lp     = 0.0f;
            p.lvl    = 0.0f;
            p.dcIn   = 0.0f;
            p.dcOut  = 0.0f;
            p.life   = (int) juce::jlimit (64.0, (double) p.line.size() * 8.0,
                                           (double) decay * sr * 1.15);

            const int L = p.len;
            float mean = 0.0f;
            for (int i = 0; i < L; ++i)
            {
                const float n = randBipolar();
                p.line[(size_t) i] = n;
                mean += n;
            }
            mean /= (float) juce::jmax (1, L);

            const float excite = juce::jlimit (0.02f, 1.0f, tp.exciteBright * (0.62f + 0.72f * ten));

            float lp = 0.0f, energy = 0.0f;
            for (int i = 0; i < L; ++i)
            {
                const float x = p.line[(size_t) i] - mean;
                lp += excite * (x - lp);
                p.line[(size_t) i] = lp;
                energy += lp * lp;
            }
            energy = std::sqrt (energy / (float) juce::jmax (1, L));
            if (energy < 1.0e-9f) energy = 1.0f;

            const float amp = juce::jlimit (0.0f, 1.6f, vel) * 0.31f * tp.amp / energy;
            for (int i = 0; i < L; ++i) p.line[(size_t) i] *= amp;

            const int off = juce::jlimit (1, juce::jmax (1, L / 2), (int) (L * tp.pluckPos));
            for (int i = L - 1; i >= off; --i)
                p.line[(size_t) i] -= 0.72f * p.line[(size_t) (i - off)];

            p.lvl = vel;
        }

        //----------------------------------------------------------------------
        void triggerPad (int midi, float vel, const TimbreParams& tp, float pan, float ten,
                         int owner, int layer = 0, int slot = 0)
        {
            midi = juce::jlimit (12, 124, midi);

            //  Three ways a pad can be asked to speak:
            //
            //  1. same note, same string, again -- re-arm the pad that is already
            //     sounding (phase and tremolo carry on), so a repeated note never
            //     stacks up;
            //  2. a different note from the same voice -- slide the pad that is
            //     sounding to the new pitch, the way a hand slides along a stopped
            //     string. Crossing the next string is heard as a legato move
            //     instead of a second drone appearing on top of the first;
            //  3. nothing sounding for that voice -- start a fresh pad.
            //
            //  `slot` splits rule 2: a harmony voice rolls six chord tones at
            //  once, and they must not slide the same pad from one tone to the
            //  next -- each tone keeps its own layer.
            Pad* target = nullptr;
            bool restrike = false, glide = false;

            for (auto& p : pads)
            {
                if (p.active && p.owner == owner && p.layer == layer && p.slot == slot
                    && p.note == midi && std::abs (p.panKey - pan) < 1.0e-4f)
                {
                    target = &p;
                    restrike = true;
                    break;
                }
            }

            if (! restrike && owner >= 0)
            {
                // newest still-sounding pad of this voice -- the one the ball
                // just left -- is the one that should slide
                int bestPos = 0x7fffffff;
                for (auto& p : pads)
                {
                    if (! p.active || p.owner != owner || p.layer != layer || p.slot != slot) continue;
                    if (p.pos >= p.life) continue;
                    if (p.pos < bestPos) { bestPos = p.pos; target = &p; }
                }
                glide = (target != nullptr);
            }

            if (target == nullptr)
            {
                for (auto& p : pads) if (! p.active) { target = &p; break; }

                if (target == nullptr)
                {
                    // steal whichever pad is furthest through its life -- it is
                    // the quietest one, so the cut is the least audible
                    float best = -1.0f;
                    for (auto& p : pads)
                    {
                        const float t = (float) p.pos / (float) juce::jmax (1, p.life);
                        if (t > best) { best = t; target = &p; }
                    }
                }
            }
            if (target == nullptr) return;

            auto& p = *target;
            const bool fresh = ! restrike && ! glide;

            const int oldNote = p.note;
            const double freq = freqOf (midi, ten);

            // how far it has to travel decides how long the slide takes: long
            // enough to be heard as a slide, short enough to settle before the
            // ball reaches the next string
            double semis = 0.0;
            if (glide && oldNote > 0 && oldNote != midi)
                semis = std::abs (12.0 * std::log2 (freq / freqOf (oldNote, ten)));

            p.atk  = (int) juce::jmax (16.0, (double) tp.attack * (1.45 - 0.95 * ten) * sr);
            p.rel  = (int) juce::jmax (16.0, (double) tp.release * sr);
            const int hold = (int) (juce::jmax (0.2, (double) tp.decay) * sr);
            p.life = p.atk + hold + p.rel;

            // a fresh pad starts at the foot of its attack; a re-struck or
            // glided one picks up at full envelope, so the note never dips on
            // its way to the next string
            p.pos  = fresh ? 0 : p.atk;
            p.note = midi;
            p.panKey = pan;
            p.owner = owner;
            p.layer = layer;
            p.slot = slot;

            if (fresh)
            {
                p.envSm  = 0.0f;
                p.gainSm = 0.0f;
            }

            static const double cents[3] = { 0.0, 1.0, -0.75 };
            p.ampH[0] = 1.00f;
            p.ampH[1] = tp.h2;
            p.ampH[2] = tp.h3;

            // a partial nobody can hear is a partial we do not compute
            p.nH = (tp.h3 > 0.02f) ? 2 : (tp.h2 > 0.02f ? 1 : 0);

            const int nGlide = glide
                ? (int) (sr * juce::jlimit (0.045, 0.11, 0.055 + 0.0022 * semis))
                : 0;

            const double spread = tp.detune * (0.35 + 1.35 * (double) ten);
            for (int h = 0; h < 3; ++h)
            {
                const double mult = (h == 0) ? 1.0 : (double) (h + 1);
                const double f = freq * mult * std::pow (2.0, cents[h] * spread / 1200.0);
                const double targetInc = juce::jlimit (0.0, (double) kTabSize * 0.45,
                                                       f / sr * (double) kTabSize);
                p.incTo[(size_t) h] = targetInc;

                if (glide)
                {
                    // linear ramp, so the pitch really arrives instead of just
                    // creeping toward it
                    p.incStep[(size_t) h] = (targetInc - p.inc[(size_t) h])
                                            / (double) juce::jmax (1, nGlide);
                }
                else
                {
                    p.inc[(size_t) h]     = targetInc;
                    p.incStep[(size_t) h] = 0.0;
                }

                if (fresh) p.ph[(size_t) h] = (double) kTabSize * (double) (h + 1) * 0.25;
            }
            p.glideLeft = nGlide;

            p.tremInc = juce::jmax (0.0, (double) tp.tremRate / sr * (double) kTabSize);
            if (fresh) p.tremPh = 0.0;
            p.tremDepth = juce::jlimit (0.0f, 0.6f, tp.tremDepth);

            // normalise so a pad sits roughly at the level of a plucked note
            const float sumH = 1.0f + tp.h2 + tp.h3;
            p.gain = juce::jlimit (0.0f, 1.6f, vel) * tp.amp * 0.36f / juce::jmax (0.2f, sumH);

            // the voice walks from string to string, so the pad follows it
            // across the stereo field -- smoothed, never a pan jump
            panGains (pan, p.glT, p.grT);
            if (fresh) { p.gl = p.glT; p.gr = p.grT; }

            p.lvl = vel;
            p.active = true;
        }

        struct Pluck
        {
            std::vector<float> line;
            int   len = 0, idx = 0, note = 0, life = 0;
            double delay = 100.0;
            float g = 0.999f, bright = 0.5f, lp = 0.0f, pan = 0.0f, lvl = 0.0f;
            float gl = 0.707f, gr = 0.707f;
            float dcIn = 0.0f, dcOut = 0.0f;
            bool  active = false;
        };

        struct Pad
        {
            double ph[3] { }, inc[3] { };
            double incTo[3] { }, incStep[3] { };   // glide target / per-sample step
            float  ampH[3] { };
            int    nH = 2;                  // highest partial this patch uses
            int    pos = 0, atk = 1, rel = 1, life = 1;
            int    glideLeft = 0;           // samples left in a legato slide
            int    note = -1;               // midi number, for re-strike matching
            int    owner = -1;              // voice that struck it, so two voices
                                            // never merge into one pad
            int    layer = 0;               // 0 root, -1 sub, +1 octave
            int    slot  = 0;               // which chord tone of the voice (harmony)
            float  panKey = 0.0f;           // which string position it came from
            float  envSm = 0.0f;            // smoothed envelope, kills re-strike steps
            float  gain = 0.2f, gainSm = 0.0f;
            double tremPh = 0.0, tremInc = 0.0;
            float  tremDepth = 0.0f;
            float  gl = 0.707f, gr = 0.707f;   // smoothed pan gains
            float  glT = 0.707f, grT = 0.707f; // pan targets
            float  lvl = 0.0f;
            bool   active = false;
        };

        std::array<Pluck, kMaxPlucks> plucks;
        std::array<Pad,   kMaxPads>   pads;
        std::array<float, kTabSize + 1> sineTab { };
        double sr = 48000.0;
        juce::uint32 rngState = 0x1234567u;
    };
}
