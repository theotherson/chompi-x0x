/** @file params.h
 *  @brief The panel's knob parameters, stored 0..1, with their defaults,
 *  MIDI CCs and real-unit conversions; the saved settings (current.txt) and
 *  the MIDI options (options.txt).
 */
#pragma once
#include "dsp.h"
#include "voice.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace x0x
{

enum Param : uint8_t
{
    CUTOFF,
    RESONANCE,
    ENV_MOD,
    DECAY,
    ACCENT,
    VOLUME,
    TUNING,
    SWING,
    DRIVE,
    SLIDE_TIME,
    TEMPO,
    NUM_PARAMS
};

struct ParamInfo
{
    const char* name; // key in current.txt, keep stable
    float       def;
    uint8_t     cc;   // MIDI CC in and out, 0 = none
};

// clang-format off
constexpr ParamInfo kParams[NUM_PARAMS] = {
    {"cutoff",     .35f,   74},
    {"resonance",  .6f,    71},
    {"env_mod",    .55f,   12},
    {"decay",      .35f,   13},
    {"accent",     .6f,    14},
    {"volume",     .7f,    7},
    {"tuning",     .5f,    15},   // -1..+1 semitone
    {"swing",      0.f,    16},
    {"drive",      .15f,   17},
    {"slide_time", .405f,  5},    // ~60 ms
    {"tempo",      60/140.f, 0},  // 60..200 BPM, default 120
};
// clang-format on

/** Knobs left to right: knobs 1-4, the big purple knob, volume; and what
 *  they set with CHOMPI held (NUM_PARAMS = nothing). */
constexpr Param kKnobParam[6]    = {RESONANCE, ENV_MOD, DECAY, ACCENT, CUTOFF, VOLUME};
constexpr Param kKnobAltParam[6] = {TUNING, SWING, DRIVE, SLIDE_TIME, NUM_PARAMS, TEMPO};

inline float TempoBpm(float v) { return 60.f + 140.f * v; }
inline float TempoKnob(float bpm) { return Clamp((bpm - 60.f) / 140.f, 0.f, 1.f); }

/** Knob values to the voice's units. */
inline void ToVoiceParams(const float* p, bool square, VoiceParams& vp)
{
    vp.cutoff_hz = KnobToExp(p[CUTOFF], 40.f, 4000.f);
    vp.resonance = p[RESONANCE];
    vp.env_oct   = 5.f * p[ENV_MOD];
    vp.decay_s   = KnobToExp(p[DECAY], 0.2f, 2.5f);
    vp.accent    = p[ACCENT];
    vp.tuning_st = (p[TUNING] - 0.5f) * 2.f;
    vp.drive     = p[DRIVE];
    vp.slide_s   = KnobToExp(p[SLIDE_TIME], 0.02f, 0.3f);
    vp.square    = square;
}

// ------------------------------------------------------------------ settings

/** What current.txt keeps: the knobs, the waveform, the pattern. */
struct Settings
{
    float params[NUM_PARAMS];
    bool  square  = false;
    int   pattern = 0;

    Settings()
    {
        for(int i = 0; i < NUM_PARAMS; i++)
            params[i] = kParams[i].def;
    }
};

/** Values as fixed-point "0.1234": the firmware's newlib-nano printf has no
 *  %f. @return bytes written, 0 if it didn't fit */
inline size_t WriteSettings(const Settings& s, char* buf, size_t size)
{
    size_t len = 0;
    for(int i = 0; i < NUM_PARAMS; i++)
    {
        const int t = static_cast<int>(Clamp(s.params[i], 0.f, 1.f) * 10000.f + 0.5f);
        const int w = snprintf(buf + len, size - len, "%s %d.%04d\n", kParams[i].name, t / 10000, t % 10000);
        if(w <= 0 || static_cast<size_t>(w) >= size - len)
            return 0;
        len += w;
    }
    const int w = snprintf(buf + len, size - len, "square %d\npattern %d\n", s.square ? 1 : 0, s.pattern + 1);
    if(w <= 0 || static_cast<size_t>(w) >= size - len)
        return 0;
    return len + w;
}

/** "0.1234", "1", "1.0000" -> 0..1; false for anything else. */
inline bool ParseUnit(const char* p, float* out)
{
    while(*p == ' ')
        p++;
    if(*p != '0' && *p != '1')
        return false;
    const int whole = *p++ - '0';
    float     frac = 0.f, scale = 0.1f;
    if(*p == '.')
    {
        p++;
        while(*p >= '0' && *p <= '9')
        {
            frac += (*p++ - '0') * scale;
            scale *= 0.1f;
        }
    }
    const float v = whole + frac;
    if(v > 1.f)
        return false;
    *out = v;
    return true;
}

/** @param text  NUL-terminated; modified while parsing */
inline void ReadSettings(char* text, Settings& s)
{
    char* line = text;
    while(line && *line)
    {
        char* next = strchr(line, '\n');
        if(next)
            *next++ = '\0';
        char* sp = strchr(line, ' ');
        if(sp)
        {
            *sp = '\0';
            if(strcmp(line, "square") == 0)
                s.square = atoi(sp + 1) != 0;
            else if(strcmp(line, "pattern") == 0)
                s.pattern = ClampInt(atoi(sp + 1), 1, 16) - 1;
            else
                for(int i = 0; i < NUM_PARAMS; i++)
                    if(strcmp(line, kParams[i].name) == 0)
                        ParseUnit(sp + 1, &s.params[i]);
        }
        line = next;
    }
}

// ------------------------------------------------------------------ options

/** MIDI options, from options.txt ("name value" lines; written with the
 *  defaults if missing). Channels are 1-16. */
struct Options
{
    int  channel_in    = 1;
    int  channel_out   = 1;
    bool clock_in      = true; // follow MIDI clock when it arrives
    bool clock_out     = true; // send MIDI clock while running on the internal clock
    bool transport_in  = true; // MIDI Start / Stop / Continue run the pattern
    bool transport_out = true;
    bool notes_out     = true;
    bool cc_in         = true;
    bool cc_out        = false;
};

inline size_t WriteOptions(const Options& o, char* buf, size_t size)
{
    const int w = snprintf(buf, size,
                           "midi_channel_in %d\nmidi_channel_out %d\nclock_in %d\nclock_out %d\n"
                           "transport_in %d\ntransport_out %d\nnotes_out %d\ncc_in %d\ncc_out %d\n",
                           o.channel_in, o.channel_out, o.clock_in, o.clock_out, o.transport_in,
                           o.transport_out, o.notes_out, o.cc_in, o.cc_out);
    return w > 0 && static_cast<size_t>(w) < size ? static_cast<size_t>(w) : 0;
}

inline void ReadOptions(char* text, Options& o)
{
    char* line = text;
    while(line && *line)
    {
        char* next = strchr(line, '\n');
        if(next)
            *next++ = '\0';
        char* sp = strchr(line, ' ');
        if(sp)
        {
            *sp         = '\0';
            const int v = atoi(sp + 1);
            if(strcmp(line, "midi_channel_in") == 0)
                o.channel_in = ClampInt(v, 1, 16);
            else if(strcmp(line, "midi_channel_out") == 0)
                o.channel_out = ClampInt(v, 1, 16);
            else if(strcmp(line, "clock_in") == 0)
                o.clock_in = v != 0;
            else if(strcmp(line, "clock_out") == 0)
                o.clock_out = v != 0;
            else if(strcmp(line, "transport_in") == 0)
                o.transport_in = v != 0;
            else if(strcmp(line, "transport_out") == 0)
                o.transport_out = v != 0;
            else if(strcmp(line, "notes_out") == 0)
                o.notes_out = v != 0;
            else if(strcmp(line, "cc_in") == 0)
                o.cc_in = v != 0;
            else if(strcmp(line, "cc_out") == 0)
                o.cc_out = v != 0;
        }
        line = next;
    }
}

} // namespace x0x
