//------------------------------------------------------------------------------
//  Offline verification for String Theory.
//
//  It does not trust the DSP by inspection: it renders the real processBlock,
//  then asserts the four behaviours the instrument is sold on --
//
//    1. it makes sound (RMS / peak / note on + off, per MIDI channel)
//    2. every timbre speaks, including the LOW/HIGH register layers
//    3. RATE really moves the balls (1/16 must travel 4x faster than 1/1)
//    4. CHORD follow really changes the notes with the progression
//
//------------------------------------------------------------------------------
#include "PluginProcessor.h"
#include <cstdio>
#include <vector>

namespace
{
    int failures = 0;

    void check (bool cond, const char* what)
    {
        std::printf ("  %-46s %s\n", what, cond ? "ok" : "FAIL");
        if (! cond) ++failures;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    StringTheoryProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    proc.world.bpm.store (72.0);
    proc.setPlaying (true);

    const int blockSize = 512;
    const int blocks = (48000 / blockSize) * 8;          // 8 seconds

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    double peak = 0.0, sum = 0.0;
    long long samples = 0;
    int noteOns = 0, noteOffs = 0;
    int perChannel[17] = { 0 };

    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        midi.clear();
        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buffer.getReadPointer (ch);
            for (int s = 0; s < blockSize; ++s)
            {
                const double v = (double) d[s];
                peak = juce::jmax (peak, std::abs (v));
                sum += v * v;
                ++samples;
            }
        }

        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                ++noteOns;
                perChannel[juce::jlimit (1, 16, m.getChannel())]++;
            }
            if (m.isNoteOff()) ++noteOffs;
        }
    }

    const double rms = std::sqrt (sum / (double) juce::jmax (1LL, samples));

    std::printf ("== audio ============================================\n");
    std::printf ("RMS %.5f  PEAK %.4f  NOTE_ON %d  NOTE_OFF %d\n", rms, peak, noteOns, noteOffs);
    std::printf ("CHANNELS:");
    for (int c = 1; c <= 16; ++c)
        if (perChannel[c] > 0) std::printf (" ch%d=%d", c, perChannel[c]);
    std::printf ("\n");

    check (rms > 0.002, "engine produces audio");
    check (peak < 1.02, "output stays inside full scale");
    check (noteOns > 20 && noteOffs > 0, "MIDI note on / off both fire");

    //---- internal synth off -> silence, MIDI continues -----------------------
    // (the pads already ringing would take 10 s to fade, so this checks the
    //  honest thing: with SYNTH off nothing NEW is produced, the tail decays)
    proc.world.internalSynth.store (false);
    proc.releaseResources();                  // kill every ringing voice
    int noteOns2 = 0;
    double headPeak = 0.0, tailPeak = 0.0;
    for (int b = 0; b < 400; ++b)             // ~4.3 s of tail
    {
        buffer.clear();
        midi.clear();
        proc.processBlock (buffer, midi);
        double p = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < blockSize; ++s)
                p = juce::jmax (p, (double) std::abs (buffer.getSample (ch, s)));
        if (b < 40) headPeak = juce::jmax (headPeak, p);
        if (b > 300) tailPeak = juce::jmax (tailPeak, p);
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn()) ++noteOns2;
    }
    std::printf ("SYNTH_OFF head %.6f -> tail %.6f  NOTE_ON %d\n", headPeak, tailPeak, noteOns2);
    check (tailPeak < juce::jmax (2.0e-4, headPeak * 0.15), "SYNTH off = silence, no new audio");
    check (noteOns2 > 0, "SYNTH off = MIDI keeps running");
    proc.world.internalSynth.store (true);

    //---- every timbre must speak --------------------------------------------
    std::printf ("== timbres ==========================================\n");
    bool timbresOk = true;
    for (int t = 0; t < st::timbreCount(); ++t)
    {
        proc.setVoiceTimbre (0, t);
        double p = 0.0, s2 = 0.0;
        long long n2 = 0;
        for (int b = 0; b < 160; ++b)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < blockSize; ++s)
                {
                    const double v = (double) buffer.getSample (ch, s);
                    p = juce::jmax (p, std::abs (v));
                    s2 += v * v;
                    ++n2;
                }
        }
        const double r2 = std::sqrt (s2 / (double) juce::jmax (1LL, n2));
        std::printf ("  %-4s %-11s peak %.4f  rms %.4f\n",
                     st::timbreFamily (t), st::timbreAt (t).name, p, r2);
        if (p < 0.002) { timbresOk = false; std::printf ("    ^ silent\n"); }
    }
    proc.setVoiceTimbre (0, st::timbreIndexOf ("ENSEMBLE"));

    //---- the menu must show LOW / MID / HIGH exactly once --------------------
    //  The choice box prints a section header wherever the family changes, so
    //  a repeated family later in the table would print the register twice.
    std::printf ("== menu grouping ====================================\n");
    {
        juce::StringArray seen;
        juce::String last;
        bool grouped = true;
        for (int t = 0; t < st::timbreCount(); ++t)
        {
            const juce::String f (st::timbreFamily (t));
            if (f != last)
            {
                if (seen.contains (f)) { grouped = false; std::printf ("    ^ %s repeats at #%d\n", f.toRawUTF8(), t); }
                seen.add (f);
                last = f;
            }
        }
        check (grouped, "families run LOW / MID / HIGH once, no repeats");
        check (seen.size() == 3, "exactly three register sections");
    }

    //---- quantisation --------------------------------------------------------
    //  A ball sounds when geometry says so, so without a grid the onsets sit at
    //  random places inside the beat and the voices never agree on a pulse.
    //  Measured here: the mean distance from the nearest 1/16 line.
    std::printf ("== quantisation ======================================\n");
    {
        const double sr = 44100.0, blockSec = 64.0 / sr, gridBeats = 0.25;

        auto meanOffGrid = [&] (int grid, float amount)
        {
            proc.setPlaying (false);
            proc.world.reset();
            proc.world.synced.store (false);
            proc.world.hostBeat.store (-1);
            proc.world.hostPpq.store (-1.0);
            proc.world.bpm.store (60.0);          // 1 beat = 1 second
            proc.world.setVoiceCount (4);
            proc.world.quantGrid.store (grid);
            proc.world.quantAmount.store (amount);
            for (int v = 0; v < 4; ++v) proc.setVoiceRate (v, 4);   // 1/16: plenty of onsets
            proc.setPlaying (true);

            double beat = 0.0, sum = 0.0;
            int notes = 0;
            for (int blk = 0; blk < 20000; ++blk)      // ~29 s
            {
                st::StrumEvent ev[128];
                int cnt = 0;
                {
                    const juce::SpinLock::ScopedLockType sl (proc.world.lock);
                    proc.world.update (blockSec, 64, ev, cnt);
                }
                for (int i = 0; i < cnt; ++i)
                {
                    const double atBeat = beat + (double) ev[i].sampleOffset / sr;
                    sum += std::abs (atBeat / gridBeats - std::round (atBeat / gridBeats)) * gridBeats;
                    ++notes;
                }
                beat += blockSec;
            }
            proc.setPlaying (false);
            std::printf ("  grid %d amount %.2f: %4d notes, mean off-grid %.4f beats\n",
                         grid, amount, notes, notes ? sum / (double) notes : 0.0);
            proc.world.quantAmount.store (1.00f);
            proc.world.quantGrid.store (3);
            return notes ? sum / (double) notes : 0.0;
        };

        const double raw     = meanOffGrid (0, 0.0f);
        const double snapped = meanOffGrid (3, 1.00f);
        // 0.015 beats is a sixth of the grid: clearly off the line. (The exact
        // mean depends on how the ball's crossing period sits against the
        // grid, so the absolute figure is only a sanity floor -- the real
        // evidence is the ratio below.)
        check (raw > 0.015, "without a grid the onsets fall inside the beat");
        // Sparse onsets are harder to pull onto the grid than dense ones, so
        // the bar is "a clear improvement" rather than a fixed ratio.
        check (snapped < raw * 0.62, "the grid pulls the onsets towards the line");
        proc.world.bpm.store (72.0);      // the sections below are written at 72
    }

    //---- R: random rate ------------------------------------------------------
    //  Two states, not five: a voice rests on a slow division, jumps straight
    //  to a fast one (1/1 -> 1/16, no ladder), holds the burst briefly, then
    //  lands back on a slow division.
    std::printf ("== random rate =======================================\n");
    {
        proc.setPlaying (false);
        proc.world.reset();
        proc.world.synced.store (false);
        proc.world.hostBeat.store (-1);
        proc.world.hostPpq.store (-1.0);
        proc.world.bpm.store (60.0);          // 1 beat = 1 second
        proc.world.setVoiceCount (2);
        proc.world.quantGrid.store (0);       // keep the grid out of this one
        proc.setVoiceRate (1, 2);
        {
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            proc.world.balls[(size_t) 1].randomRate = true;
            proc.world.balls[(size_t) 1].rateJumpIn = 0.0;
        }
        proc.setPlaying (true);

        const double sr = 44100.0, blockSec = 64.0 / sr;
        double holdSum[st::kNumRates] = { 0, 0, 0, 0, 0 };
        int    holdN[st::kNumRates]   = { 0, 0, 0, 0, 0 };
        int    changes = 0, rises = 0, falls = 0;
        int    bigJumps = 0;                  // slow -> fast, at least 2 divisions up
        int    midDescent = 0;                // fast -> a middling division (should not happen)
        int    prev = 2;
        {
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            prev = proc.world.balls[(size_t) 1].rate;
        }
        double held = 0.0;

        for (int blk = 0; blk < 41000; ++blk)      // ~60 s: 1/8 bursts are 1 in 5
        {
            st::StrumEvent ev[128];
            int cnt = 0;
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            proc.world.update (blockSec, 64, ev, cnt);

            const int r = proc.world.balls[(size_t) 1].rate;
            if (r != prev)
            {
                holdSum[prev] += held;
                ++holdN[prev];
                ++changes;
                const bool wasFast = prev >= 3, isFast = r >= 3;
                if (! wasFast && isFast)
                {
                    ++rises;
                    if (r - prev >= 2) ++bigJumps;
                }
                else if (wasFast && ! isFast)
                {
                    ++falls;
                    if (r > 2) ++midDescent;      // stopped halfway down
                }
                else if (isFast && prev >= 3 && r >= 3)
                {
                    ++midDescent;                 // fast -> fast: a stair-step
                }
                prev = r;
                held = 0.0;
            }
            held += blockSec;
        }
        proc.setPlaying (false);
        proc.world.balls[(size_t) 1].randomRate = false;

        auto avgHold = [&] (int r)
        {
            return holdN[r] > 0 ? holdSum[r] / (double) holdN[r] : 0.0;
        };
        double slowAvg = 0.0, fastAvg = 0.0;
        int slowN = 0, fastN = 0;
        for (int r = 0; r < st::kNumRates; ++r)
        {
            std::printf ("  %-4s held %5.2f s over %d visits\n", st::rateLabel (r), avgHold (r), holdN[r]);
            if (r <= 2) { slowAvg += holdSum[r]; slowN += holdN[r]; }
            else        { fastAvg += holdSum[r]; fastN += holdN[r]; }
        }
        slowAvg = slowN ? slowAvg / slowN : 0.0;
        fastAvg = fastN ? fastAvg / fastN : 0.0;

        std::printf ("  %d changes: %d slow->fast (%d big), %d fast->slow, %d halfway\n",
                     changes, rises, bigJumps, falls, midDescent);
        std::printf ("  slow held %.2f s, fast held %.2f s\n", slowAvg, fastAvg);

        check (changes >= 3, "R changes the division on its own");
        check (rises >= 2 && bigJumps >= 1, "a slow division jumps straight to a fast one");
        check (midDescent == 0, "the burst always lands back on a slow division");
        check (slowAvg > fastAvg * 1.8, "the slow rest lasts far longer than the burst");
        if (holdN[3] > 0 && holdN[4] > 0)
            check (avgHold (3) > avgHold (4) * 1.5, "a 1/8 burst is held longer than a 1/16 one");
        else
            std::printf ("  (only one fast division visited, ratio not tested)\n");
        if (holdN[4] > 0)
            check (avgHold (4) < 1.4, "a 1/16 burst is only a flash");
        proc.world.bpm.store (72.0);          // the sections below are written at 72
    }

    //---- the short voices must actually be short -----------------------------
    //  Play a little, stop, and see what is still ringing 2 s later. The
    //  reverbs are muted for this one: their tail would mask every decay.
    std::printf ("== short voices =====================================\n");
    {
        const float rev = proc.reverbMix.load(), dly = proc.delayMix.load();
        proc.reverbMix.store (0.0f);
        proc.delayMix.store (0.0f);
        proc.world.synced.store (false);          // no host clock: stop must stop
        proc.world.hostBeat.store (-1);
        proc.world.setVoiceCount (1);             // the chord voice alone, so no
                                                  // other voice's drone rings on

        auto ringOut = [&] (const char* nm)
        {
            const int t = st::timbreIndexOf (nm);
            proc.releaseResources();              // nothing from the patch before may ring on
            proc.setVoiceTimbre (0, t);
            proc.setVoiceRate (0, 4);            // 1/16: the roll must fire inside the window
            proc.setPlaying (true);

            double head = 0.0;
            for (int b = 0; b < 120; ++b)
            {
                buffer.clear(); midi.clear();
                proc.processBlock (buffer, midi);
                double p = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < blockSize; ++s)
                        p = juce::jmax (p, (double) std::abs (buffer.getSample (ch, s)));
                if (b > 60) head = juce::jmax (head, p);   // level while it is playing
            }
            proc.setPlaying (false);

            double tail = 0.0;
            for (int b = 0; b < 520; ++b)
            {
                buffer.clear(); midi.clear();
                proc.processBlock (buffer, midi);
                double p = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < blockSize; ++s)
                        p = juce::jmax (p, (double) std::abs (buffer.getSample (ch, s)));
                if (b > 460) tail = juce::jmax (tail, p);   // ~5 s after the stop
            }
            const double ratio = tail / juce::jmax (1.0e-9, head);
            std::printf ("  %-11s playing %.4f -> 5 s after the stop %.4f  (%.3f)\n", nm, head, tail, ratio);
            return ratio;
        };

        //  The bus is shared: the body resonance and the ambience stage keep a
        //  tail of their own whatever the patch, so absolute silence is not the
        //  criterion -- a short voice must simply be far quieter 5 s after the
        //  stop than a drone is at the same moment.
        const double droneTail = ringOut ("BASS DRONE");
        check (droneTail > 0.10, "a drone is still sounding 5 s after the stop (control)");

        bool shortOk = true;
        for (const char* nm : { "SUB PLUCK", "WOOD BASS", "NYLON", "MUTED", "KALIMBA", "HARMONIC", "MUSIC BOX" })
            if (ringOut (nm) > droneTail * 0.55) shortOk = false;
        check (shortOk, "short voices ring out far less than a drone does");

        proc.reverbMix.store (rev);
        proc.delayMix.store (dly);
        proc.world.setVoiceCount (2);
    }
    check (timbresOk, "every timbre speaks (LOW / MID / HIGH)");

    //---- a ball must not circle the rim in silence --------------------------
    //  The outermost string sits at 0.88 and the rim at 0.96. A ball grazing
    //  the wall used to orbit inside that band for bars without crossing
    //  anything -- a voice that was simply not there. It is pulled back in now.
    std::printf ("== rim trap ==========================================\n");
    {
        proc.setPlaying (false);
        proc.world.reset();
        proc.world.synced.store (false);
        proc.world.hostBeat.store (-1);
        proc.world.hostPpq.store (-1.0);
        proc.world.bpm.store (60.0);
        proc.world.setVoiceCount (2);
        proc.world.quantGrid.store (0);
        proc.setVoiceGain (0, 0.0);
        proc.setVoiceGain (1, 1.0);
        {
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            auto& b = proc.world.balls[(size_t) 1];
            b = st::BallState();
            b.active = true;
            b.rate = 3;                              // 1/8
            b.speed = st::rateSpeed (3, 60.0);
            b.rand = 1.0;                            // every crossing must count
            b.x = 0.50; b.y = 0.0;                   // somewhere mid-plate
            b.vx = 0.76 * b.speed;                   // heading out, off-centre
            b.vy = 0.65 * b.speed;                   // so it really hits the rim
        }
        proc.setPlaying (true);

        st::StrumEvent ev[128];
        int notes = 0;
        double longest = 0.0, since = 0.0, maxR = 0.0;
        const double step = 0.01;
        for (int i = 0; i < 600; ++i)                  // 6 s
        {
            int n = 0;
            {
                const juce::SpinLock::ScopedLockType sl (proc.world.lock);
                proc.world.update (step, 441, ev, n);
                const auto& bb = proc.world.balls[(size_t) 1];
                maxR = juce::jmax (maxR, std::sqrt (bb.x * bb.x + bb.y * bb.y));
            }
            notes += n;
            since += step;
            if (n > 0) { longest = juce::jmax (longest, since); since = 0.0; }
        }
        proc.setPlaying (false);
        std::printf ("  6 s from a rim orbit: %d notes, longest silence %.2f s, max radius %.3f\n",
                     notes, longest, maxR);
        // it must still touch the wall -- a ball stopped short of the rim reads
        // as a ball behind glass
        check (maxR > 0.94, "the ball still reaches the rim and bounces");
        check (notes >= 8, "a ball grazing the rim is pulled back onto the strings");
        check (longest < 2.5, "and never falls silent for long");

        // hand the world back the way the sections below expect it
        proc.setVoiceGain (0, 1.0);
        proc.setVoiceGain (1, 1.0);
        proc.world.quantGrid.store (3);
        proc.world.bpm.store (72.0);
        proc.world.reset();
        proc.world.setVoiceCount (2);
    }

    //---- RATE really drives the ball speed ----------------------------------
    // (voice 1 is the harmony voice -- no ball -- so the test drives voice 2)
    std::printf ("== rate =============================================\n");
    proc.setPlaying (false);
    proc.world.setVoiceCount (2);

    const int testRates[2] = { 0, 4 };        // 1/1 and 1/16
    double travelled[2] = { 0.0, 0.0 };

    for (int k = 0; k < 2; ++k)
    {
        {
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            auto& b = proc.world.balls[1];
            const int keepRate = testRates[k];
            b = st::BallState();
            b.active = true;
            b.rate = keepRate;
            b.speed = st::rateSpeed (keepRate, 72.0);
            b.x = 0.0; b.y = 0.0;
            b.vx = b.speed; b.vy = 0.0;
        }

        proc.setPlaying (true);
        st::StrumEvent ev[128];
        int n = 0;
        {
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            proc.world.update (0.05, 2400, ev, n);        // 50 ms
        }
        proc.setPlaying (false);

        const juce::SpinLock::ScopedLockType sl (proc.world.lock);
        const auto& b = proc.world.balls[1];
        travelled[k] = std::sqrt (b.x * b.x + b.y * b.y);
    }

    const double expectSlow = st::rateSpeed (0, 72.0) * 0.05;
    const double expectFast = st::rateSpeed (4, 72.0) * 0.05;
    std::printf ("1/1  50ms travelled %.4f (expect %.4f)\n", travelled[0], expectSlow);
    std::printf ("1/16 50ms travelled %.4f (expect %.4f)\n", travelled[1], expectFast);
    check (std::abs (travelled[0] - expectSlow) < expectSlow * 0.06, "1/1 speed matches the division");
    check (std::abs (travelled[1] - expectFast) < expectFast * 0.06, "1/16 speed matches the division");
    check (travelled[1] > travelled[0] * 3.6, "1/16 travels ~4x faster than 1/1");

    //---- voice 1 is the harmony voice: no ball, rolls the chord -------------
    std::printf ("== harmony voice =====================================\n");
    {
        proc.world.setVoiceCount (2);
        // freeze the bar clock on one bar so the chord cannot move under the
        // measurement (the harmony roll itself must then be perfectly regular)
        proc.world.bpm.store (60.0);
        proc.world.synced.store (true);
        proc.world.hostBpm.store (60.0);
        proc.world.hostBeat.store (8);           // bar 2 -> chord step 2
        proc.setVoiceGain (1, 0.0);              // only the harmony is heard
        proc.setVoiceTimbre (0, st::timbreIndexOf ("ENSEMBLE"));
        proc.setVoiceOctave (0, 0);
        proc.setPlaying (true);

        auto rollsFor = [&] (double seconds, int rateIdx, std::vector<st::StrumEvent>& out)
        {
            proc.setVoiceRate (0, rateIdx);
            out.clear();
            const double step = 1.0 / 100.0;
            st::StrumEvent ev[256];
            for (double t = 0.0; t < seconds; t += step)
            {
                int n = 0;
                {
                    const juce::SpinLock::ScopedLockType sl (proc.world.lock);
                    proc.world.update (step, 480, ev, n);
                }
                for (int i = 0; i < n; ++i) out.push_back (ev[i]);
            }
        };

        std::vector<st::StrumEvent> slow, fast;

        // one warm-up step: the bar clock adopts bar 2 and primes the first roll
        {
            st::StrumEvent ev[256];
            int n = 0;
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            proc.world.update (0.01, 480, ev, n);
        }

        // the chord is frozen now, so this is what every roll must play
        int expectVoicing[st::kNumStrings] { };
        for (int i = 0; i < st::kNumStrings; ++i)
            expectVoicing[i] = juce::jlimit (0, 127, proc.world.voicing[i]);

        rollsFor (8.0, 0, slow);                 // 1/1 at 60 BPM = one roll / 4 s
        rollsFor (4.0, 4, fast);                 // 1/16 = one roll / 0.25 s

        bool onlyHarmony = true, slotsOk = true, tonesOk = true;
        for (auto& e : slow)
        {
            if (e.voice != 0) onlyHarmony = false;
            if (e.midi != expectVoicing[juce::jlimit (0, st::kNumStrings - 1, e.slot)]) slotsOk = false;
            bool inVoicing = false;
            for (int i = 0; i < st::kNumStrings; ++i)
                if (e.midi == expectVoicing[i]) inVoicing = true;
            if (! inVoicing) tonesOk = false;
        }

        std::printf ("  8 s at 1/1 -> %d notes, 4 s at 1/16 -> %d notes\n",
                     (int) slow.size(), (int) fast.size());

        check (onlyHarmony && ! slow.empty(), "harmony voice is the one that speaks");
        // about one roll per bar, and a roll is a *partial* chord now: a hand
        // skips an inner string now and then instead of hitting all six
        check (slow.size() >= 4 && slow.size() <= 15, "1/1 rolls about once per bar, in a partial chord");
        check (fast.size() > slow.size(), "1/16 rolls far more often than 1/1");
        check (tonesOk, "every rolled tone is one of the chord tones");
        check (slotsOk, "each tone keeps its own slot");

        {
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            const auto& h = proc.world.balls[0];
            check (h.harmony && h.x == 0.0 && h.y == 0.0 && h.trailCount == 0,
                   "harmony voice has no ball to move");
            check (proc.world.voiceCount < 2 || ! proc.world.balls[1].harmony,
                   "the balls after it are ordinary voices");
        }

        // and it is heard in the audio, not only in the event stream
        proc.setVoiceGain (1, 1.0);
        proc.setVoiceTimbre (1, st::timbreIndexOf ("DEEP DRONE"));
        proc.setVoiceRate (0, 0);
        proc.releaseResources();
        double hPeak = 0.0;
        for (int b = 0; b < 220; ++b)            // ~2.3 s
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
            hPeak = juce::jmax (hPeak, (double) buffer.getMagnitude (0, blockSize));
        }
        std::printf ("  harmony main gain %.4f\n", hPeak);
        check (hPeak > 0.002, "the harmony voice reaches the output bus");

        proc.setPlaying (false);
        proc.world.synced.store (false);
        proc.world.hostBeat.store (-1);
        proc.world.bpm.store (72.0);
    }

    //---- chord follow --------------------------------------------------------
    std::printf ("== chord follow =====================================\n");
    proc.world.setVoiceCount (2);
    proc.world.synced.store (false);
    proc.world.progression.store (0);
    proc.setPlaying (false);

    bool chordOk = true;
    for (int step = 0; step < 4; ++step)
    {
        // drive the bar clock the same way a host would
        proc.world.synced.store (true);
        proc.world.hostBpm.store (72.0);
        proc.world.hostBeat.store (step * 4);
        {
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            st::StrumEvent ev[128];
            int n = 0;
            proc.world.update (1.0 / 48000.0, 1, ev, n);
        }

        int expect[6] { };
        st::makeVoicing (st::progressions()[0].steps[(size_t) step], 0, expect);

        std::printf ("  bar %d  chord %-5s  voicing", step,
                     st::chordName (st::progressions()[0].steps[(size_t) step]).toRawUTF8());
        for (int i = 0; i < st::kNumStrings; ++i)
            std::printf (" %s", juce::MidiMessage::getMidiNoteName (proc.world.voicing[i], true, true, 4).toRawUTF8());
        std::printf ("\n");

        for (int i = 0; i < st::kNumStrings; ++i)
            if (proc.world.voicing[i] != expect[i]) chordOk = false;
    }
    check (chordOk, "voicing matches the progression, bar by bar");

    // and the notes actually emitted must be that voicing
    proc.world.hostBeat.store (8);              // 3rd bar of the progression
    proc.setPlaying (true);

    bool eventsOk = true;
    int checked = 0;

    //  allowed pitch classes: the six voicing tones, plus the colour tones the
    //  air voice may take instead of a top rung (11th, 9th, 13th)
    bool allowedPc[12] = {};
    for (int i = 0; i < st::kNumStrings; ++i)
        allowedPc[(((proc.world.voicing[i] % 12) + 12) % 12)] = true;
    {
        const auto& progs = st::progressions();
        const int pIdx = juce::jlimit (0, (int) progs.size() - 1, proc.world.progression.load());
        const int sIdx = juce::jlimit (0, 3, proc.world.chordStep.load());
        const int rootPc = progs[(size_t) pIdx].steps[(size_t) sIdx].rootPc;
        for (int c : { 2, 5, 9 }) allowedPc[((rootPc + c) % 12 + 12) % 12] = true;
    }

    for (int b = 0; b < 120; ++b)
    {
        buffer.clear();
        midi.clear();
        proc.processBlock (buffer, midi);
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOn()) continue;
            ++checked;
            const int pc = ((m.getNoteNumber() % 12) + 12) % 12;
            if (! allowedPc[pc]) eventsOk = false;
        }
    }
    proc.setPlaying (false);
    proc.world.synced.store (false);
    proc.world.hostBeat.store (-1);
    std::printf ("  checked %d struck notes against the voicing\n", checked);
    check (checked > 10 && eventsOk, "struck notes are chord tones or colour tones");

    //  ...and each voice owns its own slice of that voicing. Before this, four
    //  voices all played the same six pitches in different octaves, which is
    //  why the texture piled up instead of reading as four parts.
    {
        bool rolesOk = true;
        for (int s = 0; s < st::kNumStrings; ++s)
        {
            const int lo = proc.world.noteFor (s, 0, true, 0);
            const int mi = proc.world.noteFor (s, 0, true, 1);
            const int hi = proc.world.noteFor (s, 0, true, 2);
            if (! (lo <= mi && mi <= hi)) rolesOk = false;
        }

        // the low voice may only sound the bottom three rungs, the air voice
        // only the top three -- or, in place of the very top, a colour tone
        bool slicesOk = true;
        for (int s = 0; s < st::kNumStrings; ++s)
        {
            const int lo = proc.world.noteFor (s, 0, true, 0);
            const int hi = proc.world.noteFor (s, 0, true, 2);
            bool loIn = false, hiIn = false;
            for (int k = 0; k <= 2; ++k) if (proc.world.voicing[k] == lo) loIn = true;
            for (int k = 3; k < st::kNumStrings; ++k) if (proc.world.voicing[k] == hi) hiIn = true;
            if (! hiIn && hi >= proc.world.voicing[3]
                && allowedPc[(((hi % 12) + 12) % 12)]) hiIn = true;    // colour tone
            if (! loIn || ! hiIn) slicesOk = false;
        }
        check (rolesOk, "on every string the low voice sits under the air voice");
        check (slicesOk, "low / mid / air each own a slice of the voicing");
    }

    //---- harmonic rhythm -----------------------------------------------------
    std::printf ("== harmonic rhythm ==================================\n");
    {
        proc.world.synced.store (true);
        proc.world.hostBpm.store (72.0);
        proc.world.progression.store (0);
        proc.setPlaying (false);

        auto stepAt = [&] (int beat)
        {
            proc.world.hostBeat.store (beat);
            const juce::SpinLock::ScopedLockType sl (proc.world.lock);
            st::StrumEvent ev[128];
            int n = 0;
            proc.world.update (1.0 / 48000.0, 1, ev, n);
            return proc.world.chordStep.load();
        };

        // 2 bars per chord: the step only turns on even bars
        proc.world.paceBars.store (2);
        const int p0 = stepAt (0), p1 = stepAt (4), p2 = stepAt (8), p3 = stepAt (12);
        std::printf ("  pace 2: bars 0..3 -> steps %d %d %d %d\n", p0, p1, p2, p3);
        check (p0 == 0 && p1 == 0, "PACE 2 holds the chord across the odd bar");
        check (p2 == 1 && p3 == 1, "PACE 2 advances the chord on the even bar");

        // HOLD freezes the harmony, the bar clock keeps ticking (EVERY retune
        // still runs off it)
        proc.world.paceBars.store (0);
        stepAt (16);
        const int frozen = stepAt (20);
        check (frozen == 1, "HOLD freezes the chord");

        proc.world.paceBars.store (1);
        proc.world.synced.store (false);
        proc.world.hostBeat.store (-1);
    }

    //---- how the chord tones are dealt across the six strings ----------------
    std::printf ("== string assignment =================================\n");
    {
        int slot[st::kNumStrings] {};
        auto readSlots = [&]
        {
            const uint32_t m = proc.world.stringMap.load();
            for (int i = 0; i < st::kNumStrings; ++i) slot[i] = st::stringSlot (m, i);
        };

        readSlots();
        std::printf ("  strings");
        for (int i = 0; i < st::kNumStrings; ++i)
            std::printf (" %s", juce::MidiMessage::getMidiNoteName (
                                    proc.world.voicing[slot[i]], true, true, 4).toRawUTF8());
        std::printf ("   (inside -> outside)\n");

        // every string must carry its own rung: a sweep with two or three
        // strings on the same pitch reads as a stutter, not as a line
        bool permOk = true;
        int used[st::kNumStrings] = { 0, 0, 0, 0, 0, 0 };
        for (int i = 0; i < st::kNumStrings; ++i)
        {
            if (slot[i] < 0 || slot[i] >= st::kNumStrings) permOk = false;
            else ++used[slot[i]];
        }
        for (int k = 0; k < st::kNumStrings; ++k)
            if (used[k] != 1) permOk = false;

        bool nonMono = false, inChord = true;
        for (int i = 1; i < st::kNumStrings; ++i)
            if (proc.world.voicing[slot[i]] <= proc.world.voicing[slot[i - 1]]) nonMono = true;

        for (int i = 0; i < st::kNumStrings; ++i)
        {
            bool found = false;
            for (int k = 0; k < st::kNumStrings; ++k)
                if (proc.world.voicing[k] == proc.world.voicing[slot[i]]) found = true;
            if (! found) inChord = false;
        }

        check (permOk, "every string carries its own pitch");
        check (nonMono, "strings are not dealt the chord in ascending order");
        check (inChord, "every string plays a chord tone");

        const uint32_t before = proc.world.stringMap.load();
        proc.retuneStrings();
        check (before != proc.world.stringMap.load(), "RETUNE deals a new assignment");

        // and the bar clock re-deals by itself when EVERY is set
        proc.world.retuneBars.store (2);
        proc.world.synced.store (true);          // let the host bar clock drive
        const uint32_t t0 = proc.world.stringMap.load();
        st::StrumEvent ev[128];
        int n = 0;
        for (int bar = 8; bar <= 16; bar += 4)
        {
            proc.world.hostBeat.store (bar);
            proc.world.update (1.0 / 60.0, 512, ev, n);
        }
        check (t0 != proc.world.stringMap.load(), "EVERY re-deals on the bar clock");

        proc.world.retuneBars.store (0);
        proc.world.synced.store (false);
        proc.world.hostBeat.store (-1);
    }

    //---- tension changes the ring but never kills it ------------------------
    std::printf ("== tension ==========================================\n");
    proc.world.setVoiceCount (4);
    const float ten[2] = { 0.10f, 0.90f };
    for (int k = 0; k < 2; ++k)
    {
        proc.world.tension.store (ten[k]);
        proc.setPlaying (true);
        double p = 0.0, s2 = 0.0;
        long long n2 = 0;
        for (int b = 0; b < 160; ++b)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < blockSize; ++s)
                {
                    const double v = (double) buffer.getSample (ch, s);
                    p = juce::jmax (p, std::abs (v));
                    s2 += v * v;
                    ++n2;
                }
        }
        std::printf ("  tension %.0f%%  peak %.4f  rms %.4f\n", ten[k] * 100.0f, p,
                     std::sqrt (s2 / (double) juce::jmax (1LL, n2)));
        check (p > 0.002, k == 0 ? "slack string still speaks" : "tight string still speaks");
    }
    proc.world.tension.store (0.5f);
    proc.setPlaying (false);


    //---- per voice level ----------------------------------------------------
    std::printf ("== voice level =======================================\n");
    proc.world.setVoiceCount (4);
    const float keepRev = proc.reverbMix.load(), keepDly = proc.delayMix.load();
    proc.reverbMix.store (0.0f);              // a reverb tail would mask the result
    proc.delayMix.store (0.0f);
    for (int i = 0; i < 4; ++i) proc.setVoiceGain (i, 0.0);
    {
        proc.releaseResources();
        proc.setPlaying (true);
        for (int b = 0; b < 60; ++b)          // let the reverb / delay tails drain
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
        }
        float p = 0.0f;
        for (int b = 0; b < 140; ++b)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < blockSize; ++s)
                    p = juce::jmax (p, std::abs (buffer.getSample (ch, s)));
        }
        std::printf ("  all voices 0 %%   peak %.5f\n", p);
        check (p < 0.0015f, "voices at 0 % are silent");
    }
    for (int i = 0; i < 4; ++i) proc.setVoiceGain (i, 1.0);
    {
        proc.releaseResources();
        proc.setPlaying (true);
        for (int b = 0; b < 60; ++b)          // let the reverb / delay tails drain
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
        }
        float p = 0.0f;
        for (int b = 0; b < 140; ++b)
        {
            buffer.clear();
            midi.clear();
            proc.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int s = 0; s < blockSize; ++s)
                    p = juce::jmax (p, std::abs (buffer.getSample (ch, s)));
        }
        std::printf ("  voices back at 100 %%  peak %.4f\n", p);
        check (p > 0.002f, "restoring the level brings the sound back");
    }
    proc.setPlaying (false);
    proc.reverbMix.store (keepRev);
    proc.delayMix.store (keepDly);

    //---- volume law ---------------------------------------------------------
    //  A fader should behave like a fader: 50 % is -6 dB. The voice gain was
    //  folded into the pluck amplitude with an offset (0.25 + 0.75 * g), so the
    //  bottom half of the travel did little. Measured on one ball voice, with
    //  the master at 40 % so the soft clipper never takes part.
    std::printf ("== volume law ========================================\n");
    {
        const float kRev = proc.reverbMix.load(), kDly = proc.delayMix.load();
        proc.reverbMix.store (0.0f);
        proc.delayMix.store (0.0f);
        proc.master.store (0.40f);
        proc.world.setVoiceCount (2);
        proc.world.synced.store (false);
        proc.world.hostBeat.store (-1);
        proc.world.hostPpq.store (-1.0);
        proc.world.bpm.store (72.0);
        proc.setVoiceRate (1, 3);
        proc.setVoiceGain (0, 0.0);           // the harmony voice stays out

        auto rmsFor = [&] (double gain)
        {
            proc.setVoiceGain (1, gain);
            proc.releaseResources();
            proc.setPlaying (false);
            proc.setPlaying (true);
            for (int b = 0; b < 90; ++b) { buffer.clear(); midi.clear(); proc.processBlock (buffer, midi); }
            double s2 = 0.0;
            long long n = 0;
            for (int b = 0; b < 300; ++b)
            {
                buffer.clear();
                midi.clear();
                proc.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < blockSize; ++s)
                    {
                        const double v = (double) buffer.getSample (ch, s);
                        s2 += v * v; ++n;
                    }
            }
            return std::sqrt (s2 / (double) juce::jmax (1LL, n));
        };

        const double full  = rmsFor (1.00);
        const double half  = rmsFor (0.50);
        const double quart = rmsFor (0.25);
        std::printf ("  VOL 100 %%  rms %.5f\n", full);
        std::printf ("  VOL  50 %%  rms %.5f   x%.2f  (ideal 0.50)\n", half, half / juce::jmax (1.0e-9, full));
        std::printf ("  VOL  25 %%  rms %.5f   x%.2f  (ideal 0.25)\n", quart, quart / juce::jmax (1.0e-9, full));
        check (half / full > 0.42 && half / full < 0.60, "VOL 50 % is about -6 dB");
        check (quart / full > 0.18 && quart / full < 0.33, "VOL 25 % is about -12 dB");

        // master: the same idea at the output fader
        auto rmsMaster = [&] (double m)
        {
            proc.master.store ((float) m);
            for (int b = 0; b < 90; ++b) { buffer.clear(); midi.clear(); proc.processBlock (buffer, midi); }
            double s2 = 0.0;
            long long n = 0;
            for (int b = 0; b < 300; ++b)
            {
                buffer.clear();
                midi.clear();
                proc.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < blockSize; ++s)
                    {
                        const double v = (double) buffer.getSample (ch, s);
                        s2 += v * v; ++n;
                    }
            }
            return std::sqrt (s2 / (double) juce::jmax (1LL, n));
        };
        proc.setVoiceGain (1, 1.0);
        const double mFull = rmsMaster (0.80);
        const double mHalf = rmsMaster (0.40);
        std::printf ("  OUT  80 %%  rms %.5f\n", mFull);
        std::printf ("  OUT  40 %%  rms %.5f   x%.2f  (ideal 0.50)\n", mHalf, mHalf / juce::jmax (1.0e-9, mFull));
        check (mHalf / mFull > 0.42 && mHalf / mFull < 0.60, "OUT 40 % is about -6 dB");

        // the chord voice must follow the same law as the ball voices -- if it
        // does not, balancing the two kinds of voice by ear is impossible
        proc.setVoiceGain (1, 0.0);
        proc.setVoiceGain (0, 1.0);
        proc.setVoiceRate (0, 2);
        proc.master.store (0.40f);
        auto rmsChord = [&] (double gain)
        {
            proc.setVoiceGain (0, gain);
            proc.releaseResources();
            proc.setPlaying (false);
            proc.setPlaying (true);
            for (int b = 0; b < 90; ++b) { buffer.clear(); midi.clear(); proc.processBlock (buffer, midi); }
            double s2 = 0.0;
            long long n = 0;
            for (int b = 0; b < 300; ++b)
            {
                buffer.clear(); midi.clear();
                proc.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < blockSize; ++s)
                    { const double v = (double) buffer.getSample (ch, s); s2 += v * v; ++n; }
            }
            return std::sqrt (s2 / (double) juce::jmax (1LL, n));
        };
        const double cFull = rmsChord (1.00);
        const double cHalf = rmsChord (0.50);
        std::printf ("  CHORD 100 %% rms %.5f\n", cFull);
        std::printf ("  CHORD  50 %% rms %.5f   x%.2f  (ideal 0.50)\n", cHalf, cHalf / juce::jmax (1.0e-9, cFull));
        check (cHalf / cFull > 0.40 && cHalf / cFull < 0.62,
               "the chord voice follows the same volume law");

        // headroom: every voice open, factory reverb and delay, master at 60 %
        {
            proc.world.setVoiceCount (4);
            for (int i = 0; i < 4; ++i) proc.setVoiceGain (i, 1.0);
            proc.master.store (0.60f);
            proc.reverbMix.store (0.67f);
            proc.delayMix  .store (0.55f);
            proc.releaseResources();
            proc.setPlaying (true);
            for (int b = 0; b < 120; ++b) { buffer.clear(); midi.clear(); proc.processBlock (buffer, midi); }

            float p = 0.0f;
            double s2 = 0.0;
            long long n = 0;
            int clipped = 0;
            for (int b = 0; b < 500; ++b)
            {
                buffer.clear(); midi.clear();
                proc.processBlock (buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < blockSize; ++s)
                    {
                        const float v = buffer.getSample (ch, s);
                        p = juce::jmax (p, std::abs (v));
                        s2 += (double) v * v; ++n;
                        if (std::abs (v) > 0.985f) ++clipped;
                    }
            }
            std::printf ("  all 4 voices @ OUT 60 %%: peak %.3f  rms %.3f  samples near clip %d\n",
                         p, (float) std::sqrt (s2 / (double) juce::jmax (1LL, n)), clipped);
            check (p < 0.99f, "four voices at the factory level do not clip");
        }

        proc.master.store (0.60f);
        proc.reverbMix.store (kRev);
        proc.delayMix.store (kDly);
        proc.setPlaying (false);
    }

    std::printf ("=====================================================\n");
    std::printf ("RESULT %s (%d failures)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
