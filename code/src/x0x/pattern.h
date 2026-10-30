/** @file pattern.h
 *  @brief A pattern: up to 16 steps, each with its own note and flags.
 *
 *  A step that is on plays its note; one that is off is a rest. Flags:
 *    octave  -1 (DOWN page) / 0 / +1 (UP page)
 *    accent  louder, and the filter's accent sweep
 *    slide   holds the gate into the next note, which glides there
 *    tie     holds the note before through this step (its own note unused)
 *    nudge   0-5 MIDI clock ticks late: the timing of a note recorded with
 *            quantize off. With quantize on these notes play on the grid
 *            instead (PlayedStep); the timing is kept for when it's off
 *
 *  There are 16 pattern numbers, each with an A and a B side, as on a
 *  TB-303. Patterns are stored as plain text, one pattern after another
 *  ("pattern 1" without a side, from earlier versions, is 1A):
 *
 *    pattern 1A
 *    length 16
 *    scale 1                   the step scale: 1 = 1/16 triplets, 2 = 1/8
 *                              triplets (absent: 1/16)
 *    step 1 12 0 1 1 0 0 0     step, note 0-24, octave, on, accent, slide, tie, nudge
 *    ...
 *    drums 16 81 00 40 ...     the drum part: its length, then each step as
 *                              hex: bits 0-6 the voices (BD SD LT HT CY OH
 *                              CH), bit 7 the accent (absent: no drums)
 *    drum_nudge 0000000 0300000 ...
 *                              each step's hits' timing, a digit a voice:
 *                              ticks late, as a step's nudge (absent:
 *                              all on the grid)
 *    drum_pitch mmmmmmm mmmmmqm ...
 *                              each hit's pitch, played on the keys in live
 *                              mode, a letter a voice: a-y = -12..+12
 *                              semitones, m none (absent: none)
 */
#pragma once
#include "dsp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace x0x
{

constexpr int kSteps     = 16;
constexpr int kPatternNumbers = 16; // 1-16, each with an A and a B side
constexpr int kPatterns       = 2 * kPatternNumbers; // index = number - 1 (+ 16 for B)

inline int PatternIndex(int number0, int side) { return number0 + kPatternNumbers * side; }
inline int PatternNumber(int index) { return index % kPatternNumbers; } // 0-15
inline int PatternSide(int index) { return index / kPatternNumbers; }   // 0 = A, 1 = B

/** "12B" (or "12", side A) -> index; -1 if it isn't one. */
inline int ParsePatternName(const char* p)
{
    while(*p == ' ')
        p++;
    const int n = atoi(p);
    if(n < 1 || n > kPatternNumbers)
        return -1;
    while(*p >= '0' && *p <= '9')
        p++;
    return PatternIndex(n - 1, (*p == 'B' || *p == 'b') ? 1 : 0);
}
constexpr int kKeyNotes  = 25; // the keybed as a keyboard: C3 to C5
constexpr int kBaseNote  = 36; // MIDI note the lowest key plays (C2)

constexpr int kMaxRatchet = 4;
/** A stored ratchet count as hits, 1-4 (0, as zeroed memory, is 1). */
constexpr int Ratchets(int r) { return r < 1 ? 1 : r > kMaxRatchet ? kMaxRatchet : r; }

struct Step
{
    uint8_t note   = 0; // 0-24, semitones above the lowest key
    int8_t  octave = 0; // -1, 0, +1
    bool    on     = false;
    bool    accent = false;
    bool    slide  = false;
    bool    tie    = false;
    uint8_t nudge  = 0; // ticks after the step's start, within it (0-5 at 1/16)
    uint8_t ratchet = 1; // hits within the step, 1-4 (Ratchets)
    bool    timed   = false; // nudge set by hand (step mode): kept with quantize on

    int Midi() const { return kBaseNote + note + 12 * octave; }
    int Hits() const { return Ratchets(ratchet); }
    bool operator==(const Step& o) const
    {
        return note == o.note && octave == o.octave && on == o.on && accent == o.accent
               && slide == o.slide && tie == o.tie && nudge == o.nudge && Hits() == o.Hits()
               && timed == o.timed;
    }
};

constexpr int kStepTicks = 6; // MIDI clock ticks a sixteenth (nudge counts ticks)

/** A pattern's step scale: what a step is. 1/16 by default; the triplets
 *  for triplet grooves (12 steps of 1/8T are a bar, 12 of 1/16T half one). */
enum Scale : uint8_t
{
    kScale16,  // 1/16: 6 ticks a step
    kScale16T, // 1/16 triplets: 4
    kScale8T,  // 1/8 triplets: 8
    kScales
};
constexpr int kScaleTicks[kScales] = {6, 4, 8};

constexpr int     kDrumVoices = 7;    // BD SD LT HT CY OH CH, as x0x::Drum
constexpr uint8_t kDrumAccent = 0x80; // a drum step's accent bit

struct Pattern
{
    Step    steps[kSteps];
    uint8_t length = kSteps;
    uint8_t scale  = kScale16; // the step scale (Scale), for the bass and the drums
    /** MIDI clock ticks a step, by the scale. */
    int StepTicks() const { return kScaleTicks[scale < kScales ? scale : 0]; }
    /** A new scale; recorded timing kept within the (maybe shorter) step. */
    void SetScale(int sc)
    {
        scale         = static_cast<uint8_t>(ClampInt(sc, 0, kScales - 1));
        const int max = StepTicks() - 1;
        for(int i = 0; i < kSteps; i++)
        {
            if(steps[i].nudge > max)
                steps[i].nudge = static_cast<uint8_t>(max);
            for(int v = 0; v < kDrumVoices; v++)
                if(drum_nudge[i][v] > max)
                    drum_nudge[i][v] = static_cast<uint8_t>(max);
        }
    }
    /** The drum part, linked to the bassline: per step, a bit for each
     *  voice and one for the accent; its own length (polymeters). */
    uint8_t drums[kSteps] = {};
    uint8_t drum_length   = kSteps;
    /** Each hit's timing, recorded with quantize off: ticks late, within the step. */
    uint8_t drum_nudge[kSteps][kDrumVoices] = {};
    /** Each hit's pitch, -12..12 semitones (recorded from the keys, live). */
    int8_t drum_pitch[kSteps][kDrumVoices] = {};
    /** Each hit's ratchet: hits within the step (0 or 1 = one). */
    uint8_t drum_ratchet[kSteps][kDrumVoices] = {};
    int DrumHits(int step, int voice) const { return Ratchets(drum_ratchet[step][voice]); }
    /** Per step, a bit for each voice whose timing was set by hand (step
     *  mode): played as set, quantize or not. */
    uint8_t drum_timed[kSteps] = {};
    bool DrumTimed(int step, int voice) const { return (drum_timed[step] >> voice) & 1; }
    void SetDrumTimed(int step, int voice, bool on)
    {
        const uint8_t bit = static_cast<uint8_t>(1 << voice);
        drum_timed[step]  = static_cast<uint8_t>(on ? drum_timed[step] | bit : drum_timed[step] & ~bit);
    }
    bool AnyDrumTimed() const
    {
        for(int i = 0; i < kSteps; i++)
            if(drum_timed[i])
                return true;
        return false;
    }

    /** Sets or clears a hit (on the grid). */
    void SetDrumHit(int step, int voice, bool on)
    {
        const uint8_t bit = static_cast<uint8_t>(1 << voice);
        drums[step]       = static_cast<uint8_t>(on ? drums[step] | bit : drums[step] & ~bit);
        drum_nudge[step][voice]   = 0;
        drum_pitch[step][voice]   = 0;
        drum_ratchet[step][voice] = 0;
        SetDrumTimed(step, voice, false);
    }
    bool DrumPitched() const
    {
        for(int i = 0; i < kSteps; i++)
            for(int v = 0; v < kDrumVoices; v++)
                if(drum_pitch[i][v])
                    return true;
        return false;
    }
    bool DrumRatcheted() const
    {
        for(int i = 0; i < kSteps; i++)
            for(int v = 0; v < kDrumVoices; v++)
                if(DrumHits(i, v) > 1)
                    return true;
        return false;
    }
    bool DrumNudged() const
    {
        for(int i = 0; i < kSteps; i++)
            for(int v = 0; v < kDrumVoices; v++)
                if(drum_nudge[i][v])
                    return true;
        return false;
    }

    bool DrumHit(int step, int voice) const { return (drums[step] >> voice) & 1; }
    bool DrumAccent(int step) const { return (drums[step] & kDrumAccent) != 0; }
    bool DrumsEmpty() const
    {
        for(int i = 0; i < kSteps; i++)
            if(drums[i] & 0x7f)
                return false;
        return true;
    }

    /** Step i as it plays with quantize on, to a grid of `grid` steps.
     *  Only notes with recorded timing (a nudge) move: each to the nearest
     *  point of the grid, wrapping round the pattern, its nudge gone. Notes
     *  right on their step stay put, so patterns entered step by step never
     *  change. Where several land on one step the nearest wins, and a note
     *  already there beats them all; a note that moves away leaves a rest. */
    Step PlayedStep(int i, int grid) const
    {
        const Step& own = steps[i];
        if(grid <= 0 || (own.on && !own.tie && (own.nudge == 0 || own.timed)))
            return own; // on its step, or its timing set by hand
        const int st   = StepTicks();
        const int gt   = grid * st;
        int       best = -1, best_d = 1 << 30;
        for(int j = 0; j < length; j++)
        {
            const Step& s = steps[j];
            if(!s.on || s.tie || s.nudge == 0 || s.timed)
                continue;
            const int tick   = j * st + s.nudge;
            const int target = (tick + gt / 2) / gt * gt; // nearest grid point, in ticks
            if((target / st) % length != i)
                continue;
            const int d = tick > target ? tick - target : target - tick;
            if(d < best_d)
                best = j, best_d = d;
        }
        if(best >= 0)
        {
            Step s  = steps[best];
            s.nudge = 0;
            return s;
        }
        if(own.on && !own.tie)
            return Step{}; // its note moved to another step
        return own;        // a rest or a tie
    }

    void Clear()
    {
        ClearBass();
        ClearDrums();
    }
    void ClearBass()
    {
        for(int i = 0; i < kSteps; i++)
            steps[i] = Step{};
        length = kSteps;
        scale  = kScale16;
    }
    /** The bassline alone (steps, length) from another pattern. */
    void CopyBassFrom(const Pattern& o)
    {
        for(int i = 0; i < kSteps; i++)
            steps[i] = o.steps[i];
        length = o.length;
        scale  = o.scale;
    }

    /** The drum part alone (hits, timing, length) from another pattern. */
    void CopyDrumsFrom(const Pattern& o)
    {
        memcpy(drums, o.drums, sizeof drums);
        memcpy(drum_nudge, o.drum_nudge, sizeof drum_nudge);
        memcpy(drum_pitch, o.drum_pitch, sizeof drum_pitch);
        memcpy(drum_ratchet, o.drum_ratchet, sizeof drum_ratchet);
        memcpy(drum_timed, o.drum_timed, sizeof drum_timed);
        drum_length = o.drum_length;
    }

    /** The bassline shifted `d` steps later (earlier if negative), round
     *  its length; each step keeps its note, flags, timing and ratchet. */
    void ShiftBass(int d)
    {
        const int len = ClampInt(length, 1, kSteps);
        d             = ((d % len) + len) % len;
        Step was[kSteps];
        for(int i = 0; i < len; i++)
            was[i] = steps[i];
        for(int i = 0; i < len; i++)
            steps[(i + d) % len] = was[i];
    }

    /** The drum part shifted `d` steps later (earlier if negative), round
     *  its own length: hits, accents, timing, pitch and ratchets. */
    void ShiftDrums(int d)
    {
        const int len = ClampInt(drum_length, 1, kSteps);
        d             = ((d % len) + len) % len;
        Pattern was   = *this;
        for(int i = 0; i < len; i++)
        {
            const int to = (i + d) % len;
            drums[to]    = was.drums[i];
            memcpy(drum_nudge[to], was.drum_nudge[i], kDrumVoices);
            memcpy(drum_pitch[to], was.drum_pitch[i], kDrumVoices);
            memcpy(drum_ratchet[to], was.drum_ratchet[i], kDrumVoices);
            drum_timed[to] = was.drum_timed[i];
        }
    }

    void ClearDrums()
    {
        for(int i = 0; i < kSteps; i++)
        {
            drums[i]      = 0;
            drum_timed[i] = 0;
            for(int v = 0; v < kDrumVoices; v++)
                drum_nudge[i][v] = 0, drum_pitch[i][v] = 0, drum_ratchet[i][v] = 0;
        }
        drum_length = kSteps;
    }

    bool BassEmpty() const
    {
        for(int i = 0; i < kSteps; i++)
            if(steps[i].on)
                return false;
        return true;
    }
    /** Nothing in it: no bass notes and no drum hits. */
    bool Empty() const { return BassEmpty() && DrumsEmpty(); }

    bool operator==(const Pattern& o) const
    {
        if(length != o.length || drum_length != o.drum_length || scale != o.scale)
            return false;
        for(int i = 0; i < kSteps; i++)
            if(!(steps[i] == o.steps[i]) || drums[i] != o.drums[i] || drum_timed[i] != o.drum_timed[i]
               || memcmp(drum_nudge[i], o.drum_nudge[i], kDrumVoices) != 0
               || memcmp(drum_pitch[i], o.drum_pitch[i], kDrumVoices) != 0)
                return false;
        for(int i = 0; i < kSteps; i++)
            for(int v = 0; v < kDrumVoices; v++)
                if(DrumHits(i, v) != o.DrumHits(i, v))
                    return false;
        return true;
    }
};

// ------------------------------------------------------------------ text

/** Writes all patterns. @return bytes written, 0 if it didn't fit */
inline size_t WritePatterns(const Pattern* pats, int count, char* buf, size_t size)
{
    size_t len = 0;
    for(int p = 0; p < count; p++)
    {
        const Pattern& pat = pats[p];
        int w = snprintf(buf + len, size - len, "pattern %d%c\nlength %d\n", PatternNumber(p) + 1,
                         PatternSide(p) ? 'B' : 'A', pat.length);
        if(w <= 0 || static_cast<size_t>(w) >= size - len)
            return 0;
        len += w;
        if(pat.scale != kScale16)
        {
            w = snprintf(buf + len, size - len, "scale %d\n", pat.scale);
            if(w <= 0 || static_cast<size_t>(w) >= size - len)
                return 0;
            len += w;
        }
        for(int i = 0; i < kSteps; i++)
        {
            const Step& s = pat.steps[i];
            w = snprintf(buf + len, size - len, "step %d %d %d %d %d %d %d %d %d %d\n", i + 1, s.note,
                         s.octave, s.on ? 1 : 0, s.accent ? 1 : 0, s.slide ? 1 : 0, s.tie ? 1 : 0,
                         s.nudge, s.Hits(), s.timed ? 1 : 0);
            if(w <= 0 || static_cast<size_t>(w) >= size - len)
                return 0;
            len += w;
        }
        if(!pat.DrumsEmpty() || pat.drum_length != kSteps)
        {
            w = snprintf(buf + len, size - len, "drums %d", pat.drum_length);
            if(w <= 0 || static_cast<size_t>(w) >= size - len)
                return 0;
            len += w;
            for(int i = 0; i < kSteps; i++)
            {
                w = snprintf(buf + len, size - len, " %02x", pat.drums[i]);
                if(w <= 0 || static_cast<size_t>(w) >= size - len)
                    return 0;
                len += w;
            }
            if(len + 1 >= size)
                return 0;
            buf[len++] = '\n';
            buf[len]   = '\0';
        }
        if(pat.DrumNudged())
        {
            w = snprintf(buf + len, size - len, "drum_nudge");
            if(w <= 0 || static_cast<size_t>(w) >= size - len)
                return 0;
            len += w;
            for(int i = 0; i < kSteps; i++)
            {
                char d[kDrumVoices + 2];
                d[0] = ' ';
                for(int v = 0; v < kDrumVoices; v++)
                    d[1 + v] = static_cast<char>('0' + ClampInt(pat.drum_nudge[i][v], 0, 9));
                d[kDrumVoices + 1] = '\0';
                w = snprintf(buf + len, size - len, "%s", d);
                if(w <= 0 || static_cast<size_t>(w) >= size - len)
                    return 0;
                len += w;
            }
            if(len + 1 >= size)
                return 0;
            buf[len++] = '\n';
            buf[len]   = '\0';
        }
        if(pat.DrumPitched())
        {
            w = snprintf(buf + len, size - len, "drum_pitch");
            if(w <= 0 || static_cast<size_t>(w) >= size - len)
                return 0;
            len += w;
            for(int i = 0; i < kSteps; i++)
            {
                char d[kDrumVoices + 2];
                d[0] = ' ';
                for(int v = 0; v < kDrumVoices; v++)
                    d[1 + v] = static_cast<char>('m' + ClampInt(pat.drum_pitch[i][v], -12, 12));
                d[kDrumVoices + 1] = '\0';
                w = snprintf(buf + len, size - len, "%s", d);
                if(w <= 0 || static_cast<size_t>(w) >= size - len)
                    return 0;
                len += w;
            }
            if(len + 1 >= size)
                return 0;
            buf[len++] = '\n';
            buf[len]   = '\0';
        }
        if(pat.AnyDrumTimed())
        {
            w = snprintf(buf + len, size - len, "drum_timed");
            if(w <= 0 || static_cast<size_t>(w) >= size - len)
                return 0;
            len += w;
            for(int i = 0; i < kSteps; i++)
            {
                w = snprintf(buf + len, size - len, " %02x", pat.drum_timed[i]);
                if(w <= 0 || static_cast<size_t>(w) >= size - len)
                    return 0;
                len += w;
            }
            if(len + 1 >= size)
                return 0;
            buf[len++] = '\n';
            buf[len]   = '\0';
        }
        if(pat.DrumRatcheted())
        {
            w = snprintf(buf + len, size - len, "drum_ratchet");
            if(w <= 0 || static_cast<size_t>(w) >= size - len)
                return 0;
            len += w;
            for(int i = 0; i < kSteps; i++)
            {
                char d[kDrumVoices + 2];
                d[0] = ' ';
                for(int v = 0; v < kDrumVoices; v++)
                    d[1 + v] = static_cast<char>('0' + pat.DrumHits(i, v));
                d[kDrumVoices + 1] = '\0';
                w = snprintf(buf + len, size - len, "%s", d);
                if(w <= 0 || static_cast<size_t>(w) >= size - len)
                    return 0;
                len += w;
            }
            if(len + 1 >= size)
                return 0;
            buf[len++] = '\n';
            buf[len]   = '\0';
        }
    }
    return len;
}

/** Reads patterns written by WritePatterns. Lines it doesn't recognise are
 *  skipped; patterns missing from the text are left as they were.
 *  @param text  NUL-terminated; it is modified while parsing */
inline void ReadPatterns(char* text, Pattern* pats, int count)
{
    Pattern* pat  = nullptr;
    char*    line = text;
    while(line && *line)
    {
        char* next = strchr(line, '\n');
        if(next)
            *next++ = '\0';

        if(strncmp(line, "pattern ", 8) == 0)
        {
            const int i = ParsePatternName(line + 8);
            pat         = i >= 0 && i < count ? &pats[i] : nullptr;
            if(pat)
                pat->Clear();
        }
        else if(pat && strncmp(line, "drums ", 6) == 0)
        {
            char* p = line + 6;
            char* end;
            const long l = strtol(p, &end, 10);
            if(end != p)
            {
                pat->drum_length = static_cast<uint8_t>(ClampInt(static_cast<int>(l), 1, kSteps));
                p = end;
                for(int i = 0; i < kSteps; i++)
                {
                    const long v = strtol(p, &end, 16);
                    if(end == p)
                        break;
                    pat->drums[i] = static_cast<uint8_t>(v & 0xff);
                    p = end;
                }
            }
        }
        else if(pat && strncmp(line, "drum_nudge ", 11) == 0)
        {
            const char* p = line + 11;
            for(int i = 0; i < kSteps; i++)
            {
                while(*p == ' ')
                    p++;
                for(int v = 0; v < kDrumVoices; v++)
                {
                    if(*p < '0' || *p > '9')
                        break;
                    pat->drum_nudge[i][v] = static_cast<uint8_t>(ClampInt(*p - '0', 0, 9));
                    p++;
                }
                while(*p && *p != ' ')
                    p++; // anything more in this step's group: skipped
            }
        }
        else if(pat && strncmp(line, "drum_timed ", 11) == 0)
        {
            char* p = line + 11;
            for(int i = 0; i < kSteps; i++)
            {
                char*      end;
                const long v = strtol(p, &end, 16);
                if(end == p)
                    break;
                pat->drum_timed[i] = static_cast<uint8_t>(v & 0x7f);
                p                  = end;
            }
        }
        else if(pat && strncmp(line, "drum_ratchet ", 13) == 0)
        {
            const char* p = line + 13;
            for(int i = 0; i < kSteps; i++)
            {
                while(*p == ' ')
                    p++;
                for(int v = 0; v < kDrumVoices; v++)
                {
                    if(*p < '1' || *p > '0' + kMaxRatchet)
                        break;
                    pat->drum_ratchet[i][v] = static_cast<uint8_t>(*p - '0');
                    p++;
                }
                while(*p && *p != ' ')
                    p++;
            }
        }
        else if(pat && strncmp(line, "drum_pitch ", 11) == 0)
        {
            const char* p = line + 11;
            for(int i = 0; i < kSteps; i++)
            {
                while(*p == ' ')
                    p++;
                for(int v = 0; v < kDrumVoices; v++)
                {
                    if(*p < 'a' || *p > 'y')
                        break;
                    pat->drum_pitch[i][v] = static_cast<int8_t>(*p - 'm');
                    p++;
                }
                while(*p && *p != ' ')
                    p++;
            }
        }
        else if(pat && strncmp(line, "length ", 7) == 0)
            pat->length = static_cast<uint8_t>(ClampInt(atoi(line + 7), 1, kSteps));
        else if(pat && strncmp(line, "scale ", 6) == 0)
            pat->scale = static_cast<uint8_t>(ClampInt(atoi(line + 6), 0, kScales - 1));
        else if(pat && strncmp(line, "step ", 5) == 0)
        {
            char* p    = line + 5;
            long  v[10] = {};
            int   got   = 0;
            for(; got < 10; got++)
            {
                char* end;
                v[got] = strtol(p, &end, 10);
                if(end == p)
                    break;
                p = end;
            }
            // 7 numbers: written before nudge existed; 8, before ratchets;
            // 9, before timing set by hand.
            if(got >= 7 && v[0] >= 1 && v[0] <= kSteps && v[1] >= 0 && v[1] < kKeyNotes
               && v[2] >= -1 && v[2] <= 1)
            {
                Step& s = pat->steps[v[0] - 1];
                s.note   = static_cast<uint8_t>(v[1]);
                s.octave = static_cast<int8_t>(v[2]);
                s.on     = v[3] != 0;
                s.accent = v[4] != 0;
                s.slide  = v[5] != 0;
                s.tie    = v[6] != 0;
                s.nudge  = static_cast<uint8_t>(ClampInt(static_cast<int>(v[7]), 0, 9));
                s.ratchet = static_cast<uint8_t>(got >= 9 ? Ratchets(static_cast<int>(v[8])) : 1);
                s.timed   = got >= 10 && v[9] != 0;
            }
        }
        line = next;
    }
    for(int p = 0; p < count; p++)
        pats[p].SetScale(pats[p].scale); // timing within each step
}

/** A starter pattern for a fresh card, so PLAY does something. */
inline void DemoPattern(Pattern& p)
{
    p.Clear();
    // note, octave, on, accent, slide, tie
    static const int8_t kSteps16[kSteps][6] = {
        {0, 0, 1, 1, 0, 0},  {0, 0, 1, 0, 0, 0},  {12, 0, 1, 0, 1, 0}, {3, 0, 1, 0, 0, 0},
        {0, 0, 1, 1, 0, 0},  {0, 0, 1, 0, 0, 1},  {7, 0, 1, 0, 1, 0},  {5, 0, 1, 0, 0, 0},
        {0, -1, 1, 1, 0, 0}, {0, 0, 0, 0, 0, 0},  {10, 0, 1, 0, 1, 0}, {12, 0, 1, 1, 0, 0},
        {3, 0, 1, 0, 0, 0},  {0, 0, 0, 0, 0, 0},  {7, 0, 1, 1, 0, 0},  {5, 0, 1, 0, 0, 0},
    };
    for(int i = 0; i < kSteps; i++)
    {
        Step& s  = p.steps[i];
        s.note   = static_cast<uint8_t>(kSteps16[i][0]);
        s.octave = kSteps16[i][1];
        s.on     = kSteps16[i][2] != 0;
        s.accent = kSteps16[i][3] != 0;
        s.slide  = kSteps16[i][4] != 0;
        s.tie    = kSteps16[i][5] != 0;
    }
}

// ------------------------------------------------------------ GM drums

/** The voices' General MIDI drum notes (channel 10), as x0x::Drum. */
constexpr uint8_t kGmDrum[kDrumVoices] = {36, 38, 45, 50, 49, 46, 42}; // BD SD LT HT CY OH CH

/** A GM drum note to the 606 voice that plays it, -1 for none. */
inline int GmToDrum(int note)
{
    switch(note)
    {
        case 35: case 36: return 0;                           // kicks
        case 37: case 38: case 39: case 40: return 1;         // snares, rim, clap
        case 41: case 43: case 45: case 47: return 2;         // low toms
        case 48: case 50: return 3;                           // high toms
        case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 4; // cymbals
        case 46: return 5;                                    // open hat
        case 42: case 44: return 6;                           // closed, pedal hat
        default: return -1;
    }
}

} // namespace x0x
