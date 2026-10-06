# CHOMPI x0x

A TB-303 / x0xb0x-style bass line machine for the CHOMPI: one 303-style voice and a 16-step sequencer you program a step at a time or record into live. No menus: every control is a key, a knob or the toggle.

Built from scratch on top of CHOMPI Club's TEMPO firmware, whose hardware layer, libraries and startup it keeps.

> Status: **complete first version, not yet tried on hardware.** The voice, sequencer and every panel behaviour are tested on the desktop (`host/`), and the firmware builds without warnings. Expect the feel (filter, accent, LEDs) to need tuning once played.

## The toggle switch: two modes

**Up: STEP mode.** Program the pattern step by step.
**Down: PITCH mode.** Play live, and record into the pattern while it runs.

PLAY runs and stops the pattern in both.

## Step mode

| Do | Does |
|---|---|
| White keys 1-8 | Steps 1-8 (or 9-16: LOOP flips; LOOP lit green = 9-16) |
| Tap a step | Select it |
| Tap the selected step again | Turn it on or off (its note is kept) |
| Hold CHOMPI | The keybed becomes a two-octave keyboard (C3-C5): the key you press is the selected step's note, and the step turns on. The selected step's key flashes |
| Black C# / D# (keys 1-2) | Octave DOWN / UP page |
| Black F# / G# / A# (keys 3-5) | ACCENT / SLIDE / TIE page |
| Black C# (key 6) | PATTERN page: step keys pick pattern 1-8 (9-16 with LOOP) |
| Black D# (key 7) | LENGTH page: a step key makes it the last step |
| Black F# (key 8) | Saw / square (amber = saw, cyan = square) |
| Black G# (key 9) | COPY: hold it and press a step key to copy this pattern to that pattern number |
| Black A# (key 10) | CLEAR: tap clears the selected step; hold 1 s clears the whole pattern |

**Pages.** On a parameter page the step keys toggle that setting for each step, lit in the page's colour (DOWN purple, UP cyan, ACCENT orange, SLIDE blue, TIE green). The page's key again goes back to the notes page.

**Lights.** On the notes page, steps that are on are red, the selected one brightest; a selected empty step is dim white. The playing step flashes white.

**Patterns.** While the pattern runs, a new one waits for the end of the bar (it blinks); press it again to switch at once. Stopped, it switches at once.

## Pitch mode

| Do | Does |
|---|---|
| Keys | Play the voice live (C3-C5). Overlapping notes slide |
| LOOP | Record on/off (LOOP red). While the pattern runs, a played note goes to the nearest step; a note held across steps ties through them |
| CHOMPI + key | Transpose the pattern: middle C = none, up to an octave either way |

Live notes take over the voice from the pattern while you hold them.

## Knobs (both modes)

| Knob | Turn | CHOMPI + turn |
|---|---|---|
| Knob 1 | Resonance | Tuning (+/- 1 semitone) |
| Knob 2 | Env mod | Swing |
| Knob 3 | Decay | Drive |
| Knob 4 | Accent | Slide time |
| Big purple | Cutoff | |
| Volume | Volume | Tempo, 1 BPM a click (60-200) |

Click a knob to set it back to its default (CHOMPI + click: its second function). CHOMPI + LOOP is tap tempo.

## The voice

One oscillator (saw, or square) into a resonant 3-pole ladder low-pass (about 18 dB/octave, run at twice the sample rate), a decay-only filter envelope, a gated amp envelope and a soft drive. Accent makes a step louder, gives the filter envelope its shortest decay, and charges an accent "capacitor" that drains slowly, so accents in a row build up. Slide holds the gate into the next step and glides there. Gates are half a step; ties and slides hold them.

## MIDI (DIN and USB)

| | In | Out |
|---|---|---|
| Clock | Followed when it arrives (the pattern then runs on it) | Sent while running on the internal clock |
| Start / Stop / Continue | Run, stop, resume | Sent with PLAY |
| Notes | Play the voice live (and record in pitch mode); velocity 112+ = accent | The pattern's and your notes, slides as overlapping notes |
| CC | 74 cutoff, 71 resonance, 12 env mod, 13 decay, 14 accent, 7 volume, 15 tuning, 16 swing, 17 drive, 5 slide time | The same CCs when knobs move (off by default) |

Channels and on/off switches are in `/X0X/options.txt`.

## On the card (`/X0X`)

| File | Holds |
|---|---|
| `patterns.txt` | The 16 patterns, plain text: one `step` line per step (note, octave, on, accent, slide, tie) |
| `current.txt` | The knobs, the waveform and the selected pattern, restored at power-on |
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

`tests` checks sequencer timing to the sample, slides, ties, swing, the external clock, pattern queueing, every step-mode and pitch-mode panel behaviour, recording, tap tempo and the file formats. `render` writes demo patterns (saw, square, accents building up, slides, swing) and checks they stay finite and in range.

## Flashing

1. Copy `CHOMPI.bin` to the root of the SD card. It must be the only `.bin` there.
2. Power on. The rainbow shows while the bootloader installs it, then a red sweep across the keys.

A CHOMPI that has never had the bootloader needs `bin/install_bootloader.sh` once first. One running any stock firmware already has it. To go back, use a card with a stock firmware's `.bin`.

## Layout of this repo

| Path | What |
|---|---|
| `code/src/x0x/voice.h` | The 303-style voice |
| `code/src/x0x/sequencer.h` | Step timing, gates, slides, ties, swing, clock |
| `code/src/x0x/pattern.h` | Patterns and their text format |
| `code/src/x0x/params.h` | Knob parameters, settings and MIDI options |
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
