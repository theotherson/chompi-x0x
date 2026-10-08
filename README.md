# CHOMPI x0x

A TB-303 / x0xb0x-style bass line machine for the CHOMPI: one 303-style voice and a 16-step sequencer you program a step at a time or record into live. No menus: every control is a key, a knob or the toggle.

Built from scratch on top of CHOMPI Club's TEMPO firmware, whose hardware layer, libraries and startup it keeps.

> Status: **complete first version, not yet tried on hardware.** The voice, sequencer and every panel behaviour are tested on the desktop (`host/`), and the firmware builds without warnings. Expect the feel (filter, accent, LEDs) to need tuning once played.

## The toggle switch: two modes

**Up: STEP mode.** Program the pattern step by step.
**Down: PITCH mode.** Play live, record in real time while it runs, or enter steps one note at a time while it's stopped.

PLAY runs and stops the pattern in both.

## Bass and drums: double-tap CHOMPI

Tap CHOMPI twice quickly (each tap short, nothing else touched in between) to swap the panel between the bass and the drums. Both always play; the swap only picks which one the keys and knobs edit. The keybed flashes the new side's colour (red for the bass, amber for the drums), and the CHOMPI light stays amber while you're on the drums. A shift combination never counts, however fast: a press only counts as a tap if no key, knob or button was used during it.

The drums (a TR-606) aren't built yet. On their side, for now, the keys, knobs 1-4 and LOOP do nothing, so the bass can't be changed by accident; PLAY, tap tempo and the volume knob (volume, drive, tempo, swing) are shared and work on both sides.

## The 16 steps on 15 white keys

White keys 1-7 are steps 1-7 and white keys 9-15 are steps 10-16. Middle C (white key 8) is step 8 or step 9. The half in view (steps 1-8 or 9-16) is lit normally and the other half dimmed. The view follows the half you last pressed a key in, and while the pattern runs it follows the playhead once the second half has notes in it. D#4 flips it by hand (CHOMPI + D#4 in pitch mode's note entry).

**Step lights:** on = red, accent = bright red, tie = dim red; steps past the pattern's length dimmer still.

## Step mode

| Do | Does |
|---|---|
| White key | Select that step |
| The selected step again | Turn it on or off (its note is kept) |
| Hold CHOMPI | The keybed becomes a two-octave keyboard (C3-C5): the key you press is the selected step's note, and the step turns on. The selected step flashes |
| Black C#3 / D#3 (keys 1-2) | Octave DOWN / UP page |
| Black F#3 / G#3 / A#3 (keys 3-5) | ACCENT / SLIDE / TIE page |
| Black C#4 (key 6) | Transpose mode: every key sets the transpose (middle C = none; C#4 itself = +1), its key lit yellow; the white keys show only the playhead. Tap any key twice quickly to set that transpose and leave; or hold C#4 for 2 s to leave (the hold doesn't change the transpose) |
| Black D#4 (key 7) | View steps 1-8 / 9-16 |
| Black F#4 (key 8) | PATTERN. Tap: the pattern page on / off. Each pattern number 1-16 has an A and a B side, as on a TB-303, and each key shows its own side: A light blue, B yellow (the current pattern bright, a queued one blinking, used ones dim). On the page a step key picks that number when you let go, on the side it shows; the current number's key again flips it, 1A to 1B and back, and only that key changes colour. Running, a change waits for the bar, and the queued key again switches at once. No playhead or dimmed half there, even while running; middle C is pattern 8 or 9 by the half you last chose. **Hold a pattern key 2 s** on the page to export every pattern as a MIDI file (see below); the key fills white, then all keys flash white (red if the card failed). **Hold PATTERN 2 s**: write protect on/off. While it's on you can edit freely, but nothing is saved to the card, so the next power-on loads your patterns as they were. Every light flashes magenta when it goes on (light blue when it goes off), and the pattern colours turn magenta (A) and orange (B). Turning it off keeps what you have now, which then saves as usual. It stays on across restarts |
| Black G#4 (key 9) | COPY: hold it and press a step key to copy this pattern to that number (on the pattern page, to the side that number shows; elsewhere, to this pattern's side). COPY + PATTERN copies it to its own other side, A to B or B to A: the quick way to start a variation |
| Black A#4 (key 10) | CLEAR (red): tap clears the selected step; hold 1 s clears the whole pattern (the key fills red as you hold) |
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
| CHOMPI + C#4 | Transpose mode on/off: while on, keys set the transpose instead of playing (middle C = none). C#4 dims and the amount's key lights yellow. A key tapped twice quickly also sets that transpose and leaves |
| CHOMPI + D#4 | Tap: quantize on/off (lit blue when on). Hold 2 s: quantize the pattern for good to the current grid: recorded notes move onto it and their timing is dropped (the key fills light blue as you hold, then all keys flash light blue). There's no undo. While it's on, white keys 1-3 show the grid and CHOMPI + white key 1 / 2 / 3 picks it: 1/16, 1/8, 1/4. In note entry (record on, stopped) it views steps 1-8 / 9-16 instead |
| CHOMPI + PLAY | Arpeggiator on/off. While CHOMPI is held, PLAY shows it: cyan on, dim cyan off (and CHOMPI is lit teal while it's on) |
| CHOMPI + LOOP | Arpeggiator latch (LOOP orange while CHOMPI is held and it's latched): keeps going after you let go; a fresh chord replaces it |
| CHOMPI + F#4 / A#4 | Arpeggiator octaves below / above the chord, 0-2 each, cycling. The keys are purple (down) and cyan (up) like the keyboard octave keys, brighter for more. White keys 1-5 show them for a moment (octaves -2 to +2: the chord's own white, those below purple, those above cyan) |
| CHOMPI + G#4 | Arpeggiator pattern: up, down, up-down, random, as played, cycling (white keys 1-5 show which) |

The step lights only show while the pattern plays or you're recording; otherwise the keybed is just a keyboard.

**Recording while running.** A played note goes into the pattern; a note held across steps ties through them. With quantize on (CHOMPI + D#4) notes land on the grid: every step (1/16), every other (1/8) or every fourth (1/4). With quantize off they keep their timing within the step, to 1/24 of a beat. Turning quantize on also plays notes already recorded that way on the grid (each to its nearest grid point, so a note played just early lands on the beat it was aiming for); turn it off and their timing comes back, as nothing in the pattern changes (until you hold CHOMPI + D#4 for 2 s, which makes it permanent). Only notes with recorded timing move: notes right on their step, like everything entered in step mode, stay put. If two land on one step the nearer plays, and a note already there wins. CHOMPI + F#3 / G#3 / A#3 toggle accent, slide or tie on the step playing now. For octave jumps, shift the keyboard with CHOMPI + C#3 / D#3 and play.

**Note entry (record on, stopped).** Arming LOOP while stopped starts at step 1. Each note you play fills the next step (in the keyboard's octave), its white key lights up, and the pattern grows to it. CHOMPI + F#3 / G#3 put an accent / slide on the last step, CHOMPI + A#3 adds a tie step, CHOMPI + the blinking next step's white key a rest (rests you've entered show dim grey, the one just entered brighter). Press PLAY with record still on to carry on recording in real time.

**Arpeggiator.** With it on, the keys you hold are played one at a time in sixteenths at the tempo: up, down, up-down, random or as played, from up to two octaves below the chord to two above (CHOMPI + F#4 / A#4), in the pattern set with CHOMPI + G#4. While the pattern runs it locks to the pattern's steps (and swing), and the pattern is silent while it plays; with record on, the arpeggio is written into the steps. In note entry the keys go into the steps as usual.

## Knobs

Turning the pattern length (knob 1, page 2), in either mode, shows it on the white keys for a moment: every step within the length dim white, the last one bright: white for steps 1-8, cyan from step 9 (middle C shows step 8 or 9, so the colour tells them apart).

Click knob 1, knob 4 or the volume knob to step through their pages (knob 4 has four); each page has its own colour, and so does each CHOMPI function (the light changes colour while CHOMPI is held). A knob never goes darker than a fifth, so you can always see which page it's on. Env mod, decay, accent and slide time turn 1.6 times faster than the other knobs, so their whole range takes about a turn and a quarter. CHOMPI + click sets both functions of that knob's page back to their defaults (pattern length back to 16); on knob 4 it resets all the effects, every page.

| Knob | Page 1 | Page 1 + CHOMPI | Page 2 | Page 2 + CHOMPI |
|---|---|---|---|---|
| Knob 1 | Saw / square (amber / cyan) | Pulse width (magenta) | Pattern length (white) | Tuning, +/- 1 semitone (sky blue) |
| Knob 2 | Env mod (green) | Accent (orange) | | |
| Knob 3 | Decay (violet) | Slide time (blue) | | |
| Knob 4 | Delay dry/wet (cyan) | Delay time: 1/16, 1/8, 3/16, 1/4, 3/8, 1/2 (white) | Tape feedback (amber) | Tape tone: dark to bright (lavender) |
| Knob 4, page 3 | Chorus into flanger (pink) | Stereo width / depth (teal) | | |
| Knob 4, page 4 | Bit depth (green) | Sample-rate reduction (red) | | |
| Big purple | Cutoff (purple) | Resonance (red) | | |
| Volume | Volume (white) | Drive (light orange to red) | Tempo, 1 BPM a click, 60-200 (yellow, flashing the beat) | Swing (pink) |

The two lights above the big purple knob show cutoff (resonance with CHOMPI held). While the pattern plays they flash yellow instead, alternating sides, on steps 1, 5, 9 and 13; the side of the current beat flashes red when a note is recorded. For a moment after you turn the knob they show its value again. PLAY is steady green while running, and the CHOMPI button's light flashes brighter on each step that plays a note (brightest on the beat); the transpose mode key (C#4) is dim yellow, the transpose amount bright yellow.

Big purple knob click: tap tempo. Volume knob click: flips between volume / drive and tempo / swing, and stops any stuck live notes.

## The voice

One oscillator (saw, or square with pulse width) into the TB-303's 4-pole diode ladder low-pass, after Tim Stinchcombe's analysis of the 303 filter: four one-pole stages at 0.128, 1.04, 2.33 and 3.24 times the cutoff inside one resonance loop. The spread stages give the 303's slope (around 18 dB/octave above the cutoff, 24 only far above) and its broad resonance, which stops well short of self-oscillation; a high-pass in the loop and saturation at its input, run at twice the sample rate. A high-pass at 120 Hz after the filter stands in for the 303's coupling capacitors, which thin out its lowest notes. Then a decay-only filter envelope and a gated amp envelope. As on a 303, the cutoff knob moves about an octave in its bottom half and nearly 2.5 in its top. Env mod does little until 12 o'clock and a lot after it: even at its minimum the envelope sweeps about half an octave, and at full it sweeps over 5. Turning env mod up raises the top of the sweep and lowers where it rests, about 70/30. So the lower the cutoff, the sooner a long note's resonance sinks into the bass and dies away. The cutoff and env mod curves, the decay and the bass loss were fitted to recordings of a real TB-303. Accent makes a step louder, gives the filter envelope its shortest decay, and charges an accent "capacitor" that drains slowly, so accents in a row build up. Slide holds the gate into the next step and glides there. Gates are half a step; ties and slides hold them.

The resonance amount, cutoff range and envelope times are first estimates, to be fitted to recordings of a real TB-303.

## The effects

In this order after the voice, each off at zero:

- **Drive** (CHOMPI + volume): a pedal-style hard clipper in the spirit of a DS-1: a high-pass tightens the low end and a treble lift puts the highs in front, up to ~60x gain hits a nearly hard, slightly asymmetric clip, and a tone low-pass (8 to 6 kHz) takes only the harshest fizz off. The level is evened out as it turns up.
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
| `patterns.txt` | The 32 patterns (1A-16B), plain text: one `step` line per step (note, octave, on, accent, slide, tie, nudge). Files from before A/B load as the A sides |
| `MIDI/01A.mid` ... `16B.mid` | Each pattern as a Standard MIDI File, written when you export (hold a pattern key 2 s on the pattern page). One pass of the pattern at the current tempo, 96 ticks a beat: recorded timing kept (on the grid if quantize is on), accents at velocity 120 and others 90, ties as longer notes, slides overlapping the next note. Swing and transpose are left out. Empty patterns have no file |
| `IMPORT/` | MIDI files to load into patterns at power-on. Name a file for its pattern, `3B.mid` (or `03B.mid`), put it here, and power on: pattern 3B is replaced and saved (even with write protect on), the file is renamed `3B.done`, and the keys flash green. A file it can't read is renamed `3B.bad`, changes nothing, and the keys blink red. Files it exported come back exactly; others are fitted: format 0 or 1, every track and channel together, the first 16 sixteenths, one note a step (the earliest, then the highest), up to 5 MIDI clock ticks late kept as timing, pitches brought into range by octaves, velocity 112 and up an accent, notes held across steps tied, and notes held into the next one slid. Other names are left alone |
| `current.txt` | Every knob setting, the selected pattern and write protect, restored at power-on. Knob settings save even while the patterns are protected |
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

`tests` checks sequencer timing to the sample, slides, ties, nudged steps, swing, the external clock, pattern queueing, the shared middle C, every step-mode and pitch-mode panel behaviour, real-time recording with each quantize setting, note entry, the knob pages, tap tempo and the file formats. `render` writes demo patterns (saw, square, pulse width, accents building up, slides, each effect, swing) and checks they stay finite and in range.

## Flashing

1. Copy `CHOMPI.bin` to the root of the SD card. It must be the only `.bin` there.
2. Power on. The rainbow shows while the bootloader installs it, then a red sweep across the keys.

A CHOMPI that has never had the bootloader needs `bin/install_bootloader.sh` once first. One running any stock firmware already has it. To go back, use a card with a stock firmware's `.bin`.

## Layout of this repo

| Path | What |
|---|---|
| `code/src/x0x/voice.h` | The 303-style voice and its diode ladder filter |
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
