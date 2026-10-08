// Renders the x0x voice and sequencer to WAV files for listening, and checks
// every render stays finite and within range.
//   make -C host && host/render host/out
#include "../code/src/x0x/machine.h"
#include "wav.h"
#include <chrono>
#include <functional>

using namespace x0x;

static constexpr float  kSr    = 48000.f;
static constexpr size_t kBlock = 48;
static Machine          g_m;
static Fx::Frame        g_delay[96000];
static int              g_fail = 0;

struct Knob
{
    Param p;
    float v;
};

/** Plays pattern 1 for `seconds` with these knob settings. */
static void RenderPattern(const std::string& dir, const char* name, const Pattern& pat,
                          std::initializer_list<Knob> knobs, float seconds,
                          std::function<void(Machine&, double)> during = nullptr)
{
    Machine& m = g_m;
    m.Init(kSr, g_delay, 96000);
    m.patterns[0] = pat;
    m.settings    = Settings{};
    for(auto& k : knobs)
        m.settings.params[k.p] = k.v;
    m.Loaded();
    m.Play();
    std::vector<float> L, R;
    float              l[kBlock], r[kBlock], peak = 0.f;
    double             cpu = 0.0;
    const size_t       blocks = static_cast<size_t>(seconds * kSr / kBlock);
    for(size_t b = 0; b < blocks; b++)
    {
        if(during)
            during(m, b * kBlock / kSr); // knob moves while it plays
        auto t0 = std::chrono::steady_clock::now();
        m.Process(l, r, kBlock);
        cpu += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for(size_t i = 0; i < kBlock; i++)
        {
            if(!std::isfinite(l[i]))
            {
                printf("  FAIL %s: not finite at %.3f s\n", name, (b * kBlock + i) / kSr);
                g_fail++;
                return;
            }
            peak = std::max(peak, fabsf(l[i]));
            L.push_back(l[i]), R.push_back(r[i]);
        }
    }
    WriteWav(dir + "/" + name + ".wav", L, R, static_cast<uint32_t>(kSr));
    printf("%-22s peak %.2f   %5.0fx realtime\n", name, peak, seconds / cpu);
    if(peak > 1.f || peak < 0.02f)
    {
        printf("  FAIL %s: peak out of range\n", name);
        g_fail++;
    }
}

int main(int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    Pattern demo;
    DemoPattern(demo);

    RenderPattern(dir, "demo_saw", demo, {}, 8.f);
    RenderPattern(dir, "demo_square", demo, {{WAVE, 1.f}}, 8.f);
    RenderPattern(dir, "narrow_pulse", demo, {{WAVE, 1.f}, {PULSE_WIDTH, .1f}}, 4.f);

    // Cutoff swept by hand would be the classic move: here, three settings.
    RenderPattern(dir, "dark_reso", demo, {{CUTOFF, .1f}, {RESONANCE, .9f}, {ENV_MOD, .8f}}, 4.f);
    RenderPattern(dir, "low_cut_max_reso", demo, {{CUTOFF, 0.f}, {RESONANCE, 1.f}}, 4.f);
    RenderPattern(dir, "open_short", demo, {{CUTOFF, .6f}, {RESONANCE, .3f}, {DECAY, 0.f}}, 4.f);
    RenderPattern(dir, "max_everything", demo,
                  {{CUTOFF, 1.f}, {RESONANCE, 1.f}, {ENV_MOD, 1.f}, {DECAY, 1.f}, {ACCENT, 1.f},
                   {DRIVE, 1.f}, {VOLUME, 1.f}},
                  4.f);

    // Accents in a row: should build up.
    Pattern acc;
    acc.Clear();
    for(int i = 0; i < kSteps; i++)
    {
        acc.steps[i].on     = true;
        acc.steps[i].accent = i >= 4 && i < 12;
    }
    RenderPattern(dir, "accent_run", acc, {{CUTOFF, .2f}, {RESONANCE, .8f}}, 4.f);

    // Slides up and down an octave.
    Pattern sl;
    sl.Clear();
    for(int i = 0; i < kSteps; i++)
    {
        sl.steps[i].on    = true;
        sl.steps[i].note  = i % 2 ? 12 : 0;
        sl.steps[i].slide = i % 4 < 2;
    }
    RenderPattern(dir, "slides", sl, {}, 4.f);

    // Effects: each on its own, then all at once.
    RenderPattern(dir, "fx_delay", demo, {{DELAY, .5f}, {DELAY_TIME, StepValue(2, kDelayDivisions)}}, 6.f);
    {
        // The demo bassline with a 606 beat in its drum part.
        Pattern both = demo;
        const char* rows[7] = {"x.....x...x.....", "....x.......x...", "...............x", "..............x.",
                               "x...............", "..x...x...x...x.", "x.x.x.x.x.x.x.x."};
        for(int v = 0; v < 7; v++)
            for(int i = 0; i < 16; i++)
                if(rows[v][i] == 'x')
                    both.drums[i] |= static_cast<uint8_t>(1 << v);
        for(int i : {0, 4, 8, 12})
            both.drums[i] |= kDrumAccent;
        RenderPattern(dir, "bass_and_drums", both, {}, 8.f);
    }
    // Synced time changed every 2 s (1/16, 3/16, 1/8T, 1/2, 1/4): crossfades.
    RenderPattern(dir, "fx_delay_synced_changes", demo, {{DELAY, .55f}, {DELAY_FB, .5f}, {DELAY_TIME, StepValue(1, 9)}},
                  12.f, [](Machine& m, double t) {
                      static const int kSeq[] = {1, 5, 2, 8, 6};
                      const int        i      = static_cast<int>(t / 2.0);
                      if(i < 5)
                          m.settings.params[DELAY_TIME] = StepValue(kSeq[i], 9);
                  });
    // Free time pushed about (300 ms, 900, 120, 600): the tape swoop.
    RenderPattern(dir, "fx_delay_free_swoops", demo,
                  {{DELAY, .55f}, {DELAY_FB, .5f}, {DELAY_FREE_ON, 1.f}, {DELAY_FREE, DelayFreeKnob(300.f)}}, 12.f,
                  [](Machine& m, double t) {
                      static const float kMs[] = {300.f, 900.f, 120.f, 600.f, 600.f};
                      const int          i     = static_cast<int>(t / 2.5);
                      if(i < 5)
                          m.settings.params[DELAY_FREE] = DelayFreeKnob(kMs[i]);
                  });
    RenderPattern(dir, "fx_tape_long", demo, {{DELAY, .5f}, {DELAY_FB, .8f}, {DELAY_TONE, .2f}}, 8.f);
    RenderPattern(dir, "fx_tape_selfosc", demo, {{DELAY, .6f}, {DELAY_FB, 1.f}, {DELAY_TONE, .8f}}, 8.f);
    RenderPattern(dir, "fx_crush_bits", demo, {{CRUSH, .7f}}, 4.f);
    RenderPattern(dir, "fx_crush_rate", demo, {{CRUSH_RATE, .6f}}, 4.f);
    RenderPattern(dir, "fx_chorus", demo, {{MOD, .3f}}, 4.f);
    RenderPattern(dir, "fx_chorus_wide", demo, {{MOD, .3f}, {MOD_WIDTH, 1.f}}, 4.f);
    RenderPattern(dir, "fx_flanger", demo, {{MOD, .9f}}, 4.f);
    RenderPattern(dir, "drive_low", demo, {{DRIVE, .3f}}, 4.f);
    RenderPattern(dir, "drive_high", demo, {{DRIVE, 1.f}}, 4.f);
    RenderPattern(dir, "fx_all_max", demo,
                  {{DELAY, 1.f}, {DELAY_FB, 1.f}, {CRUSH, 1.f}, {CRUSH_RATE, 1.f}, {MOD, 1.f}, {MOD_WIDTH, 1.f},
                   {RESONANCE, 1.f}, {DRIVE, 1.f}, {VOLUME, 1.f}},
                  6.f);

    // Swing at full.
    RenderPattern(dir, "swing", demo, {{SWING, 1.f}}, 4.f);

    printf(g_fail ? "%d FAILED\n" : "renders ok\n", g_fail);
    return g_fail ? 1 : 0;
}
