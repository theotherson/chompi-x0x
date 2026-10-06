// Renders the x0x voice and sequencer to WAV files for listening, and checks
// every render stays finite and within range.
//   make -C host && host/render host/out
#include "../code/src/x0x/machine.h"
#include "wav.h"
#include <chrono>

using namespace x0x;

static constexpr float  kSr    = 48000.f;
static constexpr size_t kBlock = 48;
static Machine          g_m;
static int              g_fail = 0;

struct Knob
{
    Param p;
    float v;
};

/** Plays pattern 1 for `seconds` with these knob settings. */
static void RenderPattern(const std::string& dir, const char* name, const Pattern& pat,
                          std::initializer_list<Knob> knobs, bool square, float seconds)
{
    Machine& m = g_m;
    m.Init(kSr);
    m.patterns[0] = pat;
    m.settings    = Settings{};
    for(auto& k : knobs)
        m.settings.params[k.p] = k.v;
    m.settings.square = square;
    m.Loaded();
    m.Play();
    std::vector<float> L, R;
    float              l[kBlock], r[kBlock], peak = 0.f;
    double             cpu = 0.0;
    const size_t       blocks = static_cast<size_t>(seconds * kSr / kBlock);
    for(size_t b = 0; b < blocks; b++)
    {
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

    RenderPattern(dir, "demo_saw", demo, {}, false, 8.f);
    RenderPattern(dir, "demo_square", demo, {}, true, 8.f);

    // Cutoff swept by hand would be the classic move: here, three settings.
    RenderPattern(dir, "dark_reso", demo, {{CUTOFF, .1f}, {RESONANCE, .9f}, {ENV_MOD, .8f}}, false, 4.f);
    RenderPattern(dir, "open_short", demo, {{CUTOFF, .6f}, {RESONANCE, .3f}, {DECAY, 0.f}}, false, 4.f);
    RenderPattern(dir, "max_everything", demo,
                  {{CUTOFF, 1.f}, {RESONANCE, 1.f}, {ENV_MOD, 1.f}, {DECAY, 1.f}, {ACCENT, 1.f},
                   {DRIVE, 1.f}, {VOLUME, 1.f}},
                  false, 4.f);

    // Accents in a row: should build up.
    Pattern acc;
    acc.Clear();
    for(int i = 0; i < kSteps; i++)
    {
        acc.steps[i].on     = true;
        acc.steps[i].accent = i >= 4 && i < 12;
    }
    RenderPattern(dir, "accent_run", acc, {{CUTOFF, .2f}, {RESONANCE, .8f}}, false, 4.f);

    // Slides up and down an octave.
    Pattern sl;
    sl.Clear();
    for(int i = 0; i < kSteps; i++)
    {
        sl.steps[i].on    = true;
        sl.steps[i].note  = i % 2 ? 12 : 0;
        sl.steps[i].slide = i % 4 < 2;
    }
    RenderPattern(dir, "slides", sl, {}, false, 4.f);

    // Swing at full.
    RenderPattern(dir, "swing", demo, {{SWING, 1.f}}, false, 4.f);

    printf(g_fail ? "%d FAILED\n" : "renders ok\n", g_fail);
    return g_fail ? 1 : 0;
}
