# CHOMPI x0x

A TB-303 / x0xb0x-style bass line firmware for the CHOMPI: one 303-style voice, a 16-step sequencer written in classic pitch and time modes, and a front panel with no menus. Built from scratch on top of CHOMPI Club's TEMPO firmware, whose hardware layer, libraries, clock and MIDI code it keeps.

> Status: **milestone 1 of 8, scaffold.** It boots, mounts the SD card, reads every key and knob and lights every LED. There is no voice or sequencer yet, and it outputs silence.

## Milestones

1. **Scaffold** (this): TEMPO with sampling, effects and menus removed; boots, card, controls, LEDs, silence.
2. Clock and transport: TEMPO's timer clock and MIDI sync.
3. Voice: 303-style oscillator, filter, envelopes, accent, slide (desktop harness first).
4. Sequencer core: patterns, pitch and time merge.
5. Front panel: pattern, pitch and time modes.
6. Storage: banks and settings on the card.
7. Extras: step edit, keyboard mode and live record, copy and paste, swing.
8. MIDI and polish.

## Trying the scaffold

Flash it (below), then:

| Do | You should see |
|---|---|
| Power on | A blue-then-orange sweep across the white keys |
| Look at the keys | Pitch keys (C3-C4) dim blue, function keys (white 9-15) dim orange, mode keys (black 6-10) dim green |
| Hold any key | It lights fully in its colour |
| Turn a knob | Its LED gets brighter or dimmer; click it to reset to half |
| Hold PLAY / LOOP / CHOMPI | Each lights while held |
| Flip the toggle | The keybed flashes amber (saw) or cyan (square) |
| CHOMPI's LED at rest | Green: the card mounted and was written to. Red: it wasn't |
| On the card | `/X0X/boots.txt` counts power-ons |

CHOMPI + PLAY + LOOP held at power-on still puts the battery in shipping mode, as in the stock firmwares.

## Layout

The full plan (control layout, modes, data model, voice) is in the design doc. In short:

| Control | x0x role |
|---|---|
| White keys 1-8 + black keys 1-5 | Pitch keys, C3 to C4 |
| White keys 9-15 | DOWN, UP, ACCENT, SLIDE, BACK, CLEAR, COPY (time mode: DOWN = note, UP = tie, ACCENT = rest) |
| Black keys 6-10 | Modes: PITCH, TIME, PATTERN, STEP, KEYBOARD |
| PLAY / LOOP / CHOMPI | Run/stop / TAP-NEXT / FUNCTION |
| Toggle | Saw / square |
| Knobs 1-4, big, volume | Resonance, env mod, decay, accent, cutoff, volume |

`code/src/panel.h` holds the mapping from CHOMPI switches and LEDs to these roles.

## Building

Toolchain: Arm GNU Embedded Toolchain **10.3-2021.10**, on your PATH. TEMPO's release was built with GCC 13.3, but its libraries were built with 10.3, and 10.3 is what CHOMPI Club recommends for custom firmware.

Build the libraries once, then the firmware:

```bash
make -C code/libs/libDaisy
make -C code/libs/DaisySP
cd code/src && make
```

The output is `code/src/build/CHOMPI.bin`.

## Flashing

1. Copy `CHOMPI.bin` to the root of the SD card. It must be the only `.bin` there.
2. Power on. The rainbow shows while the bootloader installs it.

A CHOMPI that has never had the bootloader needs `bin/install_bootloader.sh` once first (Daisy Seed in DFU mode: hold BOOT, tap RESET). One running any stock firmware already has it. To go back, use a card with a stock firmware's `.bin`.

## Layout of this repo

| Path | What |
|---|---|
| `code/src/chompi_main.cpp` | Startup, audio interrupt, main loop |
| `code/src/panel.h` | The x0x control map, LEDs, and the milestone-1 panel test |
| `code/src/hardware.h`, `encoder.*`, `temp_led_stuff.h`, `chompi_sram.lds` | TEMPO's hardware layer, unchanged |
| `code/src/clockManager.h` | TEMPO's clock, unchanged; wired in at milestone 2 |
| `code/libs/` | libDaisy, DaisySP, coreJSON as TEMPO vendors them |
| `bin/` | TEMPO's bootloader binary and install script |

The first commit is TEMPO v1.0 as released, so `git diff` against it shows everything this firmware changes.

## Credits

- Built on CHOMPI Club's open-source TEMPO firmware (MIT); see `LICENSE`, `THIRD_PARTY.md` and `TRADEMARKS.md`. The CHOMPI name belongs to CHOMPI Club, and this is a community firmware, not an official release.
- Key and LED tables, and fixes for SD card cache alignment and button edges, from hiwatts' POLY ([sfaber02/chompi-poly](https://github.com/sfaber02/chompi-poly), MIT).
