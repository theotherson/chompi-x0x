/** @file ui.h
 *  @brief The front panel's behaviour, separate from the hardware: what each
 *  key, knob and button does in each mode, and what every LED shows.
 *  code/src/panel.h feeds it the CHOMPI's switches and draws its LED frame.
 *
 *  The keybed's 25 keys are numbered 0-24 in pitch order, C3 to C5.
 *
 *  Steps: the 15 white keys show the 16 steps. White keys 1-7 are steps 1-7,
 *  white keys 9-15 are steps 10-16, and middle C (white key 8) is step 8 or
 *  step 9. The half in view (steps 1-8 or 9-16) is lit normally and the other
 *  dimmed; the view follows the half you last pressed a key in, and while the
 *  pattern runs it follows the playhead once the second half has notes.
 *
 *  Step lights: on = red, accent = bright red, tie = dim red.
 *
 *  Toggle switch: STEP mode (up) or PITCH mode (down).
 *
 *  STEP mode
 *    white key                     select that step; the selected step again
 *                                  turns it on or off (its note is kept)
 *    hold CHOMPI                   the keybed is a keyboard: a key sets the
 *                                  selected step's note and turns it on
 *    black keys 1-5                parameter pages: octave DOWN, octave UP,
 *                                  ACCENT, SLIDE, TIE. On a page the step keys
 *                                  toggle that flag; the page key again goes
 *                                  back to notes
 *    black key 6 (C#4)             transpose mode on: while on, any key sets
 *                                  the transpose (middle C = none); tap a key
 *                                  twice quickly to set it and leave (or hold
 *                                  C#4 2 s)
 *    black key 7 (D#4)             view steps 1-8 / 9-16
 *    black key 8 (F#4)             PATTERN page: step keys pick pattern 1-16.
 *                                  Running, it waits for the bar; the same key
 *                                  again switches at once. Held 2 s: write
 *                                  protect on/off (all LEDs flash red / green;
 *                                  the key is red while protected)
 *    black key 9 (G#4)             COPY: hold it and press a step key to copy
 *                                  this pattern to that pattern number
 *    black key 10 (A#4)            CLEAR: tap clears the selected step, hold
 *                                  1 s clears the pattern
 *    LOOP                          tap tempo
 *
 *  PITCH mode
 *    keys                          play live; overlapping notes slide
 *    LOOP tap                      record on/off
 *    LOOP held 2 s                 clear the pattern
 *      running, record on          played notes go into the pattern
 *                                  (quantized or not: knob 3, page 2)
 *      stopped, record on          note entry: each note fills the next step
 *                                  and the pattern grows to it
 *    CHOMPI + C#3 / D#3            live keyboard octave down / up
 *    CHOMPI + F#3 / G#3 / A#3      accent / slide / tie: on the step playing
 *                                  now while recording; in note entry accent
 *                                  and slide on the last step, A#3 adds a tie
 *    CHOMPI + C#4                  transpose mode on/off: transposes the
 *                                  arpeggio while the arpeggiator is on, the
 *                                  pattern otherwise; a key tapped twice
 *                                  quickly also sets it and leaves
 *    CHOMPI + D#4                  quantize on/off (when let go); while it's
 *                                  on, CHOMPI + white keys 1-3 pick its grid
 *                                  (1/16, 1/8, 1/4), shown on those keys.
 *                                  Held 2 s: quantizes the pattern for good
 *                                  to that grid (the recorded timing is
 *                                  gone), all keys flashing light blue. In
 *                                  note entry: view steps 1-8 / 9-16
 *    CHOMPI + PLAY                 arpeggiator on/off
 *    CHOMPI + LOOP                 arpeggiator latch: latched, each key adds
 *                                  its note to the chord or takes it out
 *    CHOMPI + F#4 / A#4            arpeggiator octaves below / above the
 *                                  chord, 0-2 each (white keys 1-5 show it)
 *    CHOMPI + G#4                  arpeggiator pattern: up, down, up-down,
 *                                  random, as played
 *    CHOMPI + the blinking step's      in note entry: a rest
 *      white key
 *
 *  CHOMPI double tap (two quick taps, nothing else touched): swap the panel
 *  between the bass and the drums. Both always play; this only picks which
 *  the keys and knobs edit. The drums aren't built yet: on their side the
 *  keys, knobs 1-4 and LOOP do nothing (PLAY, tap tempo and the volume
 *  knob, all shared, still work).
 *
 *  Knobs: clicking knob 1, knob 4 or volume steps through its pages;
 *  CHOMPI + click resets both functions of the knob's page to their
 *  defaults (knob 4: all the effects, every page). The big knob's click is
 *  tap tempo. See params.h kKnobMap for what each turns.
 */
#pragma once
#include "machine.h"

namespace x0x
{

struct Rgb
{
    float r = 0.f, g = 0.f, b = 0.f;
};

inline Rgb Scale(Rgb c, float k)
{
    return {c.r * k, c.g * k, c.b * k};
}

/** Every LED the panel drives, 0..1 per colour. */
struct LedFrame
{
    Rgb key[kKeyNotes];
    Rgb knob[6];
    Rgb big_right; // the big knob's second LED (knob[4] is its first)
    Rgb play, loop, chompi;
};

class Ui
{
  public:
    enum class Mode : uint8_t
    {
        STEP,
        PITCH,
    };

    enum class Page : uint8_t
    {
        NOTES,
        DOWN,
        UP,
        ACCENT,
        SLIDE,
        TIE,
        PATTERN,
    };

    static constexpr int kWhite[15] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24};
    static constexpr int kBlack[10] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
    static constexpr int kMiddleC   = 12; // white key 8
    static constexpr int kKeyTranspose = 13; // black key 6
    static constexpr int kKeyView      = 15; // black key 7
    static constexpr int kKeyPattern   = 18; // black key 8
    static constexpr int kKeyCopy      = 20; // black key 9
    static constexpr int kKeyClear     = 22; // black key 10

    static constexpr uint32_t kClearHoldMs  = 1000;
    static constexpr uint32_t kTransposeExitMs = 2000;
    static constexpr uint32_t kDoubleTapMs     = 350;
    // CHOMPI double tap: each tap 30-250 ms, the second pressed 40-350 ms
    // after the first is let go (the minimums keep contact bounce out).
    static constexpr uint32_t kChompiTapMinMs  = 30;
    static constexpr uint32_t kChompiTapMaxMs  = 250;
    static constexpr uint32_t kChompiGapMinMs  = 40;
    static constexpr uint32_t kChompiGapMaxMs  = 350;
    static constexpr uint32_t kSwapFlashMs     = 350;
    static constexpr uint32_t kShowLengthMs = 1200;
    static constexpr uint32_t kLoopClearMs = 2000;
    static constexpr uint32_t kQuantizeHoldMs = 2000;
    static constexpr uint32_t kProtectHoldMs  = 2000;

    void Init(Machine* m) { m_ = m; }

    Mode GetMode() const { return mode_; }
    /** Which machine the panel edits: the bass, or the drums. */
    bool OnDrums() const { return drums_; }
    Page GetPage() const { return page_; }
    int  Selected() const { return selected_; }
    int  KnobPage(int knob) const { return knob >= 0 && knob < 6 ? knob_page_[knob] : 0; }
    int  KeyboardOctave() const { return kbd_octave_; }
    bool NoteEntry() const { return mode_ == Mode::PITCH && m_->Recording() && !m_->Running(); }
    int  Cursor() const { return cursor_; }
    bool TransposeMode() const { return transpose_mode_; }

    /** Whether middle C is step 9 (true) or step 8 right now. */
    bool SecondHalf() const
    {
        const int cur = m_->CurrentStep();
        if(cur >= 0 && SecondHalfUsed())
            return cur >= 8;
        return half_ == 1;
    }

    // ------------------------------------------------------------ input

    void SetMode(Mode mode)
    {
        if(mode == mode_)
            return;
        Touched();
        ReleaseAll();
        quant_down_     = false;
        pattern_down_   = false;
        mode_           = mode;
        page_           = Page::NOTES;
        transpose_mode_ = false;
        m_->SetRecording(false);
    }

    void Chompi(bool down)
    {
        if(down == chompi_)
            return;
        ReleaseAll(); // notes started under one layer end when it changes
        chompi_ = down;
        const uint32_t now = last_tick_;
        if(down)
        {
            // The second tap must start soon (but not too soon) after the first.
            const uint32_t gap = now - chompi_up_at_;
            if(chompi_taps_ == 1 && (gap < kChompiGapMinMs || gap > kChompiGapMaxMs))
                chompi_taps_ = 0;
            chompi_down_at_ = now;
            chompi_clean_   = true;
            return;
        }
        const uint32_t held = now - chompi_down_at_;
        if(!chompi_clean_ || held < kChompiTapMinMs || held > kChompiTapMaxMs)
        {
            chompi_taps_ = 0;
            return;
        }
        chompi_up_at_ = now;
        if(++chompi_taps_ == 2)
        {
            chompi_taps_ = 0;
            SwapMachine();
        }
    }

    void KeyDown(int k, uint32_t now)
    {
        if(k < 0 || k >= kKeyNotes)
            return;
        Touched();
        held_[k] = true;
        if(drums_)
            return; // not built yet
        if(mode_ == Mode::PITCH)
            PitchKey(k, now);
        else
            StepModeKey(k, now);
    }

    void KeyUp(int k, uint32_t now)
    {
        if(k < 0 || k >= kKeyNotes)
            return;
        held_[k] = false;
        if(k == kKeyTranspose)
            c4_held_ = false;
        if(sounding_[k])
            Sound(k, false);
        if(drums_)
            return;
        if(k == kKeyPattern && pattern_down_)
        {
            pattern_down_ = false; // let go before the 2 s: a tap
            TogglePage(Page::PATTERN);
        }
        if(k == kKeyView && quant_down_)
        {
            quant_down_ = false; // let go before the 2 s: a tap
            m_->SetParam(QUANTIZE, Quantizing() ? 0.f : 1.f);
        }
        if(mode_ == Mode::STEP && !chompi_ && k == kKeyClear && !clear_done_)
        {
            m_->Current().steps[selected_] = Step{}; // a tap clears the selected step
            m_->PatternEdited();
        }
    }

    /** PLAY: run / stop. CHOMPI + PLAY: arpeggiator on / off. */
    void Play()
    {
        Touched();
        if(chompi_ && drums_)
            return;
        if(chompi_)
            m_->SetArpOn(!m_->ArpOn());
        else
            m_->TogglePlay();
    }

    /** LOOP pressed. Step mode: tap tempo. Pitch mode: record on/off when
     *  let go, unless held 2 s, which clears the pattern instead. */
    void LoopDown(uint32_t now)
    {
        Touched();
        if(drums_)
            return;
        if(chompi_)
        {
            m_->SetArpLatch(!m_->ArpLatch()); // CHOMPI + LOOP: arpeggiator latch
            return;
        }
        if(mode_ == Mode::STEP)
        {
            m_->Tap(now);
            tapped_at_ = now;
            return;
        }
        loop_down_    = true;
        loop_down_at_ = now;
        loop_cleared_ = false;
    }

    void LoopUp(uint32_t now)
    {
        if(!loop_down_)
            return;
        loop_down_ = false;
        if(loop_cleared_)
            return;
        const bool arm = !m_->Recording();
        m_->SetRecording(arm);
        if(arm && !m_->Running())
            cursor_ = 0; // note entry starts at step 1
    }

    /** A tap of LOOP (press and release). */
    void Loop(uint32_t now)
    {
        LoopDown(now);
        LoopUp(now);
    }

    /** knob 0-5: knobs 1-4, big, volume. */
    void KnobTurn(int knob, int inc, bool fast)
    {
        Touched();
        if(drums_ && knob != 5)
            return; // the volume knob (volume, drive, tempo, swing) is shared
        const uint8_t sel = kKnobMap[knob][KnobPage(knob)][chompi_ ? 1 : 0];
        if(sel == kKnobNone)
            return;
        const int dir = inc > 0 ? 1 : -1;
        if(sel == kKnobLength)
        {
            m_->SetLength(m_->Current().length + dir);
            length_shown_at_ = last_tick_; // show it on the keys for a moment
            return;
        }
        if(knob == 4)
            big_turned_at_ = last_tick_; // its LEDs show cutoff / resonance for a moment
        const Param p     = static_cast<Param>(sel);
        const int   steps = kParams[p].steps;
        const float v     = m_->settings.params[p];
        if(steps)
            m_->SetParam(p, StepValue(StepIndex(v, steps) + dir, steps));
        else if(p == TEMPO)
            m_->SetParam(p, v + inc / 140.f); // 1 BPM a click
        else
            m_->SetParam(p, v + inc * (fast ? 0.024f : 0.008f) * KnobSpeed(p));
    }

    void KnobClick(int knob, uint32_t now)
    {
        Touched();
        if(drums_ && knob < 4)
            return; // tap tempo and the volume knob are shared
        if(chompi_)
        {
            // Both functions of this knob's page back to their defaults;
            // for knob 4 (the effects) every page at once.
            const int first = knob == 3 ? 0 : KnobPage(knob);
            const int last  = knob == 3 ? kKnobPages[3] - 1 : KnobPage(knob);
            for(int page = first; page <= last; page++)
            for(int layer = 0; layer < 2; layer++)
            {
                const uint8_t sel = kKnobMap[knob][page][layer];
                if(sel == kKnobLength)
                {
                    m_->SetLength(kSteps);
                    length_shown_at_ = last_tick_;
                }
                else if(sel != kKnobNone)
                    m_->SetParam(sel, kParams[sel].def);
            }
            return;
        }
        if(knob == 4)
            m_->Tap(now);
        else
            knob_page_[knob] = (knob_page_[knob] + 1) % kKnobPages[knob];
        if(knob == 5)
        {
            // The volume knob's click also ends any stuck notes.
            ReleaseAll();
            m_->AllLiveOff();
        }
    }

    /** Once per block: timed actions (holding CLEAR, holding LOOP). */
    void Tick(uint32_t now)
    {
        last_tick_ = now;
        if(mode_ == Mode::STEP && transpose_mode_ && c4_held_ && now - c4_down_at_ >= kTransposeExitMs)
        {
            transpose_mode_ = false;
            c4_held_        = false;
            m_->SetTranspose(transpose_before_hold_);
        }
        if(pattern_down_ && now - pattern_down_at_ >= kProtectHoldMs)
        {
            pattern_down_ = false;
            m_->SetProtected(!m_->Protected());
            protect_flash_at_ = now;
        }
        if(quant_down_ && now - quant_down_at_ >= kQuantizeHoldMs)
        {
            quant_down_ = false;
            m_->QuantizePattern(QuantGridSteps(m_->settings.params[QUANT_GRID]));
            quantized_at_ = now;
        }
        if(mode_ == Mode::PITCH && loop_down_ && !loop_cleared_ && now - loop_down_at_ >= kLoopClearMs)
        {
            m_->ClearPattern();
            m_->SetRecording(false);
            loop_cleared_ = true;
            cleared_at_   = now;
            cursor_       = 0;
        }
        if(mode_ == Mode::STEP && !chompi_ && held_[kKeyClear] && !clear_done_
           && now - clear_down_ >= kClearHoldMs)
        {
            m_->Current().Clear();
            m_->PatternEdited();
            clear_done_ = true;
            cleared_at_ = now;
        }
    }

    // ------------------------------------------------------------ LEDs

    /** Called by the panel once per draw, so the step flash is timed from
     *  when a new step was first seen. */
    void NoteStep(uint32_t now)
    {
        const uint32_t c = m_->StepCount();
        if(c != last_step_count_)
        {
            last_step_count_ = c;
            step_seen_at_    = now;
        }
        const uint32_t rc = m_->RecordCount() + input_count_;
        if(rc != last_record_count_)
        {
            last_record_count_ = rc;
            rec_flash_at_      = now;
        }
        const uint32_t a = m_->ArpNoteCount();
        if(a != last_arp_count_)
        {
            last_arp_count_ = a;
            arp_seen_at_    = now;
            arp_flash_note_ = m_->ArpLastSource();
        }
    }

    void Draw(LedFrame& f, uint32_t now) const
    {
        f                   = LedFrame{};
        const bool blink    = (now / 150) % 2 == 0;
        const int  cur_step = m_->CurrentStep();
        const bool step_lit = cur_step >= 0 && now - step_seen_at_ < 70;

        if(mode_ == Mode::STEP && chompi_)
            DrawKeyboard(f, blink);
        else if(mode_ == Mode::STEP && transpose_mode_)
            DrawPlayhead(f, cur_step); // the transpose on top, as in live mode
        else if(mode_ == Mode::STEP)
            DrawStepMode(f, now, blink, cur_step, step_lit);
        else
            DrawPitchMode(f, blink, cur_step);
        if(transpose_mode_ && !chompi_)
            DrawTranspose(f);
        if(mode_ == Mode::PITCH && m_->ArpEngaged())
            DrawArp(f, now);

        if(now - length_shown_at_ < kShowLengthMs)
            DrawLength(f);
        if(now - arp_shown_at_ < kShowValueMs)
            DrawArpSetting(f);

        if(now - cleared_at_ < 300)
            for(int k = 0; k < kKeyNotes; k++)
                f.key[k] = {1.f, 0.f, 0.f};
        if(quant_down_ && now - quant_down_at_ > 300)
        {
            // Held: D#4 fills light blue towards quantizing the pattern.
            const float b = Clamp((now - quant_down_at_) / static_cast<float>(kQuantizeHoldMs), 0.f, 1.f);
            f.key[kKeyView] = Scale(kQuantizedColour, b);
        }
        if(now - quantized_at_ < 400)
            for(int k = 0; k < kKeyNotes; k++)
                f.key[k] = kQuantizedColour;

        DrawKnobs(f, now, cur_step);
        DrawBeat(f, now, cur_step);

        // PLAY: steady green while running.
        if(m_->Running())
            f.play = {0.f, .7f, .1f};
        if(mode_ == Mode::STEP)
            f.loop = now - tapped_at_ < 100 ? Rgb{1.f, 1.f, 1.f} : Rgb{}; // tap tempo
        else if(loop_down_ && !loop_cleared_ && now - loop_down_at_ > 300)
        {
            // Held: fills red towards the clear.
            const float b = Clamp((now - loop_down_at_) / static_cast<float>(kLoopClearMs), 0.f, 1.f);
            f.loop        = {b, 0.f, 0.f};
        }
        else if(m_->Recording())
            f.loop = m_->Running() && blink ? Rgb{1.f, 0.f, 0.f} : Rgb{.7f, 0.f, 0.f};
        // CHOMPI held: PLAY shows the arpeggiator (cyan), LOOP its latch
        // (orange).
        if(chompi_)
        {
            f.play = Scale(kArpOnColour, m_->ArpOn() ? 1.f : 0.12f);
            f.loop = Scale(kLatchColour, m_->ArpLatch() ? 1.f : 0.12f);
        }

        if(chompi_)
            f.chompi = {1.f, 1.f, 1.f};
        else if(NoteEntry())
            f.chompi = {.6f, 0.f, .5f};
        else if(mode_ == Mode::STEP)
            f.chompi = {.5f, 0.f, 0.f};
        else
            f.chompi = m_->ArpOn() ? Rgb{0.f, .5f, .35f} : Rgb{0.f, .15f, .5f}; // teal = arp on
        if(drums_)
            DrawDrums(f, now, cur_step, step_lit);
        // While the pattern runs it flashes brighter on each step that plays
        // a note, brightest on the beat.
        else if(!chompi_ && step_lit)
        {
            const Step& s = m_->Current().steps[cur_step];
            if(s.on && !s.tie)
            {
                const float g = cur_step % 4 == 0 ? 2.f : 1.5f;
                f.chompi      = {Clamp(f.chompi.r * g + .15f, 0.f, 1.f), Clamp(f.chompi.g * g + .15f, 0.f, 1.f),
                                 Clamp(f.chompi.b * g + .15f, 0.f, 1.f)};
            }
        }
        // Write protect switched: every light flashes red (on) or green (off).
        if(now - protect_flash_at_ < 600)
        {
            const Rgb c = m_->Protected() ? kProtectColour : kUnprotectColour;
            const Rgb d = ((now - protect_flash_at_) / 150) % 2 == 0 ? c : Rgb{};
            for(int k = 0; k < kKeyNotes; k++)
                f.key[k] = d;
            for(int k = 0; k < 6; k++)
                f.knob[k] = d;
            f.big_right = f.play = f.loop = f.chompi = d;
        }
        // Just swapped: the keybed flashes the new side's colour, fading.
        if(now - swapped_at_ < kSwapFlashMs)
        {
            const float k = 1.f - (now - swapped_at_) / static_cast<float>(kSwapFlashMs);
            for(int i = 0; i < kKeyNotes; i++)
                f.key[i] = Scale(drums_ ? kDrumColour : kRed, k);
        }
    }

    /** The drums' side, for now a stand-in: black keys 1-8 dim in the
     *  colours the 606's seven instruments and accent will have, the
     *  playhead dim white on the white keys while running. Knobs 1-4 dark;
     *  the volume knob and the beat lights work as on the bass side. */
    void DrawDrums(LedFrame& f, uint32_t now, int cur_step, bool step_lit) const
    {
        static constexpr Rgb kInst[8] = {
            {1.f, 0.f, 0.f}, {1.f, .5f, 0.f}, {1.f, 1.f, 0.f}, {0.f, 1.f, .2f},
            {0.f, .8f, 1.f}, {0.f, .2f, 1.f}, {.7f, 0.f, 1.f}, {1.f, 1.f, 1.f},
        };
        for(int i = 0; i < kKeyNotes; i++)
            f.key[i] = {};
        for(int i = 0; i < 8; i++)
            f.key[kBlack[i]] = Scale(kInst[i], 0.12f);
        if(m_->Running() && cur_step >= 0)
        {
            const int k = WhiteOfStep(cur_step);
            if(k >= 0)
                f.key[k] = Rgb{.35f, .35f, .35f};
        }
        for(int k = 0; k < 5; k++)
            f.knob[k] = {};
        DrawBeat(f, now, cur_step);
        f.loop = {};
        f.play = m_->Running() ? Rgb{0.f, .7f, .1f} : Rgb{};
        if(chompi_)
            f.chompi = {1.f, 1.f, 1.f};
        else
        {
            const float b = step_lit && cur_step % 4 == 0 ? 1.f : .45f;
            f.chompi      = Scale(kDrumColour, b);
        }
    }

    static constexpr Rgb kRed             = {1.f, 0.f, 0.f};
    static constexpr Rgb kTransposeColour    = {1.f, .85f, 0.f};  // the amount: yellow
    static constexpr Rgb kTransposeKeyColour = {1.f, .85f, 0.f};  // C#4, the mode key: yellow, dim
    static constexpr Rgb kArpOnColour        = {0.f, .85f, 1.f};  // cyan
    static constexpr Rgb kLatchColour        = {1.f, .45f, 0.f};  // orange
    static constexpr Rgb kQuantizeColour     = {0.f, .5f, 1.f};   // blue
    static constexpr Rgb kDrumColour         = {1.f, .6f, 0.f};   // the drums' side: amber
    static constexpr Rgb kQuantizedColour    = {.35f, .75f, 1.f}; // pattern quantized: light blue
    static constexpr Rgb kProtectColour      = {1.f, 0.f, 0.f};   // write protect on
    static constexpr Rgb kUnprotectColour    = {0.f, 1.f, .2f};   // write protect off
    static constexpr uint32_t kShowValueMs   = 1200;
    static constexpr int      kShowOctaves   = 0;
    static constexpr int      kShowPattern   = 1;
    static constexpr Rgb kArpColour       = {0.f, 1.f, .7f};
    // Knob colours: [knob][page][CHOMPI held], so each function has its own.
    // clang-format off
    static constexpr Rgb kKnobColour[6][kMaxKnobPages][2] = {
        {{{1.f, .55f, 0.f}, {1.f, 0.f, .8f}},     // wave (amber, cyan for square) | pulse width (magenta)
         {{1.f, 1.f, 1.f},  {.3f, .6f, 1.f}},     // length (white) | tuning (sky blue)
         {}, {}},
        {{{0.f, 1.f, .4f},  {1.f, .3f, 0.f}},     // env mod (green) | accent (orange, as its step page)
         {}, {}, {}},
        {{{.55f, 0.f, 1.f}, {0.f, .3f, 1.f}},     // decay (violet) | slide time (blue, as its step page)
         {}, {}, {}},
        {{{0.f, .9f, 1.f},  {1.f, 1.f, 1.f}},     // delay (cyan) | delay time (white)
         {{1.f, .6f, .1f},  {.6f, .4f, 1.f}},     // tape feedback (amber) | tone (lavender)
         {{1.f, 0.f, .6f},  {0.f, .8f, .8f}},     // mod (pink) | width (teal)
         {{.3f, 1.f, 0.f},  {1.f, .15f, 0.f}}},   // crush (green) | rate (red)
        {{{.7f, .2f, 1.f},  {1.f, 0.f, .2f}}, {}, {}, {}},  // cutoff (purple) | resonance (red)
        {{{1.f, 1.f, 1.f},  {}},                  // volume (white) | drive: orange to red
         {{1.f, .85f, 0.f}, {1.f, .15f, .45f}},   // tempo (yellow, flashing the beat) | swing (pink)
         {}, {}},
    };
    // clang-format on
    static constexpr Rgb kPageColour[7] = {
        {1.f, 0.f, 0.f},   // notes      red
        {.6f, 0.f, 1.f},   // DOWN       purple
        {0.f, .9f, 1.f},   // UP         cyan
        {1.f, .35f, 0.f},  // ACCENT     orange
        {0.f, .3f, 1.f},   // SLIDE      blue
        {0.f, 1.f, .2f},   // TIE        green
        {1.f, .85f, 0.f},  // PATTERN    yellow
    };

  private:
    // ------------------------------------------------------------ keys

    static int WhiteIndex(int k)
    {
        for(int i = 0; i < 15; i++)
            if(kWhite[i] == k)
                return i;
        return -1;
    }

    static int BlackIndex(int k)
    {
        for(int i = 0; i < 10; i++)
            if(kBlack[i] == k)
                return i;
        return -1;
    }

    /** The step white key w (0-14) stands for. */
    int StepOfWhite(int w) const
    {
        if(w < 7)
            return w;
        if(w > 7)
            return w + 1;
        return SecondHalf() ? 8 : 7;
    }

    /** The white key that shows step s, or -1 if it isn't shown now (step 8
     *  or 9, whichever middle C isn't). */
    int WhiteOfStep(int s) const
    {
        if(s < 7)
            return kWhite[s];
        if(s > 8)
            return kWhite[s - 1];
        return (s == 8) == SecondHalf() ? kMiddleC : -1;
    }

    bool SecondHalfUsed() const
    {
        const Pattern& p = m_->Current();
        if(p.length <= 8)
            return false;
        for(int i = 8; i < p.length; i++)
            if(p.steps[i].on)
                return true;
        return false;
    }

    void StepModeKey(int k, uint32_t now)
    {
        if(transpose_mode_ && !chompi_)
        {
            // Every key sets the transpose, C#4 included (+1). A key tapped
            // twice quickly ends transpose mode, keeping its transpose;
            // holding C#4 for 2 s does too (see Tick), undoing its +1.
            const int before = m_->Transpose();
            m_->SetTranspose(k - kMiddleC);
            if(TransposeDoubleTap(k, now))
                return;
            if(k == kKeyTranspose)
            {
                transpose_before_hold_ = before;
                c4_down_at_            = now;
                c4_held_               = true;
            }
            return;
        }
        if(chompi_)
        {
            // The keybed as a keyboard: set the selected step's note.
            Step& s = m_->Current().steps[selected_];
            s.note  = static_cast<uint8_t>(k);
            s.on    = true;
            s.tie   = false;
            m_->PatternEdited();
            Sound(k, true);
            return;
        }
        const int w = WhiteIndex(k);
        if(w >= 0)
        {
            const int step = StepOfWhite(w);
            if(w != 7)
                half_ = w > 7 ? 1 : 0;
            StepKey(step);
            return;
        }
        switch(BlackIndex(k))
        {
            case 0: TogglePage(Page::DOWN); break;
            case 1: TogglePage(Page::UP); break;
            case 2: TogglePage(Page::ACCENT); break;
            case 3: TogglePage(Page::SLIDE); break;
            case 4: TogglePage(Page::TIE); break;
            case 5: transpose_mode_ = true, tap_key_ = -1; break;
            case 6: half_ = SecondHalf() ? 0 : 1; break;
            case 7: pattern_down_ = true, pattern_down_at_ = now; break; // a tap or a 2 s hold: see KeyUp, Tick
            case 9: clear_down_ = now, clear_done_ = false; break;
            default: break; // 8 = COPY, used while held
        }
    }

    void TogglePage(Page p) { page_ = page_ == p ? Page::NOTES : p; }

    /** Anything but CHOMPI was used: CHOMPI's press isn't a bare tap. */
    void Touched()
    {
        chompi_clean_ = false;
        chompi_taps_  = 0;
    }

    void SwapMachine()
    {
        ReleaseAll();
        drums_          = !drums_;
        transpose_mode_ = false;
        swapped_at_     = last_tick_;
    }

    bool Quantizing() const { return StepIndex(m_->settings.params[QUANTIZE], 2) == 1; }

    /** In transpose mode: the same key twice within kDoubleTapMs ends it. */
    bool TransposeDoubleTap(int k, uint32_t now)
    {
        const bool twice = k == tap_key_ && now - tap_at_ <= kDoubleTapMs;
        tap_key_         = twice ? -1 : k;
        tap_at_          = now;
        if(twice)
            transpose_mode_ = c4_held_ = false;
        return twice;
    }

    void StepKey(int step)
    {
        Pattern& pat = m_->Current();
        if(held_[kKeyCopy])
        {
            if(step != m_->CurrentPattern())
            {
                m_->patterns[step] = pat;
                m_->PatternEdited();
            }
            return;
        }
        Step& s = pat.steps[step];
        switch(page_)
        {
            case Page::NOTES:
                if(step == selected_)
                {
                    s.on = !s.on;
                    m_->PatternEdited();
                }
                else
                    selected_ = step;
                return;
            case Page::DOWN: s.octave = s.octave == -1 ? 0 : -1; break;
            case Page::UP: s.octave = s.octave == 1 ? 0 : 1; break;
            case Page::ACCENT: s.accent = !s.accent; break;
            case Page::SLIDE: s.slide = !s.slide; break;
            case Page::TIE: s.tie = !s.tie; break;
            case Page::PATTERN:
                // Running: the first press queues it, a second switches now.
                m_->SelectPattern(step, m_->QueuedPattern() == step);
                return;
        }
        m_->PatternEdited();
    }

    void PitchKey(int k, uint32_t now)
    {
        if(chompi_)
        {
            switch(BlackIndex(k))
            {
                case 0: kbd_octave_ = ClampInt(kbd_octave_ - 1, -1, 1); break;
                case 1: kbd_octave_ = ClampInt(kbd_octave_ + 1, -1, 1); break;
                case 2:
                case 3:
                case 4:
                    if(NoteEntry())
                        NoteEntryCommand(BlackIndex(k));
                    else if(m_->Recording() && m_->Running())
                        LiveFlag(BlackIndex(k));
                    break;
                case 5: transpose_mode_ = !transpose_mode_, tap_key_ = -1; break;
                case 6:
                    if(NoteEntry())
                        half_ = SecondHalf() ? 0 : 1;
                    else
                        quant_down_ = true, quant_down_at_ = now; // a tap or a 2 s hold: see KeyUp, Tick
                    break;
                case 7: CycleParam(ARP_OCT_DOWN, 3), arp_shown_ = kShowOctaves; break;
                case 8: CycleParam(ARP_MODE, 5), arp_shown_ = kShowPattern; break;
                case 9: CycleParam(ARP_OCT_UP, 3), arp_shown_ = kShowOctaves; break;
                default:
                {
                    const int w = WhiteIndex(k);
                    // Note entry: the next step's key (the one blinking)
                    // enters a rest. Middle C counts for step 8 or 9.
                    if(w >= 0 && NoteEntry())
                    {
                        if(StepOfWhite(w) == cursor_ || (w == 7 && (cursor_ == 7 || cursor_ == 8)))
                            Append(Step{});
                    }
                    // White keys 1-3 pick the quantize grid while it's on.
                    else if(w >= 0 && w < 3 && Quantizing())
                        m_->SetParam(QUANT_GRID, StepValue(w, 3));
                    break;
                }
            }
            return;
        }
        if(transpose_mode_)
        {
            // With the arpeggiator on, transpose moves the arpeggio, not
            // the pattern.
            if(m_->ArpOn())
                m_->SetArpTranspose(k - kMiddleC);
            else
                m_->SetTranspose(k - kMiddleC);
            TransposeDoubleTap(k, now);
            return;
        }
        Sound(k, true);
        if(NoteEntry())
        {
            Step s;
            s.note   = static_cast<uint8_t>(k);
            s.octave = static_cast<int8_t>(kbd_octave_);
            s.on     = true;
            Append(s);
        }
    }

    /** Note entry with CHOMPI held: F#3 accent and G#3 slide on the last
     *  step, A#3 a tie step. */
    void NoteEntryCommand(int b)
    {
        Pattern&  pat  = m_->Current();
        const int last = cursor_ - 1;
        switch(b)
        {
            case 2:
            case 3:
            {
                if(last < 0)
                    return;
                Step& s = pat.steps[last];
                if(b == 2)
                    s.accent = !s.accent;
                else
                    s.slide = !s.slide;
                m_->PatternEdited();
                return;
            }
            case 4: // tie: holds the note before through a new step
            {
                Step s;
                s.on  = true;
                s.tie = true;
                if(last >= 0)
                    s.note = pat.steps[last].note, s.octave = pat.steps[last].octave;
                Append(s);
                return;
            }
            default: return;
        }
    }

    /** Steps a stepped parameter on, wrapping round; shows it for a moment. */
    void CycleParam(Param p, int steps)
    {
        m_->SetParam(p, StepValue((StepIndex(m_->settings.params[p], steps) + 1) % steps, steps));
        arp_shown_at_ = last_tick_;
    }

    /** Recording while running, CHOMPI + F#3 / G#3 / A#3: accent, slide or
     *  tie on the step playing now. */
    void LiveFlag(int b)
    {
        const int step = m_->NearestStep();
        if(step < 0)
            return;
        Step& s = m_->Current().steps[step];
        switch(b)
        {
            case 2: s.accent = !s.accent; break;
            case 3: s.slide = !s.slide; break;
            case 4:
                s.tie = !s.tie;
                if(s.tie)
                    s.on = true;
                break;
        }
        m_->PatternEdited();
    }

    /** Note entry: writes the next step, and the pattern grows to it. */
    void Append(const Step& s)
    {
        if(cursor_ >= kSteps)
            return;
        m_->Current().steps[cursor_] = s;
        cursor_++;
        input_count_++;
        half_ = cursor_ > 8 ? 1 : 0;
        m_->SetLength(cursor_);
    }

    /** Plays (or stops) key k's note live, in the live keyboard's octave in
     *  pitch mode. The note is remembered, so changing octave while a key is
     *  down still lets the right note go. */
    void Sound(int k, bool on)
    {
        if(on)
        {
            const int note = kBaseNote + k + (mode_ == Mode::PITCH ? 12 * kbd_octave_ : 0);
            m_->LiveNoteOn(note);
            sounding_note_[k] = note;
            sounding_[k]      = true;
        }
        else
        {
            m_->LiveNoteOff(sounding_note_[k]);
            sounding_[k] = false;
        }
    }

    void ReleaseAll()
    {
        for(int k = 0; k < kKeyNotes; k++)
            if(sounding_[k])
                Sound(k, false);
    }

    // ------------------------------------------------------------ drawing

    /** The 16 steps on the 15 white keys, via colour(step). The half not in
     *  view (steps 1-8 or 9-16) is dimmed. */
    template <typename F>
    void ForEachShownStep(F colour, LedFrame& f) const
    {
        const bool second = SecondHalf();
        for(int s = 0; s < kSteps; s++)
        {
            const int k = WhiteOfStep(s);
            if(k < 0)
                continue;
            const bool in_view = (s >= 8) == second;
            f.key[k]           = in_view ? colour(s) : Scale(colour(s), 0.2f);
        }
    }

    /** A step's colour on the notes view: on = red, accent = bright red,
     *  tie = dim red; dimmer past the pattern's length. */
    static Rgb NoteColour(const Step& s, bool past_end)
    {
        Rgb c;
        if(s.on)
            c = Scale(kRed, s.tie ? 0.12f : s.accent ? 1.f : 0.4f);
        return past_end ? Scale(c, 0.3f) : c;
    }

    /** Marks a step as the one being worked on: whitened. */
    static Rgb Whiten(Rgb c, float k)
    {
        return {Clamp(c.r + k, 0.f, 1.f), Clamp(c.g + k, 0.f, 1.f), Clamp(c.b + k, 0.f, 1.f)};
    }

    void DrawStepMode(LedFrame& f, uint32_t now, bool blink, int cur_step, bool step_lit) const
    {
        const Pattern& pat = m_->Current();
        const Rgb      pc  = kPageColour[static_cast<int>(page_)];

        ForEachShownStep(
            [&](int step) {
                const Step& s        = pat.steps[step];
                const bool  past_end = step >= pat.length;
                Rgb         c;
                switch(page_)
                {
                    case Page::NOTES:
                        c = NoteColour(s, past_end);
                        if(step == selected_)
                            c = s.on ? Whiten(c, 0.25f) : Rgb{.15f, .15f, .15f};
                        break;
                    case Page::PATTERN:
                        if(step == m_->CurrentPattern())
                            c = pc;
                        else if(step == m_->QueuedPattern())
                            c = blink ? pc : Rgb{};
                        else if(!m_->patterns[step].Empty())
                            c = Scale(pc, 0.12f);
                        break;
                    default:
                    {
                        bool flag = false;
                        switch(page_)
                        {
                            case Page::DOWN: flag = s.octave == -1; break;
                            case Page::UP: flag = s.octave == 1; break;
                            case Page::ACCENT: flag = s.accent; break;
                            case Page::SLIDE: flag = s.slide; break;
                            case Page::TIE: flag = s.tie; break;
                            default: break;
                        }
                        if(flag)
                            c = Scale(pc, past_end ? 0.3f : 1.f);
                        else if(s.on)
                            c = Scale(kRed, 0.12f);
                        break;
                    }
                }
                if(step_lit && cur_step == step)
                    c = {1.f, 1.f, 1.f}; // the playhead
                return c;
            },
            f);

        const Page pages[5] = {Page::DOWN, Page::UP, Page::ACCENT, Page::SLIDE, Page::TIE};
        for(int i = 0; i < 5; i++)
            f.key[kBlack[i]] = Scale(kPageColour[static_cast<int>(pages[i])], page_ == pages[i] ? 1.f : 0.1f);
        f.key[kKeyTranspose] = Scale(kTransposeKeyColour, transpose_mode_ ? 1.f : 0.15f);
        f.key[kKeyView]      = SecondHalf() ? Rgb{.5f, .5f, .5f} : Rgb{.08f, .08f, .08f};
        // The pattern key: yellow, or red while the patterns are write-protected.
        f.key[kKeyPattern] = Scale(m_->Protected() ? kProtectColour : kPageColour[static_cast<int>(Page::PATTERN)],
                                   page_ == Page::PATTERN ? 1.f : (m_->Protected() ? 0.3f : 0.1f));
        if(pattern_down_ && now - pattern_down_at_ > 300)
        {
            // Held: fills towards the switch, red to protect, green to unprotect.
            const float b = Clamp((now - pattern_down_at_) / static_cast<float>(kProtectHoldMs), 0.f, 1.f);
            f.key[kKeyPattern] = Scale(m_->Protected() ? kUnprotectColour : kProtectColour, b);
        }
        f.key[kKeyCopy]      = held_[kKeyCopy] ? Rgb{1.f, 1.f, 1.f} : Rgb{.08f, .08f, .08f};
        if(held_[kKeyClear] && !clear_done_)
        {
            const float b = 0.2f + 0.8f * Clamp((now - clear_down_) / static_cast<float>(kClearHoldMs), 0.f, 1.f);
            f.key[kKeyClear] = {b, 0.f, 0.f};
        }
        else
            f.key[kKeyClear] = {.12f, 0.f, 0.f};
    }

    /** Step mode with CHOMPI held: the keybed is a keyboard. The selected
     *  step's key flashes red; its note's key is lit blue. */
    void DrawKeyboard(LedFrame& f, bool blink) const
    {
        const Step& s = m_->Current().steps[selected_];
        if(s.on && !s.tie)
            f.key[s.note] = {0.f, .4f, 1.f};
        const int k = WhiteOfStep(selected_);
        if(k >= 0)
            f.key[k] = blink ? kRed : Rgb{};
        for(int i = 0; i < kKeyNotes; i++)
            if(held_[i])
                f.key[i] = {1.f, 1.f, 1.f};
    }

    /** One red light on the step playing (dim for a tie, dark for a rest). */
    void DrawPlayhead(LedFrame& f, int cur_step) const
    {
        if(!m_->Running() || cur_step < 0)
            return;
        const int k = WhiteOfStep(cur_step);
        if(k >= 0)
            f.key[k] = NoteColour(m_->Current().steps[cur_step], false);
    }

    /** Pitch mode. Running: one red light moves across the white keys in
     *  tempo, on the step playing (dim for a tie, dark for a rest). Step
     *  input: the steps entered so far, the last one whitened and the next
     *  one blinking. Otherwise the keybed is just a keyboard. Keys you hold
     *  are blue. With CHOMPI held the black keys show what they do now. */
    void DrawPitchMode(LedFrame& f, bool blink, int cur_step) const
    {
        const Pattern& pat   = m_->Current();
        const bool     input = NoteEntry();
        if(m_->Running() && cur_step >= 0)
            DrawPlayhead(f, cur_step);
        else if(input)
            ForEachShownStep(
                [&](int step) {
                    const Step& st = pat.steps[step];
                    Rgb         c  = NoteColour(st, step >= pat.length);
                    // Rests entered show dim grey (the one just entered
                    // brighter), so a rest is seen to go in.
                    if(!st.on && step < cursor_)
                        c = step == cursor_ - 1 ? Rgb{.3f, .3f, .3f} : Rgb{.06f, .06f, .06f};
                    if(step == cursor_ - 1 && st.on)
                        c = Whiten(c, 0.25f);
                    if(step == cursor_ && blink)
                        c = {.4f, .4f, .4f};
                    return c;
                },
                f);
        if(chompi_)
        {
            // Keyboard octave, flags, transpose, view, arp, latch, rest.
            f.key[kBlack[0]] = Scale(kPageColour[1], kbd_octave_ < 0 ? 1.f : 0.1f);
            f.key[kBlack[1]] = Scale(kPageColour[2], kbd_octave_ > 0 ? 1.f : 0.1f);
            const bool flags = input || (m_->Recording() && m_->Running());
            for(int i = 2; i < 5 && flags; i++)
                f.key[kBlack[i]] = kPageColour[i + 1];
            f.key[kKeyTranspose] = Scale(kTransposeKeyColour, transpose_mode_ ? 0.15f : 0.6f);
            if(input)
                f.key[kKeyView] = {.5f, .5f, .5f};
            else
            {
                // Quantize on/off; while on, white keys 1-3 show its grid.
                f.key[kKeyView] = Scale(kQuantizeColour, Quantizing() ? 1.f : 0.12f);
                if(Quantizing())
                {
                    const int g = StepIndex(m_->settings.params[QUANT_GRID], 3);
                    for(int i = 0; i < 3; i++)
                        f.key[kWhite[i]] = Scale(kQuantizeColour, i == g ? 1.f : 0.1f);
                }
            }
            // Arpeggiator: octaves down (F#4) and up (A#4) in the octave
            // keys' purple and cyan, brighter for more; its pattern (G#4).
            const float* p   = m_->settings.params;
            f.key[kBlack[7]] = Scale(kPageColour[1], 0.15f + 0.425f * StepIndex(p[ARP_OCT_DOWN], 3));
            f.key[kBlack[8]] = kArpColour;
            f.key[kBlack[9]] = Scale(kPageColour[2], 0.15f + 0.425f * StepIndex(p[ARP_OCT_UP], 3));
        }
        for(int i = 0; i < kKeyNotes; i++)
            if(held_[i])
                f.key[i] = {0.f, .4f, 1.f};
    }

    /** After CHOMPI + F#4 / A#4 / G#4: white keys 1-5 show the arpeggiator's
     *  octaves (-2 to +2, the chord's own octave bright, the others in use
     *  lit) or its pattern (up, down, up-down, random, as played). */
    void DrawArpSetting(LedFrame& f) const
    {
        const float* p = m_->settings.params;
        for(int i = 0; i < 5; i++)
            f.key[kWhite[i]] = {};
        if(arp_shown_ == kShowPattern)
        {
            const int mode = StepIndex(p[ARP_MODE], 5);
            for(int i = 0; i < 5; i++)
                f.key[kWhite[i]] = Scale(kArpColour, i == mode ? 1.f : 0.08f);
            return;
        }
        const int down = StepIndex(p[ARP_OCT_DOWN], 3), up = StepIndex(p[ARP_OCT_UP], 3);
        for(int o = -2; o <= 2; o++)
        {
            Rgb c;
            // Below the chord purple, above it cyan, as the octave keys.
            const Rgb oc = o < 0 ? kPageColour[1] : kPageColour[2];
            if(o == 0)
                c = {1.f, 1.f, 1.f};
            else if(o >= -down && o <= up)
                c = Scale(oc, 0.6f);
            else
                c = Scale(oc, 0.05f);
            f.key[kWhite[o + 2]] = c;
        }
    }

    /** Turning the length knob, in either mode: every step within the
     *  length dim white, the last one bright (white up to step 8, cyan from
     *  step 9), the rest dark. Middle C shows
     *  step 8 or 9, whichever keeps the last step in view. */
    void DrawLength(LedFrame& f) const
    {
        const int len    = m_->Current().length;
        const bool second = len - 1 >= 8;
        for(int st = 0; st < kSteps; st++)
        {
            int k;
            if(st < 7)
                k = kWhite[st];
            else if(st > 8)
                k = kWhite[st - 1];
            else if((st == 8) == second)
                k = kMiddleC;
            else
                continue;
            // The last step bright: white for steps 1-8, cyan from step 9
            // (middle C shows step 8 or 9, so the colour tells them apart).
            const Rgb last = second ? Rgb{0.f, .8f, 1.f} : Rgb{1.f, 1.f, 1.f};
            f.key[k]       = st == len - 1 ? last : st < len ? Rgb{.12f, .12f, .12f} : Rgb{};
        }
    }

    /** The keybed key that plays MIDI note `note` in pitch mode now, -1 if
     *  it's off the keybed (the keyboard octave counts). */
    int KeyOfNote(int note) const
    {
        const int k = note - kBaseNote - 12 * kbd_octave_;
        return k >= 0 && k < kKeyNotes ? k : -1;
    }

    /** The arpeggiator: latched notes steady in its colour, and each note
     *  flashing white as it plays, on top of everything. */
    void DrawArp(LedFrame& f, uint32_t now) const
    {
        const Arp& arp = m_->GetArp();
        // In transpose mode the keys show the transpose instead; the notes
        // still flash.
        if(arp.Latch() && !chompi_ && !transpose_mode_)
            for(int i = 0; i < arp.Count(); i++)
            {
                const int k = KeyOfNote(arp.NoteAt(i));
                if(k >= 0)
                    f.key[k] = Scale(kArpColour, 0.45f);
            }
        if(now - arp_seen_at_ < 80 && (arp.Active() || m_->SoundingNote() >= 0))
        {
            const int k = KeyOfNote(arp_flash_note_);
            if(k >= 0)
                f.key[k] = {1.f, 1.f, 1.f};
        }
    }

    /** Transpose mode: C#4 dim yellow, middle C (none) dim white, and the
     *  key of the transpose amount bright yellow, drawn last so C#4 is
     *  bright too when it is the amount. */
    void DrawTranspose(LedFrame& f) const
    {
        f.key[kMiddleC]      = {.15f, .15f, .15f};
        f.key[kKeyTranspose] = Scale(kTransposeKeyColour, 0.15f);
        const bool arp       = mode_ == Mode::PITCH && m_->ArpOn();
        const int  t         = kMiddleC + (arp ? m_->ArpTranspose() : m_->Transpose());
        if(t >= 0 && t < kKeyNotes)
            f.key[t] = kTransposeColour;
    }

    /** Each knob in its page's colour, brightness = the value it turns (the
     *  CHOMPI layer while CHOMPI is held). */
    /** The big knob's two LEDs show cutoff (resonance with CHOMPI held).
     *  While the pattern plays they flash instead, alternating sides, on
     *  steps 1, 5, 9 and 13 (the beats); the side of the current beat
     *  flashes red when a note is recorded. For a moment after the knob
     *  turns they show its value again. */
    void DrawBeat(LedFrame& f, uint32_t now, int cur_step) const
    {
        f.big_right = f.knob[4];
        if(!m_->Running() || cur_step < 0 || now - big_turned_at_ < kShowValueMs)
            return;
        const bool right = (cur_step / 4) % 2 == 1;
        Rgb        c;
        if(now - rec_flash_at_ < 150)
            c = {.6f, 0.f, 0.f};
        else if(cur_step % 4 == 0 && now - step_seen_at_ < 100)
            c = {.6f, .48f, 0.f};
        f.knob[4]   = right ? Rgb{} : c;
        f.big_right = right ? c : Rgb{};
    }

    /** On the beat: from the pattern while it runs, else from the tempo. */
    bool TempoBeat(uint32_t now, int cur_step) const
    {
        if(m_->Running() && cur_step >= 0)
            return cur_step % 4 == 0 && now - step_seen_at_ < 100;
        const float beats = now * m_->TempoBpmNow() / 60000.f;
        return beats - floorf(beats) < 0.1f;
    }

    void DrawKnobs(LedFrame& f, uint32_t now, int cur_step) const
    {
        for(int k = 0; k < 6; k++)
        {
            const int     page = KnobPage(k);
            const uint8_t sel  = kKnobMap[k][page][chompi_ ? 1 : 0];
            Rgb           c    = kKnobColour[k][page][chompi_ ? 1 : 0];
            float         v    = 0.f;
            if(sel == kKnobLength)
                v = m_->Current().length / static_cast<float>(kSteps);
            else if(sel == WAVE)
                c = StepIndex(m_->settings.params[WAVE], 2) ? Rgb{0.f, .8f, 1.f} : Rgb{1.f, .55f, 0.f}, v = 1.f;
            else if(sel == DRIVE)
            {
                // Orange at the bottom, red at the top.
                v = m_->settings.params[DRIVE];
                c = {1.f, .45f * (1.f - v), 0.f};
            }
            else if(sel == TEMPO)
                v = TempoBeat(now, cur_step) ? 1.f : 0.f; // flashes the beat
            else if(sel != kKnobNone)
                v = m_->settings.params[sel];
            // Never below a fifth, so the page's colour always shows.
            f.knob[k] = Scale(c, 0.2f + 0.8f * v);
        }
    }

    Machine* m_       = nullptr;
    Mode     mode_    = Mode::STEP;
    Page     page_    = Page::NOTES;
    int      selected_ = 0;
    int      half_     = 0;
    int      cursor_   = 0;
    bool     chompi_   = false;
    bool     transpose_mode_   = false;
    bool     c4_held_               = false; // step mode, transpose mode: C#4 down
    uint32_t c4_down_at_            = 0;
    int      transpose_before_hold_ = 0;
    int      tap_key_               = -1; // transpose mode: the last key, for double taps
    bool     drums_                 = false;
    bool     quant_down_            = false; // CHOMPI + D#4 down in live mode
    bool     pattern_down_          = false; // step mode: F#4 down (tap: page, hold: protect)
    uint32_t pattern_down_at_       = 0;
    uint32_t protect_flash_at_      = 0x80000000u;
    uint32_t quant_down_at_         = 0;
    uint32_t quantized_at_          = 0x80000000u;
    bool     chompi_clean_          = false; // CHOMPI down and nothing else touched
    int      chompi_taps_           = 0;
    uint32_t chompi_down_at_        = 0;
    uint32_t chompi_up_at_          = 0;
    uint32_t swapped_at_            = 0x80000000u;
    uint32_t tap_at_                = 0;
    int      knob_page_[6] = {};
    int      kbd_octave_   = 0; // pitch mode's live keyboard: -1, 0, +1
    int      sounding_note_[kKeyNotes] = {};
    bool     loop_down_    = false;
    bool     loop_cleared_ = false;
    uint32_t loop_down_at_ = 0;
    uint32_t tapped_at_    = 0x80000000u;
    bool     held_[kKeyNotes]     = {};
    bool     sounding_[kKeyNotes] = {};
    uint32_t clear_down_      = 0;
    bool     clear_done_      = true;
    uint32_t cleared_at_      = 0x80000000u;
    uint32_t last_tick_       = 0;
    uint32_t big_turned_at_   = 0x80000000u;
    uint32_t arp_shown_at_    = 0x80000000u;
    int      arp_shown_       = 0;
    uint32_t rec_flash_at_    = 0x80000000u;
    uint32_t last_record_count_ = 0;
    uint32_t input_count_     = 0;
    uint32_t length_shown_at_ = 0x80000000u;
    uint32_t last_step_count_ = 0;
    uint32_t step_seen_at_    = 0;
    uint32_t last_arp_count_  = 0;
    uint32_t arp_seen_at_     = 0x80000000u;
    int      arp_flash_note_  = -1;
};

// Out-of-class definitions for the arrays, which C++14 (the firmware's
// standard) needs when they are indexed.
constexpr int Ui::kWhite[15];
constexpr int Ui::kBlack[10];
constexpr Rgb Ui::kRed;
constexpr Rgb Ui::kTransposeColour;
constexpr Rgb Ui::kTransposeKeyColour;
constexpr Rgb Ui::kArpOnColour;
constexpr Rgb Ui::kProtectColour;
constexpr Rgb Ui::kUnprotectColour;
constexpr Rgb Ui::kQuantizedColour;
constexpr Rgb Ui::kDrumColour;
constexpr Rgb Ui::kQuantizeColour;
constexpr Rgb Ui::kLatchColour;
constexpr Rgb Ui::kArpColour;
constexpr Rgb Ui::kKnobColour[6][kMaxKnobPages][2];
constexpr Rgb Ui::kPageColour[7];

} // namespace x0x
