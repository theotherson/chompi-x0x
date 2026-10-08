/** @file midifile.h
 *  @brief Patterns to and from Standard MIDI Files.
 *
 *  Writing, for a DAW: one track (format 0), 96 ticks a beat, one pass of
 *  the pattern at the given tempo, in 4/4.
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

/** Reads a MIDI file into a pattern, the reverse of WriteMidiFile, so an
 *  exported pattern comes back as it was. Other files are fitted to the
 *  pattern: format 0 or 1 (every track and channel together), the first 16
 *  sixteenths; one note a step (the earliest, then the highest); notes
 *  snapped to their step, up to 5 MIDI clock ticks late kept as timing;
 *  pitches brought into the keyboard's range by octaves; velocity 112 and up
 *  an accent; a note held across steps ties through them, and one held into
 *  the next note slides to it. The length is the file's, up to 16 steps.
 *
 *  Safe with anything: every read is checked against `size`, and `out` is
 *  only written when the whole file read cleanly and has at least one note.
 *  @return false (out untouched) for anything it can't read */
inline bool ReadMidiFile(const uint8_t* d, size_t size, Pattern& out)
{
    struct R
    {
        const uint8_t* d;
        size_t         n, pos = 0;
        bool           ok = true;
        int            U8()
        {
            if(pos >= n)
                return ok = false, 0;
            return d[pos++];
        }
        uint32_t U32()
        {
            uint32_t v = 0;
            for(int i = 0; i < 4; i++)
                v = (v << 8) | static_cast<uint32_t>(U8());
            return v;
        }
        uint32_t Var()
        {
            uint32_t v = 0;
            for(int i = 0; i < 4; i++)
            {
                const int b = U8();
                v           = (v << 7) | (b & 0x7f);
                if(!(b & 0x80))
                    return v;
            }
            ok = false; // longer than a MIDI file allows
            return 0;
        }
        void Skip(uint32_t len)
        {
            if(len > n - pos)
                ok = false, pos = n;
            else
                pos += len;
        }
    } r{d, size};

    // Header: "MThd", format 0 or 1, ticks a beat (not SMPTE time).
    if(size < 14 || d[0] != 'M' || d[1] != 'T' || d[2] != 'h' || d[3] != 'd')
        return false;
    r.pos               = 4;
    const uint32_t hlen = r.U32();
    if(hlen < 6 || hlen > size - 8)
        return false;
    const int format = (r.U8() << 8) | r.U8();
    const int tracks = (r.U8() << 8) | r.U8();
    const int div    = (r.U8() << 8) | r.U8();
    if(!r.ok || format > 1 || tracks < 1 || tracks > 64 || (div & 0x8000) || div < 4)
        return false;
    r.Skip(hlen - 6);
    const float tps = div / 4.f; // ticks a step

    // The notes, from every track. Only what can land in 16 steps matters;
    // later notes are passed over (but the file is still read through, so a
    // broken one is still turned down).
    struct Note
    {
        uint32_t on, off;
        uint8_t  pitch, vel;
    };
    static constexpr int kMaxNotes = 256;
    Note                 notes[kMaxNotes];
    int                  count    = 0;
    uint32_t             file_end = 0;
    const uint32_t       horizon  = static_cast<uint32_t>(kSteps * tps);

    for(int trk = 0; trk < tracks && r.pos + 8 <= size; trk++)
    {
        const bool     mtrk = d[r.pos] == 'M' && d[r.pos + 1] == 'T' && d[r.pos + 2] == 'r' && d[r.pos + 3] == 'k';
        r.pos += 4;
        const uint32_t len = r.U32();
        if(!r.ok || len > size - r.pos)
            return false;
        if(!mtrk)
        {
            r.Skip(len); // another kind of chunk: passed over
            trk--;
            continue;
        }
        R t{d, r.pos + len, r.pos};
        r.pos += len;
        int32_t  open[128]; // where each pitch's sounding note is in notes[], -1 none
        uint32_t open_at[128];
        uint8_t  open_vel[128];
        for(int i = 0; i < 128; i++)
            open[i] = -1;
        uint32_t now = 0;
        int      status = 0;
        bool     ended  = false;
        auto close = [&](int pitch, uint32_t at) {
            if(open[pitch] < 0)
                return;
            if(open_at[pitch] < horizon && count < kMaxNotes)
                notes[count++] = {open_at[pitch], at > open_at[pitch] ? at : open_at[pitch] + 1,
                                  static_cast<uint8_t>(pitch), open_vel[pitch]};
            open[pitch] = -1;
        };
        while(t.ok && t.pos < t.n && !ended)
        {
            const uint32_t delta = t.Var();
            if(delta > 0x0fffffff - now)
                return false;
            now += delta;
            int b = t.U8();
            if(b == 0xff)
            {
                const int      type = t.U8();
                const uint32_t mlen = t.Var();
                t.Skip(mlen);
                if(type == 0x2f)
                    ended = true;
                continue;
            }
            if(b == 0xf0 || b == 0xf7)
            {
                t.Skip(t.Var());
                continue;
            }
            if(b & 0x80)
            {
                if(b >= 0xf0)
                    return false; // not allowed in a file
                status = b;
                b      = t.U8();
            }
            else if(!status)
                return false; // data with no status before it
            const int hi   = status & 0xf0;
            const int data = b;
            int       data2 = 0;
            if(hi != 0xc0 && hi != 0xd0)
                data2 = t.U8();
            if(!t.ok || data > 0x7f || data2 > 0x7f)
                return false;
            if(hi == 0x90 && data2 > 0)
            {
                close(data, now); // the same pitch again: the old one ends
                open[data]     = 0;
                open_at[data]  = now;
                open_vel[data] = static_cast<uint8_t>(data2);
            }
            else if(hi == 0x80 || hi == 0x90)
                close(data, now);
        }
        if(!t.ok)
            return false;
        for(int p = 0; p < 128; p++)
            close(p, now); // still sounding at the track's end
        if(now > file_end)
            file_end = now;
    }
    if(!r.ok || count == 0)
        return false;
    for(int i = 0; i < count; i++)
        if(notes[i].off > file_end)
            file_end = notes[i].off;

    // Onto the steps.
    Pattern p;
    p.Clear();
    int len = static_cast<int>(file_end / tps + 0.5f);
    p.length = static_cast<uint8_t>(ClampInt(len, 1, kSteps));
    int at[kSteps]; // the note on each step, -1 none
    for(int i = 0; i < kSteps; i++)
        at[i] = -1;
    uint8_t nudge_of[kSteps] = {};
    for(int i = 0; i < count; i++)
    {
        const float pos   = notes[i].on / tps;
        int         step  = static_cast<int>(pos);
        int         nudge = static_cast<int>((pos - step) * kStepTicks + 0.5f);
        if(nudge >= kStepTicks)
            step++, nudge = 0;
        if(step >= p.length)
            continue;
        const int  o    = at[step];
        const bool take = o < 0 || notes[i].on < notes[o].on
                          || (notes[i].on == notes[o].on && notes[i].pitch > notes[o].pitch);
        if(take)
            at[step] = i, nudge_of[step] = static_cast<uint8_t>(nudge > 5 ? 5 : nudge);
    }
    bool any = false;
    for(int st = 0; st < p.length; st++)
    {
        if(at[st] < 0 || p.steps[st].on)
            continue; // nothing here, or a tie from the note before
        const Note& n   = notes[at[st]];
        int         rel = n.pitch - kBaseNote, oct = 0;
        while(rel < 0 && oct > -1)
            rel += 12, oct--;
        while(rel >= kKeyNotes && oct < 1)
            rel -= 12, oct++;
        while(rel < 0)
            rel += 12;
        while(rel >= kKeyNotes)
            rel -= 12;
        Step& s  = p.steps[st];
        s.on     = true;
        s.note   = static_cast<uint8_t>(rel);
        s.octave = static_cast<int8_t>(oct);
        s.accent = n.vel >= 112;
        s.nudge  = nudge_of[st];
        any      = true;
        // Held on: ties through the steps it covers (more than a quarter
        // into each), up to the next note; held into that note: a slide.
        int next = st + 1;
        while(next < p.length && at[next] < 0)
            next++;
        int last = st;
        for(int k = st + 1; k < next; k++)
        {
            if(n.off <= static_cast<uint32_t>(k * tps + tps / 4))
                break;
            Step& t  = p.steps[k];
            t        = s;
            t.tie    = true;
            t.accent = false;
            t.nudge  = 0;
            last     = k;
        }
        if(next < p.length && next == last + 1 && n.off > notes[at[next]].on)
            p.steps[last].slide = true;
    }
    if(!any)
        return false;
    out = p;
    return true;
}

/** An import file's name, "3B.mid" or "03b.MID", to its pattern index;
 *  -1 for any other name. */
inline int ParseImportName(const char* name)
{
    int n = 0, digits = 0;
    while(*name >= '0' && *name <= '9' && digits < 2)
        n = n * 10 + (*name++ - '0'), digits++;
    if(!digits || n < 1 || n > kPatternNumbers)
        return -1;
    int side;
    if(*name == 'A' || *name == 'a')
        side = 0;
    else if(*name == 'B' || *name == 'b')
        side = 1;
    else
        return -1;
    name++;
    const char* ext = ".mid";
    for(int i = 0; i < 4; i++, name++)
    {
        char c = *name;
        if(c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if(c != ext[i])
            return -1;
    }
    return *name ? -1 : PatternIndex(n - 1, side);
}

} // namespace x0x
