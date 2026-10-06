// Desktop tests of the x0x core: sequencer timing, patterns, storage format.
//   make -C host && host/tests
#include "../code/src/x0x/ui.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace x0x;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do                                                                   \
    {                                                                    \
        if(!(c))                                                         \
        {                                                                \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while(0)

struct Ev
{
    double t; // seconds
    Sequencer::Event e;
};

static constexpr float  kSr    = 48000.f;
static constexpr size_t kBlock = 48;

/** Runs the sequencer on its internal clock for `seconds`. */
static std::vector<Ev> RunInternal(Sequencer& s, double seconds, bool ticks = false)
{
    std::vector<Ev> out;
    Sequencer::Event ev[Sequencer::kMaxEvents];
    const size_t blocks = static_cast<size_t>(seconds * kSr / kBlock);
    for(size_t b = 0; b < blocks; b++)
    {
        const int n = s.Process(kBlock, ev);
        for(int i = 0; i < n; i++)
            if(ticks || ev[i].type != Sequencer::Event::TICK)
                out.push_back({(b * kBlock + ev[i].offset) / kSr, ev[i]});
    }
    return out;
}

static std::vector<Ev> Of(const std::vector<Ev>& v, Sequencer::Event::Type t)
{
    std::vector<Ev> r;
    for(auto& e : v)
        if(e.e.type == t)
            r.push_back(e);
    return r;
}

static void TestDemoTiming()
{
    printf("demo pattern at 120 BPM\n");
    Pattern p;
    DemoPattern(p);
    Sequencer s;
    s.Init(kSr);
    s.SetTempo(120.f);
    s.SetPattern(&p);
    s.Start();
    auto ev = RunInternal(s, 2.0);
    auto steps = Of(ev, Sequencer::Event::STEP);
    CHECK(steps.size() == 16);
    for(size_t i = 0; i < steps.size(); i++)
        CHECK(fabs(steps[i].t - i * 0.125) < 1.0 / kSr + 1e-9); // sample-exact sixteenths

    // Step 0: on at 0, off half a step later.
    auto ons  = Of(ev, Sequencer::Event::NOTE_ON);
    auto offs = Of(ev, Sequencer::Event::NOTE_OFF);
    CHECK(ons[0].t == 0.0 && ons[0].e.note == 36 && ons[0].e.accent);
    CHECK(fabs(offs[0].t - 0.0625) < 1e-4);

    // Step 2 slides into step 3: step 3's note arrives legato, before step 2's off.
    bool slid = false;
    for(size_t i = 0; i + 1 < ev.size(); i++)
        if(ev[i].e.type == Sequencer::Event::NOTE_ON && ev[i].e.slide && fabs(ev[i].t - 0.375) < 1e-6)
        {
            slid = ev[i].e.note == 39 && ev[i + 1].e.type == Sequencer::Event::NOTE_OFF
                   && ev[i + 1].e.note == 48;
        }
    CHECK(slid);

    // Step 4 is tied into step 5: no off at 0.5625, the off comes at the end of step 5's gate.
    bool off_mid_tie = false, off_after_tie = false;
    for(auto& e : offs)
    {
        if(fabs(e.t - 0.5625) < 1e-6)
            off_mid_tie = true;
        if(fabs(e.t - 0.6875) < 1e-6 && e.e.note == 36)
            off_after_tie = true;
    }
    CHECK(!off_mid_tie);
    CHECK(off_after_tie);

    // Step 9 is off: nothing starts there.
    for(auto& e : ons)
        CHECK(fabs(e.t - 1.125) > 1e-6);
}

static void TestExternalMatchesInternal()
{
    printf("external clock gives the same notes\n");
    Pattern p;
    DemoPattern(p);
    Sequencer a, b;
    a.Init(kSr), b.Init(kSr);
    a.SetTempo(120.f), b.SetTempo(120.f);
    a.SetPattern(&p), b.SetPattern(&p);
    a.Start(), b.Start();
    auto ia = RunInternal(a, 2.0);
    std::vector<Sequencer::Event> eb;
    Sequencer::Event ev[Sequencer::kMaxEvents];
    for(int t = 0; t < 96; t++) // 4 beats of MIDI clock
    {
        const int n = b.ExternalTick(0, ev);
        for(int i = 0; i < n; i++)
            if(ev[i].type == Sequencer::Event::NOTE_ON || ev[i].type == Sequencer::Event::NOTE_OFF)
                eb.push_back(ev[i]);
    }
    std::vector<Sequencer::Event> ea;
    for(auto& e : ia)
        if(e.e.type == Sequencer::Event::NOTE_ON || e.e.type == Sequencer::Event::NOTE_OFF)
            ea.push_back(e.e);
    CHECK(ea.size() == eb.size());
    for(size_t i = 0; i < ea.size() && i < eb.size(); i++)
        CHECK(ea[i].type == eb[i].type && ea[i].note == eb[i].note && ea[i].slide == eb[i].slide);
}

static void TestSwingAndTicks()
{
    printf("swing and clock ticks\n");
    Pattern p;
    DemoPattern(p);
    Sequencer s;
    s.Init(kSr);
    s.SetTempo(120.f);
    s.SetSwing(1.f);
    s.SetPattern(&p);
    s.Start();
    auto ev    = RunInternal(s, 1.0, true);
    auto steps = Of(ev, Sequencer::Event::STEP);
    // Odd steps 2 ticks late: 2/24 of a beat = 1/24 s at 120 BPM.
    CHECK(fabs(steps[1].t - (0.125 + 1.0 / 24.0)) < 1e-4);
    CHECK(fabs(steps[2].t - 0.25) < 1e-4);
    auto ticks = Of(ev, Sequencer::Event::TICK);
    CHECK(ticks.size() == 48); // 24 PPQN, 2 beats in a second
}

static void TestQueueAndLength()
{
    printf("pattern queue and length\n");
    Pattern a, b;
    DemoPattern(a);
    b.Clear();
    b.length = 4;
    for(int i = 0; i < 4; i++)
    {
        b.steps[i].on   = true;
        b.steps[i].note = 7;
    }
    Sequencer s;
    s.Init(kSr);
    s.SetTempo(120.f);
    s.SetPattern(&a);
    s.Start();
    RunInternal(s, 0.5);
    s.Queue(&b);
    auto ev = RunInternal(s, 2.0);
    auto ch = Of(ev, Sequencer::Event::PATTERN_CHANGE);
    CHECK(ch.size() == 1 && fabs(ch[0].t + 0.5 - 2.0) < 1e-4); // at the end of the bar
    // Then 4-step bars: step 0 again every half second.
    int zeros = 0;
    for(auto& e : Of(ev, Sequencer::Event::STEP))
        if(e.e.step == 0)
            zeros++;
    CHECK(zeros == 1); // b's first bar starts at 1.5 s into this run; its next would be at 2.0
    for(auto& e : Of(ev, Sequencer::Event::STEP))
        if(e.t > 1.5 + 1e-6)
            CHECK(e.e.step < 4);
}

static void TestTextRoundTrip()
{
    printf("pattern text round trip\n");
    Pattern pats[kPatterns], back[kPatterns];
    DemoPattern(pats[0]);
    pats[3].length              = 7;
    pats[3].steps[6].on         = true;
    pats[3].steps[6].octave     = 1;
    pats[3].steps[6].note       = 24;
    static char buf[16384];
    const size_t n = WritePatterns(pats, kPatterns, buf, sizeof buf);
    CHECK(n > 0);
    buf[n] = '\0';
    ReadPatterns(buf, back, kPatterns);
    for(int i = 0; i < kPatterns; i++)
        CHECK(pats[i] == back[i]);
    char junk[] = "pattern 2\nlength 99\nstep 40 1 1 1 1 1 1\nstep 2 30 0 1 0 0 0\nhello\n";
    Pattern j[kPatterns];
    ReadPatterns(junk, j, kPatterns);
    CHECK(j[1].length == 16 && j[1].Empty());
}

// ------------------------------------------------------------ machine + panel

struct Rig
{
    Machine  m;
    Ui       ui;
    uint32_t now = 1000; // ms
    Rig()
    {
        m.Init(kSr);
        m.patterns[0].Clear();
        ui.Init(&m);
    }
    /** Runs the audio for ms milliseconds, a block at a time. */
    void Run(int ms)
    {
        float l[kBlock], r[kBlock];
        for(int i = 0; i < ms; i++)
        {
            m.Process(l, r, kBlock); // 48 samples = 1 ms
            now++;
            ui.Tick(now);
        }
    }
    void Key(int k) { ui.KeyDown(k, now), ui.KeyUp(k, now); }
    Step& S(int i) { return m.Current().steps[i]; }
};

static void TestStepEntry()
{
    printf("step mode: select, pitch with CHOMPI, toggle\n");
    Rig r;
    r.Key(Ui::kStepKeys[2]); // select step 3
    CHECK(r.ui.Selected() == 2 && !r.S(2).on);
    r.ui.Chompi(true);
    r.ui.KeyDown(19, r.now); // G4 on the keyboard
    CHECK(r.m.SoundingNote() == kBaseNote + 19); // auditions
    r.ui.KeyUp(19, r.now);
    r.ui.Chompi(false);
    CHECK(r.S(2).on && r.S(2).note == 19);
    r.Key(Ui::kStepKeys[2]); // tap the selected step: off, note kept
    CHECK(!r.S(2).on && r.S(2).note == 19);
    r.Key(Ui::kStepKeys[2]);
    CHECK(r.S(2).on);
    r.Key(Ui::kStepKeys[5]); // another step: only selects
    CHECK(r.ui.Selected() == 5 && !r.S(5).on);
    r.ui.Loop(r.now); // steps 9-16
    r.Key(Ui::kStepKeys[0]);
    CHECK(r.ui.Selected() == 8);
}

static void TestPages()
{
    printf("step mode: parameter pages, length, wave\n");
    Rig r;
    r.Key(Ui::kKeyAccent);
    CHECK(r.ui.GetPage() == Ui::Page::ACCENT);
    r.Key(Ui::kStepKeys[0]);
    r.Key(Ui::kStepKeys[3]);
    CHECK(r.S(0).accent && r.S(3).accent && !r.S(1).accent);
    r.Key(Ui::kStepKeys[3]);
    CHECK(!r.S(3).accent);
    r.Key(Ui::kKeyDown);
    r.Key(Ui::kStepKeys[1]);
    CHECK(r.S(1).octave == -1);
    r.Key(Ui::kKeyUp);
    r.Key(Ui::kStepKeys[1]);
    CHECK(r.S(1).octave == 1);
    r.Key(Ui::kKeySlide), r.Key(Ui::kStepKeys[4]);
    r.Key(Ui::kKeyTie), r.Key(Ui::kStepKeys[6]);
    CHECK(r.S(4).slide && r.S(6).tie);
    r.Key(Ui::kKeyTie);
    CHECK(r.ui.GetPage() == Ui::Page::NOTES); // the page key again: back to notes
    r.Key(Ui::kKeyLength), r.Key(Ui::kStepKeys[3]);
    CHECK(r.m.Current().length == 4);
    r.ui.Loop(r.now), r.Key(Ui::kStepKeys[7]);
    CHECK(r.m.Current().length == 16);
    const bool sq = r.m.settings.square;
    r.Key(Ui::kKeyWave);
    CHECK(r.m.settings.square != sq);
}

static void TestPatternsCopyClear()
{
    printf("step mode: patterns, copy, clear\n");
    Rig r;
    DemoPattern(r.m.patterns[0]);
    // Copy pattern 1 to pattern 6.
    r.ui.KeyDown(Ui::kKeyCopy, r.now);
    r.Key(Ui::kStepKeys[5]);
    r.ui.KeyUp(Ui::kKeyCopy, r.now);
    CHECK(r.m.patterns[5] == r.m.patterns[0]);
    // Stopped: picking a pattern switches at once.
    r.Key(Ui::kKeyPattern), r.Key(Ui::kStepKeys[5]);
    CHECK(r.m.CurrentPattern() == 5);
    // Running: it waits for the bar.
    r.Key(Ui::kStepKeys[0]);
    CHECK(r.m.CurrentPattern() == 0);
    r.ui.Play();
    r.Run(100);
    r.Key(Ui::kStepKeys[2]);
    CHECK(r.m.CurrentPattern() == 0 && r.m.QueuedPattern() == 2);
    r.Run(2000); // a bar at 120 BPM
    CHECK(r.m.CurrentPattern() == 2 && r.m.QueuedPattern() == -1);
    r.Key(Ui::kStepKeys[0]), r.Key(Ui::kStepKeys[0]); // twice: now
    CHECK(r.m.CurrentPattern() == 0);
    r.ui.Play();
    // CLEAR: a tap clears the selected step, holding clears the pattern.
    r.Key(Ui::kKeyPattern); // back to notes
    r.Key(Ui::kStepKeys[0]); // step 1 is already selected: toggles it, then back
    r.Key(Ui::kStepKeys[0]);
    CHECK(r.S(0).on);
    r.Key(Ui::kKeyClear);
    CHECK(!r.S(0).on && !r.S(0).accent);
    CHECK(!r.m.Current().Empty());
    r.ui.KeyDown(Ui::kKeyClear, r.now);
    r.Run(1100);
    r.ui.KeyUp(Ui::kKeyClear, r.now);
    CHECK(r.m.Current().Empty());
}

static void TestPitchModeRecording()
{
    printf("pitch mode: live play, record, ties, transpose\n");
    Rig r;
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.KeyDown(7, r.now); // stopped: just plays
    CHECK(r.m.SoundingNote() == kBaseNote + 7);
    r.ui.KeyUp(7, r.now);
    r.Run(20);
    CHECK(r.m.SoundingNote() == -1);
    CHECK(r.m.Current().Empty());

    r.ui.Loop(r.now); // record on
    CHECK(r.m.Recording());
    r.ui.Play();
    r.Run(10); // early in step 1 (steps are 125 ms)
    r.ui.KeyDown(3, r.now);
    r.Run(5);
    r.ui.KeyUp(3, r.now);
    CHECK(r.S(0).on && r.S(0).note == 3 && !r.S(0).tie);
    r.Run(100); // late in step 1 (at 115 ms): goes to step 2
    r.ui.KeyDown(10, r.now);
    r.Run(300); // held through steps 3 and 4
    r.ui.KeyUp(10, r.now);
    CHECK(r.S(1).on && r.S(1).note == 10);
    CHECK(r.S(2).on && r.S(2).tie && r.S(3).on && r.S(3).tie);
    CHECK(!r.S(4).on);

    r.ui.Chompi(true);
    r.ui.KeyDown(19, r.now); // G4: +7
    r.ui.KeyUp(19, r.now);
    r.ui.Chompi(false);
    CHECK(r.m.Transpose() == 7);
    r.ui.SetMode(Ui::Mode::STEP);
    CHECK(!r.m.Recording()); // leaving pitch mode stops recording
}

static void TestKnobsTempo()
{
    printf("knobs, tap tempo, MIDI clock\n");
    Rig r;
    const float c = r.m.settings.params[CUTOFF];
    r.ui.KnobTurn(4, 3, false);
    CHECK(fabsf(r.m.settings.params[CUTOFF] - (c + 0.024f)) < 1e-5);
    r.ui.KnobClick(4);
    CHECK(r.m.settings.params[CUTOFF] == kParams[CUTOFF].def);
    r.ui.Chompi(true);
    r.ui.KnobTurn(5, 10, false); // tempo +10 BPM
    CHECK(fabsf(TempoBpm(r.m.settings.params[TEMPO]) - 130.f) < 0.01f);
    for(int i = 0; i < 4; i++) // taps every 400 ms: 150 BPM
        r.ui.Loop(5000 + i * 400);
    CHECK(fabsf(TempoBpm(r.m.settings.params[TEMPO]) - 150.f) < 0.5f);
    r.ui.Chompi(false);

    // MIDI clock: Start, then 24 clocks a beat drive the steps.
    DemoPattern(r.m.patterns[0]);
    r.m.Loaded();
    r.m.MidiStart();
    float l[kBlock], rr[kBlock];
    uint32_t steps0 = r.m.StepCount();
    for(int t = 0; t < 48; t++) // two beats, one clock a block
    {
        r.m.MidiClock();
        r.m.Process(l, rr, kBlock);
    }
    CHECK(r.m.ExternalClock());
    CHECK(r.m.StepCount() - steps0 == 8);
    r.m.MidiStop();
    CHECK(!r.m.Running());
}

static void TestSettingsOptions()
{
    printf("settings and options text\n");
    Settings s;
    s.params[CUTOFF] = 0.1234f;
    s.square         = true;
    s.pattern        = 11;
    char buf[1024];
    const size_t n = WriteSettings(s, buf, sizeof buf);
    CHECK(n > 0);
    Settings b;
    ReadSettings(buf, b);
    CHECK(fabsf(b.params[CUTOFF] - 0.1234f) < 1e-4 && b.square && b.pattern == 11);
    Options o;
    o.channel_in = 10, o.cc_out = true, o.clock_out = false;
    CHECK(WriteOptions(o, buf, sizeof buf) > 0);
    Options ob;
    ReadOptions(buf, ob);
    CHECK(ob.channel_in == 10 && ob.cc_out && !ob.clock_out && ob.notes_out);
}

int main()
{
    TestDemoTiming();
    TestExternalMatchesInternal();
    TestSwingAndTicks();
    TestQueueAndLength();
    TestTextRoundTrip();
    TestStepEntry();
    TestPages();
    TestPatternsCopyClear();
    TestPitchModeRecording();
    TestKnobsTempo();
    TestSettingsOptions();
    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
