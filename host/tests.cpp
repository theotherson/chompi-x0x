// Desktop tests of the x0x core: sequencer timing, patterns, storage format.
//   make -C host && host/tests
#include "../code/src/x0x/ui.h"
#include "../code/src/x0x/midifile.h"
#include "../code/src/x0x/defaults.h"
#include "../code/src/x0x/drums.h"
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
static float    g_reverb[Reverb::kReverbFrames];

struct Rig
{
    Machine  m;
    Ui       ui;
    uint32_t now = 1000; // ms
    Rig()
    {
        m.Init(kSr, g_delay, 96000, g_reverb, Reverb::kReverbFrames);
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
    r.White(0); // pattern 1 (again would flip it to 1B)
    CHECK(r.m.CurrentPattern() == 0);
    r.Black(7); // back to notes
    CHECK(r.ui.GetPage() == Ui::Page::NOTES);

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
    r.ui.KnobClick(5, r.now), r.ui.KnobClick(5, r.now), r.ui.KnobClick(5, r.now); // the mix, the compressor, round
    CHECK(r.ui.KnobPage(5) == 0);

    r.ui.KnobTurn(3, 20, false); // delay
    CHECK(p[DELAY] > 0.f);
    r.ui.Chompi(true), r.ui.KnobTurn(3, 2, false), r.ui.Chompi(false); // delay time
    CHECK(StepIndex(p[DELAY_TIME], kDelayDivisions) == 6); // one turn event = one position (3/16 to 1/4)
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
    r.White(2);  // not the next step's key: nothing
    r.White(4);  // the next step's key (step 5): a rest
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
    {
        // A#4 is the arpeggiator's octave up, in note entry too.
        const float up0 = r.m.settings.params[ARP_OCT_UP];
        r.ui.Chompi(true), r.Black(9), r.ui.Chompi(false);
        CHECK(r.m.settings.params[ARP_OCT_UP] != up0 && r.m.Current().length == 5);
        r.m.SetParam(ARP_OCT_UP, up0);
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
        // -1 lit purple, -2 not; +1 (cyan) not.
        CHECK(f.key[Ui::kWhite[1]].b > 0.4f && f.key[Ui::kWhite[1]].g < 0.1f && f.key[Ui::kWhite[0]].b < 0.1f);
        CHECK(f.key[Ui::kWhite[3]].b < 0.1f);
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
    // Eighth notes, alternating: left on steps 1, 5, 9, 13; right on 3, 7, 11, 15.
    at_step(2);
    CHECK(f.knob[4].r == 0.f && f.big_right.r > 0.5f);
    at_step(4);
    CHECK(f.knob[4].r > 0.5f && f.big_right.r == 0.f);
    at_step(6);
    CHECK(f.knob[4].r == 0.f && f.big_right.r > 0.5f);
    at_step(12);
    CHECK(f.knob[4].r > 0.5f && f.big_right.r == 0.f);

    // A recorded note flashes the current eighth's side red.
    r.ui.Loop(r.now); // record on
    at_step(13);
    r.ui.KeyDown(5, r.now), r.ui.KeyUp(5, r.now);
    r.ui.NoteStep(r.now), r.ui.Draw(f, r.now);
    CHECK(f.knob[4].r > 0.5f && f.knob[4].g == 0.f); // step 14 (with 13): the left side

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
    CHECK(r.m.settings.params[ENV_MOD] - e0 > 1.2f * (r.m.settings.params[CUTOFF] - c0));

    // Acceleration: slow clicks fine, a normal turn the range in a turn or
    // two, a spin in under one; never slower when turned faster.
    float prev = 1.f;
    for(int dt = 15; dt <= 400; dt += 5)
    {
        CHECK(KnobStep(dt) <= prev + 1e-6f);
        prev = KnobStep(dt);
    }
    CHECK(1.f / KnobStep(400) >= 90.f);                                // fine: ~1 %
    CHECK(1.f / KnobStep(100) <= 48.f && 1.f / KnobStep(50) <= 25.f); // 10 / 20 clicks a second
    CHECK(1.f / KnobStep(20) <= 15.f);
    Rig q;
    const float cut = q.m.settings.params[CUTOFF];
    for(int i = 0; i < 20; i++)
        q.ui.KnobTurn(4, 1, false, 50); // 20 clicks at 20 a second
    CHECK(q.m.settings.params[CUTOFF] - cut > 0.6f || q.m.settings.params[CUTOFF] == 1.f);
    const float t0 = q.m.settings.params[TEMPO];
    q.ui.KnobClick(5, q.now);          // volume page 2: tempo
    q.ui.KnobTurn(5, 1, false, 400);   // slow: 1 BPM
    CHECK(fabsf(TempoBpm(q.m.settings.params[TEMPO]) - TempoBpm(t0) - 1.f) < 0.01f);
    q.ui.KnobTurn(5, 1, false, 20);    // spun: 5
    CHECK(fabsf(TempoBpm(q.m.settings.params[TEMPO]) - TempoBpm(t0) - 6.f) < 0.01f);
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

static void TestWriteProtect()
{
    printf("write protect: hold the pattern key 2 s; flashes; kept in current.txt\n");
    Rig r;
    CHECK(!r.m.Protected());
    r.ui.KeyDown(Ui::kKeyPattern, r.now);
    r.Run(1000);
    CHECK(!r.m.Protected() && r.ui.GetPage() == Ui::Page::NOTES); // not yet, and no page
    r.Run(1100);
    CHECK(r.m.Protected());
    LedFrame f;
    r.ui.Draw(f, r.now); // everything flashes red
    CHECK(f.key[0].r > 0.9f && f.key[0].g < 0.1f && f.play.r > 0.9f && f.knob[2].r > 0.9f);
    r.ui.KeyUp(Ui::kKeyPattern, r.now);
    CHECK(r.ui.GetPage() == Ui::Page::NOTES); // the hold didn't open the page
    r.Run(1000);
    r.ui.Draw(f, r.now); // the pattern key stays red
    CHECK(f.key[Ui::kKeyPattern].r > 0.2f && f.key[Ui::kKeyPattern].g < 0.05f);
    // A tap still opens the pattern page, and edits still work.
    r.Key(Ui::kKeyPattern);
    CHECK(r.ui.GetPage() == Ui::Page::PATTERN);
    r.Key(Ui::kKeyPattern); // closed
    const bool was_on = r.S(2).on;
    r.White(2), r.White(2); // select step 3, then turn it on / off
    CHECK(r.S(2).on != was_on);
    // Kept across a restart.
    char buf[2048];
    const size_t n = WriteSettings(r.m.settings, buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "protect 1"));
    Settings back;
    ReadSettings(buf, back);
    CHECK(back.protect);
    // Hold again: off, flashing light blue; the key light blue after.
    r.ui.KeyDown(Ui::kKeyPattern, r.now);
    r.Run(2100);
    CHECK(!r.m.Protected());
    r.ui.Draw(f, r.now);
    CHECK(f.key[0].b > 0.9f && f.key[0].r < 0.4f && f.key[0].g > 0.4f);
    r.ui.KeyUp(Ui::kKeyPattern, r.now);
    r.Run(1000);
    r.ui.Draw(f, r.now);
    const Rgb pk = f.key[Ui::kKeyPattern];
    CHECK(pk.b > 0.05f && pk.r < 0.4f * pk.b && pk.g > 0.4f * pk.b);
    // CLEAR (A#4) is red.
    const Rgb cl = f.key[Ui::kKeyClear];
    CHECK(cl.r > 0.05f && cl.g == 0.f && cl.b == 0.f);
}

static void TestPatternPageWhileRunning()
{
    printf("pattern page while running: no playhead, no dimmed half\n");
    Rig r;
    for(int i = 0; i < kPatterns; i++)
        DemoPattern(r.m.patterns[i]); // all in use, second halves too
    r.m.TogglePlay();
    r.Key(Ui::kKeyPattern);
    CHECK(r.ui.GetPage() == Ui::Page::PATTERN);
    float lo = 9.f, hi = 0.f;
    bool  white = false;
    for(int t = 0; t < 400; t++)
    {
        r.Run(5);
        r.ui.NoteStep(r.now);
        LedFrame f;
        r.ui.Draw(f, r.now);
        for(int w = 1; w < 15; w++) // every pattern key but the current one (pattern 1)
        {
            const Rgb c = f.key[Ui::kWhite[w]];
            lo = std::min(lo, c.r), hi = std::max(hi, c.r);
            white |= c.g > 0.5f; // the playhead is white; the pattern colours aren't green
        }
    }
    CHECK(!white && hi - lo < 0.01f);
    // Middle C is pattern 8 (the half last chosen), wherever the playhead is.
    for(int t = 0; t < 2; t++)
    {
        r.Run(t ? 1100 : 600);
        r.White(7);
        CHECK(r.m.QueuedPattern() == 7);
        r.White(7); // switch at once
        CHECK(r.m.CurrentPattern() == 7);
        r.m.SelectPattern(0, true);
    }
}

static void TestPatternSides()
{
    printf("patterns A / B: the page shows A, and B while CHOMPI is held\n");
    Rig r;
    DemoPattern(r.m.patterns[0]);
    r.Key(Ui::kKeyPattern); // the page; F#4 again closes it
    LedFrame f;
    // Drawn while the current pattern's slow flash is on (it flashes 400 ms
    // on, 400 ms off).
    auto draw_on  = [&]() { r.ui.Draw(f, r.now - r.now % 800); };
    auto draw_off = [&]() { r.ui.Draw(f, r.now - r.now % 800 + 400); };
    auto blue   = [](Rgb c) { return c.b > 0.9f && c.r < 0.4f && c.g > 0.4f; };  // side A
    auto yellow = [](Rgb c) { return c.r > 0.9f && c.g > 0.6f && c.b < 0.1f; };  // side B
    auto dim_yellow = [](Rgb c) { return c.r > 0.05f && c.r < 0.5f && c.g > 0.6f * c.r && c.b < 0.1f * c.r; };
    r.m.patterns[PatternIndex(1, 0)].steps[0].on = true; // 2A in use
    r.m.patterns[PatternIndex(1, 1)].steps[0].on = true; // 2B in use
    // The current pattern flashes: bright, then dim as it has notes...
    draw_on();
    const float bright = f.key[Ui::kWhite[0]].b;
    draw_off();
    CHECK(f.key[Ui::kWhite[0]].b > 0.05f && f.key[Ui::kWhite[0]].b < 0.3f * bright);
    // ...or off if it's empty.
    r.White(5); // 6A: empty
    CHECK(r.m.CurrentPattern() == 5);
    draw_off();
    CHECK(f.key[Ui::kWhite[5]].b == 0.f);
    draw_on();
    CHECK(f.key[Ui::kWhite[5]].b > 0.9f);
    r.White(0);
    draw_on();
    CHECK(blue(f.key[Ui::kWhite[0]]));                      // 1A, current
    CHECK(f.key[Ui::kWhite[1]].b > 0.05f && f.key[Ui::kWhite[1]].r < 0.4f * f.key[Ui::kWhite[1]].b); // 2A dim
    r.ui.Chompi(true);
    draw_on();
    CHECK(dim_yellow(f.key[Ui::kWhite[1]]));               // 2B dim
    CHECK(blue(f.key[Ui::kWhite[0]]));                      // the current (1A) still shows
    r.White(1);                                             // CHOMPI + 2: 2B
    r.ui.Chompi(false);
    CHECK(r.m.CurrentPattern() == PatternIndex(1, 1));
    draw_on();
    CHECK(yellow(f.key[Ui::kWhite[1]]) && yellow(f.key[Ui::kKeyPattern])); // in the A view too
    r.White(1);                                             // 2: 2A
    CHECK(r.m.CurrentPattern() == PatternIndex(1, 0));
    r.White(1);                                             // again: stays 2A (no flipping)
    CHECK(r.m.CurrentPattern() == PatternIndex(1, 0));
    r.White(0);
    CHECK(r.m.CurrentPattern() == 0);
    // Running: 1A to 2B straight; the same key again (CHOMPI held) switches now.
    r.ui.Play();
    r.Run(100);
    r.ui.Chompi(true), r.White(1), r.ui.Chompi(false);
    CHECK(r.m.CurrentPattern() == 0 && r.m.QueuedPattern() == PatternIndex(1, 1));
    r.ui.Chompi(true), r.White(1), r.ui.Chompi(false);
    CHECK(r.m.CurrentPattern() == PatternIndex(1, 1));
    r.White(0); // queue 1A, then switch now
    r.White(0);
    CHECK(r.m.CurrentPattern() == 0);
    r.ui.Play();
    CHECK(r.m.patterns[0] == [] { Pattern d; DemoPattern(d); return d; }()); // CHOMPI didn't set a note
    r.Key(Ui::kKeyPattern);
    CHECK(r.ui.GetPage() == Ui::Page::NOTES);
    // COPY + PATTERN: 1A to 1B.
    r.ui.KeyDown(Ui::kKeyCopy, r.now);
    r.Key(Ui::kKeyPattern);
    r.ui.KeyUp(Ui::kKeyCopy, r.now);
    CHECK(r.m.patterns[PatternIndex(0, 1)] == r.m.patterns[0] && r.ui.GetPage() == Ui::Page::NOTES);
    // Locked: A magenta, B orange.
    r.m.SetProtected(true);
    r.Run(1000);
    draw_on();
    const Rgb lk = f.key[Ui::kKeyPattern];
    CHECK(lk.r > 0.05f && lk.b > 0.9f * lk.r && lk.g < 0.02f);
    r.Key(Ui::kKeyPattern);
    r.ui.Chompi(true), r.White(0), r.ui.Chompi(false); // 1B
    draw_on();
    CHECK(f.key[Ui::kWhite[0]].r > 0.9f && f.key[Ui::kWhite[0]].g > 0.2f && f.key[Ui::kWhite[0]].g < 0.5f
          && f.key[Ui::kWhite[0]].b < 0.05f);
    r.White(0); // back to 1A
    r.Key(Ui::kKeyPattern);
    r.m.SetProtected(false);

    // Files: 32 patterns round trip; "pattern 3" from earlier versions is 3A.
    static char buf[24576];
    r.m.patterns[PatternIndex(15, 1)].steps[3].on = true;
    const size_t n = WritePatterns(r.m.patterns, kPatterns, buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "pattern 16B"));
    static Pattern back[kPatterns];
    ReadPatterns(buf, back, kPatterns);
    for(int i = 0; i < kPatterns; i++)
        CHECK(back[i] == r.m.patterns[i]);
    char old[] = "pattern 3\nlength 8\nstep 1 5 0 1 0 0 0\n";
    ReadPatterns(old, back, kPatterns);
    CHECK(back[2].length == 8 && back[2].steps[0].note == 5 && back[PatternIndex(2, 1)] == r.m.patterns[PatternIndex(2, 1)]);
    Settings st;
    st.pattern = PatternIndex(4, 1);
    char sb[2048];
    CHECK(WriteSettings(st, sb, sizeof sb) > 0 && strstr(sb, "pattern 5B"));
    Settings st2;
    ReadSettings(sb, st2);
    CHECK(st2.pattern == PatternIndex(4, 1));
    char so[] = "pattern 7\n";
    ReadSettings(so, st2);
    CHECK(st2.pattern == 6);
}

static void TestMidiExport()
{
    printf("MIDI export: the hold, the flash, and the file\n");
    Rig r;
    DemoPattern(r.m.patterns[0]);
    r.Key(Ui::kKeyPattern);
    // A pattern key held 2 s asks for the export and doesn't pick it.
    const uint32_t req = r.m.export_requests;
    r.ui.KeyDown(Ui::kWhite[3], r.now);
    r.Run(2100);
    r.ui.KeyUp(Ui::kWhite[3], r.now);
    CHECK(r.m.export_requests == req + 1 && r.m.CurrentPattern() == 0);
    r.m.ExportDone(true);
    r.ui.NoteStep(r.now);
    LedFrame f;
    r.ui.Draw(f, r.now);
    CHECK(f.key[0].r > 0.9f && f.key[0].g > 0.9f && f.key[0].b > 0.9f);
    // A short press still picks it.
    r.Run(500);
    r.White(3);
    CHECK(r.m.CurrentPattern() == 3 && r.m.export_requests == req + 1);

    // The file: parse it back.
    static uint8_t mf[4096];
    const size_t   n = WriteMidiFile(r.m.patterns[0], 120.f, 0, "x0x 1A", mf, sizeof mf);
    CHECK(n > 30 && memcmp(mf, "MThd", 4) == 0 && memcmp(mf + 14, "MTrk", 4) == 0);
    CHECK(mf[12] == 0 && mf[13] == 96); // 96 ticks a beat
    const size_t trk = (mf[18] << 24) | (mf[19] << 16) | (mf[20] << 8) | mf[21];
    CHECK(22 + trk == n);
    struct N { uint32_t t; int st, note, vel; };
    std::vector<N> ev;
    uint32_t t = 0, tempo = 0, end = 0;
    for(size_t i = 22; i < n;)
    {
        uint32_t d = 0;
        while(mf[i] & 0x80)
            d = (d << 7) | (mf[i++] & 0x7f);
        d = (d << 7) | mf[i++];
        t += d;
        if(mf[i] == 0xff)
        {
            const int type = mf[i + 1], len = mf[i + 2];
            if(type == 0x51)
                tempo = (mf[i + 3] << 16) | (mf[i + 4] << 8) | mf[i + 5];
            if(type == 0x2f)
                end = t;
            i += 3 + len;
        }
        else
            ev.push_back({t, mf[i], mf[i + 1], mf[i + 2]}), i += 3;
    }
    CHECK(tempo == 500000 && end == 16 * 24);
    auto on_at = [&](uint32_t at) { for(auto& e : ev) if(e.st == 0x90 && e.t == at) return e; return N{0, 0, 0, 0}; };
    auto off_of = [&](int note, uint32_t after) {
        for(auto& e : ev) if(e.st == 0x80 && e.note == note && e.t > after) return e.t;
        return 0u;
    };
    CHECK(on_at(0).note == 36 && on_at(0).vel == 120);           // step 1: accent
    CHECK(off_of(36, 0) == 12);                                  // half a step
    CHECK(on_at(24).vel == 90);                                  // step 2: plain
    CHECK(on_at(48).note == 48 && off_of(48, 48) == 72 + 2);     // step 3 slides into 4
    CHECK(on_at(96).note == 36 && off_of(36, 96) == 5 * 24 + 12); // step 5 tied through 6
    // Recorded timing kept, or on the grid with quantize.
    Pattern late;
    late.steps[1].on = true, late.steps[1].note = 2, late.steps[1].nudge = 2;
    std::vector<uint8_t> v(1024);
    WriteMidiFile(late, 120.f, 0, "", v.data(), v.size());
    // ...the note on's delta: after the header (22), tempo (7), time sig (8), name (4)
    CHECK(v[22 + 7 + 8 + 4] == 24 + 2 * 4);
    WriteMidiFile(late, 120.f, 1, "", v.data(), v.size());
    CHECK(v[22 + 7 + 8 + 4] == 24);

    // For trying in a DAW: the demo pattern.
    if(FILE* fp = fopen("out/demo_pattern.mid", "wb"))
        fwrite(mf, 1, n, fp), fclose(fp);
}

static void TestMidiImport()
{
    printf("MIDI import: round trip, other files fitted, anything else turned down\n");
    static uint8_t buf[8192];
    // What the x0x writes comes back as it was: the demo, and recorded timing.
    Pattern demo;
    DemoPattern(demo);
    size_t  n = WriteMidiFile(demo, 120.f, 0, "x", buf, sizeof buf);
    Pattern back;
    CHECK(ReadMidiFile(buf, n, back) && back == demo);
    Pattern late;
    late.length = 12;
    late.steps[1].on = true, late.steps[1].note = 2, late.steps[1].nudge = 3;
    late.steps[2].on = late.steps[2].tie = true, late.steps[2].note = 2;
    late.steps[5].on = true, late.steps[5].note = 24, late.steps[5].octave = 1, late.steps[5].accent = true;
    n = WriteMidiFile(late, 133.f, 0, "x", buf, sizeof buf);
    CHECK(ReadMidiFile(buf, n, back) && back == late);
    // Drums round-trip too (channel 10), with the bassline.
    Pattern both;
    DemoPattern(both);
    both.drums[0] = 1 << BD | kDrumAccent, both.drums[4] = 1 << SD | 1 << CH, both.drums[14] = 1 << OH;
    n = WriteMidiFile(both, 120.f, 0, "x", buf, sizeof buf);
    CHECK(ReadMidiFile(buf, n, back) && back == both);
    // A drums-only file reads as a drum part.
    Pattern drums_only;
    drums_only.drums[2] = 1 << LT;
    n = WriteMidiFile(drums_only, 120.f, 0, "x", buf, sizeof buf);
    CHECK(ReadMidiFile(buf, n, back) && back.BassEmpty() && back.DrumHit(2, LT));

    // A DAW-style file: format 1, 480 ticks a beat, two tracks, a chord, a
    // note out of range, two bars long (only the first is kept).
    std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 0x01, 0xe0};
    auto track = [&](std::vector<uint8_t> ev) {
        ev.insert(ev.end(), {0x00, 0xff, 0x2f, 0x00});
        f.insert(f.end(), {'M', 'T', 'r', 'k', 0, 0, static_cast<uint8_t>(ev.size() >> 8), static_cast<uint8_t>(ev.size())});
        f.insert(f.end(), ev.begin(), ev.end());
    };
    track({0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20});
    track({
        0x00, 0x90, 40, 100, 0x00, 0x90, 43, 127, // step 1: a chord: the higher (43), accented
        0x60, 0x80, 40, 0, 0x00, 43, 0,           // 96 ticks later off (running status, vel 0)
        0x81, 0x68, 0x90, 100, 80,                 // step 3 (240 ticks after): MIDI 100, folded into range
        0x87, 0x40, 0x80, 100, 0,                  // held 1000 ticks: ties on through steps 4-10
        0x8f, 0x00, 0x90, 50, 80, 0x60, 0x80, 50, 0, // way later: bar 2, dropped
    });
    CHECK(ReadMidiFile(f.data(), f.size(), back));
    CHECK(back.length == 16);
    CHECK(back.steps[0].on && back.steps[0].Midi() == 43 && back.steps[0].accent && !back.steps[0].tie);
    CHECK(back.steps[2].on && back.steps[2].Midi() >= kBaseNote - 12 && back.steps[2].Midi() <= kBaseNote + 36);
    CHECK(back.steps[3].tie && back.steps[9].tie && !back.steps[11].on);

    // Turned down, and `out` untouched: not MIDI, SMPTE time, no notes, cut
    // short, broken lengths... and thousands of corrupted and random files.
    Pattern keep;
    DemoPattern(keep);
    Pattern out = keep;
    const uint8_t junk[] = "hello, this is not a MIDI file at all";
    CHECK(!ReadMidiFile(junk, sizeof junk, out) && out == keep);
    std::vector<uint8_t> smpte = f;
    smpte[12] = 0xe7;
    CHECK(!ReadMidiFile(smpte.data(), smpte.size(), out) && out == keep);
    const uint8_t empty[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k', 0, 0, 0, 4, 0, 0xff, 0x2f, 0};
    CHECK(!ReadMidiFile(empty, sizeof empty, out) && out == keep);
    for(size_t cut = 0; cut < f.size(); cut++)
        if(ReadMidiFile(f.data(), cut, out))
            CHECK(out.length >= 1 && out.length <= kSteps); // a shorter read may still be whole; never broken
    out = keep;
    uint32_t seed = 12345;
    auto rnd = [&]() { return seed = seed * 1664525u + 1013904223u; };
    int read = 0;
    for(int trial = 0; trial < 20000; trial++)
    {
        std::vector<uint8_t> g = trial % 2 ? f : std::vector<uint8_t>(buf, buf + n);
        const int flips = 1 + rnd() % 8;
        for(int i = 0; i < flips; i++)
            g[rnd() % g.size()] = static_cast<uint8_t>(rnd());
        if(trial % 5 == 0)
            g.resize(rnd() % (g.size() + 1));
        if(trial % 7 == 0)
            for(auto& b : g)
                b = static_cast<uint8_t>(rnd());
        Pattern o = keep;
        if(ReadMidiFile(g.data(), g.size(), o))
        {
            read++;
            bool fine = o.length >= 1 && o.length <= kSteps;
            for(int i = 0; i < kSteps; i++)
                fine &= o.steps[i].note < kKeyNotes && o.steps[i].octave >= -1 && o.steps[i].octave <= 1
                        && o.steps[i].nudge <= 5;
            CHECK(fine);
        }
        else
            CHECK(o == keep);
    }
    printf("  (fuzz: %d of 20000 corrupted files still read as valid patterns)\n", read);

    // File names: only "3B.mid"-style names are taken.
    CHECK(ParseImportName("3B.mid") == PatternIndex(2, 1) && ParseImportName("03b.MID") == PatternIndex(2, 1));
    CHECK(ParseImportName("16A.mid") == 15 && ParseImportName("1a.Mid") == 0);
    for(const char* bad : {"0A.mid", "17A.mid", "3C.mid", "3B.midi", "3B.mi", "B3.mid", "3B.done", "3B.try", "3B.bad",
                           "bassline.mid", "", "123A.mid"})
        CHECK(ParseImportName(bad) == -1);

    // At power-on: the keys flash green after imports, red if any failed.
    Rig r;
    r.m.imported = 2;
    r.ui.NoteStep(r.now);
    LedFrame lf;
    r.ui.Draw(lf, r.now);
    CHECK(lf.key[0].g > 0.9f && lf.key[0].r < 0.1f);
    Rig r2;
    r2.m.imported = 1, r2.m.import_failed = 1;
    r2.ui.NoteStep(r2.now);
    r2.ui.Draw(lf, r2.now);
    CHECK(lf.key[0].r > 0.9f && lf.key[0].g < 0.1f);
}

static void TestDelayTime()
{
    printf("delay time: synced changes crossfade (no pitch bend), free glides\n");
    // A 1 kHz tone through the delay, wet only, no feedback. The pitch of
    // what comes out, from zero crossings, around a change of time.
    auto pitch_swing = [](bool free, float a, float b) {
        static Fx::Frame mem[96000];
        Fx fx;
        fx.Init(48000.f, mem, 96000);
        Fx::Settings st;
        st.dly_mix = 1.f, st.dly_fb = 0.f, st.bpm = 120.f;
        st.dly_free = free;
        st.dly_div = static_cast<int>(a), st.dly_free_ms = a;
        fx.Set(st);
        float in[48], l[48], r[48];
        double ph = 0.0, lo = 1e9, hi = 0.0;
        float  prev = 0.f;
        int    last = -1, n = 0;
        for(int blk = 0; blk < 3000; blk++) // 3 s; the change at 1.5 s
        {
            if(blk == 1500)
                st.dly_div = static_cast<int>(b), st.dly_free_ms = b, fx.Set(st);
            for(int i = 0; i < 48; i++)
                in[i] = 0.5f * sinf(static_cast<float>(ph)), ph += 2.0 * M_PI * 1000.0 / 48000.0;
            fx.Process(in, nullptr, l, r, 48);
            for(int i = 0; i < 48; i++, n++)
            {
                if(prev < 0.f && l[i] >= 0.f) // the delay is full by then: never silent
                {
                    if(last >= 0 && n > 1500 * 48 - 2400 && n < 1500 * 48 + 48000)
                    {
                        const double hz = 48000.0 / (n - last);
                        lo = std::min(lo, hz), hi = std::max(hi, hz);
                    }
                    last = n;
                }
                prev = l[i];
            }
        }
        return std::max(1000.0 - lo, hi - 1000.0);
    };
    const double still  = pitch_swing(false, 1, 1); // no change: the wow and flutter alone
    const double synced = pitch_swing(false, 1, 7); // 1/16 to 3/8: a big jump
    const double free   = pitch_swing(true, 200.f, 600.f);
    printf("  (pitch swing at 1 kHz: unchanged %.1f Hz, synced change %.1f Hz, free change %.1f Hz)\n", still,
           synced, free);
    CHECK(synced < still + 15.0); // a crossfade: no bend beyond a little phase drift
    CHECK(free > 100.0 && free < 400.0); // the tape glide bends it, but never runs backwards

    // The panel: push knob 4 and turn = free time; a click (no turn) still
    // flips the page, on release; CHOMPI + turn goes back to synced.
    Rig r;
    const float* p = r.m.settings.params;
    r.ui.KnobDown(3);
    r.ui.KnobTurn(3, 5, false);
    r.ui.KnobUp(3, r.now);
    CHECK(StepIndex(p[DELAY_FREE_ON], 2) == 1 && r.ui.KnobPage(3) == 0);
    const float ms = DelayFreeMs(p[DELAY_FREE]);
    CHECK(ms > 375.f && ms < 500.f); // from the synced 3/16 (375 ms), up a little
    r.ui.KnobDown(3);
    r.ui.KnobUp(3, r.now);
    CHECK(r.ui.KnobPage(3) == 1);
    r.ui.KnobDown(3), r.ui.KnobUp(3, r.now), r.ui.KnobDown(3), r.ui.KnobUp(3, r.now), r.ui.KnobDown(3), r.ui.KnobUp(3, r.now);
    CHECK(r.ui.KnobPage(3) == 0);
    const int div = StepIndex(p[DELAY_TIME], kDelayDivisions);
    r.ui.Chompi(true), r.ui.KnobTurn(3, 1, false), r.ui.Chompi(false);
    CHECK(StepIndex(p[DELAY_FREE_ON], 2) == 0 && StepIndex(p[DELAY_TIME], kDelayDivisions) == div); // back, same setting
    r.ui.Chompi(true), r.ui.KnobTurn(3, 1, false), r.ui.Chompi(false);
    CHECK(StepIndex(p[DELAY_TIME], kDelayDivisions) == div + 1);
    LedFrame f;
    r.ui.Draw(f, r.now); // white keys 1-9: the settings, this one bright
    CHECK(f.key[Ui::kWhite[div + 1]].r > 0.9f && f.key[Ui::kWhite[div]].r < 0.1f && f.key[Ui::kWhite[div]].r > 0.f);
    // Push and turn on another page: a normal turn, and no click after.
    r.ui.KnobClick(3, r.now); // page 2: tape feedback
    const float fb = p[DELAY_FB];
    r.ui.KnobDown(3), r.ui.KnobTurn(3, 3, false), r.ui.KnobUp(3, r.now);
    CHECK(p[DELAY_FB] > fb && r.ui.KnobPage(3) == 1);
}

static void TestDefaults()
{
    printf("a new card's defaults: the demo patterns and settings parse\n");
    static char    buf[sizeof(kDefaultPatterns)];
    static Pattern pats[kPatterns];
    memcpy(buf, kDefaultPatterns, sizeof buf);
    ReadPatterns(buf, pats, kPatterns);
    int used = 0;
    for(int i = 0; i < kPatterns; i++)
        used += !pats[i].Empty();
    CHECK(used == 3 && !pats[PatternIndex(13, 0)].Empty() && !pats[PatternIndex(14, 0)].Empty()
          && !pats[PatternIndex(15, 0)].Empty());
    char sb[sizeof(kDefaultSettings)];
    memcpy(sb, kDefaultSettings, sizeof sb);
    Settings st;
    st.protect = true;
    ReadSettings(sb, st);
    CHECK(st.pattern == PatternIndex(15, 0) && !st.protect); // 16A; never write-protected on a new card
    CHECK(st.params[RESONANCE] == 1.f && st.params[VOLUME] > 0.1f);
}

static void TestDrumVoices()
{
    printf("drums: every voice sounds and settles; accent, decay, choke\n");
    // Level (dB) over a window of a single hit, and how long it rings.
    auto hit = [](Drum v, float accent, float decay, float secs, std::vector<float>& b) {
        static Drums d;
        d.Init(48000.f);
        d.Params(v).decay = decay;
        d.Trigger(v, accent);
        b.assign(static_cast<size_t>(48000 * secs), 0.f);
        for(size_t i = 0; i + 48 <= b.size(); i += 48)
            d.Process(&b[i], 48);
    };
    auto rms_db = [](const std::vector<float>& b, double t0, double t1) {
        double e = 0;
        size_t n = 0;
        for(size_t i = static_cast<size_t>(t0 * 48000); i < static_cast<size_t>(t1 * 48000) && i < b.size(); i++, n++)
            e += b[i] * b[i];
        return 10.0 * log10(e / (n ? n : 1) + 1e-20);
    };
    std::vector<float> b, a;
    for(int v = 0; v < NUM_DRUMS; v++)
    {
        hit(static_cast<Drum>(v), 0.f, 0.5f, 3.f, b);
        bool finite = true;
        float peak = 0.f;
        for(float x : b)
            finite &= std::isfinite(x), peak = std::max(peak, fabsf(x));
        CHECK(finite && peak > 0.02f && peak < 2.f);
        CHECK(rms_db(b, 0.0, 0.05) - rms_db(b, 2.5, 3.0) > 60.0); // dies away
        // Accent: louder, and the tail longer (it's hit harder).
        hit(static_cast<Drum>(v), 1.f, 0.5f, 1.f, a);
        CHECK(rms_db(a, 0.0, 0.02) > rms_db(b, 0.0, 0.02) + 3.0);
        // Decay up: rings longer.
        hit(static_cast<Drum>(v), 0.f, 0.9f, 1.f, a);
        CHECK(rms_db(a, 0.1, 0.2) > rms_db(b, 0.1, 0.2) + 3.0);
    }
    // A closed hat chokes an open one.
    Drums d;
    d.Init(48000.f);
    std::vector<float> open(48000), choked(48000);
    d.Trigger(OH, 0.f);
    for(size_t i = 0; i < open.size(); i += 48)
        d.Process(&open[i], 48);
    d.Init(48000.f);
    d.Trigger(OH, 0.f);
    for(size_t i = 0; i < choked.size(); i += 48)
    {
        if(i == 4800)
            d.Trigger(CH, 0.f); // 100 ms in
        d.Process(&choked[i], 48);
    }
    CHECK(rms_db(choked, 0.25, 0.35) < rms_db(open, 0.25, 0.35) - 30.0);
}

static void TestDrumSequencing()
{
    printf("drum part: plays on its steps, own length, live hits, recording, files\n");
    Rig r;
    Pattern& p = r.m.Current();
    p.ClearBass();
    p.drums[0] = 1 << BD | kDrumAccent;
    p.drums[4] = 1 << SD;
    p.drum_length = 6; // a polymeter against the bass's 16
    r.m.Play();
    std::vector<int> bd_steps;
    uint32_t last = r.m.DrumHitCount(BD);
    for(int t = 0; t < 4000; t++)
    {
        r.Run(1);
        if(r.m.DrumHitCount(BD) != last)
            last = r.m.DrumHitCount(BD), bd_steps.push_back(static_cast<int>(r.m.StepCount()));
    }
    // 120 BPM sixteenths: 4 s is 32 steps; a BD every 6 steps from the first.
    CHECK(bd_steps.size() >= 5);
    for(size_t i = 1; i < bd_steps.size(); i++)
        CHECK(bd_steps[i] - bd_steps[i - 1] == 6);
    CHECK(r.m.DrumHitCount(SD) == bd_steps.size() || r.m.DrumHitCount(SD) + 1 == bd_steps.size());
    // Live hits play, and record (to the nearest step) only when recording.
    const uint32_t ch = r.m.DrumHitCount(CH);
    r.m.DrumHit(CH, false);
    r.Run(2);
    CHECK(r.m.DrumHitCount(CH) == ch + 1);
    bool any = false;
    for(int i = 0; i < kSteps; i++)
        any |= p.DrumHit(i, CH);
    CHECK(!any);
    r.m.SetRecording(true);
    r.m.DrumHit(CH, true);
    r.Run(2);
    r.m.SetRecording(false);
    int recorded = -1;
    for(int i = 0; i < kSteps; i++)
        if(p.DrumHit(i, CH))
            recorded = i;
    CHECK(recorded >= 0 && recorded < 6 && p.DrumAccent(recorded));
    r.m.Stop();
    // Clearing the bassline keeps the drums; files keep both.
    r.m.ClearPattern();
    CHECK(!p.DrumsEmpty() && p.drum_length == 6);
    static char buf[24576];
    CHECK(WritePatterns(r.m.patterns, kPatterns, buf, sizeof buf) > 0 && strstr(buf, "drums 6 81"));
    static Pattern back[kPatterns];
    ReadPatterns(buf, back, kPatterns);
    CHECK(back[r.m.CurrentPattern()] == p);
    char old[] = "pattern 2\nlength 16\nstep 1 0 0 1 0 0 0 0\n"; // no drums line: none
    back[1].drums[3] = 0xff;
    ReadPatterns(old, back, kPatterns);
    CHECK(back[1].DrumsEmpty() && back[1].drum_length == kSteps);
}

static void TestDrumPanel()
{
    printf("drum side of the panel: steps, accent, clear, knobs, live, recording\n");
    Rig r;
    r.Run(1000);
    r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false), r.Run(120), r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false);
    CHECK(r.ui.OnDrums());
    Pattern& p = r.m.Current();
    p.ClearDrums();
    // Step mode: a voice's page, its steps on and off.
    r.Black(0); // BD
    r.White(0), r.White(4), r.White(9), r.White(11); // steps 1 5 11 13 (white 10 = step 11)
    CHECK(p.DrumHit(0, BD) && p.DrumHit(4, BD) && p.DrumHit(10, BD) && p.DrumHit(12, BD));
    r.Black(1); // SD
    r.White(4);
    CHECK(p.DrumHit(4, SD) && !p.DrumHit(0, SD));
    r.Black(4), r.Black(4); // A#3: CH, again: OH
    r.White(2);
    CHECK(p.DrumHit(2, OH) && !p.DrumHit(2, CH));
    r.Black(4); // and back to CH
    r.White(2);
    CHECK(p.DrumHit(2, CH));
    r.White(2);
    r.Black(5); // C#4: the accent page
    r.White(0);
    CHECK(p.DrumAccent(0) && p.DrumHit(0, BD));
    r.White(0);
    CHECK(!p.DrumAccent(0));
    // CLEAR tap: the selected voice's hits; held: the whole part.
    r.Black(1); // SD
    r.Key(Ui::kKeyClear);
    CHECK(!p.DrumHit(4, SD) && p.DrumHit(4, BD));
    // Knobs act on the selected voice; CHOMPI + knob 1: the accent level.
    r.Black(0);
    const float lv = r.m.settings.params[DRUM_PARAMS + 3 * BD];
    r.ui.KnobTurn(0, -5, false);
    CHECK(r.m.settings.params[DRUM_PARAMS + 3 * BD] < lv);
    const float dk = r.m.settings.params[DRUM_PARAMS + 3 * BD + 2];
    r.ui.KnobTurn(2, 5, false);
    CHECK(r.m.settings.params[DRUM_PARAMS + 3 * BD + 2] > dk);
    const float ac = r.m.settings.params[DRUM_ACCENT];
    r.ui.Chompi(true), r.ui.KnobTurn(0, 5, false), r.ui.Chompi(false);
    CHECK(r.m.settings.params[DRUM_ACCENT] > ac);
    r.ui.KnobClick(0, r.now); // page 2: the drum part's length
    for(int i = 0; i < 4; i++)
        r.ui.KnobTurn(0, -1, false); // a step a click
    CHECK(p.drum_length == 12 && p.length == 16);
    r.ui.KnobClick(0, r.now);
    const float tempo = r.m.settings.params[TEMPO];
    r.ui.KnobTurn(4, 10, false); // the purple knob: tempo
    CHECK(fabsf(TempoBpm(r.m.settings.params[TEMPO]) - TempoBpm(tempo) - 10.f) < 0.01f);
    // The voice key lights in its colour.
    LedFrame f;
    r.Run(400); // past the swap's flash
    r.ui.Draw(f, r.now);
    CHECK(f.key[Ui::kBlack[0]].r > 0.5f && f.key[Ui::kBlack[1]].r < 0.3f);
    // The step lights: the page's voice in its colour, other voices dim
    // grey; the accent page, the accents blue. (BD on 1, 5, 11, 13; SD
    // cleared above; OH / CH on 3.)
    r.Black(0); // the BD page
    r.Run(1300); // past the length display (the length was just turned)
    r.ui.Draw(f, r.now);
    const Rgb bd_led = f.key[Ui::kWhite[0]], other = f.key[Ui::kWhite[2]];
    CHECK(bd_led.r > 0.5f && bd_led.g < 0.2f && bd_led.b < 0.2f);             // red
    CHECK(other.r > 0.f && other.r < 0.1f && other.r == other.g && other.g == other.b); // dim grey
    r.Black(5); // the accent page; accent step 5
    r.White(4);
    r.ui.Draw(f, r.now);
    CHECK(f.key[Ui::kWhite[4]].b > 0.5f && f.key[Ui::kWhite[4]].r < 0.1f);     // blue
    CHECK(f.key[Ui::kWhite[0]].r < 0.1f);                                       // a plain hit: dim
    r.White(4);
    r.Black(0);

    // CLEAR held 1 s: the whole drum part.
    r.ui.KeyDown(Ui::kKeyClear, r.now);
    r.Run(1100);
    r.ui.KeyUp(Ui::kKeyClear, r.now);
    CHECK(p.DrumsEmpty());

    // Live mode: black keys play; C#4 held: accented; recording writes them.
    r.ui.SetMode(Ui::Mode::PITCH);
    const uint32_t sd = r.m.DrumHitCount(SD);
    r.Black(1);
    r.Run(2);
    CHECK(r.m.DrumHitCount(SD) == sd + 1);
    // White keys: the last voice played, pitched; never recorded.
    r.White(9);
    r.Run(2);
    CHECK(r.m.DrumHitCount(SD) == sd + 2);
    r.ui.Loop(r.now); // record on
    r.ui.Play();
    r.Run(300);
    r.ui.Chompi(true), r.ui.Loop(r.now), r.ui.Chompi(false); // CHOMPI + LOOP: live accent on
    r.Black(0); // an accented BD
    r.ui.Chompi(true), r.ui.Loop(r.now), r.ui.Chompi(false); // and off
    r.White(10); // pitched: not recorded
    r.Run(2);
    int bd = -1, pitched = 0;
    for(int i = 0; i < kSteps; i++)
    {
        if(p.DrumHit(i, BD))
            bd = i;
        pitched += p.DrumHit(i, SD);
    }
    CHECK(bd >= 0 && p.DrumAccent(bd) && pitched == 0);
    r.ui.Play();
    r.ui.Loop(r.now); // record off
    // The pattern page works on the drums' side too.
    r.Key(Ui::kKeyPattern);
    r.White(2);
    CHECK(r.m.CurrentPattern() == 2);
}

static void TestDrumMuteSoloMix()
{
    printf("drums: toms / hats alternate live; mute, solo; the bass / drums mix\n");
    Rig r;
    r.Run(1000);
    r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false), r.Run(120), r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false);
    r.Run(500);
    r.ui.SetMode(Ui::Mode::PITCH);
    // Live: every voice its own key: LT HT CY on F#3-A#3, CH OH on C#4 D#4.
    const int order[7] = {BD, SD, LT, HT, CY, CH, OH};
    for(int b = 0; b < 7; b++)
    {
        const uint32_t c = r.m.DrumHitCount(order[b]);
        r.Black(b), r.Run(2);
        CHECK(r.m.DrumHitCount(order[b]) == c + 1);
    }
    // A pattern: BD and SD on every step.
    Pattern& p = r.m.Current();
    p.ClearDrums();
    for(int i = 0; i < kSteps; i++)
        p.drums[i] = 1 << BD | 1 << SD;
    // CHOMPI + SD: muted; the pattern's SD stops, BD carries on; a live SD
    // still sounds.
    r.ui.Chompi(true), r.Black(1), r.ui.Chompi(false);
    CHECK(r.m.DrumMuted(SD));
    r.m.Play();
    uint32_t bd = r.m.DrumHitCount(BD), sd = r.m.DrumHitCount(SD);
    r.Run(1000);
    CHECK(r.m.DrumHitCount(BD) > bd + 5 && r.m.DrumHitCount(SD) == sd);
    r.Black(1), r.Run(2);
    CHECK(r.m.DrumHitCount(SD) == sd + 1);
    r.ui.Chompi(true), r.Black(1), r.ui.Chompi(false);
    CHECK(!r.m.DrumMuted(SD));
    // Hold SD 2 s: soloed; BD stops.
    r.ui.KeyDown(Ui::kBlack[1], r.now);
    r.Run(2100);
    r.ui.KeyUp(Ui::kBlack[1], r.now);
    CHECK(r.m.DrumSoloed(SD));
    bd = r.m.DrumHitCount(BD), sd = r.m.DrumHitCount(SD);
    r.Run(1000);
    CHECK(r.m.DrumHitCount(BD) == bd && r.m.DrumHitCount(SD) > sd + 5);
    r.ui.KeyDown(Ui::kBlack[1], r.now), r.Run(2100), r.ui.KeyUp(Ui::kBlack[1], r.now);
    CHECK(!r.m.DrumSoloed(SD));
    r.m.Stop();

    // The mix: centre both; left drums only; right bass only; CHOMPI layer mutes.
    float b, d;
    MixGains(0.5f, 0.5f, &b, &d);
    CHECK(b == 1.f && d == 1.f);
    MixGains(0.f, 0.5f, &b, &d);
    CHECK(b < 0.01f && d == 1.f);
    MixGains(1.f, 0.5f, &b, &d);
    CHECK(b == 1.f && d < 0.01f);
    MixGains(0.25f, 0.5f, &b, &d);
    CHECK(b > 0.5f && b < 0.9f && d == 1.f);
    MixGains(0.5f, 0.f, &b, &d);
    CHECK(b == 0.f && d == 1.f);
    MixGains(0.5f, 1.f, &b, &d);
    CHECK(b == 1.f && d == 0.f);
    // On the panel (the drums' side): the volume knob's pages are volume,
    // the mix and the compressor; no tempo / swing (the purple knob has them).
    r.ui.KnobClick(5, r.now);
    CHECK(r.ui.KnobPage(5) == 2);
    r.ui.KnobClick(5, r.now);
    CHECK(r.ui.KnobPage(5) == 3);
    r.ui.KnobClick(5, r.now);
    CHECK(r.ui.KnobPage(5) == 0);
    r.ui.KnobClick(5, r.now);
    CHECK(r.ui.KnobPage(5) == 2);
    r.ui.KnobTurn(5, -10, false);
    CHECK(r.m.settings.params[MIX] < 0.5f);
    r.ui.Chompi(true), r.ui.KnobTurn(5, 1, false), r.ui.Chompi(false);
    CHECK(StepIndex(r.m.settings.params[MIX_MUTE], 3) == 2); // drums muted
}

static void TestDrumEffects()
{
    printf("drum effects: reverb, the shared delay's send, filter, the knob 4 pages\n");
    // The reverb: an impulse rings on, longer for a bigger size; stable.
    auto tail_db = [](float size) {
        static float mem[Reverb::kReverbFrames];
        Reverb rv;
        rv.Init(48000.f, mem, Reverb::kReverbFrames);
        rv.Set(size);
        std::vector<float> in(48000 * 3, 0.f), l(in.size(), 0.f), r(in.size(), 0.f);
        in[0] = 1.f;
        for(size_t i = 0; i < in.size(); i += 48)
            rv.Process(&in[i], &l[i], &r[i], 48);
        double e1 = 0, e2 = 0;
        bool   finite = true;
        for(size_t i = 0; i < l.size(); i++)
        {
            finite &= std::isfinite(l[i]) && std::isfinite(r[i]);
            if(i < 9600) e1 += l[i] * l[i];
            else if(i >= 48000 && i < 57600) e2 += l[i] * l[i];
        }
        CHECK(finite && e1 > 0);
        return 10 * log10((e2 + 1e-30) / e1); // 1.0-1.2 s against the first 0.2 s
    };
    const double small = tail_db(0.1f), big = tail_db(0.9f);
    printf("  (reverb at 1 s: size 0.1 %.0f dB, size 0.9 %.0f dB)\n", small, big);
    CHECK(big > small + 20 && small < -40);
    // The plate: a pre-delay (silent for the first 10 ms), wide (left and
    // right uncorrelated), bigger rooms darker; stable fed noise at full size.
    {
        static float mem[Reverb::kReverbFrames];
        auto ir = [&](float size, std::vector<float>& l, std::vector<float>& r) {
            Reverb rv;
            rv.Init(48000.f, mem, Reverb::kReverbFrames);
            rv.Set(size);
            std::vector<float> in(48000, 0.f);
            in[0] = 1.f;
            l.assign(in.size(), 0.f), r.assign(in.size(), 0.f);
            for(size_t i = 0; i < in.size(); i += 48)
                rv.Process(&in[i], &l[i], &r[i], 48);
        };
        std::vector<float> l, r, l2, r2;
        ir(0.1f, l, r);
        float early = 0.f;
        for(int i = 0; i < 480; i++)
            early = std::max(early, std::max(fabsf(l[i]), fabsf(r[i])));
        CHECK(early == 0.f);
        double sl = 0, sr = 0, slr = 0;
        for(int i = 2400; i < 24000; i++)
            sl += l[i] * l[i], sr += r[i] * r[i], slr += l[i] * r[i];
        CHECK(fabs(slr / sqrt(sl * sr)) < 0.2);
        // Brightness: the tail's high (first-difference) energy against its total.
        auto bright = [](const std::vector<float>& x) {
            double hi = 0, tot = 0;
            for(int i = 4800; i < 24000; i++)
                hi += (x[i] - x[i - 1]) * (x[i] - x[i - 1]), tot += x[i] * x[i];
            return hi / tot;
        };
        ir(0.9f, l2, r2);
        CHECK(bright(l2) < bright(l) * 0.6);
        Reverb rv;
        rv.Init(48000.f, mem, Reverb::kReverbFrames);
        rv.Set(1.f);
        float in[48], L[48], R[48], peak = 0.f;
        uint32_t seed = 7;
        for(int b = 0; b < 20000; b++)
        {
            for(int i = 0; i < 48; i++)
                seed = seed * 1664525u + 1013904223u, in[i] = static_cast<int32_t>(seed) * 2.3e-10f, L[i] = R[i] = 0.f;
            rv.Process(in, L, R, 48);
            for(int i = 0; i < 48; i++)
                peak = std::max(peak, std::max(fabsf(L[i]), fabsf(R[i])));
        }
        CHECK(std::isfinite(peak) && peak < 2.f);
    }

    // A drum part: one BD at step 1.
    Rig r;
    Pattern& p = r.m.Current();
    p.ClearBass(), p.ClearDrums();
    p.drums[0] = 1 << BD;
    auto run_energy = [&](double t0, double t1) {
        // The output's energy between t0 and t1 seconds after PLAY.
        r.m.Stop();
        r.Run(300);
        r.m.Play();
        double e = 0;
        float  L[48], R[48];
        for(int ms = 0; ms < static_cast<int>(t1 * 1000); ms++)
        {
            r.m.Process(L, R, 48);
            r.now++;
            r.ui.Tick(r.now);
            if(ms >= t0 * 1000)
                for(int i = 0; i < 48; i++)
                    e += L[i] * L[i];
        }
        return e;
    };
    float* prm = r.m.settings.params;
    prm[DELAY_TIME] = StepValue(3, kDelayDivisions); // 1/8: 250 ms at 120 BPM
    // No send: quiet between 260 and 400 ms (the BD has died away, step 2 is
    // empty... the next BD is at 2 s).
    const double dry = run_energy(0.26, 0.4);
    prm[DRUM_DELAY] = 0.8f;
    const double wet = run_energy(0.26, 0.4);
    CHECK(wet > dry * 30);
    // The mix knob's 'mute the drums' silences their echoes too.
    prm[MIX_MUTE] = 1.f;
    const double muted = run_energy(0.26, 0.4);
    CHECK(muted < wet * 0.01);
    prm[MIX_MUTE] = 0.5f, prm[DRUM_DELAY] = 0.f;
    // The reverb send: a tail after the hit.
    prm[DRUM_REVERB] = 0.8f;
    const double rev = run_energy(0.26, 0.4);
    CHECK(rev > dry * 30);
    prm[DRUM_REVERB] = 0.f;

    // The filter: low-pass left takes the highs, high-pass right the lows.
    auto band = [](float filter, bool high) {
        DrumFx fx;
        fx.Init(48000.f);
        DrumFx::Settings st;
        st.filter = filter;
        fx.Set(st);
        uint32_t seed = 1;
        double   e    = 0;
        float    b[48];
        float    lp = 0.f;
        // Below 300 Hz, or above 5 kHz (one-pole splits).
        const float k = high ? 1.f - expf(-2.f * 3.14159f * 5000.f / 48000.f) : 1.f - expf(-2.f * 3.14159f * 300.f / 48000.f);
        for(int blk = 0; blk < 1000; blk++)
        {
            for(int i = 0; i < 48; i++)
                seed = seed * 1664525u + 1013904223u, b[i] = static_cast<int32_t>(seed) * 4.6e-10f;
            fx.Process(b, 48);
            for(int i = 0; i < 48; i++)
            {
                lp += (b[i] - lp) * k;
                const float d = high ? b[i] - lp : lp;
                e += d * d;
            }
        }
        return e;
    };
    CHECK(band(0.1f, true) < band(0.5f, true) * 0.3);
    CHECK(band(0.9f, false) < band(0.5f, false) * 0.3);

    // The panel: drums' knob 4, its pages and CHOMPI layers.
    r.Run(1000);
    r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false), r.Run(120), r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false);
    CHECK(r.ui.OnDrums());
    r.ui.KnobTurn(3, 5, false);
    CHECK(prm[DRUM_REVERB] > 0.f);
    r.ui.Chompi(true), r.ui.KnobTurn(3, -5, false), r.ui.Chompi(false);
    CHECK(prm[REVERB_SIZE] < 0.5f);
    r.ui.KnobClick(3, r.now); // page 2: delay send / the shared delay's time
    const int div = StepIndex(prm[DELAY_TIME], kDelayDivisions);
    r.ui.Chompi(true), r.ui.KnobTurn(3, 1, false), r.ui.Chompi(false);
    CHECK(StepIndex(prm[DELAY_TIME], kDelayDivisions) == div + 1);
    r.ui.KnobClick(3, r.now), r.ui.KnobClick(3, r.now); // page 4: filter / drive
    r.ui.KnobTurn(3, -5, false);
    CHECK(prm[DRUM_FILTER] < 0.5f);
    r.ui.Chompi(true), r.ui.KnobClick(3, r.now), r.ui.Chompi(false); // reset them all
    CHECK(prm[DRUM_REVERB] == 0.f && prm[REVERB_SIZE] == kParams[REVERB_SIZE].def && prm[DRUM_FILTER] == 0.5f);
    CHECK(StepIndex(prm[DELAY_TIME], kDelayDivisions) == div + 1); // the shared time stays
}

static void TestCompressorAndSidechain()
{
    printf("master compressor and the kick's sidechain\n");
    // The compressor: loud parts down, quiet ones (with make-up) up; off is off.
    auto level = [](float amount, float in) {
        Compressor c;
        c.Init(48000.f);
        c.Set(amount);
        float  l[48], r[48];
        double e = 0;
        for(int blk = 0; blk < 1000; blk++)
        {
            for(int i = 0; i < 48; i++)
                l[i] = r[i] = in * sinf(6.2832f * 200.f * (blk * 48 + i) / 48000.f);
            c.Process(l, r, 48);
            if(blk > 500)
                for(int i = 0; i < 48; i++)
                    e += l[i] * l[i];
        }
        return 10 * log10(e / (499.0 * 48) + 1e-20);
    };
    const double loud0 = level(0.f, 0.9f), quiet0 = level(0.f, 0.05f);
    const double loud = level(0.8f, 0.9f), quiet = level(0.8f, 0.05f);
    printf("  (range: off %.1f dB, heavy %.1f dB)\n", loud0 - quiet0, loud - quiet);
    CHECK(fabs((loud0 - quiet0) - 25.1) < 0.5);     // off: untouched
    CHECK(loud - quiet < (loud0 - quiet0) - 10.0);  // heavy: much less range
    CHECK(loud < loud0);

    // The sidechain: a BD ducks the bass, which recovers by the next beat;
    // the drums aren't ducked.
    Rig r;
    Pattern& p = r.m.Current();
    DemoPattern(p); // a bassline
    p.ClearDrums();
    p.drums[0] = 1 << BD;
    p.drums[8] = 1 << BD;
    float* prm = r.m.settings.params;
    prm[SIDECHAIN] = 1.f;
    r.m.Play();
    float  dmin = 1.f, dmax = 0.f;
    for(int ms = 0; ms < 2000; ms++)
    {
        r.Run(1);
        dmin = std::min(dmin, r.m.Duck()), dmax = std::max(dmax, r.m.Duck());
    }
    CHECK(dmax > 0.9f && dmin < 0.1f); // ducks on each kick, recovers between
    r.m.Stop();
}

static void TestDrumDistortion()
{
    printf("drum distortion: its own (CHOMPI + volume on the drums' side), with a mix\n");
    Rig r;
    float* prm = r.m.settings.params;
    // Bass side: CHOMPI + volume is the bass's drive.
    r.ui.Chompi(true), r.ui.KnobTurn(5, 5, false), r.ui.Chompi(false);
    CHECK(prm[DRIVE] > 0.f && prm[DRUM_DRIVE] == 0.f);
    const float bass_drive = prm[DRIVE];
    // Drums' side: the drums' own.
    r.Run(1000);
    r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false), r.Run(120), r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false);
    CHECK(r.ui.OnDrums());
    r.ui.Chompi(true), r.ui.KnobTurn(5, 8, false), r.ui.Chompi(false);
    CHECK(prm[DRUM_DRIVE] > 0.f && prm[DRIVE] == bass_drive);
    // CHOMPI + knob 4 (page 4): the mix.
    for(int i = 0; i < 3; i++)
        r.ui.KnobClick(3, r.now);
    r.ui.Chompi(true), r.ui.KnobTurn(3, -10, false), r.ui.Chompi(false);
    CHECK(prm[DRUM_DIST_MIX] < 1.f);

    // The sound: distorted is louder relative to its peak (squashed), and a
    // mix of 0 is the dry signal exactly.
    auto run = [](float drive, float mix, std::vector<float>& out) {
        DrumFx fx;
        fx.Init(48000.f);
        DrumFx::Settings st;
        st.drive = drive, st.dist_mix = mix;
        fx.Set(st);
        out.resize(4800);
        for(size_t i = 0; i < out.size(); i++)
            out[i] = 0.6f * sinf(6.2832f * 110.f * i / 48000.f) * expf(-static_cast<float>(i) / 1200.f);
        for(size_t i = 0; i < out.size(); i += 48)
            fx.Process(&out[i], 48);
    };
    std::vector<float> dry, dist, mix0;
    run(0.f, 1.f, dry), run(0.8f, 1.f, dist), run(0.8f, 0.f, mix0);
    auto crest = [](const std::vector<float>& v) {
        double e = 0, pk = 0;
        for(float x : v)
            e += x * x, pk = std::max(pk, static_cast<double>(fabsf(x)));
        return pk / sqrt(e / v.size());
    };
    CHECK(crest(dist) < crest(dry) * 0.7); // squashed
    bool same = true;
    for(size_t i = 0; i < dry.size(); i++)
        same &= fabsf(dry[i] - mix0[i]) < 1e-6f;
    CHECK(same);
}

static void TestDrumBeatLights()
{
    printf("drums' side: the purple knob's lights alternate in eighths\n");
    Rig r;
    r.Run(1000);
    r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false), r.Run(120), r.ui.Chompi(true), r.Run(60), r.ui.Chompi(false);
    r.Run(500);
    // Stopped: from the tempo (120 BPM: an eighth every 250 ms), left then right.
    LedFrame f;
    bool left = false, right = false, both = false;
    for(int ms = 0; ms < 1000; ms++)
    {
        r.Run(1);
        r.ui.Draw(f, r.now);
        left |= f.knob[4].r > 0.3f, right |= f.big_right.r > 0.3f;
        both |= f.knob[4].r > 0.3f && f.big_right.r > 0.3f;
    }
    CHECK(left && right && !both);
    // Running: from the pattern, as the bass side.
    DemoPattern(r.m.patterns[0]);
    r.m.Play();
    while(r.m.CurrentStep() != 2)
        r.Run(1);
    r.Run(20), r.ui.NoteStep(r.now), r.ui.Draw(f, r.now);
    CHECK(f.knob[4].r == 0.f && f.big_right.r > 0.5f);
    r.m.Stop();
}

static void TestTempoRange()
{
    printf("tempo: 40-240 BPM; old saved tempos keep their BPM\n");
    CHECK(TempoBpm(0.f) == 40.f && TempoBpm(1.f) == 240.f);
    CHECK(fabsf(TempoBpm(kParams[TEMPO].def) - 120.f) < 0.01f);
    // A setting saved with the old 60-200 range: 0.4929 was 129 BPM.
    char old[] = "tempo 0.4929\n";
    Settings st;
    ReadSettings(old, st);
    CHECK(fabsf(TempoBpm(st.params[TEMPO]) - (60.f + 140.f * 0.4929f)) < 0.05f);
    // And the new name round-trips.
    char buf[4096];
    CHECK(WriteSettings(st, buf, sizeof buf) > 0 && strstr(buf, "tempo2 "));
    Settings back;
    ReadSettings(buf, back);
    CHECK(fabsf(TempoBpm(back.params[TEMPO]) - TempoBpm(st.params[TEMPO])) < 0.05f);
    // The ends are reachable by turning: down to 40, up to 240.
    Rig r;
    r.ui.KnobClick(5, r.now); // volume, page 2: tempo
    for(int i = 0; i < 300; i++)
        r.ui.KnobTurn(5, -1, false, 400);
    CHECK(fabsf(TempoBpm(r.m.settings.params[TEMPO]) - 40.f) < 0.01f);
    for(int i = 0; i < 300; i++)
        r.ui.KnobTurn(5, 1, false, 400);
    CHECK(fabsf(TempoBpm(r.m.settings.params[TEMPO]) - 240.f) < 0.01f);
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
    TestWriteProtect();
    TestPatternPageWhileRunning();
    TestPatternSides();
    TestMidiExport();
    TestMidiImport();
    TestDelayTime();
    TestDefaults();
    TestDrumVoices();
    TestDrumSequencing();
    TestDrumPanel();
    TestDrumMuteSoloMix();
    TestDrumEffects();
    TestCompressorAndSidechain();
    TestDrumDistortion();
    TestDrumBeatLights();
    TestTempoRange();
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
