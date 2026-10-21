#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <mutex>
#include <vector>

#include "StrumModel.h"
#include "StringSynth.h"

//==============================================================================
//  String Theory -- strummed-string ambience generator
//
//  Audio path :  ball physics -> pluck events -> Karplus-Strong string bank
//                -> body resonance -> tempo-synced ping-pong delay -> reverb
//
//  MIDI path  :  every pluck is also emitted as a note on the voice's own MIDI
//                channel, either to the host bus (VST3) or to a hardware port
//                (standalone).
//==============================================================================
class StringTheoryProcessor : public juce::AudioProcessor
{
public:
    StringTheoryProcessor();
    ~StringTheoryProcessor() override = default;

    //==== lifecycle ===========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==== editor ==============================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==== metadata ============================================================
    const juce::String getName() const override { return "String Theory"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==== model ===============================================================
    st::StrumWorld world;

    // ---- global parameters (message thread writes, audio thread reads) ------
    // defaults = the factory sound: a fairly wet ambient send, dotted-eighth
    // echo off, straight 1/8, output just under unity
    std::atomic<float> reverbMix { 0.67f };
    std::atomic<float> delayMix  { 0.55f };
    std::atomic<float> delayFb   { 0.30f };
    std::atomic<int>   delayDiv  { 0 };      // 0 = 1/8, 1 = 1/8D, 2 = 1/4, 3 = 1/4D, 4 = 1/2
    std::atomic<float> master    { 0.60f };
    // TENSION lives in `world.tension` -- the model owns it, so the editor, the
    // visuals and the synth all read one single value.

    //==== transport ===========================================================
    void setPlaying (bool on)
    {
        if (! on) panicMidi();
        world.playing.store (on);
    }
    bool isRunning() const { return world.playing.load(); }

    //==== voices ==============================================================
    void addVoice();
    void removeVoice (int index);
    int  voiceCount() const { return world.voiceCount; }
    void setVoiceRate  (int i, int r);
    void setVoiceRandomRate (int i, bool on);   // R: re-roll the division every few beats
    void setVoiceRand  (int i, double v);
    void setVoiceGain  (int i, double g);
    void setVoiceTimbre (int i, int t);

    // re-assign the six chord tones across the strings (RETUNE)
    void retuneStrings()      { world.retuneNow(); }
    void setRetuneBars (int bars) { world.retuneBars.store (juce::jlimit (0, 16, bars)); }
    void setVoiceOctave (int i, int o);
    void setVoiceChannel (int i, int ch);
    void setVoiceFollow (int i, bool f);
    void setVoiceMute (int i, bool m);
    void setVoiceSolo (int i, bool s);

    //==== MIDI out ============================================================
    juce::StringArray midiPorts() const;
    void setMidiRoute (const juce::String& routeId);   // "OFF" | "HOST" | device id
    juce::String getMidiRoute() const { return midiRoute; }
    void flushMidi();
    void panicMidi();

    //==== state ===============================================================
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    void fireNote (const st::StrumEvent& e);
    void renderSegment (float* l, float* r, int start, int num);

    st::StringSynth synth;
    juce::dsp::StateVariableTPTFilter<float> body;
    juce::Reverb reverb;

    double currentSampleRate = 48000.0;

    // delay
    std::vector<float> delayBufferL, delayBufferR;
    int delayWrite = 0, delaySize = 0;
    float delayDampL = 0.0f, delayDampR = 0.0f;

    juce::SmoothedValue<float> wetDelay, wetReverb, outGain;
    juce::AudioBuffer<float> scratch;    // dry copy for the reverb send

    // silence gate: after a couple of seconds of a silent bus the FX chain is
    // skipped entirely, so an idle instrument draws no CPU at all
    double silentSeconds = 0.0;
    bool   fxFrozen = false;
    float  prevOutPeak = 1.0f;

    // host / hardware MIDI
    std::vector<juce::MidiMessage> hostNoteOffs;    // parallel arrays are cheaper
    std::vector<int> hostNoteOffSamples;
    juce::MidiBuffer* hostMidi = nullptr;

    struct PendingMsg { double atMs; juce::MidiMessage msg; };
    std::vector<PendingMsg> pendingMidi;
    std::mutex midiMutex;
    std::unique_ptr<juce::MidiOutput> midiOutput;
    juce::String midiRoute { "HOST" };
    std::atomic<bool> midiPortOpen { false };

    double gateSeconds = 0.30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StringTheoryProcessor)
};
