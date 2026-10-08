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
 *    step 1 12 0 1 1 0 0 0     step, note 0-24, octave, on, accent, slide, tie, nudge
 *    ...
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

struct Step
{
    uint8_t note   = 0; // 0-24, semitones above the lowest key
    int8_t  octave = 0; // -1, 0, +1
    bool    on     = false;
    bool    accent = false;
    bool    slide  = false;
    bool    tie    = false;
    uint8_t nudge  = 0; // 0-5 ticks after the step's start

    int Midi() const { return kBaseNote + note + 12 * octave; }
    bool operator==(const Step& o) const
    {
        return note == o.note && octave == o.octave && on == o.on && accent == o.accent
               && slide == o.slide && tie == o.tie && nudge == o.nudge;
    }
};

constexpr int kStepTicks = 6; // MIDI clock ticks a step (nudge counts these)

struct Pattern
{
    Step    steps[kSteps];
    uint8_t length = kSteps;

    /** Step i as it plays with quantize on, to a grid of `grid` steps.
     *  Only notes with recorded timing (a nudge) move: each to the nearest
     *  point of the grid, wrapping round the pattern, its nudge gone. Notes
     *  right on their step stay put, so patterns entered step by step never
     *  change. Where several land on one step the nearest wins, and a note
     *  already there beats them all; a note that moves away leaves a rest. */
    Step PlayedStep(int i, int grid) const
    {
        const Step& own = steps[i];
        if(grid <= 0 || (own.on && !own.tie && own.nudge == 0))
            return own;
        const int gt   = grid * kStepTicks;
        int       best = -1, best_d = 1 << 30;
        for(int j = 0; j < length; j++)
        {
            const Step& s = steps[j];
            if(!s.on || s.tie || s.nudge == 0)
                continue;
            const int tick   = j * kStepTicks + s.nudge;
            const int target = (tick + gt / 2) / gt * gt; // nearest grid point, in ticks
            if((target / kStepTicks) % length != i)
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
        for(int i = 0; i < kSteps; i++)
            steps[i] = Step{};
        length = kSteps;
    }

    bool Empty() const
    {
        for(int i = 0; i < kSteps; i++)
            if(steps[i].on)
                return false;
        return true;
    }

    bool operator==(const Pattern& o) const
    {
        if(length != o.length)
            return false;
        for(int i = 0; i < kSteps; i++)
            if(!(steps[i] == o.steps[i]))
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
        for(int i = 0; i < kSteps; i++)
        {
            const Step& s = pat.steps[i];
            w = snprintf(buf + len, size - len, "step %d %d %d %d %d %d %d %d\n", i + 1, s.note,
                         s.octave, s.on ? 1 : 0, s.accent ? 1 : 0, s.slide ? 1 : 0, s.tie ? 1 : 0,
                         s.nudge);
            if(w <= 0 || static_cast<size_t>(w) >= size - len)
                return 0;
            len += w;
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
        else if(pat && strncmp(line, "length ", 7) == 0)
            pat->length = static_cast<uint8_t>(ClampInt(atoi(line + 7), 1, kSteps));
        else if(pat && strncmp(line, "step ", 5) == 0)
        {
            char* p    = line + 5;
            long  v[8] = {};
            int   got  = 0;
            for(; got < 8; got++)
            {
                char* end;
                v[got] = strtol(p, &end, 10);
                if(end == p)
                    break;
                p = end;
            }
            // 7 numbers: written before nudge existed.
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
                s.nudge  = static_cast<uint8_t>(ClampInt(static_cast<int>(v[7]), 0, 5));
            }
        }
        line = next;
    }
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

} // namespace x0x
