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
    SLIDE_TIME,
    WAVE,
    PULSE_WIDTH,
    TUNING,
    TEMPO,
    SWING,
    QUANTIZE,
    QUANT_GRID,
    DELAY,
    DELAY_TIME,
    DELAY_FB,
    DELAY_TONE,
    MOD,
    MOD_WIDTH,
    CRUSH,
    CRUSH_RATE,
    VOLUME,
    DRIVE,
    ARP_MODE,
    ARP_OCT_DOWN,
    ARP_OCT_UP,
    ARP_ON,
    NUM_PARAMS
};

struct ParamInfo
{
    const char* name;  // key in current.txt, keep stable
    float       def;
    uint8_t     steps; // 0 = continuous, else number of positions
    uint8_t     cc;    // MIDI CC in and out, 0 = none
};

// clang-format off
constexpr ParamInfo kParams[NUM_PARAMS] = {
    {"cutoff",      .35f,     0, 74},
    {"resonance",   .6f,      0, 71},
    {"env_mod",     .55f,     0, 12},
    {"decay",       .35f,     0, 13},
    {"accent",      .6f,      0, 14},
    {"slide_time",  .405f,    0, 5},   // ~60 ms
    {"wave",        0.f,      2, 70},  // saw, square
    {"pulse_width", .5f,      0, 77},
    {"tuning",      .5f,      0, 15},  // -1..+1 semitone
    {"tempo",       60/140.f, 0, 0},   // 60..200 BPM, default 120
    {"swing",       0.f,      0, 16},
    {"quantize",    1.f,      2, 0},   // off, on
    {"quant_grid",  0.f,      3, 0},   // 1/16, 1/8, 1/4
    {"delay",       0.f,      0, 91},  // dry/wet
    {"delay_time",  .4f,      6, 92},  // 1/16 1/8 3/16 1/4 3/8 1/2
    {"delay_fb",    .35f,     0, 94},  // feedback, just past self-oscillation at the top
    {"delay_tone",  .4f,      0, 95},  // tape EQ: dark .. bright
    {"mod",         0.f,      0, 93},  // off, chorus .. flanger
    {"mod_width",   .5f,      0, 0},   // stereo width / depth
    {"crush",       0.f,      0, 18},  // bit depth
    {"crush_rate",  0.f,      0, 19},  // sample-rate reduction
    {"volume",      .7f,      0, 7},
    {"drive",       0.f,      0, 17},  // hard clip, off by default
    {"arp_mode",    0.f,      5, 0},   // up, down, up-down, random, as played (CHOMPI + G#4)
    {"arp_oct_down", 0.f,     3, 0},   // octaves below the chord, 0-2 (CHOMPI + F#4)
    {"arp_oct_up",  0.f,      3, 0},   // octaves above, 0-2 (CHOMPI + A#4)
    {"arp_on",      0.f,      2, 0},   // CHOMPI + F#4 in live mode
};
// clang-format on

/** Index of a stepped parameter's position. */
inline int StepIndex(float v, int steps)
{
    const int i = static_cast<int>(v * (steps - 1) + 0.5f);
    return ClampInt(i, 0, steps - 1);
}

inline float StepValue(int idx, int steps)
{
    return steps > 1 ? static_cast<float>(ClampInt(idx, 0, steps - 1)) / (steps - 1) : 0.f;
}

/** What a knob sets: a parameter, the pattern's length, or nothing. */
constexpr uint8_t kKnobLength = 254;
constexpr uint8_t kKnobNone   = 255;

/** Knobs left to right: knobs 1-4, the big purple knob, volume.
 *  [knob][page][CHOMPI held]. Clicking knobs 1-4 steps through their pages;
 *  the big knob and volume have one. */
constexpr int     kMaxKnobPages       = 4;
constexpr int     kKnobPages[6]       = {2, 2, 2, 4, 1, 1};
constexpr uint8_t kKnobMap[6][kMaxKnobPages][2] = {
    {{WAVE, PULSE_WIDTH}, {kKnobLength, TUNING}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
    {{ENV_MOD, DECAY}, {ACCENT, SLIDE_TIME}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
    {{TEMPO, SWING}, {QUANTIZE, QUANT_GRID}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
    {{DELAY, DELAY_TIME}, {DELAY_FB, DELAY_TONE}, {MOD, MOD_WIDTH}, {CRUSH, CRUSH_RATE}},
    {{CUTOFF, RESONANCE}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
    {{VOLUME, DRIVE}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
};

inline float TempoBpm(float v) { return 60.f + 140.f * v; }
inline float TempoKnob(float bpm) { return Clamp((bpm - 60.f) / 140.f, 0.f, 1.f); }

/** Quantize grid in steps: 1/16, 1/8, 1/4. */
inline int QuantGridSteps(float v)
{
    static constexpr int kGrid[3] = {1, 2, 4};
    return kGrid[StepIndex(v, 3)];
}

/** Knob values to the voice's units. */
inline void ToVoiceParams(const float* p, VoiceParams& vp)
{
    vp.cutoff_hz   = KnobToExp(p[CUTOFF], 40.f, 4000.f);
    vp.resonance   = p[RESONANCE];
    vp.env_oct     = 5.f * p[ENV_MOD];
    // Fitted to a TB-303: the filter sweep is 90 % done in ~175 ms at the
    // shortest decay and ~2 s at the longest (decay_s is the time to 1 %).
    vp.decay_s     = KnobToExp(p[DECAY], 0.35f, 4.0f);
    vp.accent      = p[ACCENT];
    vp.tuning_st   = (p[TUNING] - 0.5f) * 2.f;
    vp.slide_s     = KnobToExp(p[SLIDE_TIME], 0.02f, 0.3f);
    vp.square      = StepIndex(p[WAVE], 2) == 1;
    vp.pulse_width = 0.1f + 0.8f * p[PULSE_WIDTH];
}

// ------------------------------------------------------------------ settings

/** What current.txt keeps: the knobs and the selected pattern. */
struct Settings
{
    float params[NUM_PARAMS];
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
    const int w = snprintf(buf + len, size - len, "pattern %d\n", s.pattern + 1);
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
            if(strcmp(line, "square") == 0) // written by earlier versions
                s.params[WAVE] = atoi(sp + 1) ? 1.f : 0.f;
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
