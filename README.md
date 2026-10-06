# CHOMPI x0x

A TB-303 / x0xb0x-style bass line machine for the CHOMPI: one 303-style voice and a 16-step sequencer you program a step at a time or record into live. No menus: every control is a key, a knob or the toggle.

Built from scratch on top of CHOMPI Club's TEMPO firmware, whose hardware layer, libraries and startup it keeps.

> Status: **complete first version, not yet tried on hardware.** The voice, sequencer and every panel behaviour are tested on the desktop (`host/`), and the firmware builds without warnings. Expect the feel (filter, accent, LEDs) to need tuning once played.

## The toggle switch: two modes

**Up: STEP mode.** Program the pattern step by step.
**Down: PITCH mode.** Play live, record in real time while it runs, or enter steps one note at a time while it's stopped.

PLAY runs and stops the pattern in both.

## The 16 steps on 15 white keys

White keys 1-7 are steps 1-7 and white keys 9-15 are steps 10-16. Middle C (white key 8) is step 8 or step 9. The half in view (steps 1-8 or 9-16) is lit normally and the other half dimmed. The view follows the half you last pressed a key in, and while the pattern runs it follows the playhead once the second half has notes in it. D#4 flips it by hand (CHOMPI + D#4 in pitch mode).

**Step lights:** on = red, accent = bright red, tie = dim red; steps past the pattern's length dimmer still.

## Step mode

| Do | Does |
|---|---|
| White key | Select that step |
| The selected step again | Turn it on or off (its note is kept) |
| Hold CHOMPI | The keybed becomes a two-octave keyboard (C3-C5): the key you press is the selected step's note, and the step turns on. The selected step flashes |
| Black C#3 / D#3 (keys 1-2) | Octave DOWN / UP page |
| Black F#3 / G#3 / A#3 (keys 3-5) | ACCENT / SLIDE / TIE page |
| Black C#4 (key 6) | Transpose mode on/off: while on, any key sets the transpose (middle C = none), lit yellow |
| Black D#4 (key 7) | View steps 1-8 / 9-16 |
| Black F#4 (key 8) | PATTERN page: the step keys pick pattern 1-16 |
| Black G#4 (key 9) | COPY: hold it and press a step key to copy this pattern to that pattern number |
| Black A#4 (key 10) | CLEAR: tap clears the selected step; hold 1 s clears the whole pattern |
| LOOP | Tap tempo |

**Pages.** On a parameter page the step keys toggle that setting for each step, lit in the page's colour (DOWN purple, UP cyan, ACCENT orange, SLIDE blue, TIE green). The page's key again goes back to the notes page.

**Lights.** The selected step is whitened (a selected empty step is dim white). The playing step flashes white.

**Patterns.** While the pattern runs, a new one waits for the end of the bar (it blinks); press it again to switch at once.

## Pitch mode

| Do | Does |
|---|---|
| Keys | Play the voice live. Overlapping notes slide |
| LOOP (tap) | Record on/off (LOOP red) |
| LOOP (hold 2 s) | Clear the pattern (LOOP fills red as you hold) |
| CHOMPI + C#3 / D#3 | Live keyboard an octave down / up (one each way) |
| CHOMPI + F#3 / G#3 / A#3 | Accent / slide / tie (see below) |
| CHOMPI + C#4 | Transpose mode on/off: while on, keys set the transpose instead of playing (middle C = none). C#4 dims and the amount's key lights yellow |
| CHOMPI + D#4 | View steps 1-8 / 9-16 |
| CHOMPI + F#4 | Arpeggiator on/off (CHOMPI lit teal while on) |
| CHOMPI + G#4 | Arpeggiator latch: keeps going after you let go; a fresh chord replaces it |
| CHOMPI + A#4 | Step input: add a rest |

The step lights only show while the pattern plays or you're recording; otherwise the keybed is just a keyboard.

**Recording while running.** A played note goes into the pattern; a note held across steps ties through them. With quantize on (knob 3, page 2) notes land on the grid: every step (1/16), every other (1/8) or every fourth (1/4). With quantize off they keep their timing within the step, to 1/24 of a beat. CHOMPI + F#3 / G#3 / A#3 toggle accent, slide or tie on the step playing now. For octave jumps, shift the keyboard with CHOMPI + C#3 / D#3 and play.

**Step input (record on, stopped).** Arming LOOP while stopped starts at step 1. Each note you play fills the next step (in the keyboard's octave), its white key lights up, and the pattern grows to it. CHOMPI + F#3 / G#3 put an accent / slide on the last step, CHOMPI + A#3 adds a tie step, CHOMPI + A#4 a rest. Press PLAY with record still on to carry on recording in real time.

**Arpeggiator.** With it on, the keys you hold are played one at a time in sixteenths at the tempo: up, down, up-down, random or as played, over 1-3 octaves (knob 3, page 3). While the pattern runs it locks to the pattern's steps (and swing), and the pattern is silent while it plays; with record on, the arpeggio is written into the steps. In step input the keys go into the steps as usual.

## Knobs

Click knobs 1-4 to step through their pages (knob 3 has three, knob 4 four); each page has its own colour, and a knob never goes darker than a fifth, so you can always see which page it's on. CHOMPI + click sets both functions of that knob's page back to their defaults (pattern length back to 16).

| Knob | Page 1 | Page 1 + CHOMPI | Page 2 | Page 2 + CHOMPI |
|---|---|---|---|---|
| Knob 1 | Saw / square (amber / cyan) | Pulse width | Pattern length | Tuning (+/- 1 semitone) |
| Knob 2 | Env mod | Decay | Accent | Slide time |
| Knob 3 | Tempo, 1 BPM a click (60-200) | Swing | Quantize on/off | Quantize grid: 1/16, 1/8, 1/4 |
| Knob 3, page 3 | Arpeggiator mode: up, down, up-down, random, as played | Arpeggiator range: 1-3 octaves | | |
| Knob 4 | Delay dry/wet (cyan) | Delay time: 1/16, 1/8, 3/16, 1/4, 3/8, 1/2 | Tape feedback (amber) | Tape tone: dark to bright |
| Knob 4, page 3 | Chorus into flanger (pink) | Stereo width / depth | | |
| Knob 4, page 4 | Bit depth (green) | Sample-rate reduction | | |
| Big purple | Cutoff | Resonance | | |
| Volume | Volume | Drive (light orange to red) | | |

Big purple knob click: tap tempo. Volume knob click: stop any stuck live notes.

## The effects

In this order after the voice, each off at zero:

- **Drive** (CHOMPI + volume): a pedal-style hard clipper in the spirit of a DS-1: a high-pass tightens the low end, up to ~60x gain hits a nearly hard, slightly asymmetric clip, and a tone low-pass takes the fizz off. The level is evened out as it turns up.
- **Bit crusher** (knob 4, page 4): bit depth 16 down to 4, and sample rate down to 1/32, separately.
- **Chorus / flanger** (knob 4, page 3): the first half of the turn is chorus, the second half flanger with rising feedback; width spreads the sides apart and deepens it.
- **Tape delay** (knob 4, pages 1-2): tempo-synced ping-pong. Dry/wet: the middle is 50/50, the top all echoes. Every repeat goes through tape EQ (a low-pass set by tone, a high-pass) and saturation, so repeats darken as they fade, with a little wow and flutter. Feedback goes just past self-oscillation at the top, where the saturation holds it.

A soft limiter keeps the output in range however hard they're pushed.

## MIDI (DIN and USB)

| | In | Out |
|---|---|---|
| Clock | Followed when it arrives (the pattern then runs on it) | Sent while running on the internal clock |
| Start / Stop / Continue | Run, stop, resume | Sent with PLAY |
| Notes | Play the voice live (and record in pitch mode); velocity 112+ = accent | The pattern's and your notes, slides as overlapping notes |
| CC | 74 cutoff, 71 resonance, 12 env mod, 13 decay, 14 accent, 5 slide time, 70 wave, 77 pulse width, 15 tuning, 16 swing, 91 delay mix, 92 delay time, 94 delay feedback, 95 delay tone, 93 chorus/flanger, 18 bit crush, 19 sample-rate crush, 7 volume, 17 drive | The same CCs when knobs move (off by default) |

Channels and on/off switches are in `/X0X/options.txt`.

## On the card (`/X0X`)

| File | Holds |
|---|---|
| `patterns.txt` | The 16 patterns, plain text: one `step` line per step (note, octave, on, accent, slide, tie, nudge) |
| `current.txt` | Every knob setting and the selected pattern, restored at power-on |
| `options.txt` | MIDI channels (1-16) and which MIDI in/out is on; written with the defaults the first time |

Patterns save 2 s after the last edit and settings 3 s after the last change. A fresh card starts with a demo pattern in pattern 1. CHOMPI blinking red three times at power-on means the card didn't mount; it then runs without saving.

CHOMPI + PLAY + LOOP held at power-on puts the battery in shipping mode, as in the stock firmwares.

## Building

Toolchain: Arm GNU Embedded Toolchain **10.3-2021.10**, on your PATH. Build the libraries once, then the firmware:

```bash
make -C code/libs/libDaisy
make -C code/libs/DaisySP
cd code/src && make
```

The output is `code/src/build/CHOMPI.bin`.

## Testing on the desktop

`code/src/x0x/` is the whole instrument without the hardware: plain C++ that also builds on a computer. `host/` tests it and renders it to WAV:

```bash
make -C host
host/tests
mkdir -p host/out && host/render host/out
```

`tests` checks sequencer timing to the sample, slides, ties, nudged steps, swing, the external clock, pattern queueing, the shared middle C, every step-mode and pitch-mode panel behaviour, real-time recording with each quantize setting, step input, the knob pages, tap tempo and the file formats. `render` writes demo patterns (saw, square, pulse width, accents building up, slides, each effect, swing) and checks they stay finite and in range.

## Flashing

1. Copy `CHOMPI.bin` to the root of the SD card. It must be the only `.bin` there.
2. Power on. The rainbow shows while the bootloader installs it, then a red sweep across the keys.

A CHOMPI that has never had the bootloader needs `bin/install_bootloader.sh` once first. One running any stock firmware already has it. To go back, use a card with a stock firmware's `.bin`.

## Layout of this repo

| Path | What |
|---|---|
| `code/src/x0x/voice.h` | The 303-style voice |
| `code/src/x0x/fx.h` | Bit crusher, doubler / chorus / flanger, delay |
| `code/src/x0x/arp.h` | The arpeggiator |
| `code/src/x0x/sequencer.h` | Step timing, gates, slides, ties, swing, clock |
| `code/src/x0x/pattern.h` | Patterns and their text format |
| `code/src/x0x/params.h` | Knob parameters and pages, settings and MIDI options |
| `code/src/x0x/machine.h` | The instrument: patterns, voice, sequencer, live play, recording, MIDI out queue |
| `code/src/x0x/ui.h` | What every control does and every LED shows |
| `code/src/panel.h` | CHOMPI switches and LEDs to `x0x/ui.h` |
| `code/src/midi_io.h` | MIDI in and out, DIN and USB |
| `code/src/storage.h` | The card |
| `code/src/chompi_main.cpp` | Startup, audio interrupt, main loop |
| `code/src/hardware.h`, `encoder.*`, `temp_led_stuff.h`, `chompi_sram.lds` | TEMPO's hardware layer, unchanged |
| `code/libs/` | libDaisy, DaisySP, coreJSON as TEMPO vendors them |
| `host/` | Desktop tests and renders |
| `bin/` | TEMPO's bootloader binary and install script |

The first commit is TEMPO v1.0 as released, so `git diff` against it shows everything this firmware changes.

## Credits

- Built on CHOMPI Club's open-source TEMPO firmware (MIT); see `LICENSE`, `THIRD_PARTY.md` and `TRADEMARKS.md`. The CHOMPI name belongs to CHOMPI Club, and this is a community firmware, not an official release.
- Key, LED and knob tables, and fixes for SD card cache alignment, codec start-up and button edges, from hiwatts' POLY ([sfaber02/chompi-poly](https://github.com/sfaber02/chompi-poly), MIT). TEMPO's MidiManager is the model for the DMA MIDI out.
