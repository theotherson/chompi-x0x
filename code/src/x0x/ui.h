/** @file ui.h
 *  @brief The front panel's behaviour, separate from the hardware: what each
 *  key, knob and button does in each mode, and what every LED shows.
 *  code/src/panel.h feeds it the CHOMPI's switches and draws its LED frame.
 *
 *  The keybed's 25 keys are numbered 0-24 in pitch order, C3 to C5.
 *
 *  Steps: the 15 white keys show the 16 steps. White keys 1-7 are steps 1-7,
 *  white keys 9-15 are steps 10-16, and middle C (white key 8) is step 8 or
 *  step 9: it follows the half you last pressed a key in, and while the
 *  pattern runs it follows the playhead once the second half has notes.
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
 *    black key 6                   PATTERN page: step keys pick pattern 1-16.
 *                                  Running, it waits for the bar; the same key
 *                                  again switches at once
 *    black key 9                   COPY: hold it and press a step key to copy
 *                                  this pattern to that pattern number
 *    black key 10                  CLEAR: tap clears the selected step, hold
 *                                  1 s clears the pattern
 *    LOOP                          middle C: step 8 / step 9
 *
 *  PITCH mode
 *    keys                          play live; overlapping notes slide
 *    LOOP                          record on/off
 *      running, record on          played notes go into the pattern
 *                                  (quantized or not: knob 3, page 2)
 *      stopped, record on          step input: each note fills the next step
 *                                  and the pattern grows to it. Hold CHOMPI:
 *                                  black keys 1-4 = octave down, octave up,
 *                                  accent, slide on the last step; black key 5
 *                                  adds a tie, black key 6 a rest, black key 7
 *                                  deletes the last step
 *    CHOMPI + key                  transpose (middle C = none), except in step
 *                                  input
 *
 *  Knobs: clicking knobs 1-4 flips each between two pages. The big knob's
 *  click is tap tempo. See params.h kKnobMap for what each turns.
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
    static constexpr int kKeyCopy   = 20; // black key 9
    static constexpr int kKeyClear  = 22; // black key 10

    static constexpr uint32_t kClearHoldMs = 1000;

    void Init(Machine* m) { m_ = m; }

    Mode GetMode() const { return mode_; }
    Page GetPage() const { return page_; }
    int  Selected() const { return selected_; }
    int  KnobPage(int knob) const { return knob < 4 ? knob_page_[knob] : 0; }
    bool StepInput() const { return mode_ == Mode::PITCH && m_->Recording() && !m_->Running(); }
    int  Cursor() const { return cursor_; }

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
        mode_ = mode;
        page_ = Page::NOTES;
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

    void Loop()
    {
        if(mode_ == Mode::STEP)
        {
            half_ ^= 1;
            return;
        }
        const bool arm = !m_->Recording();
        m_->SetRecording(arm);
        if(arm && !m_->Running())
            cursor_ = 0; // step input starts at step 1
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
        if(knob < 4)
            knob_page_[knob] ^= 1;
        else if(knob == 4)
            m_->Tap(now);
        else
        {
            ReleaseAll();
            m_->AllLiveOff();
        }
    }

    /** Once per block: timed actions (holding CLEAR). */
    void Tick(uint32_t now)
    {
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

        if(mode_ == Mode::STEP && now - cleared_at_ < 300)
            for(int k = 0; k < kKeyNotes; k++)
                f.key[k] = {1.f, 0.f, 0.f};

        DrawKnobs(f);

        if(m_->Running())
        {
            const float b = step_lit ? (cur_step % 4 == 0 ? 1.f : 0.45f) : 0.08f;
            f.play        = {b, b * .85f, 0.f};
        }
        if(mode_ == Mode::STEP)
            f.loop = half_ ? Rgb{0.f, 1.f, .3f} : Rgb{0.f, .12f, .04f};
        else if(m_->Recording())
            f.loop = m_->Running() && blink ? Rgb{1.f, 0.f, 0.f} : Rgb{.7f, 0.f, 0.f};
        if(chompi_)
            f.chompi = {1.f, 1.f, 1.f};
        else if(StepInput())
            f.chompi = {.6f, 0.f, .5f};
        else
            f.chompi = mode_ == Mode::STEP ? Rgb{.5f, 0.f, 0.f} : Rgb{0.f, .15f, .5f};
    }

    static constexpr Rgb kRed = {1.f, 0.f, 0.f};
    // Knob colours, page 1 and page 2.
    static constexpr Rgb kKnobColour[6][2] = {
        {{1.f, .55f, 0.f}, {1.f, 1.f, 1.f}},  // wave (amber) / length (white)
        {{0.f, 1.f, .4f}, {1.f, .3f, 0.f}},   // env mod (green) / accent (orange)
        {{1.f, .85f, 0.f}, {0.f, .5f, 1.f}},  // tempo (yellow) / quantize (blue)
        {{0.f, .9f, 1.f}, {1.f, 0.f, .6f}},   // delay (cyan) / crush + mod (pink)
        {{.7f, .2f, 1.f}, {.7f, .2f, 1.f}},   // cutoff (purple)
        {{1.f, 1.f, 1.f}, {1.f, 1.f, 1.f}},   // volume (white)
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
            case 5: TogglePage(Page::PATTERN); break;
            case 9: clear_down_ = now, clear_done_ = false; break;
            default: break; // 6, 7 free; 8 = COPY, used while held
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
            if(StepInput())
                StepInputCommand(BlackIndex(k));
            else
                m_->SetTranspose(k - kMiddleC);
            return;
        }
        Sound(k, true);
        if(StepInput())
        {
            Step s;
            s.note = static_cast<uint8_t>(k);
            s.on   = true;
            Append(s);
        }
    }

    /** Step input with CHOMPI held: the black keys. */
    void StepInputCommand(int b)
    {
        Pattern&  pat  = m_->Current();
        const int last = cursor_ - 1;
        switch(b)
        {
            case 0:
            case 1:
            case 2:
            case 3:
            {
                if(last < 0)
                    return;
                Step& s = pat.steps[last];
                if(b == 0)
                    s.octave = s.octave == -1 ? 0 : -1;
                else if(b == 1)
                    s.octave = s.octave == 1 ? 0 : 1;
                else if(b == 2)
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
            case 5: Append(Step{}); return; // rest
            case 6: // delete the last step
                if(cursor_ > 0)
                {
                    cursor_--;
                    pat.steps[cursor_] = Step{};
                    m_->SetLength(cursor_ > 0 ? cursor_ : 1);
                }
                return;
            default: return;
        }
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

    /** Plays (or stops) key k's note live. */
    void Sound(int k, bool on)
    {
        const int note = kBaseNote + k;
        if(on)
            m_->LiveNoteOn(note);
        else
            m_->LiveNoteOff(note);
        sounding_[k] = on;
    }

    void ReleaseAll()
    {
        for(int k = 0; k < kKeyNotes; k++)
            if(sounding_[k])
                Sound(k, false);
    }

    // ------------------------------------------------------------ drawing

    /** The 16 steps on the 15 white keys, via colour(step). */
    template <typename F>
    void ForEachShownStep(F colour, LedFrame& f) const
    {
        for(int s = 0; s < kSteps; s++)
        {
            const int k = WhiteOfStep(s);
            if(k >= 0)
                f.key[k] = colour(s);
        }
    }

    void DrawStepMode(LedFrame& f, uint32_t now, bool blink, int cur_step, bool step_lit) const
    {
        const Pattern& pat = m_->Current();
        const Rgb      pc  = kPageColour[static_cast<int>(page_)];

        ForEachShownStep(
            [&](int step) {
                const Step& s = pat.steps[step];
                Rgb         c;
                const float past_end = step < pat.length ? 1.f : 0.25f;
                switch(page_)
                {
                    case Page::NOTES:
                        if(s.on)
                            c = Scale(kRed, (step == selected_ ? 1.f : 0.45f) * past_end);
                        else if(step == selected_)
                            c = {.15f, .15f, .15f};
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
                            c = Scale(pc, past_end);
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

        const Page pages[6] = {Page::DOWN, Page::UP, Page::ACCENT, Page::SLIDE, Page::TIE, Page::PATTERN};
        for(int i = 0; i < 6; i++)
            f.key[kBlack[i]] = Scale(kPageColour[static_cast<int>(pages[i])], page_ == pages[i] ? 1.f : 0.1f);
        f.key[kKeyCopy] = held_[kKeyCopy] ? Rgb{1.f, 1.f, 1.f} : Rgb{.08f, .08f, .08f};
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

    /** Pitch mode: the white keys show the pattern's steps (dim red, ties
     *  green), the playhead flashes white, in step input the next step to
     *  fill blinks; keys you hold are blue. */
    void DrawPitchMode(LedFrame& f, bool blink, int cur_step, bool step_lit) const
    {
        const Pattern& pat   = m_->Current();
        const bool     input = StepInput();
        ForEachShownStep(
            [&](int step) {
                const Step& s = pat.steps[step];
                Rgb         c;
                if(step < pat.length && s.on)
                    c = s.tie ? Rgb{0.f, .3f, .06f} : Scale(kRed, input && step == cursor_ - 1 ? 1.f : 0.3f);
                if(input && step == cursor_ && blink)
                    c = {.4f, .4f, .4f};
                if(step_lit && cur_step == step)
                    c = {1.f, 1.f, 1.f};
                return c;
            },
            f);
        if(input && chompi_)
        {
            // The step-input commands on the black keys.
            const Rgb cmd[7] = {kPageColour[1], kPageColour[2], kPageColour[3], kPageColour[4],
                                kPageColour[5], {.3f, .3f, .3f}, {.5f, 0.f, 0.f}};
            for(int i = 0; i < 7; i++)
                f.key[kBlack[i]] = cmd[i];
        }
        else if(chompi_)
        {
            const int t = kMiddleC + m_->Transpose();
            if(t >= 0 && t < kKeyNotes)
                f.key[t] = {1.f, .85f, 0.f};
        }
        for(int i = 0; i < kKeyNotes; i++)
            if(held_[i])
                f.key[i] = {0.f, .4f, 1.f};
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
            else if(sel != kKnobNone)
                v = m_->settings.params[sel];
            f.knob[k] = Scale(c, 0.06f + 0.94f * v);
        }
    }

    Machine* m_       = nullptr;
    Mode     mode_    = Mode::STEP;
    Page     page_    = Page::NOTES;
    int      selected_ = 0;
    int      half_     = 0;
    int      cursor_   = 0;
    bool     chompi_   = false;
    int      knob_page_[4] = {};
    bool     held_[kKeyNotes]     = {};
    bool     sounding_[kKeyNotes] = {};
    uint32_t clear_down_      = 0;
    bool     clear_done_      = true;
    uint32_t cleared_at_      = 0x80000000u;
    uint32_t last_step_count_ = 0;
    uint32_t step_seen_at_    = 0;
};

// Out-of-class definitions for the arrays, which C++14 (the firmware's
// standard) needs when they are indexed.
constexpr int Ui::kWhite[15];
constexpr int Ui::kBlack[10];
constexpr Rgb Ui::kRed;
constexpr Rgb Ui::kKnobColour[6][2];
constexpr Rgb Ui::kPageColour[7];

} // namespace x0x
