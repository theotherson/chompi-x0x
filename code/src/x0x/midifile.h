/** @file midifile.h
 *  @brief A pattern as a Standard MIDI File, for a DAW: one track (format
 *  0), 96 ticks a beat, one pass of the pattern at the given tempo, in 4/4.
 *
 *  The notes are the pattern as it plays: recorded timing kept (or put on
 *  the grid, if quantize is on), accents as velocity 120 (others 90, so a
 *  file played back into the x0x accents the same notes), ties as longer
 *  notes, and slides as notes overlapping the next one. Gates are half a
 *  step, as the sequencer plays them. Swing and transpose are left out.
 */
#pragma once
#include "pattern.h"
#include <cstddef>
#include <cstdint>

namespace x0x
{

constexpr int kMidiFilePpq       = 96;
constexpr int kMidiTicksPerStep  = kMidiFilePpq / 4;               // a sixteenth
constexpr int kMidiTicksPerNudge = kMidiTicksPerStep / kStepTicks; // a MIDI clock tick
constexpr int kMidiAccentVel     = 120;
constexpr int kMidiNormalVel     = 90;

namespace detail
{
struct MidiWriter
{
    uint8_t* buf;
    size_t   size, len = 0;
    bool     ok = true;

    void Byte(int b)
    {
        if(len < size)
            buf[len++] = static_cast<uint8_t>(b);
        else
            ok = false;
    }
    void U16(int v) { Byte(v >> 8), Byte(v); }
    void U32(uint32_t v) { Byte(v >> 24), Byte(v >> 16), Byte(v >> 8), Byte(v); }
    void Var(uint32_t v) // a variable-length quantity
    {
        uint8_t b[4];
        int     n = 0;
        do
            b[n++] = v & 0x7f, v >>= 7;
        while(v && n < 4);
        while(n-- > 0)
            Byte(b[n] | (n ? 0x80 : 0));
    }
};

struct MidiEvent
{
    uint32_t t;
    uint8_t  status, note, vel;
};
} // namespace detail

/** Writes `pat` (played with quantize to `grid` steps, 0 = off) as a MIDI
 *  file named `name` (the track name). @return bytes written, 0 if it
 *  didn't fit */
inline size_t WriteMidiFile(const Pattern& pat, float bpm, int grid, const char* name, uint8_t* buf, size_t size)
{
    const int len = pat.length;
    Step      e[kSteps];
    for(int i = 0; i < len; i++)
        e[i] = pat.PlayedStep(i, grid);
    auto start = [&](int i) { return static_cast<uint32_t>(i * kMidiTicksPerStep + e[i].nudge * kMidiTicksPerNudge); };

    detail::MidiEvent ev[2 * kSteps];
    int               n = 0;
    for(int i = 0; i < len; i++)
    {
        if(!e[i].on || e[i].tie)
            continue;
        int j = i; // the last step it holds through: ties
        while(j + 1 < len && e[j + 1].on && e[j + 1].tie)
            j++;
        uint32_t end;
        if(e[j].slide && j + 1 < len && e[j + 1].on)
            end = start(j + 1) + 2; // into the next note, overlapping it
        else
        {
            end = start(j) + kMidiTicksPerStep / 2; // half a step...
            if(j + 1 < len && end > start(j + 1))
                end = start(j + 1);                 // ...never past the next step
        }
        const int note = ClampInt(e[i].Midi(), 0, 127);
        ev[n++]        = {start(i), 0x90, static_cast<uint8_t>(note),
                          static_cast<uint8_t>(e[i].accent ? kMidiAccentVel : kMidiNormalVel)};
        ev[n++]        = {end, 0x80, static_cast<uint8_t>(note), 0};
    }
    // In time order; at the same tick, offs before ons.
    for(int a = 1; a < n; a++)
        for(int b = a; b > 0; b--)
        {
            const detail::MidiEvent &x = ev[b - 1], &y = ev[b];
            if(x.t < y.t || (x.t == y.t && !(x.status == 0x90 && y.status == 0x80)))
                break;
            const detail::MidiEvent t = ev[b - 1];
            ev[b - 1]                 = ev[b];
            ev[b]                     = t;
        }

    detail::MidiWriter w{buf, size};
    w.Byte('M'), w.Byte('T'), w.Byte('h'), w.Byte('d');
    w.U32(6), w.U16(0), w.U16(1), w.U16(kMidiFilePpq);
    w.Byte('M'), w.Byte('T'), w.Byte('r'), w.Byte('k');
    const size_t len_at = w.len;
    w.U32(0); // the track's length, filled in below

    const uint32_t us = static_cast<uint32_t>(60000000.f / (bpm > 1.f ? bpm : 120.f) + 0.5f);
    w.Var(0), w.Byte(0xff), w.Byte(0x51), w.Byte(3), w.Byte(us >> 16), w.Byte(us >> 8), w.Byte(us); // tempo
    w.Var(0), w.Byte(0xff), w.Byte(0x58), w.Byte(4), w.Byte(4), w.Byte(2), w.Byte(24), w.Byte(8);    // 4/4
    int name_len = 0;
    while(name && name[name_len])
        name_len++;
    w.Var(0), w.Byte(0xff), w.Byte(0x03), w.Var(name_len);
    for(int i = 0; i < name_len; i++)
        w.Byte(name[i]);

    uint32_t t = 0;
    for(int i = 0; i < n; i++)
    {
        w.Var(ev[i].t - t);
        w.Byte(ev[i].status), w.Byte(ev[i].note), w.Byte(ev[i].vel);
        t = ev[i].t;
    }
    // The end of the track: the end of the pattern (or the last note off).
    const uint32_t end = static_cast<uint32_t>(len * kMidiTicksPerStep);
    w.Var(end > t ? end - t : 0), w.Byte(0xff), w.Byte(0x2f), w.Byte(0);

    if(!w.ok)
        return 0;
    const uint32_t track = static_cast<uint32_t>(w.len - len_at - 4);
    buf[len_at]          = static_cast<uint8_t>(track >> 24);
    buf[len_at + 1]      = static_cast<uint8_t>(track >> 16);
    buf[len_at + 2]      = static_cast<uint8_t>(track >> 8);
    buf[len_at + 3]      = static_cast<uint8_t>(track);
    return w.len;
}

} // namespace x0x
