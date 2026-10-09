/** @file params.h
 *  @brief The panel's knob parameters, stored 0..1, with their defaults,
 *  MIDI CCs and real-unit conversions; the saved settings (current.txt) and
 *  the MIDI options (options.txt).
 */
#pragma once
#include "dsp.h"
#include "pattern.h"
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
    DELAY_FREE,      // the free delay time (push knob 4 and turn)
    DELAY_FREE_ON,   // the delay on its free time, not synced
    // The drums: each voice's level, attack (click) and decay, as x0x::Drum
    // (BD SD LT HT CY OH CH), then the accent level.
    DRUM_PARAMS,
    DRUM_ACCENT = DRUM_PARAMS + 3 * 7,
    MIX,             // bass / drums balance: centre both, left drums only, right bass only
    MIX_MUTE,        // the mutes: bit 0 the bass, bit 1 the drums (CHOMPI + LOOP, step mode)
    FX_MIX,          // CHOMPI + the mix: what feeds the delay and reverb, drums only .. both .. bass only
    DRUM_REVERB,     // the drums' effects (their knob 4): reverb send,
    REVERB_SIZE,     //   its size,
    DRUM_DELAY,      //   the shared delay's send,
    DRUM_CRUSH,      //   bit crush,
    DRUM_CRUSH_RATE, //   sample-rate reduction,
    DRUM_FILTER,     //   one-knob filter (low-pass left, high-pass right),
    DRUM_FILTER_RES, //   its resonance (CHOMPI + it),
    DRUM_FENV,       //   its envelope's amount (each hit opens it),
    DRUM_FENV_DECAY, //   and decay (CHOMPI + it),
    DRUM_LFO_SHAPE,  //   its LFO: off, triangle, ramp up, ramp down, S+H,
    DRUM_LFO_RATE,   //   and its rate, synced (kLfoBeats)
    DRUM_QUANTIZE,   // live drum recording: on the grid (on) or as played
    DRUM_QUANT_GRID, //   its grid: 1/16, 1/8, 1/4
    DRUM_ROLL_RATE,  // drum rolls: 1/8, 1/8T, 1/16, 1/16T, 1/32
    DRUM_DRIVE,      //   distortion (CHOMPI + volume on the drums' side)
    DRUM_FX_SENDS,   // which drum voices go to the reverb and delay (a mask: v * 127)
    COMP,            // the master compressor (volume knob, page 4)
    SIDECHAIN,       // CHOMPI + it: the kick ducks the rest (sidechain depth)
    // Each drum voice's tuning (centre: the 606's, +-24 semitones) and
    // feedback FM (CHOMPI + knobs 2 and 3), as x0x::Drum.
    DRUM_TUNE,
    DRUM_FM = DRUM_TUNE + 7,
    DRUM_PAN = DRUM_FM + 7, // each voice's pan (CHOMPI + knob 1), centre 0.5
    // Each voice's LFO target (kLfoTargets) and depth (knob 1, page 4).
    DRUM_LFO_TARGET = DRUM_PAN + 7,
    DRUM_LFO_DEPTH  = DRUM_LFO_TARGET + 7,
    NUM_PARAMS      = DRUM_LFO_DEPTH + 7
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
    {"tempo2",      .4f,      0, 0},   // 40..240 BPM, default 120 ("tempo": the old 60..200)
    {"swing",       0.f,      0, 16},
    {"quantize",    1.f,      2, 0},   // off, on
    {"quant_grid",  0.f,      3, 0},   // 1/16, 1/8, 1/4
    {"delay",       0.f,      0, 91},  // dry/wet
    {"delay_time",  .625f,    9, 92},  // 1/16T 1/16 1/8T 1/8 1/4T 3/16 1/4 3/8 1/2 (default 3/16)
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
    {"delay_free",  .55f,     0, 0},   // 30 ms .. 1.9 s, exponential (~300 ms)
    {"delay_free_on", 0.f,    2, 0},   // synced (0) or free (1)
    {"bd_level", .75f,      0, 0},
    {"bd_attack", .5f,       0, 0},
    {"bd_decay", .5f,       0, 0},
    {"sd_level", .75f,      0, 0},
    {"sd_attack", .5f,       0, 0},
    {"sd_decay", .5f,       0, 0},
    {"lt_level", .75f,      0, 0},
    {"lt_attack", .5f,       0, 0},
    {"lt_decay", .5f,       0, 0},
    {"ht_level", .75f,      0, 0},
    {"ht_attack", .5f,       0, 0},
    {"ht_decay", .5f,       0, 0},
    {"cy_level", .75f,      0, 0},
    {"cy_attack", .5f,       0, 0},
    {"cy_decay", .5f,       0, 0},
    {"oh_level", .75f,      0, 0},
    {"oh_attack", .5f,       0, 0},
    {"oh_decay", .5f,       0, 0},
    {"ch_level", .75f,      0, 0},
    {"ch_attack", .5f,       0, 0},
    {"ch_decay", .5f,       0, 0},
    {"drum_accent", .5f,     0, 0},   // accent: up to 3x as loud
    {"mix",         .5f,      0, 0},   // bass / drums
    {"mutes",       0.f,      4, 0},   // none / bass / drums / both
    {"fx_mix",      .5f,      0, 0},   // both at full
    {"drum_reverb", 0.f,      0, 0},
    {"reverb_size", .5f,      0, 0},
    {"drum_delay",  0.f,      0, 0},
    {"drum_crush",  0.f,      0, 0},
    {"drum_crush_rate", 0.f,  0, 0},
    {"drum_filter", .5f,      0, 0},   // centre: off
    {"drum_filter_res", 0.f,  0, 0},   // none
    {"drum_fenv",   0.f,      0, 0},   // off
    {"drum_fenv_decay", .4f,  0, 0},   // ~60 ms
    {"drum_lfo_shape", 0.f,   5, 0},   // off
    {"drum_lfo_rate", .2f,    11, 0},  // 1 bar
    {"drum_quantize", 1.f,    2, 0},   // on
    {"drum_quant_grid", 0.f,  3, 0},   // 1/16
    {"drum_roll_rate", .5f,   5, 0},   // 1/16
    {"drum_drive",  0.f,      0, 0},   // off by default
    {"drum_fx_sends", 1.f,    128, 0}, // every voice in
    {"comp",        0.f,      0, 0},   // off .. heavy
    {"sidechain",   0.f,      0, 0},   // none .. -20 dB on each kick
    {"bd_tune", .5f, 0, 0}, {"sd_tune", .5f, 0, 0}, {"lt_tune", .5f, 0, 0}, {"ht_tune", .5f, 0, 0},
    {"cy_tune", .5f, 0, 0}, {"oh_tune", .5f, 0, 0}, {"ch_tune", .5f, 0, 0},
    {"bd_fm", 0.f, 0, 0}, {"sd_fm", 0.f, 0, 0}, {"lt_fm", 0.f, 0, 0}, {"ht_fm", 0.f, 0, 0},
    {"cy_fm", 0.f, 0, 0}, {"oh_fm", 0.f, 0, 0}, {"ch_fm", 0.f, 0, 0},
    {"bd_pan", .5f, 0, 0}, {"sd_pan", .5f, 0, 0}, {"lt_pan", .5f, 0, 0}, {"ht_pan", .5f, 0, 0},
    {"cy_pan", .5f, 0, 0}, {"oh_pan", .5f, 0, 0}, {"ch_pan", .5f, 0, 0},
    {"bd_lfo", 0.f, 7, 0}, {"sd_lfo", 0.f, 7, 0}, {"lt_lfo", 0.f, 7, 0}, {"ht_lfo", 0.f, 7, 0},
    {"cy_lfo", 0.f, 7, 0}, {"oh_lfo", 0.f, 7, 0}, {"ch_lfo", 0.f, 7, 0},
    {"bd_lfo_depth", .5f, 0, 0}, {"sd_lfo_depth", .5f, 0, 0}, {"lt_lfo_depth", .5f, 0, 0}, {"ht_lfo_depth", .5f, 0, 0},
    {"cy_lfo_depth", .5f, 0, 0}, {"oh_lfo_depth", .5f, 0, 0}, {"ch_lfo_depth", .5f, 0, 0},
};
// clang-format on

/** Drum rolls' rates, in MIDI clock ticks: 1/8, 1/8T, 1/16, 1/16T, 1/32. */
constexpr int kRollRates           = 5;
constexpr int kRollTicks[kRollRates] = {12, 8, 6, 4, 3};

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
 *  [knob][page][CHOMPI held]. Clicking a knob steps through its pages
 *  (knob 1: 2, knob 4: 4, volume: 2); the big knob's click is tap tempo.
 *  Quantize and its grid are on the keys (live mode, CHOMPI + D#4). */
constexpr int     kMaxKnobPages       = 4;
constexpr int     kKnobPages[6]       = {2, 1, 1, 4, 1, 4};
constexpr uint8_t kKnobMap[6][kMaxKnobPages][2] = {
    {{WAVE, PULSE_WIDTH}, {kKnobLength, TUNING}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
    {{ENV_MOD, ACCENT}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
    {{DECAY, SLIDE_TIME}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
    {{DELAY, DELAY_TIME}, {DELAY_FB, DELAY_TONE}, {MOD, MOD_WIDTH}, {CRUSH, CRUSH_RATE}},
    {{CUTOFF, RESONANCE}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}, {kKnobNone, kKnobNone}},
    {{VOLUME, DRIVE}, {TEMPO, SWING}, {MIX, FX_MIX}, {COMP, SIDECHAIN}},
};

/** How far one click of a knob moves a continuous parameter, by how long
 *  since that knob's last click (ms): slow single clicks ~1 % (fine
 *  control), a normal turn 2-4 % (the range in a turn or two of a 20-24
 *  detent encoder), a fast spin ~7 %. */
inline float KnobStep(uint32_t dt_ms)
{
    const float dt = dt_ms < 1 ? 1.f : static_cast<float>(dt_ms);
    return Clamp((1.f / 96.f) * powf(250.f / dt, 0.9f), 1.f / 96.f, 1.f / 14.f);
}

/** How fast a knob turns a continuous parameter, relative to the rest:
 *  the envelope, accent and slide knobs cover their range a little sooner. */
inline float KnobSpeed(Param p)
{
    return (p == ENV_MOD || p == DECAY || p == ACCENT || p == SLIDE_TIME) ? 1.25f : 1.f;
}

/** The free delay time's knob value to ms (30 ms .. 1.9 s), and back. */
inline float DelayFreeMs(float v) { return 30.f * FastExp2(v * 5.985f); } // log2(1900 / 30)
inline float DelayFreeKnob(float ms) { return Clamp(log2f(ms / 30.f) / 5.985f, 0.f, 1.f); }

/** The bass's and the drums' gains for the mix knob (0 drums only, 0.5
 *  both, 1 bass only) and its CHOMPI layer (0 mute the bass, 0.5 neither,
 *  1 mute the drums). Each fades out over its half of the knob. */
/** mute: MIX_MUTE's value (bit 0 the bass, bit 1 the drums). */
inline void MixGains(float mix, float mute, float* bass, float* drums)
{
    const float m = Clamp(mix, 0.f, 1.f);
    *bass         = m < 0.5f ? sinf(kPi * m) : 1.f;          // 0 at the left end
    *drums        = m > 0.5f ? sinf(kPi * (1.f - m)) : 1.f;  // 0 at the right end
    const int mu  = StepIndex(mute, 4);
    if(mu & 1)
        *bass = 0.f;
    if(mu & 2)
        *drums = 0.f;
}

/** The tempo knob: 40 to 240 BPM. */
constexpr float kTempoMin  = 40.f;
constexpr float kTempoSpan = 200.f;
inline float    TempoBpm(float v) { return kTempoMin + kTempoSpan * v; }
inline float    TempoKnob(float bpm) { return Clamp((bpm - kTempoMin) / kTempoSpan, 0.f, 1.f); }

/** Quantize grid in steps: 1/16, 1/8, 1/4. */
inline int QuantGridSteps(float v)
{
    static constexpr int kGrid[3] = {1, 2, 4};
    return kGrid[StepIndex(v, 3)];
}

/** The cutoff and env mod knobs, fitted to a TB-303 (held notes and a
 *  test line recorded off one, resonance at full):
 *   - Cutoff sets where the sweep rests, in log2 Hz a + b v + c v^2 (in
 *     the voice's units, before Voice::kCutoffScale).
 *   - The filter envelope sweeps env_min + range * env^curve octaves: even
 *     at env mod's minimum a 303 sweeps most of an octave.
 *   - Env mod also lowers the resting cutoff, by `shift` of the depth it
 *     adds: on a 303 the top of the sweep rises as the bottom falls.
 *  Everything else (the decay's speed, why the resonance dies sooner the
 *  lower the cutoff) follows from these. */
struct FilterFit
{
    float cut_a   = 7.17f; // 144 Hz, 290 Hz at 12 o'clock, 1.5 kHz: the
    float cut_b   = 0.68f; // bottom half moves 1 octave, the top half 2.4
    float cut_c   = 2.72f;
    float env_min = 0.46f; // octaves
    float range   = 4.84f; // so 0.9 octave at 12 o'clock, 5.3 at full
    float curve   = 3.34f;
    float shift   = 0.28f;
};

inline void FilterToVoice(float cutoff, float env_mod, const FilterFit& f, VoiceParams& vp)
{
    const float added = f.range * powf(env_mod, f.curve);
    vp.env_oct        = f.env_min + added;
    vp.cutoff_hz      = FastExp2(f.cut_a + cutoff * (f.cut_b + f.cut_c * cutoff) - f.shift * added);
}

/** Knob values to the voice's units. */
inline void ToVoiceParams(const float* p, VoiceParams& vp)
{
    FilterToVoice(p[CUTOFF], p[ENV_MOD], FilterFit{}, vp);
    vp.resonance   = p[RESONANCE];
    // Fitted to a TB-303: the filter sweep is 90 % done in ~175 ms at the
    // shortest decay and ~2.3 s at the longest (decay_s is the time to 1 %).
    vp.decay_s     = KnobToExp(p[DECAY], 0.35f, 4.6f);
    vp.accent      = p[ACCENT];
    vp.tuning_st   = (p[TUNING] - 0.5f) * 2.f;
    vp.slide_s     = KnobToExp(p[SLIDE_TIME], 0.02f, 0.3f);
    vp.square      = StepIndex(p[WAVE], 2) == 1;
    // The knob's centre is a 47 % pulse, not 50: the 303 makes its square by
    // shaping the saw, and it comes out lopsided enough to keep even
    // harmonics 8-20 dB under the odd ones (fitted to recordings of one).
    vp.pulse_width = 0.07f + 0.8f * p[PULSE_WIDTH];
}

// ------------------------------------------------------------------ settings

/** What current.txt keeps: the knobs, the selected pattern, and whether
 *  the patterns are write-protected (edits not saved to the card). */
struct Settings
{
    float params[NUM_PARAMS];
    int   pattern      = 0;
    int   drum_pattern = -1; // the drums' own (-1: the bass's, as before it existed)
    bool  protect      = false;

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
    const int dp = s.drum_pattern >= 0 ? s.drum_pattern : s.pattern;
    const int w  = snprintf(buf + len, size - len, "pattern %d%c\ndrum_pattern %d%c\nprotect %d\n",
                           PatternNumber(s.pattern) + 1, PatternSide(s.pattern) ? 'B' : 'A', PatternNumber(dp) + 1,
                           PatternSide(dp) ? 'B' : 'A', s.protect ? 1 : 0);
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
            {
                const int i = ParsePatternName(sp + 1);
                if(i >= 0)
                    s.pattern = i;
            }
            else if(strcmp(line, "drum_pattern") == 0)
            {
                const int i = ParsePatternName(sp + 1);
                if(i >= 0)
                    s.drum_pattern = i;
            }
            else if(strcmp(line, "tempo") == 0)
            {
                // Saved before the range was 40..240: the same BPM.
                float v;
                if(ParseUnit(sp + 1, &v))
                    s.params[TEMPO] = TempoKnob(60.f + 140.f * v);
            }
            else if(strcmp(line, "protect") == 0)
                s.protect = atoi(sp + 1) != 0;
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
