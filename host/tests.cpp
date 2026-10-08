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

static Fx::Frame g_delay[96000];

struct Rig
{
    Machine  m;
    Ui       ui;
    uint32_t now = 1000; // ms
    Rig()
    {
        m.Init(kSr, g_delay, 96000);
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
    void White(int w) { Key(Ui::kWhite[w]); }  // white key w+1
    void Black(int b) { Key(Ui::kBlack[b]); }  // black key b+1
    Step& S(int i) { return m.Current().steps[i]; }
};

static void TestStepKeys()
{
    printf("step mode: 15 white keys, middle C shared\n");
    Rig r;
    r.White(2);
    CHECK(r.ui.Selected() == 2);
    r.White(7); // middle C, lower half: step 8
    CHECK(r.ui.Selected() == 7);
    r.White(8); // white key 9: step 10
    CHECK(r.ui.Selected() == 9);
    r.White(7); // middle C after the upper half: step 9
    CHECK(r.ui.Selected() == 8);
    r.White(14);
    CHECK(r.ui.Selected() == 15);
    r.White(0);
    r.White(7);
    CHECK(r.ui.Selected() == 7);
    r.Key(Ui::kKeyView); // D#4 flips middle C by hand
    r.White(7);
    CHECK(r.ui.Selected() == 8);

    // CHOMPI: the keybed sets the selected step's note.
    r.White(3);
    r.ui.Chompi(true);
    r.ui.KeyDown(19, r.now);
    CHECK(r.m.SoundingNote() == kBaseNote + 19);
    r.ui.KeyUp(19, r.now);
    r.ui.Chompi(false);
    CHECK(r.S(3).on && r.S(3).note == 19);
    r.White(3); // the selected step again: off, note kept
    CHECK(!r.S(3).on && r.S(3).note == 19);
}

static void TestFollowPlayhead()
{
    printf("step mode: middle C follows the playhead\n");
    Rig r;
    r.S(0).on  = true;
    r.S(11).on = true; // the second half has a note
    r.ui.Play();
    r.Run(125 * 3 + 10); // step 4
    CHECK(!r.ui.SecondHalf());
    r.Run(125 * 6); // step 10
    CHECK(r.ui.SecondHalf());
    r.ui.Play();
    r.Run(5);
    r.S(11).on = false; // only the first half has notes: no following
    r.ui.Play();
    r.Run(125 * 9 + 10);
    CHECK(!r.ui.SecondHalf());
}

static void TestPagesCopyClear()
{
    printf("step mode: pages, patterns, copy, clear\n");
    Rig r;
    r.Black(2); // ACCENT page
    CHECK(r.ui.GetPage() == Ui::Page::ACCENT);
    r.White(0), r.White(10);
    CHECK(r.S(0).accent && r.S(11).accent);
    r.Black(0), r.White(1);
    CHECK(r.S(1).octave == -1);
    r.Black(1), r.White(1);
    CHECK(r.S(1).octave == 1);
    r.Black(3), r.White(4);
    r.Black(4), r.White(5);
    CHECK(r.S(4).slide && r.S(5).tie);
    r.Black(4);
    CHECK(r.ui.GetPage() == Ui::Page::NOTES);

    DemoPattern(r.m.patterns[0]);
    r.ui.KeyDown(Ui::kKeyCopy, r.now);
    r.White(14); // copy to pattern 16
    r.ui.KeyUp(Ui::kKeyCopy, r.now);
    CHECK(r.m.patterns[15] == r.m.patterns[0]);
    r.Black(7), r.White(14); // pattern page (F#4): pattern 16
    CHECK(r.m.CurrentPattern() == 15);
    r.White(0);
    r.ui.Play();
    r.Run(100);
    r.White(2);
    CHECK(r.m.CurrentPattern() == 0 && r.m.QueuedPattern() == 2);
    r.Run(2000);
    CHECK(r.m.CurrentPattern() == 2);
    r.ui.Play();
    r.White(0), r.White(0);
    r.Black(7); // back to notes

    // CLEAR: a tap clears the selected step, holding clears the pattern.
    r.White(1);
    r.White(0);
    CHECK(r.ui.Selected() == 0 && r.S(0).on);
    r.Key(Ui::kKeyClear);
    CHECK(!r.S(0).on);
    CHECK(!r.m.Current().Empty());
    r.ui.KeyDown(Ui::kKeyClear, r.now);
    r.Run(1100);
    r.ui.KeyUp(Ui::kKeyClear, r.now);
    CHECK(r.m.Current().Empty());
}

static void TestKnobPages()
{
    printf("knobs: two pages, CHOMPI layer, tap tempo\n");
    Rig r;
    const float* p = r.m.settings.params;
    r.ui.KnobTurn(0, 1, false); // knob 1 page 1: wave
    CHECK(StepIndex(p[WAVE], 2) == 1);
    r.ui.Chompi(true);
    const float pw = p[PULSE_WIDTH];
    r.ui.KnobTurn(0, -5, false);
    CHECK(p[PULSE_WIDTH] < pw);
    r.ui.Chompi(false);
    r.ui.KnobClick(0, r.now); // page 2: length
    CHECK(r.ui.KnobPage(0) == 1);
    r.ui.KnobTurn(0, -1, false), r.ui.KnobTurn(0, -1, false), r.ui.KnobTurn(0, -1, false);
    CHECK(r.m.Current().length == 13);
    r.ui.Chompi(true);
    r.ui.KnobTurn(0, 5, false); // tuning
    CHECK(p[TUNING] > 0.5f);
    r.ui.Chompi(false);

    // Knob 2: env mod / accent; knob 3: decay / slide time; one page each.
    const float env = p[ENV_MOD];
    r.ui.KnobTurn(1, 3, false);
    CHECK(p[ENV_MOD] > env);
    r.ui.Chompi(true), r.ui.KnobTurn(1, 3, false), r.ui.Chompi(false); // accent
    CHECK(p[ACCENT] > kParams[ACCENT].def);
    r.ui.KnobTurn(2, -3, false); // decay
    CHECK(p[DECAY] < kParams[DECAY].def);
    r.ui.Chompi(true), r.ui.KnobTurn(2, 3, false), r.ui.Chompi(false); // slide time
    CHECK(p[SLIDE_TIME] > kParams[SLIDE_TIME].def);
    r.ui.KnobClick(1, r.now), r.ui.KnobClick(2, r.now);
    CHECK(r.ui.KnobPage(1) == 0 && r.ui.KnobPage(2) == 0);

    // Volume, page 2: tempo / swing.
    r.ui.KnobClick(5, r.now);
    CHECK(r.ui.KnobPage(5) == 1);
    r.ui.KnobTurn(5, 10, false); // tempo +10
    CHECK(fabsf(TempoBpm(p[TEMPO]) - 130.f) < 0.01f);
    r.ui.Chompi(true), r.ui.KnobTurn(5, 4, false), r.ui.Chompi(false); // swing
    CHECK(p[SWING] > 0.f);
    r.ui.KnobClick(5, r.now);
    CHECK(r.ui.KnobPage(5) == 0);

    r.ui.KnobTurn(3, 20, false); // delay
    CHECK(p[DELAY] > 0.f);
    r.ui.Chompi(true), r.ui.KnobTurn(3, 2, false), r.ui.Chompi(false); // delay time
    CHECK(StepIndex(p[DELAY_TIME], kDelayDivisions) == 3); // one turn event = one position
    r.ui.KnobClick(3, r.now); // page 2: tape feedback / tone
    r.ui.KnobTurn(3, 10, false);
    r.ui.Chompi(true), r.ui.KnobTurn(3, -10, false), r.ui.Chompi(false);
    CHECK(p[DELAY_FB] > kParams[DELAY_FB].def && p[DELAY_TONE] < kParams[DELAY_TONE].def);
    r.ui.KnobClick(3, r.now); // page 3: chorus/flanger / width
    r.ui.KnobTurn(3, 10, false);
    r.ui.Chompi(true), r.ui.KnobTurn(3, 10, false), r.ui.Chompi(false);
    CHECK(p[MOD] > 0.f && p[MOD_WIDTH] > kParams[MOD_WIDTH].def);
    r.ui.KnobClick(3, r.now); // page 4: crush bits / rate
    r.ui.KnobTurn(3, 10, false);
    r.ui.Chompi(true), r.ui.KnobTurn(3, 10, false), r.ui.Chompi(false);
    CHECK(p[CRUSH] > 0.f && p[CRUSH_RATE] > 0.f);
    r.ui.KnobClick(3, r.now);
    CHECK(r.ui.KnobPage(3) == 0); // four pages, then round again
    {
        // Every knob page lights at least a fifth, even at zero.
        LedFrame f;
        r.ui.Draw(f, r.now);
        for(int k = 0; k < 6; k++)
            CHECK(f.knob[k].r + f.knob[k].g + f.knob[k].b > 0.15f);
    }

    r.ui.Chompi(true), r.ui.KnobTurn(4, 5, false), r.ui.Chompi(false); // resonance
    CHECK(p[RESONANCE] > kParams[RESONANCE].def);
    r.ui.Chompi(true), r.ui.KnobTurn(5, 5, false), r.ui.Chompi(false); // drive
    CHECK(p[DRIVE] > kParams[DRIVE].def);

    for(int i = 0; i < 4; i++) // big knob clicks every 400 ms: 150 BPM
        r.ui.KnobClick(4, 5000 + i * 400);
    CHECK(fabsf(TempoBpm(p[TEMPO]) - 150.f) < 0.5f);
}

static void TestRealtimeRecording()
{
    printf("pitch mode: live play, real-time record, quantize, transpose\n");
    Rig r;
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.KeyDown(7, r.now);
    CHECK(r.m.SoundingNote() == kBaseNote + 7);
    r.ui.KeyUp(7, r.now);

    r.ui.Play(); // running, then record
    r.ui.Loop(r.now);
    CHECK(r.m.Recording() && !r.ui.NoteEntry());
    r.Run(10); // early in step 1
    r.ui.KeyDown(3, r.now), r.Run(5), r.ui.KeyUp(3, r.now);
    CHECK(r.S(0).on && r.S(0).note == 3 && r.S(0).nudge == 0);
    r.Run(100); // late in step 1: step 2
    r.ui.KeyDown(10, r.now), r.Run(300), r.ui.KeyUp(10, r.now); // held through 3 and 4
    CHECK(r.S(1).on && r.S(1).note == 10);
    CHECK(r.S(2).tie && r.S(3).tie && !r.S(4).on);

    // Grid 1/8: a note late in step 6 goes to step 7 (index 6), the nearest eighth.
    r.m.SetParam(QUANT_GRID, StepValue(1, 3));
    while(r.m.CurrentStep() != 5)
        r.Run(1);
    r.Run(90);
    r.ui.KeyDown(5, r.now), r.ui.KeyUp(5, r.now);
    CHECK(r.S(6).on && r.S(6).note == 5 && !r.S(5).on);

    // Quantize off: a note half way through step 9 is nudged 3 ticks.
    r.m.SetParam(QUANTIZE, 0.f);
    while(r.m.CurrentStep() != 8)
        r.Run(1);
    r.Run(62);
    r.ui.KeyDown(9, r.now), r.ui.KeyUp(9, r.now);
    CHECK(r.S(8).on && r.S(8).nudge == 3);

    // CHOMPI + black keys while recording: flags on the step playing now.
    r.m.SetParam(QUANTIZE, 1.f);
    while(r.m.CurrentStep() != 10)
        r.Run(1);
    r.Run(20);
    r.ui.Chompi(true);
    r.Black(2); // accent
    r.Black(3); // slide
    r.Black(4); // tie
    r.ui.Chompi(false);
    CHECK(r.S(10).accent && r.S(10).slide && r.S(10).tie && r.S(10).on);

    // Transpose mode: CHOMPI + C#4 latches it; keys then set the transpose.
    r.ui.Chompi(true), r.Key(Ui::kKeyTranspose), r.ui.Chompi(false);
    CHECK(r.ui.TransposeMode());
    r.ui.KeyDown(19, r.now), r.ui.KeyUp(19, r.now); // +7
    CHECK(r.m.Transpose() == 7 && r.m.SoundingNote() != kBaseNote + 19);
    r.ui.Chompi(true), r.Key(Ui::kKeyTranspose), r.ui.Chompi(false);
    CHECK(!r.ui.TransposeMode());
    r.ui.SetMode(Ui::Mode::STEP);
    CHECK(!r.m.Recording());
}

static void TestNoteEntry()
{
    printf("pitch mode: note entry, keyboard octave, LOOP hold clears\n");
    Rig r;
    DemoPattern(r.m.patterns[0]);
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.Loop(r.now); // stopped + record = note entry
    CHECK(r.ui.NoteEntry() && r.ui.Cursor() == 0);
    r.Key(0), r.Key(7);
    r.ui.Chompi(true), r.Black(1), r.ui.Chompi(false); // keyboard up an octave
    CHECK(r.ui.KeyboardOctave() == 1);
    r.ui.KeyDown(12, r.now);
    CHECK(r.m.SoundingNote() == kBaseNote + 24);
    r.ui.KeyUp(12, r.now);
    CHECK(r.m.Current().length == 3);
    CHECK(r.S(0).note == 0 && r.S(1).note == 7 && r.S(2).note == 12 && r.S(2).octave == 1);
    r.ui.Chompi(true);
    r.Black(2); // accent on the last step
    r.Black(3); // slide
    r.Black(4); // a tie
    r.Black(9); // a rest (A#4)
    r.Black(0); // keyboard back down
    r.ui.Chompi(false);
    CHECK(r.S(2).accent && r.S(2).slide);
    CHECK(r.S(3).on && r.S(3).tie);
    CHECK(!r.S(4).on);
    CHECK(r.m.Current().length == 5);
    CHECK(r.ui.KeyboardOctave() == 0);
    {
        // The rest just entered shows grey on its key.
        LedFrame f;
        r.ui.Draw(f, 0); // a moment when the next-step blink is off
        const Rgb c = f.key[Ui::kWhite[4]];
        CHECK(c.r > 0.2f && c.r == c.g && c.g == c.b);
    }
    // CHOMPI + D#4 switches the view of steps 1-8 / 9-16.
    const bool second = r.ui.SecondHalf();
    r.ui.Chompi(true), r.Key(Ui::kKeyView), r.ui.Chompi(false);
    CHECK(r.ui.SecondHalf() != second);

    // LOOP held 2 s clears the pattern (and doesn't toggle record).
    r.ui.LoopDown(r.now);
    r.Run(1500);
    CHECK(!r.m.Current().Empty());
    r.Run(600);
    r.ui.LoopUp(r.now);
    CHECK(r.m.Current().Empty() && !r.m.Recording());
}

static void TestArp()
{
    printf("pitch mode: arpeggiator\n");
    Rig r;
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.Chompi(true), r.ui.Play(), r.ui.Chompi(false); // CHOMPI + PLAY: arp on
    CHECK(r.m.ArpOn());
    // Stopped: it runs on its own clock, sixteenths at 120 BPM, up.
    r.ui.KeyDown(0, r.now), r.ui.KeyDown(4, r.now), r.ui.KeyDown(7, r.now);
    std::vector<int> heard;
    int last = -2;
    for(int i = 0; i < 1000; i++)
    {
        r.Run(1);
        const int n = r.m.SoundingNote();
        if(n != last && n >= 0)
            heard.push_back(n);
        last = n;
    }
    CHECK(heard.size() >= 7 && heard.size() <= 9); // 8 sixteenths in a second
    CHECK(heard.size() > 3 && heard[0] == kBaseNote && heard[1] == kBaseNote + 4
          && heard[2] == kBaseNote + 7 && heard[3] == kBaseNote);
    r.ui.KeyUp(0, r.now), r.ui.KeyUp(4, r.now), r.ui.KeyUp(7, r.now);
    r.Run(200);
    CHECK(r.m.SoundingNote() == -1); // stops when let go

    // Latch: keeps going after the keys are let go.
    r.ui.Chompi(true), r.ui.Loop(r.now), r.ui.Chompi(false); // CHOMPI + LOOP: latch
    CHECK(r.m.ArpLatch());
    r.ui.KeyDown(2, r.now), r.ui.KeyUp(2, r.now);
    r.Run(300);
    bool sounded = false;
    for(int i = 0; i < 200; i++)
        r.Run(1), sounded |= r.m.SoundingNote() == kBaseNote + 2;
    CHECK(sounded);

    // Latched: each press adds a note, a second press takes it out.
    r.ui.KeyDown(9, r.now), r.ui.KeyUp(9, r.now); // add A3
    CHECK(r.m.GetArp().Count() == 2);
    r.ui.KeyDown(2, r.now), r.ui.KeyUp(2, r.now); // D3 again: out
    CHECK(r.m.GetArp().Count() == 1 && r.m.GetArp().NoteAt(0) == kBaseNote + 9);

    // Lights: the latched note steady in the arp colour, and a white flash
    // on each note as it plays.
    {
        LedFrame f;
        bool     flashed = false, steady = false;
        for(int i = 0; i < 300; i++)
        {
            r.Run(1);
            r.ui.NoteStep(r.now);
            r.ui.Draw(f, r.now);
            const Rgb k = f.key[9];
            flashed |= k.r > 0.9f && k.g > 0.9f && k.b > 0.9f;
            steady |= k.r == 0.f && k.g > 0.3f && k.b > 0.2f;
        }
        CHECK(flashed && steady);
    }

    // Running and recording: the arpeggio lands in the steps.
    r.ui.Chompi(true), r.ui.Loop(r.now), r.ui.Chompi(false); // unlatch
    r.Run(300);
    r.ui.Play();
    r.ui.Loop(r.now); // record
    r.ui.KeyDown(0, r.now), r.ui.KeyDown(12, r.now);
    r.Run(510); // steps 1-4 and the start of 5
    r.ui.KeyUp(0, r.now), r.ui.KeyUp(12, r.now);
    CHECK(r.S(1).on && r.S(2).on && r.S(3).on);
    CHECK(r.S(1).note != r.S(2).note);

    // CHOMPI + G#4: pattern; F#4 / A#4: octaves down / up, cycling 0-2.
    r.ui.Play(); // stop
    r.ui.Loop(r.now); // record off (note entry is where A#4 adds rests)
    r.ui.Chompi(true);
    r.Key(Ui::kBlack[8]);
    CHECK(StepIndex(r.m.settings.params[ARP_MODE], 5) == 1);
    r.Key(Ui::kBlack[7]), r.Key(Ui::kBlack[9]), r.Key(Ui::kBlack[9]);
    CHECK(StepIndex(r.m.settings.params[ARP_OCT_DOWN], 3) == 1);
    CHECK(StepIndex(r.m.settings.params[ARP_OCT_UP], 3) == 2);
    r.Key(Ui::kBlack[9]);
    CHECK(StepIndex(r.m.settings.params[ARP_OCT_UP], 3) == 0); // round again
    r.ui.Chompi(false);
    {
        // White keys 1-5 show the octaves: -1 and 0 here.
        LedFrame f;
        r.ui.Draw(f, r.now);
        CHECK(f.key[Ui::kWhite[2]].r > 0.9f);                            // the chord's own
        CHECK(f.key[Ui::kWhite[1]].g > 0.4f && f.key[Ui::kWhite[0]].g < 0.1f); // -1 lit, -2 not
        CHECK(f.key[Ui::kWhite[3]].g < 0.1f);                            // +1 not
    }
    // An octave down and up: a single held C3 plays C2, C3 (down 1, up 0), up mode.
    r.m.SetParam(ARP_MODE, 0.f);
    r.ui.Chompi(true), r.ui.Loop(r.now), r.ui.Chompi(false); // unlatch
    r.ui.KeyDown(0, r.now);
    std::vector<int> notes;
    int prev = -2;
    for(int i = 0; i < 400; i++)
    {
        r.Run(1);
        const int n = r.m.SoundingNote();
        if(n != prev && n >= 0)
            notes.push_back(n);
        prev = n;
    }
    r.ui.KeyUp(0, r.now);
    CHECK(notes.size() >= 2 && notes[0] == kBaseNote - 12 && notes[1] == kBaseNote);
    CHECK(r.ui.KnobPage(2) == 0);
    r.ui.KnobClick(2, r.now), r.ui.KnobClick(2, r.now);
    CHECK(r.ui.KnobPage(2) == 0); // knob 3 has two pages again
}

static void TestLiveLights()
{
    printf("pitch mode lights, LOOP tap tempo in step mode\n");
    Rig r;
    DemoPattern(r.m.patterns[0]);
    r.ui.SetMode(Ui::Mode::PITCH);
    LedFrame f;
    r.ui.Draw(f, r.now);
    bool any = false;
    for(int w = 0; w < 15; w++)
        any |= f.key[Ui::kWhite[w]].r > 0.f;
    CHECK(!any); // stopped, not recording: no step lights
    r.ui.Play();
    r.Run(30);
    r.ui.Draw(f, r.now + 200);
    any = false;
    for(int w = 0; w < 15; w++)
        any |= f.key[Ui::kWhite[w]].r > 0.f;
    CHECK(any);
    r.ui.Play();
    // Transpose mode: C#4 dim, the amount bright.
    r.ui.Chompi(true), r.Key(Ui::kKeyTranspose), r.ui.Chompi(false);
    r.Key(14); // +2
    r.ui.Draw(f, r.now);
    CHECK(f.key[Ui::kKeyTranspose].r < 0.3f && f.key[14].r > 0.9f);

    r.ui.SetMode(Ui::Mode::STEP);
    for(int i = 0; i < 4; i++) // LOOP taps every 500 ms: 120 BPM
        r.ui.Loop(8000 + i * 500);
    CHECK(fabsf(TempoBpm(r.m.settings.params[TEMPO]) - 120.f) < 0.5f);
}

static void TestNudgeTiming()
{
    printf("nudged steps play late\n");
    Pattern p;
    p.Clear();
    p.steps[0].on    = true;
    p.steps[1].on    = true;
    p.steps[1].nudge = 3;
    p.steps[2].on    = true;
    p.steps[2].nudge = 5;
    Sequencer s;
    s.Init(kSr);
    s.SetTempo(120.f);
    s.SetPattern(&p);
    s.Start();
    auto ev  = RunInternal(s, 0.5);
    auto ons = Of(ev, Sequencer::Event::NOTE_ON);
    auto offs = Of(ev, Sequencer::Event::NOTE_OFF);
    const double tick = 0.5 / 24.0; // at 120 BPM
    CHECK(ons.size() == 3);
    CHECK(fabs(ons[1].t - (0.125 + 3 * tick)) < 1e-4);
    CHECK(fabs(ons[2].t - (0.25 + 5 * tick)) < 1e-4);
    // Step 3's gate stops at step 4's start, not 3 ticks after it started.
    CHECK(fabs(offs[2].t - 0.375) < 1e-4);
}

static void TestStepModeExtras()
{
    printf("step mode: transpose mode, view key, knob reset, step lights\n");
    Rig r;
    // C#4 turns transpose mode on; keys set the transpose; holding C#4 2 s
    // ends it, keeping the amount.
    r.Key(Ui::kKeyTranspose);
    CHECK(r.ui.TransposeMode());
    r.Key(7); // G3: -5
    CHECK(r.m.Transpose() == -5 && r.ui.Selected() == 0);
    r.ui.KeyDown(Ui::kKeyTranspose, r.now);
    r.Run(2100);
    r.ui.KeyUp(Ui::kKeyTranspose, r.now);
    CHECK(!r.ui.TransposeMode() && r.m.Transpose() == -5);
    // D#4 flips the view; middle C follows it.
    r.Key(Ui::kKeyView);
    CHECK(r.ui.SecondHalf());
    r.White(7);
    CHECK(r.ui.Selected() == 8);
    r.Key(Ui::kKeyView);
    CHECK(!r.ui.SecondHalf());

    // CHOMPI + click: both functions of the knob's page back to defaults.
    r.ui.KnobTurn(1, 10, false);
    r.ui.Chompi(true), r.ui.KnobTurn(1, 10, false);
    r.ui.KnobClick(1, r.now);
    r.ui.Chompi(false);
    CHECK(r.m.settings.params[ENV_MOD] == kParams[ENV_MOD].def);
    CHECK(r.m.settings.params[DECAY] == kParams[DECAY].def);
    r.ui.KnobClick(0, r.now); // page 2: length / tuning
    r.ui.KnobTurn(0, -4, false);
    r.ui.Chompi(true), r.ui.KnobClick(0, r.now), r.ui.Chompi(false);
    CHECK(r.m.Current().length == 16);
    // Knob 4: CHOMPI + click resets every effect, whatever page it's on.
    for(Param fx : {DELAY, DELAY_TIME, DELAY_FB, DELAY_TONE, MOD, MOD_WIDTH, CRUSH, CRUSH_RATE})
        r.m.SetParam(fx, kParams[fx].def < 0.5f ? 0.9f : 0.1f);
    r.ui.KnobClick(3, r.now), r.ui.KnobClick(3, r.now); // page 3
    r.ui.Chompi(true), r.ui.KnobClick(3, r.now), r.ui.Chompi(false);
    for(Param fx : {DELAY, DELAY_TIME, DELAY_FB, DELAY_TONE, MOD, MOD_WIDTH, CRUSH, CRUSH_RATE})
        CHECK(r.m.settings.params[fx] == kParams[fx].def);
    CHECK(r.ui.KnobPage(3) == 2); // the page stays where it was
    r.ui.Chompi(true), r.ui.KnobClick(4, r.now), r.ui.Chompi(false); // big knob: no tap, reset
    CHECK(r.m.settings.params[CUTOFF] == kParams[CUTOFF].def);

    r.Run(1300); // let the length display (from the knob above) clear
    // Step lights: on red, accent bright, tie dim; the other half dimmed.
    r.S(1).on = true;
    r.S(2).on = r.S(2).accent = true;
    r.S(3).on = r.S(3).tie = true;
    r.S(12).on = true; // shown on white key 12 (index 11)
    r.White(0);        // view: steps 1-8, step 1 selected
    LedFrame f;
    r.ui.Draw(f, r.now);
    const Rgb on = f.key[Ui::kWhite[1]], acc = f.key[Ui::kWhite[2]], tie = f.key[Ui::kWhite[3]];
    CHECK(on.r > 0.f && on.g == 0.f && acc.r > on.r && tie.r > 0.f && tie.r < on.r);
    const Rgb far = f.key[Ui::kWhite[11]];
    CHECK(far.r > 0.f && far.r < on.r * 0.5f);
}

static void TestTransposeC()
{
    printf("transpose mode: C#4 as the amount, exits, brightness\n");
    Rig r;
    // Step mode: enter; C#4 is a note like any other (+1), as often as you
    // like (unless tapped twice quickly); holding it 2 s exits, and the hold
    // doesn't change the amount.
    r.Key(Ui::kKeyTranspose);
    r.Key(Ui::kKeyTranspose);
    CHECK(r.ui.TransposeMode() && r.m.Transpose() == 1);
    LedFrame f;
    r.ui.Draw(f, r.now);
    CHECK(f.key[Ui::kKeyTranspose].r > 0.9f);
    r.Run(400);
    r.Key(16);
    r.Run(400);
    r.Key(Ui::kKeyTranspose);
    r.Run(400);
    r.Key(Ui::kKeyTranspose);
    CHECK(r.ui.TransposeMode() && r.m.Transpose() == 1);
    r.Run(400);
    r.Key(16);
    CHECK(r.m.Transpose() == 4);
    r.ui.Draw(f, r.now);
    CHECK(f.key[Ui::kKeyTranspose].r < 0.3f && f.key[16].r > 0.9f);
    r.ui.KeyDown(Ui::kKeyTranspose, r.now);
    r.Run(1500);
    CHECK(r.ui.TransposeMode()); // not yet
    r.Run(600);
    r.ui.KeyUp(Ui::kKeyTranspose, r.now);
    CHECK(!r.ui.TransposeMode() && r.m.Transpose() == 4);

    // A key tapped twice quickly sets its transpose and ends transpose mode,
    // in step mode (C#4 included) and in pitch mode; slower taps don't.
    r.Key(Ui::kKeyTranspose);
    r.Run(400);
    r.Key(7), r.Run(100), r.Key(7);
    CHECK(!r.ui.TransposeMode() && r.m.Transpose() == -5);
    r.Key(Ui::kKeyTranspose);
    r.Run(400);
    r.Key(Ui::kKeyTranspose), r.Run(100), r.Key(Ui::kKeyTranspose);
    CHECK(!r.ui.TransposeMode() && r.m.Transpose() == 1);
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.Chompi(true), r.Key(Ui::kKeyTranspose), r.ui.Chompi(false);
    r.Key(14), r.Run(500), r.Key(14);
    CHECK(r.ui.TransposeMode() && r.m.Transpose() == 2);
    r.Run(100), r.Key(14);
    CHECK(!r.ui.TransposeMode() && r.m.Transpose() == 2);
    r.ui.SetMode(Ui::Mode::STEP);
    r.Key(Ui::kKeyTranspose);
    r.Run(400);
    r.Key(16);
    r.Run(400);

    // In transpose mode the white keys show only the playhead.
    DemoPattern(r.m.patterns[0]);
    r.Key(Ui::kKeyTranspose);
    r.ui.Play();
    r.Run(40);
    r.ui.NoteStep(r.now), r.ui.Draw(f, r.now);
    int red = 0;
    for(int w = 0; w < 15; w++)
        red += f.key[Ui::kWhite[w]].r > 0.f && f.key[Ui::kWhite[w]].g == 0.f;
    CHECK(red == 1);
    r.ui.Play();
    r.ui.KeyDown(Ui::kKeyTranspose, r.now), r.Run(2100), r.ui.KeyUp(Ui::kKeyTranspose, r.now);

    // Pitch mode: CHOMPI + C#4 enters; C#4 picked is as bright as any key.
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.Chompi(true), r.Key(Ui::kKeyTranspose), r.ui.Chompi(false);
    r.Key(Ui::kKeyTranspose);
    CHECK(r.m.Transpose() == 1);
    r.ui.Draw(f, r.now);
    CHECK(f.key[Ui::kKeyTranspose].r > 0.9f);
}

static void TestArpTranspose()
{
    printf("pitch mode: transpose moves the latched arpeggio, not the pattern\n");
    Rig r;
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.Chompi(true), r.ui.Play(), r.ui.Loop(r.now), r.ui.Chompi(false); // arp on, latch
    r.Key(0), r.Key(4); // C3, E3 latched
    r.ui.Chompi(true), r.Key(Ui::kKeyTranspose), r.ui.Chompi(false); // transpose mode
    r.Key(19); // +7
    CHECK(r.m.ArpTranspose() == 7 && r.m.Transpose() == 0);
    CHECK(r.m.GetArp().Count() == 2); // the key didn't join the chord

    std::vector<int> heard;
    bool flashed = false, teal = false;
    LedFrame f;
    int last = -2;
    for(int i = 0; i < 600; i++)
    {
        r.Run(1);
        const int n = r.m.SoundingNote();
        if(n != last && n >= 0)
            heard.push_back(n);
        last = n;
        r.ui.NoteStep(r.now);
        r.ui.Draw(f, r.now);
        for(int k : {0, 4})
        {
            flashed |= f.key[k].r > 0.9f && f.key[k].b > 0.9f;
            teal |= f.key[k].r == 0.f && f.key[k].g > 0.3f;
        }
        CHECK(f.key[19].r > 0.9f && f.key[19].b == 0.f); // the amount, yellow
    }
    CHECK(heard.size() >= 3 && (heard[0] == kBaseNote + 7 || heard[0] == kBaseNote + 11));
    CHECK(flashed && !teal); // flashes on, the steady chord hidden

    // Out of transpose mode: the chord is back; the arpeggio stays up 7.
    r.ui.Chompi(true), r.Key(Ui::kKeyTranspose), r.ui.Chompi(false);
    r.ui.Draw(f, r.now);
    CHECK(f.key[0].g > 0.3f || f.key[4].g > 0.3f);
    CHECK(r.m.ArpTranspose() == 7);

    // Arpeggiator off: transpose goes back to the pattern.
    r.ui.Chompi(true), r.ui.Play(), r.Key(Ui::kKeyTranspose), r.ui.Chompi(false);
    r.Key(10); // -2
    CHECK(r.m.Transpose() == -2 && r.m.ArpTranspose() == 7);
}

static void TestLengthShown()
{
    printf("length knob shows the length on the keys, both modes\n");
    for(Ui::Mode mode : {Ui::Mode::STEP, Ui::Mode::PITCH})
    {
        Rig r;
        r.ui.SetMode(mode);
        r.Run(5);
        r.ui.KnobClick(0, r.now); // knob 1 page 2: length
        for(int i = 0; i < 4; i++)
            r.ui.KnobTurn(0, -1, false); // 16 -> 12
        CHECK(r.m.Current().length == 12);
        LedFrame f;
        r.ui.Draw(f, r.now);
        // Step 12 is on white key 11 (index 10): bright cyan (past step 8);
        // steps 1-11 dim; 13-16 dark.
        CHECK(f.key[Ui::kWhite[10]].r < 0.1f && f.key[Ui::kWhite[10]].b > 0.9f);
        CHECK(f.key[Ui::kWhite[0]].r > 0.05f && f.key[Ui::kWhite[0]].r < 0.3f);
        CHECK(f.key[Ui::kWhite[7]].r > 0.05f); // middle C = step 9, within
        CHECK(f.key[Ui::kWhite[11]].r == 0.f && f.key[Ui::kWhite[14]].r == 0.f);
        r.Run(1300);
        r.ui.Draw(f, r.now);
        CHECK(!(f.key[Ui::kWhite[10]].r > 0.9f && f.key[Ui::kWhite[10]].g > 0.9f)); // gone again
        // Length 8: middle C is the last step.
        for(int i = 0; i < 4; i++)
            r.ui.KnobTurn(0, -1, false);
        r.ui.Draw(f, r.now);
        CHECK(f.key[Ui::kMiddleC].g > 0.9f);
    }
}

static void TestShortcutsAndLights()
{
    printf("CHOMPI + PLAY / LOOP for the arp, beat lights, colours\n");
    Rig r;
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.Chompi(true);
    r.ui.Play(); // arp on, transport untouched
    CHECK(r.m.ArpOn() && !r.m.Running());
    r.ui.LoopDown(r.now), r.ui.LoopUp(r.now); // latch, record untouched
    CHECK(r.m.ArpLatch() && !r.m.Recording());
    LedFrame f;
    r.ui.Draw(f, r.now);
    CHECK(f.play.b > 0.9f && f.play.r == 0.f);   // cyan: arp on
    CHECK(f.loop.r > 0.9f && f.loop.b == 0.f);   // orange: latched
    r.ui.Play(), r.ui.LoopDown(r.now), r.ui.LoopUp(r.now);
    CHECK(!r.m.ArpOn() && !r.m.ArpLatch());
    r.ui.Chompi(false);

    // Stopped: the big knob's LEDs just show cutoff (purple), both.
    r.ui.Draw(f, r.now);
    CHECK(f.knob[4].b > 0.f && f.knob[4].r == f.big_right.r && f.knob[4].b == f.big_right.b);

    // Running: PLAY green; the big knob's LEDs flash on steps 1 and 9 left,
    // 5 and 13 right, yellow, a little dimmer than full.
    DemoPattern(r.m.patterns[0]);
    r.ui.Play();
    auto at_step = [&](int st) {
        while(r.m.CurrentStep() != st)
            r.Run(1);
        r.Run(20);
        r.ui.NoteStep(r.now), r.ui.Draw(f, r.now);
    };
    at_step(0);
    CHECK(f.play.g > 0.f && f.play.r == 0.f);
    CHECK(f.knob[4].r > 0.5f && f.knob[4].r < 0.9f && f.knob[4].b == 0.f && f.big_right.r == 0.f);
    at_step(1);
    r.Run(80); // between beats: both dark
    r.ui.Draw(f, r.now);
    CHECK(f.knob[4].r == 0.f && f.big_right.r == 0.f);
    at_step(4);
    CHECK(f.knob[4].r == 0.f && f.big_right.r > 0.5f);
    at_step(8);
    CHECK(f.knob[4].r > 0.5f && f.big_right.r == 0.f);
    at_step(12);
    CHECK(f.knob[4].r == 0.f && f.big_right.r > 0.5f);

    // A recorded note flashes the beat side red.
    r.ui.Loop(r.now); // record on
    at_step(13);
    r.ui.KeyDown(5, r.now), r.ui.KeyUp(5, r.now);
    r.ui.NoteStep(r.now), r.ui.Draw(f, r.now);
    CHECK(f.big_right.r > 0.5f && f.big_right.g == 0.f); // steps 13-16: the right side

    // Turning cutoff shows cutoff on both, for a moment.
    r.ui.KnobTurn(4, 1, false);
    r.ui.Draw(f, r.now);
    CHECK(f.knob[4].b > 0.f && f.big_right.b > 0.f);
    r.Run(1300);
    r.ui.NoteStep(r.now), r.ui.Draw(f, r.now);
    CHECK(f.knob[4].b == 0.f && f.big_right.b == 0.f);

    // Transpose mode key: dim yellow.
    r.ui.SetMode(Ui::Mode::STEP);
    r.ui.Draw(f, r.now);
    const Rgb t = f.key[Ui::kKeyTranspose];
    CHECK(t.r > 0.f && t.r < 0.3f && t.g > 0.5f * t.r && t.b == 0.f);
}

static void TestLivePlayhead()
{
    printf("pitch mode: one red light moving in tempo, dark on rests\n");
    Rig r;
    DemoPattern(r.m.patterns[0]); // step 10 (index 9) is a rest
    r.ui.SetMode(Ui::Mode::PITCH);
    r.ui.Play();
    LedFrame f;
    for(int step = 0; step < 12; step++)
    {
        while(r.m.CurrentStep() != step)
            r.Run(1);
        r.Run(40); // well inside the step
        r.ui.NoteStep(r.now);
        r.ui.Draw(f, r.now);
        int lit = 0;
        for(int w = 0; w < 15; w++)
            lit += f.key[Ui::kWhite[w]].r > 0.f;
        CHECK(lit == (r.S(step).on ? 1 : 0));
    }
}

/** The ladder's gain at f (Hz) for a quiet sine, run at 96 kHz. */
static double LadderGainDb(float wc, float k, double f)
{
    DiodeLadder d;
    d.Init(96000.f);
    d.SetCutoff(wc);
    double peak = 0.0;
    for(int i = 0; i < 96000; i++)
    {
        const float y = d.Process(0.01f * sinf(2.f * kPi * static_cast<float>(f * i / 96000.0)), k);
        if(i > 48000)
            peak = std::max(peak, static_cast<double>(fabsf(y)));
    }
    return 20.0 * log10(peak / 0.01);
}

static void TestDiodeLadder()
{
    printf("diode ladder matches Stinchcombe's transfer function\n");
    const double c[5] = {1.0, pow(2.0, 2.75), 10.0 * sqrt(2.0), pow(2.0, 3.25), 1.0};
    auto theory = [&](double f, double wc, double k) {
        const double w  = f / wc;
        const double re = c[0] * pow(w, 4) - c[2] * w * w + c[4] + k; // D(jw) + k
        const double im = -c[1] * pow(w, 3) + c[3] * w;
        return -10.0 * log10(re * re + im * im);
    };
    for(float k : {0.f, 8.5f, Voice::kMaxLoopGain})
        for(double f : {100.0, 300.0, 480.0, 600.0, 1000.0})
        {
            // Relative to 50 Hz, so the loop high-pass's tiny effect there cancels.
            const double got  = LadderGainDb(480.f, k, f) - LadderGainDb(480.f, k, 50.0);
            const double want = theory(f, 480.0, k) - theory(50.0, 480.0, k);
            if(fabs(got - want) > 1.5)
                printf("    k %.1f f %.0f: got %.1f want %.1f\n", k, f, got, want);
            CHECK(fabs(got - want) <= 1.5);
        }
    // At high cutoffs (where env mod pushes it) the resonance must not run
    // away: it once reached +60 dB above 6 kHz with a one-step loop delay.
    for(float wc : {6000.f, 12000.f, 16000.f})
    {
        double peak = -100.0;
        for(double f = 0.8 * wc; f < 1.3 * wc; f += 0.02 * wc)
            peak = std::max(peak, LadderGainDb(wc, Voice::kMaxLoopGain, f));
        const double rel = peak - LadderGainDb(wc, Voice::kMaxLoopGain, 0.05 * wc);
        // No more than 3 dB over the transfer function's own peak.
        double want = -100.0;
        for(double w = 0.8; w < 1.3; w += 0.002)
            want = std::max(want, theory(w * wc, wc, Voice::kMaxLoopGain) - theory(0.05 * wc, wc, Voice::kMaxLoopGain));
        if(rel > want + 3.0)
            printf("    wc %.0f: peak +%.1f dB, transfer function +%.1f\n", wc, rel, want);
        CHECK(rel < want + 3.0);
    }
}

static void TestSettingsOptions()
{
    printf("settings and options text\n");
    Settings s;
    s.params[CUTOFF] = 0.1234f;
    s.params[WAVE]   = 1.f;
    s.pattern        = 11;
    char buf[2048];
    const size_t n = WriteSettings(s, buf, sizeof buf);
    CHECK(n > 0);
    Settings b;
    ReadSettings(buf, b);
    CHECK(fabsf(b.params[CUTOFF] - 0.1234f) < 1e-4 && b.params[WAVE] == 1.f && b.pattern == 11);
    char old[] = "square 1\npattern 2\n"; // an earlier version's file
    Settings c;
    ReadSettings(old, c);
    CHECK(c.params[WAVE] == 1.f && c.pattern == 1);
    Options o;
    o.channel_in = 10, o.cc_out = true, o.clock_out = false;
    CHECK(WriteOptions(o, buf, sizeof buf) > 0);
    Options ob;
    ReadOptions(buf, ob);
    CHECK(ob.channel_in == 10 && ob.cc_out && !ob.clock_out && ob.notes_out);
}

static void TestMidiClock()
{
    printf("MIDI clock drives the steps\n");
    Rig r;
    DemoPattern(r.m.patterns[0]);
    r.m.Loaded();
    r.m.MidiStart();
    float l[kBlock], rr[kBlock];
    const uint32_t steps0 = r.m.StepCount();
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

static void TestKnobColours()
{
    printf("knob LEDs: each CHOMPI function its own colour; faster envelope knobs\n");
    Rig r;
    for(int k = 0; k < 6; k++)
        for(int page = 0; page < kKnobPages[k]; page++)
        {
            LedFrame a, b;
            r.ui.Draw(a, r.now);
            r.ui.Chompi(true), r.ui.Draw(b, r.now), r.ui.Chompi(false);
            // Compare hues: normalise out the brightness (the value).
            auto norm = [](Rgb c) {
                const float m = std::max(c.r, std::max(c.g, c.b)) + 1e-6f;
                return Rgb{c.r / m, c.g / m, c.b / m};
            };
            const Rgb   x = norm(a.knob[k]), y = norm(b.knob[k]);
            const float d = fabsf(x.r - y.r) + fabsf(x.g - y.g) + fabsf(x.b - y.b);
            if(kKnobMap[k][page][1] != kKnobNone)
                CHECK(d > 0.3f);
            if(k < 4)
                r.ui.KnobClick(k, r.now);
        }
    // Env mod covers its range in fewer clicks than cutoff.
    const float e0 = r.m.settings.params[ENV_MOD], c0 = r.m.settings.params[CUTOFF];
    r.ui.KnobTurn(1, 5, false), r.ui.KnobTurn(4, 5, false);
    CHECK(r.m.settings.params[ENV_MOD] - e0 > 1.5f * (r.m.settings.params[CUTOFF] - c0));
}

static void TestLiveQuantizeAndLights()
{
    printf("live mode: quantize key and grid; tempo, PLAY and CHOMPI lights\n");
    Rig r;
    const float* p = r.m.settings.params;
    r.ui.SetMode(Ui::Mode::PITCH);
    // CHOMPI + D#4: quantize on/off; while on, white keys 1-3 pick the grid.
    const bool q0 = StepIndex(p[QUANTIZE], 2) == 1;
    r.ui.Chompi(true), r.Key(Ui::kKeyView);
    CHECK((StepIndex(p[QUANTIZE], 2) == 1) != q0);
    if(StepIndex(p[QUANTIZE], 2) == 0)
        r.Key(Ui::kKeyView);
    CHECK(StepIndex(p[QUANTIZE], 2) == 1);
    r.White(1);
    CHECK(QuantGridSteps(p[QUANT_GRID]) == 2);
    LedFrame f;
    r.ui.Draw(f, r.now);
    CHECK(f.key[Ui::kWhite[1]].b > 0.9f && f.key[Ui::kWhite[0]].b < 0.2f);
    CHECK(f.key[Ui::kKeyView].b > 0.9f);
    r.Key(Ui::kKeyView); // off: the grid keys go dark and don't change it
    CHECK(StepIndex(p[QUANTIZE], 2) == 0);
    r.White(2);
    CHECK(QuantGridSteps(p[QUANT_GRID]) == 2);
    r.ui.Chompi(false);
    // Note entry (record on, stopped): CHOMPI + D#4 is the view key again.
    r.ui.Loop(r.now);
    CHECK(r.ui.NoteEntry());
    const bool half = r.ui.SecondHalf();
    r.ui.Chompi(true), r.Key(Ui::kKeyView), r.ui.Chompi(false);
    CHECK(r.ui.SecondHalf() != half && StepIndex(p[QUANTIZE], 2) == 0);
    r.ui.Loop(r.now);

    // Volume page 2 (tempo): the light flashes the beat, stopped or running.
    r.ui.KnobClick(5, r.now);
    float lo = 9.f, hi = 0.f;
    for(int i = 0; i < 100; i++)
    {
        r.Run(10);
        r.ui.Draw(f, r.now);
        const float b = f.knob[5].r + f.knob[5].g;
        lo = std::min(lo, b), hi = std::max(hi, b);
    }
    CHECK(hi > 1.5f && lo < 0.5f);

    // Running: PLAY steady green; CHOMPI flashes on note steps.
    DemoPattern(r.m.patterns[0]);
    r.m.TogglePlay();
    float play_lo = 9.f, play_hi = 0.f, ch_lo = 9.f, ch_hi = 0.f;
    for(int i = 0; i < 200; i++)
    {
        r.Run(5);
        r.ui.NoteStep(r.now);
        r.ui.Draw(f, r.now);
        play_lo = std::min(play_lo, f.play.g), play_hi = std::max(play_hi, f.play.g);
        const float c = f.chompi.r + f.chompi.g + f.chompi.b;
        ch_lo = std::min(ch_lo, c), ch_hi = std::max(ch_hi, c);
    }
    CHECK(play_lo > 0.5f && play_hi - play_lo < 0.01f);
    CHECK(ch_hi > ch_lo + 0.5f);
}

static void TestQuantizedPlayback()
{
    printf("quantize on: recorded notes play on the grid, the timing kept\n");
    auto note = [](int n, int nudge) {
        Step s;
        s.note = static_cast<uint8_t>(n), s.on = true, s.nudge = static_cast<uint8_t>(nudge);
        return s;
    };
    Pattern p;
    p.steps[0] = note(1, 0); // on its step: never moves
    p.steps[2] = note(2, 2); // a little late: stays on step 3
    p.steps[5] = note(3, 4); // most of a step late: plays on step 7
    p.steps[6].on = p.steps[6].tie = true; // ...over the tie that followed it
    p.steps[9] = note(4, 5); // nearly on step 11, which has its own note
    p.steps[10] = note(5, 0);
    p.steps[15] = note(6, 4); // wraps to step 1, where step 1's own note wins
    CHECK(p.PlayedStep(0, 1).note == 1 && p.PlayedStep(0, 1).nudge == 0);
    CHECK(p.PlayedStep(2, 1).note == 2 && p.PlayedStep(2, 1).nudge == 0);
    CHECK(!p.PlayedStep(5, 1).on);
    CHECK(p.PlayedStep(6, 1).note == 3 && !p.PlayedStep(6, 1).tie);
    CHECK(!p.PlayedStep(9, 1).on && p.PlayedStep(10, 1).note == 5);
    CHECK(!p.PlayedStep(15, 1).on);
    CHECK(p.PlayedStep(5, 0).note == 3 && p.PlayedStep(5, 0).nudge == 4); // off: as recorded
    // 1/8 (every 12 ticks): step 3 + 2 ticks (14) is nearest 12, step 3;
    // step 6 + 4 ticks (34) is nearest 36, step 7.
    CHECK(p.PlayedStep(2, 2).note == 2 && p.PlayedStep(6, 2).note == 3);
    // Two late notes aiming at one step: the nearer wins.
    Pattern q;
    q.steps[3] = note(7, 4);  // 22 ticks: 2 from step 5 (24)
    q.steps[4] = note(8, 1);  // 25 ticks: 1 from it
    CHECK(q.PlayedStep(4, 1).note == 8 && !q.PlayedStep(3, 1).on);

    // In the sequencer: the late note starts on the grid with quantize on,
    // late without it.
    Pattern r;
    r.steps[1] = note(9, 2);
    for(int quant : {0, 1})
    {
        Sequencer s;
        s.Init(kSr);
        s.SetTempo(120.f);
        s.SetPattern(&r);
        s.SetQuantize(quant);
        s.Start();
        auto ons = Of(RunInternal(s, 1.0), Sequencer::Event::NOTE_ON);
        CHECK(!ons.empty());
        const double want = 0.125 + (quant ? 0.0 : 2 * 0.125 / 6);
        CHECK(fabs(ons[0].t - want) < 1e-4);
    }
}

static void TestChompiDoubleTap()
{
    printf("CHOMPI double tap: bass / drums, and what doesn't count\n");
    Rig r;
    r.Run(1000);
    auto tap = [&](int hold_ms) { r.ui.Chompi(true), r.Run(hold_ms), r.ui.Chompi(false); };
    auto dbl = [&]() { tap(60), r.Run(120), tap(60); };

    dbl();
    CHECK(r.ui.OnDrums());
    LedFrame f;
    r.ui.Draw(f, r.now); // the swap flash: the keybed amber
    CHECK(f.key[0].r > 0.5f && f.key[0].g > 0.3f && f.key[0].b < 0.1f);
    r.Run(500);
    r.ui.Draw(f, r.now);
    CHECK(f.chompi.r > 0.3f && f.chompi.g > 0.2f && f.chompi.b < 0.1f);

    // On the drums' side the bass can't be edited; shared controls work.
    const Pattern before = r.m.Current();
    const float   env    = r.m.settings.params[ENV_MOD];
    const float   vol    = r.m.settings.params[VOLUME];
    r.White(3), r.Black(2), r.ui.KnobTurn(1, 5, false), r.ui.Loop(r.now);
    CHECK(r.m.Current().steps[3] == before.steps[3] && r.ui.Selected() == 0);
    CHECK(r.m.settings.params[ENV_MOD] == env && !r.m.Recording());
    r.ui.KnobTurn(5, -3, false);
    CHECK(r.m.settings.params[VOLUME] < vol);
    r.ui.Play();
    CHECK(r.m.Running());
    r.ui.Play();
    r.Run(500);

    dbl();
    CHECK(!r.ui.OnDrums());
    r.Run(500);

    // Not a double tap: one tap; a long press; too slow; too quick (bounce);
    // anything else used in between or during.
    tap(60);
    r.Run(1000);
    CHECK(!r.ui.OnDrums());
    tap(400), r.Run(120), tap(60);
    r.Run(1000);
    CHECK(!r.ui.OnDrums());
    tap(60), r.Run(600), tap(60);
    r.Run(1000);
    CHECK(!r.ui.OnDrums());
    tap(60), r.Run(10), tap(60);
    r.Run(1000);
    CHECK(!r.ui.OnDrums());
    tap(60), r.Run(60), r.ui.KnobTurn(0, 1, false), r.Run(60), tap(60);
    r.Run(1000);
    CHECK(!r.ui.OnDrums());
    // CHOMPI + key, twice quickly (a fast shift combination).
    for(int i = 0; i < 2; i++)
    {
        r.ui.Chompi(true), r.Run(30);
        r.Key(Ui::kBlack[6]);
        r.Run(30), r.ui.Chompi(false), r.Run(80);
    }
    CHECK(!r.ui.OnDrums());
}

static void TestQuantizeHold()
{
    printf("CHOMPI + D#4 held 2 s: quantizes the pattern for good\n");
    Rig r;
    r.ui.SetMode(Ui::Mode::PITCH);
    r.m.SetParam(QUANTIZE, 0.f);
    r.m.SetParam(QUANT_GRID, 0.f); // 1/16
    Pattern& p = r.m.Current();
    p.Clear();
    p.steps[2].on = true, p.steps[2].note = 3, p.steps[2].nudge = 1; // stays on step 3
    p.steps[5].on = true, p.steps[5].note = 4, p.steps[5].nudge = 4; // to step 7
    r.ui.Chompi(true);
    r.ui.KeyDown(Ui::kKeyView, r.now);
    r.Run(1000);
    LedFrame f;
    r.ui.Draw(f, r.now); // filling light blue
    CHECK(f.key[Ui::kKeyView].b > 0.3f && f.key[Ui::kKeyView].b < 0.8f);
    CHECK(p.steps[5].on); // not yet
    r.Run(1100);
    CHECK(p.steps[2].on && p.steps[2].nudge == 0 && p.steps[2].note == 3);
    CHECK(!p.steps[5].on && p.steps[6].on && p.steps[6].note == 4 && p.steps[6].nudge == 0);
    r.ui.Draw(f, r.now);
    for(int k = 0; k < kKeyNotes; k++)
        CHECK(f.key[k].b > 0.9f && f.key[k].r < 0.5f);
    r.ui.KeyUp(Ui::kKeyView, r.now);
    r.ui.Chompi(false);
    CHECK(StepIndex(r.m.settings.params[QUANTIZE], 2) == 0); // the hold doesn't toggle it
    // A tap still toggles, when let go.
    r.Run(500);
    r.ui.Chompi(true);
    r.ui.KeyDown(Ui::kKeyView, r.now);
    CHECK(StepIndex(r.m.settings.params[QUANTIZE], 2) == 0);
    r.Run(200);
    r.ui.KeyUp(Ui::kKeyView, r.now);
    r.ui.Chompi(false);
    CHECK(StepIndex(r.m.settings.params[QUANTIZE], 2) == 1);
}

int main()
{
    TestDemoTiming();
    TestExternalMatchesInternal();
    TestSwingAndTicks();
    TestQueueAndLength();
    TestTextRoundTrip();
    TestNudgeTiming();
    TestDiodeLadder();
    TestStepKeys();
    TestFollowPlayhead();
    TestPagesCopyClear();
    TestKnobPages();
    TestRealtimeRecording();
    TestNoteEntry();
    TestStepModeExtras();
    TestKnobColours();
    TestLiveQuantizeAndLights();
    TestQuantizedPlayback();
    TestChompiDoubleTap();
    TestQuantizeHold();
    TestArp();
    TestTransposeC();
    TestLivePlayhead();
    TestShortcutsAndLights();
    TestLengthShown();
    TestArpTranspose();
    TestLiveLights();
    TestSettingsOptions();
    TestMidiClock();
    printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
