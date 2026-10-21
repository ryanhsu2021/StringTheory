#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

//==============================================================================
namespace
{
    const double kDivBeats[5] = { 0.25, 0.5, 0.75, 1.0, 2.0 };   // 1/16, 1/8, 1/8D, 1/4, 1/2

    // tanh without the libm call: Pade form, monotonic, exact at 0, saturates
    // at +-1 by |x| = 3 (where tanh is already 0.995). Two of these run on
    // every sample, so this is the cheapest real win in the master stage.
    inline float softClip (float x) noexcept
    {
        if (x <= -3.0f) return -1.0f;
        if (x >=  3.0f) return  1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
}

//==============================================================================
StringTheoryProcessor::StringTheoryProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), false)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    hostNoteOffs.reserve (1024);
    hostNoteOffSamples.reserve (1024);
    pendingMidi.reserve (512);
}

//==============================================================================
void StringTheoryProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    synth.prepare (currentSampleRate);
    world.prepare (currentSampleRate);

    juce::dsp::ProcessSpec spec { currentSampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    body.prepare (spec);
    body.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
    body.setCutoffFrequency (192.0f);
    body.setResonance (1.15f);
    body.reset();

    reverb.setSampleRate (currentSampleRate);
    reverb.reset();

    delaySize = juce::jmax (1024, (int) (currentSampleRate * 3.2));
    delayBufferL.assign ((size_t) delaySize, 0.0f);
    delayBufferR.assign ((size_t) delaySize, 0.0f);
    delayWrite = 0;
    delayDampL = delayDampR = 0.0f;

    wetDelay.reset (currentSampleRate, 0.05);
    wetReverb.reset (currentSampleRate, 0.08);
    outGain.reset (currentSampleRate, 0.05);
    wetDelay.setCurrentAndTargetValue (delayMix.load());
    wetReverb.setCurrentAndTargetValue (reverbMix.load());
    outGain.setCurrentAndTargetValue (master.load());

    hostNoteOffs.clear();
    hostNoteOffSamples.clear();
    pendingMidi.clear();

    silentSeconds = 0.0;
    fxFrozen = false;
    prevOutPeak = 1.0f;
}

void StringTheoryProcessor::releaseResources()
{
    synth.reset();
    panicMidi();
}

bool StringTheoryProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::disabled() || in == out;
}

//==============================================================================
void StringTheoryProcessor::renderSegment (float* l, float* r, int start, int num)
{
    if (num > 0)
        synth.render (l, r, start, num);
}

void StringTheoryProcessor::fireNote (const st::StrumEvent& e)
{
    const int ch   = juce::jlimit (1, 16, e.channel);
    const int note = juce::jlimit (0, 127, e.midi);
    const int velB = juce::jlimit (1, 127, (int) std::round (e.velocity * 127.0f));
    const float pan = ((float) e.stringIdx / (float) (st::kNumStrings - 1) - 0.5f) * 0.55f;

    if (world.internalSynth.load (std::memory_order_relaxed))
    {
        // the synth wants 1 = tight, but the knob is a softness control
        const float tight = 1.0f - st::slackOf (world.tension.load (std::memory_order_relaxed));
        synth.trigger (note, e.velocity, e.timbre, pan, tight, e.voice, e.slot);
    }

    if (midiRoute == "OFF")
        return;

    // ---- host / plugin MIDI out bus -----------------------------------------
    if (hostMidi != nullptr)
    {
        hostMidi->addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) velB), e.sampleOffset);
        hostNoteOffs.push_back (juce::MidiMessage::noteOff (ch, note));
        hostNoteOffSamples.push_back (e.sampleOffset + (int) (gateSeconds * currentSampleRate));

        if (hostNoteOffs.size() > 1000)     // safety valve, never leave hanging notes
        {
            for (size_t k = 0; k < hostNoteOffs.size(); ++k)
                hostMidi->addEvent (hostNoteOffs[k], 0);
            hostNoteOffs.clear();
            hostNoteOffSamples.clear();
        }
    }

    // ---- hardware port (standalone) ----------------------------------------
    if (midiPortOpen.load (std::memory_order_relaxed) && midiOutput != nullptr)
    {
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const std::unique_lock<std::mutex> sl (midiMutex, std::try_to_lock);
        if (sl.owns_lock() && pendingMidi.size() + 2 <= pendingMidi.capacity())
        {
            pendingMidi.push_back ({ nowMs, juce::MidiMessage::noteOn (ch, note, (juce::uint8) velB) });
            pendingMidi.push_back ({ nowMs + gateSeconds * 1000.0, juce::MidiMessage::noteOff (ch, note) });
        }
    }
}

//==============================================================================
void StringTheoryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    buffer.clear();
    hostMidi = &midiMessages;

    // ---- host transport ------------------------------------------------------
    bool hostPlaying = false;
    double hostBpm = 0.0, ppq = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm())        hostBpm = *b;
            if (auto p = pos->getPpqPosition()) ppq    = *p;
            hostPlaying = pos->getIsPlaying();
        }
    }

    const bool synced = world.synced.load (std::memory_order_relaxed);
    world.hostBpm.store (synced ? hostBpm : 0.0, std::memory_order_relaxed);
    world.hostBeat.store ((synced && hostPlaying) ? (int) std::floor (ppq) : -1,
                          std::memory_order_relaxed);
    // the quantiser needs the position inside the beat, not just the bar
    world.hostPpq.store ((synced && hostPlaying && ppq >= 0.0) ? ppq : -1.0,
                         std::memory_order_relaxed);

    // ---- physics -> pluck events --------------------------------------------
    st::StrumEvent events[160];
    int nEv = 0;
    {
        const juce::SpinLock::ScopedTryLockType sl (world.lock);
        if (sl.isLocked())
            world.update (numSamples / currentSampleRate, numSamples, events, nEv);
    }

    if (nEv > 1)
        std::sort (events, events + nEv, [] (const st::StrumEvent& a, const st::StrumEvent& b)
                                        { return a.sampleOffset < b.sampleOffset; });

    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : l;

    int cursor = 0;
    for (int i = 0; i < nEv; ++i)
    {
        const int off = juce::jlimit (0, numSamples - 1, events[i].sampleOffset);
        if (off > cursor) renderSegment (l, r, cursor, off - cursor);
        fireNote (events[i]);
        cursor = off;
    }
    if (cursor < numSamples) renderSegment (l, r, cursor, numSamples - cursor);

    // ---- host note-offs ------------------------------------------------------
    for (size_t i = hostNoteOffSamples.size(); i-- > 0;)
    {
        if (hostNoteOffSamples[i] < numSamples)
        {
            if (hostMidi != nullptr)
                hostMidi->addEvent (hostNoteOffs[i], juce::jmax (0, hostNoteOffSamples[i]));
            hostNoteOffs.erase (hostNoteOffs.begin() + (long) i);
            hostNoteOffSamples.erase (hostNoteOffSamples.begin() + (long) i);
        }
        else
        {
            hostNoteOffSamples[i] -= numSamples;
        }
    }

    // ---- silence gate --------------------------------------------------------
    // An idle instrument must cost nothing. Once the synth has nothing left to
    // play AND the bus has actually gone quiet (measured on the previous block,
    // so reverb and delay tails have decayed on their own), the whole FX chain
    // is skipped and zeros are returned. The first block that carries sound
    // again clears the frozen tails, so nothing stale ever replays.
    {
        const bool quiet = ! synth.anyActive() && prevOutPeak < 2.0e-4f;

        if (quiet)
        {
            silentSeconds += numSamples / currentSampleRate;

            if (silentSeconds > 0.5)
            {
                fxFrozen = true;
                buffer.clear();
                prevOutPeak = 0.0f;
                hostMidi = nullptr;
                return;
            }
        }
        else
        {
            silentSeconds = 0.0;

            if (fxFrozen)
            {
                fxFrozen = false;
                std::fill (delayBufferL.begin(), delayBufferL.end(), 0.0f);
                std::fill (delayBufferR.begin(), delayBufferR.end(), 0.0f);
                delayDampL = delayDampR = 0.0f;
                delayWrite = 0;
                reverb.reset();
            }
        }
    }

    // ---- body resonance ------------------------------------------------------
    const int chans = juce::jmin (2, buffer.getNumChannels());
    for (int ch = 0; ch < chans; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int s = 0; s < numSamples; ++s)
        {
            const float x = d[s];
            const float bp = body.processSample (ch, x);
            d[s] = x + 0.34f * bp;
        }
    }

    // ---- tempo-synced ping-pong delay ---------------------------------------
    const double useBpm = (synced && hostBpm > 1.0) ? hostBpm : world.bpm.load (std::memory_order_relaxed);
    const int div = juce::jlimit (0, 4, delayDiv.load (std::memory_order_relaxed));
    const double timeSec = juce::jlimit (0.02, 3.0, kDivBeats[div] * 60.0 / juce::jmax (20.0, useBpm));
    const int ds = juce::jlimit (1, delaySize - 1, (int) (timeSec * currentSampleRate));
    const float fb = juce::jlimit (0.0f, 0.82f, delayFb.load (std::memory_order_relaxed));

    wetDelay.setTargetValue (delayMix.load (std::memory_order_relaxed));
    wetDelay.skip (numSamples);
    const float dmix = wetDelay.getCurrentValue();

    if (delaySize > 0 && dmix > 0.0005f)
    {
        for (int s = 0; s < numSamples; ++s)
        {
            int rd = delayWrite - ds;
            if (rd < 0) rd += delaySize;

            float dl = delayBufferL[(size_t) rd];
            float dr = delayBufferR[(size_t) rd];

            delayDampL += 0.32f * (dl - delayDampL);
            delayDampR += 0.32f * (dr - delayDampR);

            const float inL = l[s], inR = r[s];
            delayBufferL[(size_t) delayWrite] = inL + delayDampR * fb;
            delayBufferR[(size_t) delayWrite] = inR + delayDampL * fb;

            delayWrite = (delayWrite + 1) % delaySize;

            l[s] = inL + dl * dmix;
            r[s] = inR + dr * dmix;
        }
    }
    else
    {
        // keep the buffers fed so switching the delay back on does not replay stale audio
        for (int s = 0; s < numSamples; ++s)
        {
            delayBufferL[(size_t) delayWrite] = l[s];
            delayBufferR[(size_t) delayWrite] = r[s];
            delayWrite = (delayWrite + 1) % delaySize;
        }
        delayDampL = delayDampR = 0.0f;
    }

    // ---- ambience reverb -----------------------------------------------------
    wetReverb.setTargetValue (reverbMix.load (std::memory_order_relaxed));
    wetReverb.skip (numSamples);
    const float rmix = wetReverb.getCurrentValue();

    juce::Reverb::Parameters rp;
    rp.roomSize = 0.86f;
    rp.damping  = 0.44f;
    rp.width    = 1.0f;
    rp.wetLevel = juce::jlimit (0.0f, 0.9f, rmix);
    rp.dryLevel = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);

    if (buffer.getNumChannels() > 1) reverb.processStereo (l, r, numSamples);
    else                             reverb.processMono (l, numSamples);

    // ---- master --------------------------------------------------------------
    outGain.setTargetValue (master.load (std::memory_order_relaxed));
    outGain.skip (numSamples);
    const float g = outGain.getCurrentValue();
    for (int s = 0; s < numSamples; ++s)
    {
        l[s] = softClip (l[s] * g) * 0.92f;
        if (r != l) r[s] = softClip (r[s] * g) * 0.92f;
    }

    prevOutPeak = buffer.getMagnitude (0, numSamples);
    hostMidi = nullptr;
}

//==============================================================================
void StringTheoryProcessor::addVoice()
{
    world.setVoiceCount (world.voiceCount + 1);
}

void StringTheoryProcessor::removeVoice (int index)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (world.voiceCount <= 2) return;          // harmony + at least one ball
    index = juce::jlimit (0, world.voiceCount - 1, index);
    if (index == 0) return;                     // the harmony voice is fixed

    for (int i = index; i < world.voiceCount - 1; ++i)
        world.balls[(size_t) i] = world.balls[(size_t) (i + 1)];

    world.balls[(size_t) (world.voiceCount - 1)] = st::BallState();
    world.voiceCount--;
}

void StringTheoryProcessor::setVoiceRate (int i, int r)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
    {
        auto& b = world.balls[(size_t) i];
        b.rate  = juce::jlimit (0, st::kNumRates - 1, r);
        b.speed = st::rateSpeed (b.rate, world.bpm.load());
    }
}

void StringTheoryProcessor::setVoiceRandomRate (int i, bool on)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
    {
        auto& b = world.balls[(size_t) i];
        b.randomRate = on;
        // arming counts as "lurch now": the first jump happens at once, the
        // ones after it land a random couple of beats apart
        b.rateJumpIn = 0.0;
    }
}

void StringTheoryProcessor::setVoiceRand (int i, double v)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
        world.balls[(size_t) i].rand = juce::jlimit (0.0, 1.0, v);
}

void StringTheoryProcessor::setVoiceGain (int i, double g)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
        world.balls[(size_t) i].gain = (float) juce::jlimit (0.0, 1.0, g);
}

void StringTheoryProcessor::setVoiceTimbre (int i, int t)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
    {
        world.balls[(size_t) i].timbre = juce::jlimit (0, st::timbreCount() - 1, t);
        // the old patch would ring on behind the new one for bars -- let it go
        synth.releaseOwned (i);
    }
}

void StringTheoryProcessor::setVoiceOctave (int i, int o)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
        world.balls[(size_t) i].octave = juce::jlimit (-2, 2, o);
}

void StringTheoryProcessor::setVoiceChannel (int i, int ch)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
        world.balls[(size_t) i].channel = juce::jlimit (1, 16, ch);
}

void StringTheoryProcessor::setVoiceFollow (int i, bool f)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
        world.balls[(size_t) i].follow = f;
}

void StringTheoryProcessor::setVoiceMute (int i, bool m)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
        world.balls[(size_t) i].mute = m;
}

void StringTheoryProcessor::setVoiceSolo (int i, bool s)
{
    const juce::SpinLock::ScopedLockType sl (world.lock);
    if (juce::isPositiveAndBelow (i, world.voiceCount))
        world.balls[(size_t) i].solo = s;
}

//==============================================================================
juce::StringArray StringTheoryProcessor::midiPorts() const
{
    juce::StringArray names;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        names.add (d.name);
    return names;
}

void StringTheoryProcessor::setMidiRoute (const juce::String& routeId)
{
    if (midiRoute == routeId) return;

    panicMidi();
    midiRoute = routeId;

    if (midiOutput != nullptr)
    {
        midiOutput->stopBackgroundThread();
        midiOutput.reset();
    }
    midiPortOpen.store (false);

    if (routeId.isEmpty() || routeId == "HOST" || routeId == "OFF")
        return;

    for (auto& d : juce::MidiOutput::getAvailableDevices())
    {
        if (d.identifier == routeId)
        {
            midiOutput = juce::MidiOutput::openDevice (d.identifier);
            if (midiOutput != nullptr)
            {
                midiOutput->startBackgroundThread();
                midiPortOpen.store (true);
            }
            break;
        }
    }
}

void StringTheoryProcessor::flushMidi()
{
    if (! midiPortOpen.load() || midiOutput == nullptr) return;

    const double now = juce::Time::getMillisecondCounterHiRes();
    std::vector<PendingMsg> due;

    {
        const std::lock_guard<std::mutex> sl (midiMutex);
        for (size_t i = 0; i < pendingMidi.size();)
        {
            if (pendingMidi[i].atMs <= now)
            {
                due.push_back (pendingMidi[i]);
                pendingMidi.erase (pendingMidi.begin() + (long) i);
            }
            else ++i;
        }
    }

    for (auto& m : due)
        midiOutput->sendMessageNow (m.msg);
}

void StringTheoryProcessor::panicMidi()
{
    if (hostMidi != nullptr)
        for (size_t i = 0; i < hostNoteOffs.size(); ++i)
            hostMidi->addEvent (hostNoteOffs[i], 0);

    hostNoteOffs.clear();
    hostNoteOffSamples.clear();

    if (midiOutput != nullptr)
    {
        const std::lock_guard<std::mutex> sl (midiMutex);
        for (auto& m : pendingMidi)
            if (m.msg.isNoteOn()) midiOutput->sendMessageNow (juce::MidiMessage::noteOff (m.msg.getChannel(), m.msg.getNoteNumber()));
        pendingMidi.clear();
    }
}

//==============================================================================
void StringTheoryProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement xml ("StringTheory");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("bpm", world.bpm.load());
    xml.setAttribute ("synced", world.synced.load() ? 1 : 0);
    xml.setAttribute ("internal", world.internalSynth.load() ? 1 : 0);
    xml.setAttribute ("progression", world.progression.load());
    xml.setAttribute ("reverb", reverbMix.load());
    xml.setAttribute ("delay", delayMix.load());
    xml.setAttribute ("feedback", delayFb.load());
    xml.setAttribute ("div", delayDiv.load());
    xml.setAttribute ("master", master.load());
    xml.setAttribute ("tension", world.tension.load());
    xml.setAttribute ("stringMap", (int) world.stringMap.load());
    xml.setAttribute ("retuneBars", world.retuneBars.load());
    xml.setAttribute ("paceBars", world.paceBars.load());
    xml.setAttribute ("midiRoute", midiRoute);

    {
        const juce::SpinLock::ScopedLockType sl (world.lock);
        xml.setAttribute ("voices", world.voiceCount);
        for (int i = 0; i < world.voiceCount; ++i)
        {
            const auto& b = world.balls[(size_t) i];
            auto* v = xml.createNewChildElement ("voice");
            v->setAttribute ("speed", b.speed);
            v->setAttribute ("rate", b.rate);
            v->setAttribute ("rand", b.rand);
            v->setAttribute ("gain", b.gain);
            v->setAttribute ("timbre", b.timbre);
            v->setAttribute ("octave", b.octave);
            v->setAttribute ("channel", b.channel);
            v->setAttribute ("follow", b.follow ? 1 : 0);
            v->setAttribute ("randomRate", b.randomRate ? 1 : 0);
            v->setAttribute ("mute", b.mute ? 1 : 0);
            v->setAttribute ("solo", b.solo ? 1 : 0);
        }
    }

    copyXmlToBinary (xml, destData);
}

void StringTheoryProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("StringTheory")) return;

    world.bpm.store ((double) xml->getDoubleAttribute ("bpm", 60.0));
    world.synced.store (xml->getIntAttribute ("synced", 0) != 0);
    world.internalSynth.store (xml->getIntAttribute ("internal", 1) != 0);
    world.progression.store (juce::jlimit (0, st::progressionCount() - 1, xml->getIntAttribute ("progression", 0)));
    reverbMix.store ((float) xml->getDoubleAttribute ("reverb", 0.36));
    delayMix.store ((float) xml->getDoubleAttribute ("delay", 0.24));
    delayFb.store ((float) xml->getDoubleAttribute ("feedback", 0.36));
    delayDiv.store (juce::jlimit (0, 4, xml->getIntAttribute ("div", 1)));
    master.store ((float) xml->getDoubleAttribute ("master", 0.85));
    world.tension.store ((float) juce::jlimit (0.0, 1.0, xml->getDoubleAttribute ("tension", 0.5)));
    world.paceBars.store (juce::jlimit (0, 8, xml->getIntAttribute ("paceBars", 1)));

    const int voices = juce::jlimit (1, st::kMaxVoices, xml->getIntAttribute ("voices", 4));
    world.setVoiceCount (voices);

    {
        const juce::SpinLock::ScopedLockType sl (world.lock);
        int i = 0;
        for (auto* v : xml->getChildWithTagNameIterator ("voice"))
        {
            if (i >= world.voiceCount) break;
            auto& b = world.balls[(size_t) i];
            b.speed   = juce::jlimit (0.25, 3.0, v->getDoubleAttribute ("speed", b.speed));
            b.rate    = juce::jlimit (0, st::kNumRates - 1,
                                      v->getIntAttribute ("rate", st::rateFromSpeed (b.speed)));
            b.rand    = juce::jlimit (0.0, 1.0, v->getDoubleAttribute ("rand", b.rand));
            b.gain    = (float) juce::jlimit (0.0, 1.0, v->getDoubleAttribute ("gain", b.gain));
            b.timbre  = juce::jlimit (0, st::timbreCount() - 1, v->getIntAttribute ("timbre", b.timbre));
            b.octave  = juce::jlimit (-2, 2, v->getIntAttribute ("octave", b.octave));
            b.channel = juce::jlimit (1, 16, v->getIntAttribute ("channel", b.channel));
            b.follow  = v->getIntAttribute ("follow", 1) != 0;
            b.randomRate = v->getIntAttribute ("randomRate", 0) != 0;
            b.rateJumpIn = 0.0;
            b.mute    = v->getIntAttribute ("mute", 0) != 0;
            b.solo    = v->getIntAttribute ("solo", 0) != 0;
            ++i;
        }
    }

    {
        const juce::SpinLock::ScopedLockType sl (world.lock);
        const int sm = xml->getIntAttribute ("stringMap", -1);
        if (sm >= 0) world.adoptStringMap ((uint32_t) sm);
        world.retuneBars.store (juce::jlimit (0, 16, xml->getIntAttribute ("retuneBars", 0)));
    }

    setMidiRoute (xml->getStringAttribute ("midiRoute", "HOST"));
}

//==============================================================================
juce::AudioProcessorEditor* StringTheoryProcessor::createEditor()
{
    return new StringTheoryEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StringTheoryProcessor();
}
