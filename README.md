# String Theory

A string instrument plugin where **the rhythm is physics, not programming**.

Six strings lie across a disc. A ball moves on the disc. Every time the ball
crosses a string, a note sounds. Change how the ball moves, and the music
changes with it.

```
        ╭──────────────────────╮
        │        ╱│╲           │
        │      ╱  │  ╲         │
        │    ╱    │    ╲       │
        │  ╱      │      ●     │   ● the ball
        │╲────────┼────────╱   │   ─ the six strings
        │  ╲      │      ╱     │
        │    ╲    │    ╱       │
        │      ╲  │  ╱         │
        │        ╲│╱           │
        ╰──────────────────────╯
```

## Download

Grab the latest build from [Releases](https://github.com/ryanhsu2021/StringTheory/releases).

| Build | How to use |
|---|---|
| **VST3** (`...-Windows-VST3.zip`) | Unzip, copy `String Theory.vst3` into `C:\Program Files\Common Files\VST3\`, rescan in your DAW |
| **Standalone** (`...-Windows-Standalone.zip`) | Unzip and run `String Theory.exe` — no install, no runtime |

Windows 10/11 x64. The builds are unsigned, so SmartScreen may ask once —
*More info → Run anyway*.

## Controls

| Control | What it does |
|---|---|
| **RATE** | How fast the ball travels. This is the note density. |
| **PACE** | How fast the chord changes. |
| **PROB** | Chance that a crossing actually sounds. |
| **TENSION** | Harmonic brightness — how tense the voicing gets. |
| **QNTZ** | Pulls onsets onto the 1/16 grid. |
| **DELAY** | Tempo-synced echo. |
| **STRINGS** | Shows the pitch each string is tuned to. |

## How the tuning works

The six strings are not tuned to a scale in ascending order. They are dealt a
**true permutation** of six chord tones — six strings, six different pitches,
low on the inside and high on the outside, like a harp or a guitar's fretboard
seen from above. A retune periodically deals a new permutation, so the same
ball path keeps producing different melodies.

The bass voice always reaches the root. Extension tones avoid the classic
avoid-note clashes (no 11th over a major third).

## Build from source

Needs JUCE and a C++17 compiler. On Windows:

```bat
REM standalone only
build.bat

REM standalone + VST3
build.bat vst
```

JUCE is expected at `./JUCE` (a symlink is fine).

## Test

`build_test.bat` compiles the audio-side harness; run it to get a printed
report of the model's behaviour (chord tones, voicing ranges, quantisation
accuracy, ball physics, retune behaviour, and so on).

```bat
cd build
..\build_test.bat
StringAudioCheck_artefacts\Release\StringAudioCheck.exe
```

## Licence

Source is provided as-is for study and personal use.
