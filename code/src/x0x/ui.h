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
 *    black key 6 (C#4)             transpose mode on/off: while on, any key
 *                                  sets the transpose (middle C = none)
 *    black key 7 (D#4)             view steps 1-8 / 9-16
 *    black key 8 (F#4)             PATTERN page: step keys pick pattern 1-16.
 *                                  Running, it waits for the bar; the same key
 *                                  again switches at once
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
 *      stopped, record on          step input: each note fills the next step
 *                                  and the pattern grows to it
 *    CHOMPI + C#3 / D#3            live keyboard octave down / up
 *    CHOMPI + F#3 / G#3 / A#3      accent / slide / tie: on the step playing
 *                                  now while recording; in step input accent
 *                                  and slide on the last step, A#3 adds a tie
 *    CHOMPI + C#4                  transpose mode on/off
 *    CHOMPI + D#4                  view steps 1-8 / 9-16
 *    CHOMPI + F#4                  arpeggiator on/off (knob 3, page 3: mode
 *                                  and range)
 *    CHOMPI + G#4                  arpeggiator latch: latched, each key adds
 *                                  its note to the chord or takes it out
 *    CHOMPI + A#4                  step input: a rest
 *
 *  Knobs: clicking knobs 1-4 flips each between two pages; CHOMPI + click
 *  resets both functions of the knob's page to their defaults. The big
 *  knob's click is tap tempo. See params.h kKnobMap for what each turns.
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

    static constexpr uint32_t kClearHoldMs = 1000;
    static constexpr uint32_t kLoopClearMs = 2000;

    void Init(Machine* m) { m_ = m; }

    Mode GetMode() const { return mode_; }
    Page GetPage() const { return page_; }
    int  Selected() const { return selected_; }
    int  KnobPage(int knob) const { return knob < 4 ? knob_page_[knob] : 0; }
    int  KeyboardOctave() const { return kbd_octave_; }
    bool StepInput() const { return mode_ == Mode::PITCH && m_->Recording() && !m_->Running(); }
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
        ReleaseAll();
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
    }

    void KeyDown(int k, uint32_t now)
    {
        if(k < 0 || k >= kKeyNotes)
            return;
        held_[k] = true;
        if(mode_ == Mode::PITCH)
            PitchKey(k);
        else
            StepModeKey(k, now);
    }

    void KeyUp(int k, uint32_t now)
    {
        if(k < 0 || k >= kKeyNotes)
            return;
        held_[k] = false;
        if(sounding_[k])
            Sound(k, false);
        if(mode_ == Mode::STEP && !chompi_ && k == kKeyClear && !clear_done_)
        {
            m_->Current().steps[selected_] = Step{}; // a tap clears the selected step
            m_->PatternEdited();
        }
    }

    void Play() { m_->TogglePlay(); }

    /** LOOP pressed. Step mode: tap tempo. Pitch mode: record on/off when
     *  let go, unless held 2 s, which clears the pattern instead. */
    void LoopDown(uint32_t now)
    {
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
            cursor_ = 0; // step input starts at step 1
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
        const uint8_t sel = kKnobMap[knob][KnobPage(knob)][chompi_ ? 1 : 0];
        if(sel == kKnobNone)
            return;
        const int dir = inc > 0 ? 1 : -1;
        if(sel == kKnobLength)
        {
            m_->SetLength(m_->Current().length + dir);
            return;
        }
        const Param p     = static_cast<Param>(sel);
        const int   steps = kParams[p].steps;
        const float v     = m_->settings.params[p];
        if(steps)
            m_->SetParam(p, StepValue(StepIndex(v, steps) + dir, steps));
        else if(p == TEMPO)
            m_->SetParam(p, v + inc / 140.f); // 1 BPM a click
        else
            m_->SetParam(p, v + inc * (fast ? 0.024f : 0.008f));
    }

    void KnobClick(int knob, uint32_t now)
    {
        if(chompi_)
        {
            // Both functions of this knob's page back to their defaults.
            for(int layer = 0; layer < 2; layer++)
            {
                const uint8_t sel = kKnobMap[knob][KnobPage(knob)][layer];
                if(sel == kKnobLength)
                    m_->SetLength(kSteps);
                else if(sel != kKnobNone)
                    m_->SetParam(sel, kParams[sel].def);
            }
            return;
        }
        if(knob < 4)
            knob_page_[knob] = (knob_page_[knob] + 1) % kKnobPages[knob];
        else if(knob == 4)
            m_->Tap(now);
        else
        {
            ReleaseAll();
            m_->AllLiveOff();
        }
    }

    /** Once per block: timed actions (holding CLEAR, holding LOOP). */
    void Tick(uint32_t now)
    {
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
        else if(mode_ == Mode::STEP)
            DrawStepMode(f, now, blink, cur_step, step_lit);
        else
            DrawPitchMode(f, blink, cur_step, step_lit);
        if(transpose_mode_ && !chompi_)
            DrawTranspose(f);
        if(mode_ == Mode::PITCH && m_->ArpEngaged())
            DrawArp(f, now);

        if(now - cleared_at_ < 300)
            for(int k = 0; k < kKeyNotes; k++)
                f.key[k] = {1.f, 0.f, 0.f};

        DrawKnobs(f);

        if(m_->Running())
        {
            const float b = step_lit ? (cur_step % 4 == 0 ? 1.f : 0.45f) : 0.08f;
            f.play        = {b, b * .85f, 0.f};
        }
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
        if(chompi_)
            f.chompi = {1.f, 1.f, 1.f};
        else if(StepInput())
            f.chompi = {.6f, 0.f, .5f};
        else if(mode_ == Mode::STEP)
            f.chompi = {.5f, 0.f, 0.f};
        else
            f.chompi = m_->ArpOn() ? Rgb{0.f, .5f, .35f} : Rgb{0.f, .15f, .5f}; // teal = arp on
    }

    static constexpr Rgb kRed             = {1.f, 0.f, 0.f};
    static constexpr Rgb kTransposeColour = {1.f, .85f, 0.f};
    static constexpr Rgb kArpColour       = {0.f, 1.f, .7f};
    // Knob colours, page 1 and page 2.
    // Knob colours, by page.
    static constexpr Rgb kKnobColour[6][kMaxKnobPages] = {
        {{1.f, .55f, 0.f}, {1.f, 1.f, 1.f}, {}, {}},               // wave (amber) / length (white)
        {{0.f, 1.f, .4f}, {1.f, .3f, 0.f}, {}, {}},                // env mod (green) / accent (orange)
        {{1.f, .85f, 0.f}, {0.f, .5f, 1.f}, {0.f, .7f, .5f}, {}},  // tempo (yellow) / quantize (blue) / arp (teal)
        {{0.f, .9f, 1.f}, {1.f, .6f, .1f}, {1.f, 0.f, .6f}, {.3f, 1.f, 0.f}}, // delay (cyan) / tape (amber) / mod (pink) / crush (green)
        {{.7f, .2f, 1.f}, {}, {}, {}},                             // cutoff (purple)
        {{1.f, 1.f, 1.f}, {}, {}, {}},                             // volume (white); drive orange to red
    };
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
            if(k == kKeyTranspose)
                transpose_mode_ = false;
            else
                m_->SetTranspose(k - kMiddleC);
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
            case 5: transpose_mode_ = true; break;
            case 6: half_ = SecondHalf() ? 0 : 1; break;
            case 7: TogglePage(Page::PATTERN); break;
            case 9: clear_down_ = now, clear_done_ = false; break;
            default: break; // 8 = COPY, used while held
        }
    }

    void TogglePage(Page p) { page_ = page_ == p ? Page::NOTES : p; }

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

    void PitchKey(int k)
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
                    if(StepInput())
                        StepInputCommand(BlackIndex(k));
                    else if(m_->Recording() && m_->Running())
                        LiveFlag(BlackIndex(k));
                    break;
                case 5: transpose_mode_ = !transpose_mode_; break;
                case 6: half_ = SecondHalf() ? 0 : 1; break;
                case 7: m_->SetArpOn(!m_->ArpOn()); break;
                case 8: m_->SetArpLatch(!m_->ArpLatch()); break;
                case 9:
                    if(StepInput())
                        Append(Step{}); // a rest
                    break;
                default: break; // white keys: nothing
            }
            return;
        }
        if(transpose_mode_)
        {
            m_->SetTranspose(k - kMiddleC);
            return;
        }
        Sound(k, true);
        if(StepInput())
        {
            Step s;
            s.note   = static_cast<uint8_t>(k);
            s.octave = static_cast<int8_t>(kbd_octave_);
            s.on     = true;
            Append(s);
        }
    }

    /** Step input with CHOMPI held: F#3 accent and G#3 slide on the last
     *  step, A#3 a tie step. */
    void StepInputCommand(int b)
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

    /** Step input: writes the next step, and the pattern grows to it. */
    void Append(const Step& s)
    {
        if(cursor_ >= kSteps)
            return;
        m_->Current().steps[cursor_] = s;
        cursor_++;
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
        f.key[kKeyTranspose] = Scale(kTransposeColour, 0.1f);
        f.key[kKeyView]      = SecondHalf() ? Rgb{.5f, .5f, .5f} : Rgb{.08f, .08f, .08f};
        f.key[kKeyPattern]   = Scale(kPageColour[static_cast<int>(Page::PATTERN)], page_ == Page::PATTERN ? 1.f : 0.1f);
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

    /** Pitch mode: while playing or recording the white keys show the
     *  pattern's steps, the playhead flashes white, and in step input the
     *  last step entered is whitened and the next one blinks. Keys you hold
     *  are blue. With CHOMPI held the black keys show what they do now. */
    void DrawPitchMode(LedFrame& f, bool blink, int cur_step, bool step_lit) const
    {
        const Pattern& pat   = m_->Current();
        const bool     input = StepInput();
        if(m_->Running() || m_->Recording())
            ForEachShownStep(
                [&](int step) {
                    Rgb c = NoteColour(pat.steps[step], step >= pat.length);
                    if(input && step == cursor_ - 1 && pat.steps[step].on)
                        c = Whiten(c, 0.25f);
                    if(input && step == cursor_ && blink)
                        c = {.4f, .4f, .4f};
                    if(step_lit && cur_step == step)
                        c = {1.f, 1.f, 1.f};
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
            f.key[kKeyTranspose] = Scale(kTransposeColour, transpose_mode_ ? 0.12f : 0.5f);
            f.key[kKeyView]      = {.5f, .5f, .5f};
            f.key[kBlack[7]]     = Scale(kArpColour, m_->ArpOn() ? 1.f : 0.1f);
            f.key[kBlack[8]]     = Scale(kArpColour, m_->ArpLatch() ? 1.f : 0.1f);
            if(input)
                f.key[kBlack[9]] = {.3f, .3f, .3f}; // rest
        }
        for(int i = 0; i < kKeyNotes; i++)
            if(held_[i])
                f.key[i] = {0.f, .4f, 1.f};
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
        if(arp.Latch() && !chompi_)
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

    /** Transpose mode: C#4 and the key of the transpose amount in yellow,
     *  middle C (none) dim white. */
    void DrawTranspose(LedFrame& f) const
    {
        f.key[kMiddleC]      = {.15f, .15f, .15f};
        const int t          = kMiddleC + m_->Transpose();
        if(t >= 0 && t < kKeyNotes)
            f.key[t] = kTransposeColour;
        f.key[kKeyTranspose] = Scale(kTransposeColour, 0.12f); // dim: the amount is the bright key
    }

    /** Each knob in its page's colour, brightness = the value it turns (the
     *  CHOMPI layer while CHOMPI is held). */
    void DrawKnobs(LedFrame& f) const
    {
        for(int k = 0; k < 6; k++)
        {
            const int     page = KnobPage(k);
            const uint8_t sel  = kKnobMap[k][page][chompi_ ? 1 : 0];
            Rgb           c    = kKnobColour[k][page];
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
    bool     transpose_mode_ = false;
    int      knob_page_[4] = {};
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
constexpr Rgb Ui::kArpColour;
constexpr Rgb Ui::kKnobColour[6][kMaxKnobPages];
constexpr Rgb Ui::kPageColour[7];

} // namespace x0x
