/** @file ui.h
 *  @brief The front panel's behaviour, separate from the hardware: what each
 *  key, knob and button does in each mode, and what every LED shows.
 *  code/src/panel.h feeds it the CHOMPI's switches and draws its LED frame.
 *
 *  The keybed's 25 keys are numbered 0-24 in pitch order, C3 to C5.
 *
 *  Toggle switch: STEP mode or PITCH mode.
 *
 *  STEP mode
 *    white keys 1-8 (keys 0 2 4 5 7 9 11 12)   steps 1-8, or 9-16 (LOOP flips)
 *    tap a step                    select it; tap the selected step again to
 *                                  turn it on or off (its note is kept)
 *    hold CHOMPI                   the keybed is a keyboard: a key sets the
 *                                  selected step's note and turns it on
 *    black keys 1-5 (1 3 6 8 10)   parameter pages: octave DOWN, octave UP,
 *                                  ACCENT, SLIDE, TIE. On a page, the step
 *                                  keys toggle that flag. The page key again
 *                                  goes back to notes.
 *    black key 6 (13)              PATTERN page: step keys pick pattern 1-8
 *                                  (9-16 with LOOP). Running: it waits for
 *                                  the bar; the same key again switches now
 *    black key 7 (15)              LENGTH page: a step key sets the last step
 *    black key 8 (18)              saw / square
 *    black key 9 (20)              COPY: hold it and press a step key to copy
 *                                  this pattern to pattern 1-8 (9-16)
 *    black key 10 (22)             CLEAR: tap clears the selected step,
 *                                  hold 1 s clears the pattern
 *    LOOP                          steps 1-8 / 9-16
 *
 *  PITCH mode
 *    keys                          play live (overlapping notes slide)
 *    LOOP                          record on/off: played notes go to the
 *                                  nearest step; held notes tie
 *    CHOMPI + key                  transpose the pattern (middle C = none)
 *
 *  Both
 *    PLAY                          run / stop
 *    knobs                         resonance, env mod, decay, accent, cutoff,
 *                                  volume; CHOMPI + turn: tuning, swing,
 *                                  drive, slide time, -, tempo
 *    knob click                    back to its default (CHOMPI: the second one)
 *    CHOMPI + LOOP                 tap tempo
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
        LENGTH,
    };

    // Keybed roles in step mode (key numbers 0-24, pitch order).
    static constexpr int kStepKeys[8] = {0, 2, 4, 5, 7, 9, 11, 12};
    static constexpr int kKeyDown     = 1;
    static constexpr int kKeyUp       = 3;
    static constexpr int kKeyAccent   = 6;
    static constexpr int kKeySlide    = 8;
    static constexpr int kKeyTie      = 10;
    static constexpr int kKeyPattern  = 13;
    static constexpr int kKeyLength   = 15;
    static constexpr int kKeyWave     = 18;
    static constexpr int kKeyCopy     = 20;
    static constexpr int kKeyClear    = 22;
    static constexpr int kMiddleC     = 12;

    static constexpr uint32_t kClearHoldMs = 1000;

    void Init(Machine* m) { m_ = m; }

    Mode     GetMode() const { return mode_; }
    Page     GetPage() const { return page_; }
    int      Selected() const { return selected_; }
    int      Half() const { return half_; }
    bool     ChompiHeld() const { return chompi_; }

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
        // Notes started under one layer end when it changes.
        ReleaseAll();
        chompi_ = down;
    }

    void KeyDown(int k, uint32_t now)
    {
        if(k < 0 || k >= kKeyNotes)
            return;
        held_[k] = true;
        if(mode_ == Mode::PITCH)
        {
            if(chompi_)
                m_->SetTranspose(k - kMiddleC);
            else
                Sound(k, true);
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
        const int step = StepOfKey(k);
        if(step >= 0)
        {
            StepKey(step);
            return;
        }
        switch(k)
        {
            case kKeyDown: TogglePage(Page::DOWN); break;
            case kKeyUp: TogglePage(Page::UP); break;
            case kKeyAccent: TogglePage(Page::ACCENT); break;
            case kKeySlide: TogglePage(Page::SLIDE); break;
            case kKeyTie: TogglePage(Page::TIE); break;
            case kKeyPattern: TogglePage(Page::PATTERN); break;
            case kKeyLength: TogglePage(Page::LENGTH); break;
            case kKeyWave: m_->SetSquare(!m_->settings.square); break;
            case kKeyClear: clear_down_ = now, clear_done_ = false; break;
            default: break;
        }
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
            // A tap clears the selected step.
            Step& s = m_->Current().steps[selected_];
            s       = Step{};
            m_->PatternEdited();
        }
    }

    void Play() { m_->TogglePlay(); }

    void Loop(uint32_t now)
    {
        if(chompi_)
        {
            m_->Tap(now);
            return;
        }
        if(mode_ == Mode::STEP)
            half_ ^= 1;
        else
            m_->SetRecording(!m_->Recording());
    }

    /** knob 0-5: knobs 1-4, big, volume. */
    void KnobTurn(int knob, int inc, bool fast)
    {
        const Param p = chompi_ ? kKnobAltParam[knob] : kKnobParam[knob];
        if(p == NUM_PARAMS)
            return;
        // Tempo moves 1 BPM a click; the rest a fine step, faster when spun.
        const float step = p == TEMPO ? 1.f / 140.f : (fast ? 0.024f : 0.008f);
        m_->SetParam(p, m_->settings.params[p] + inc * step);
    }

    void KnobClick(int knob)
    {
        const Param p = chompi_ ? kKnobAltParam[knob] : kKnobParam[knob];
        if(p != NUM_PARAMS)
            m_->SetParam(p, kParams[p].def);
    }

    /** Once per block: timed actions (holding CLEAR). */
    void Tick(uint32_t now)
    {
        if(mode_ == Mode::STEP && !chompi_ && held_[kKeyClear] && !clear_done_
           && now - clear_down_ >= kClearHoldMs)
        {
            m_->Current().Clear();
            m_->PatternEdited();
            clear_done_  = true;
            cleared_at_  = now;
        }
    }

    // ------------------------------------------------------------ LEDs

    void Draw(LedFrame& f, uint32_t now) const
    {
        f = LedFrame{};
        const bool blink     = (now / 150) % 2 == 0;
        const int  cur_step  = m_->CurrentStep();
        const bool step_lit  = cur_step >= 0 && now - step_seen_at_ < 70;
        const int  sounding  = m_->SoundingNote();

        if(mode_ == Mode::STEP && !chompi_)
            DrawStepMode(f, now, blink, cur_step, step_lit);
        else if(mode_ == Mode::STEP)
            DrawStepKeyboard(f, blink);
        else
            DrawPitchMode(f, sounding);

        // Clear held: the keybed fills red towards the moment it clears.
        if(mode_ == Mode::STEP && now - cleared_at_ < 300)
            for(int k = 0; k < kKeyNotes; k++)
                f.key[k] = {1.f, 0.f, 0.f};

        // Knobs: their colour, brightness = value (the second layer with CHOMPI).
        for(int k = 0; k < 6; k++)
        {
            const Param p = chompi_ ? kKnobAltParam[k] : kKnobParam[k];
            if(p == NUM_PARAMS)
                continue;
            f.knob[k] = Scale(kKnobColour[k], 0.06f + 0.94f * m_->settings.params[p]);
        }

        // PLAY: a pulse each step, brighter on the beat.
        if(m_->Running())
        {
            const float b = step_lit ? (cur_step % 4 == 0 ? 1.f : 0.45f) : 0.08f;
            f.play        = {b, b * .85f, 0.f};
        }
        // LOOP: which half in step mode; record in pitch mode.
        if(mode_ == Mode::STEP)
            f.loop = half_ ? Rgb{0.f, 1.f, .3f} : Rgb{0.f, .12f, .04f};
        else if(m_->Recording())
            f.loop = blink && m_->Running() ? Rgb{1.f, 0.f, 0.f} : Rgb{.6f, 0.f, 0.f};
        // CHOMPI: white while held, else the mode's colour.
        if(chompi_)
            f.chompi = {1.f, 1.f, 1.f};
        else
            f.chompi = mode_ == Mode::STEP ? Rgb{.5f, 0.f, 0.f} : Rgb{0.f, .15f, .5f};
    }

    /** Called by the panel once per draw so the step pulse is timed from
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

    static constexpr Rgb kRed       = {1.f, 0.f, 0.f};
    static constexpr Rgb kKnobColour[6] = {
        {1.f, .8f, 0.f},  // resonance  yellow
        {0.f, 1.f, .4f},  // env mod    green
        {0.f, .5f, 1.f},  // decay      blue
        {1.f, .3f, 0.f},  // accent     orange
        {.7f, .2f, 1.f},  // cutoff     purple
        {1.f, 1.f, 1.f},  // volume     white
    };
    static constexpr Rgb kPageColour[8] = {
        {1.f, 0.f, 0.f},   // notes      red
        {.6f, 0.f, 1.f},   // DOWN       purple
        {0.f, .9f, 1.f},   // UP         cyan
        {1.f, .35f, 0.f},  // ACCENT     orange
        {0.f, .3f, 1.f},   // SLIDE      blue
        {0.f, 1.f, .2f},   // TIE        green
        {1.f, .85f, 0.f},  // PATTERN    yellow
        {1.f, 1.f, 1.f},   // LENGTH     white
    };

  private:
    static int StepOfKey(int k)
    {
        for(int i = 0; i < 8; i++)
            if(kStepKeys[i] == k)
                return i;
        return -1;
    }

    void TogglePage(Page p) { page_ = page_ == p ? Page::NOTES : p; }

    void StepKey(int i)
    {
        const int step = half_ * 8 + i;
        Pattern&  pat  = m_->Current();
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
                    s.on = !s.on;
                else
                    selected_ = step;
                break;
            case Page::DOWN: s.octave = s.octave == -1 ? 0 : -1; break;
            case Page::UP: s.octave = s.octave == 1 ? 0 : 1; break;
            case Page::ACCENT: s.accent = !s.accent; break;
            case Page::SLIDE: s.slide = !s.slide; break;
            case Page::TIE: s.tie = !s.tie; break;
            case Page::PATTERN:
                // Running: the first press queues it, a second switches now.
                m_->SelectPattern(step, m_->QueuedPattern() == step);
                return;
            case Page::LENGTH: pat.length = static_cast<uint8_t>(step + 1); break;
        }
        if(page_ != Page::NOTES || step == selected_)
            m_->PatternEdited();
    }

    /** Plays (or stops) key k's note live. */
    void Sound(int k, bool on)
    {
        const int note = kBaseNote + k;
        if(on)
        {
            m_->LiveNoteOn(note);
            sounding_[k] = true;
        }
        else
        {
            m_->LiveNoteOff(note);
            sounding_[k] = false;
        }
    }

    void ReleaseAll()
    {
        for(int k = 0; k < kKeyNotes; k++)
            if(sounding_[k])
                Sound(k, false);
    }

    void DrawStepMode(LedFrame& f, uint32_t now, bool blink, int cur_step, bool step_lit) const
    {
        const Pattern& pat = m_->Current();
        const Rgb      pc  = kPageColour[static_cast<int>(page_)];

        for(int i = 0; i < 8; i++)
        {
            const int   step = half_ * 8 + i;
            const Step& s    = pat.steps[step];
            Rgb         c;
            switch(page_)
            {
                case Page::NOTES:
                    if(s.on)
                        c = Scale(kRed, step == selected_ ? 1.f : 0.45f);
                    else if(step == selected_)
                        c = {.15f, .15f, .15f};
                    break;
                case Page::PATTERN:
                {
                    const bool cur    = step == m_->CurrentPattern();
                    const bool queued = step == m_->QueuedPattern();
                    if(cur)
                        c = pc;
                    else if(queued)
                        c = blink ? pc : Rgb{};
                    else if(!m_->patterns[step].Empty())
                        c = Scale(pc, 0.12f);
                    break;
                }
                case Page::LENGTH:
                    if(step == pat.length - 1)
                        c = pc;
                    else if(step < pat.length)
                        c = Scale(pc, 0.1f);
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
                        c = pc;
                    else if(s.on)
                        c = Scale(kRed, 0.12f);
                    break;
                }
            }
            // The playhead passes over as a white flash.
            if(step_lit && cur_step == step)
                c = {1.f, 1.f, 1.f};
            f.key[kStepKeys[i]] = c;
        }

        // Page keys: dim in their colour, the open page bright.
        const int  page_keys[7]  = {kKeyDown, kKeyUp, kKeyAccent, kKeySlide, kKeyTie, kKeyPattern, kKeyLength};
        const Page pages[7]      = {Page::DOWN, Page::UP, Page::ACCENT, Page::SLIDE, Page::TIE, Page::PATTERN, Page::LENGTH};
        for(int i = 0; i < 7; i++)
            f.key[page_keys[i]] = Scale(kPageColour[static_cast<int>(pages[i])], page_ == pages[i] ? 1.f : 0.1f);
        f.key[kKeyWave]  = m_->settings.square ? Rgb{0.f, .8f, 1.f} : Rgb{1.f, .55f, 0.f};
        f.key[kKeyCopy]  = held_[kKeyCopy] ? Rgb{1.f, 1.f, 1.f} : Rgb{.08f, .08f, .08f};
        if(held_[kKeyClear] && !clear_done_)
        {
            const float b = 0.2f + 0.8f * Clamp((now - clear_down_) / static_cast<float>(kClearHoldMs), 0.f, 1.f);
            f.key[kKeyClear] = {b, 0.f, 0.f};
        }
        else
            f.key[kKeyClear] = {.12f, 0.f, 0.f};
    }

    /** Step mode with CHOMPI held: the keybed is a keyboard. The selected
     *  step's key flashes; its note's key is lit blue. */
    void DrawStepKeyboard(LedFrame& f, bool blink) const
    {
        const Step& s = m_->Current().steps[selected_];
        if(s.on && !s.tie)
            f.key[s.note] = {0.f, .4f, 1.f};
        if(selected_ / 8 == half_)
            f.key[kStepKeys[selected_ % 8]] = blink ? kRed : Rgb{};
        for(int k = 0; k < kKeyNotes; k++)
            if(held_[k])
                f.key[k] = {1.f, 1.f, 1.f};
    }

    void DrawPitchMode(LedFrame& f, int sounding) const
    {
        // The note playing, from the pattern or the keys.
        const int k = sounding - kBaseNote;
        if(k >= 0 && k < kKeyNotes)
            f.key[k] = {.25f, .25f, .25f};
        for(int i = 0; i < kKeyNotes; i++)
            if(held_[i])
                f.key[i] = {0.f, .4f, 1.f};
        // CHOMPI held: the transpose key (middle C = none).
        if(chompi_)
        {
            const int t = kMiddleC + m_->Transpose();
            if(t >= 0 && t < kKeyNotes)
                f.key[t] = {1.f, .85f, 0.f};
        }
    }

    Machine* m_        = nullptr;
    Mode     mode_     = Mode::STEP;
    Page     page_     = Page::NOTES;
    int      selected_ = 0;
    int      half_     = 0;
    bool     chompi_   = false;
    bool     held_[kKeyNotes]     = {};
    bool     sounding_[kKeyNotes] = {};
    uint32_t clear_down_  = 0;
    bool     clear_done_  = false;
    uint32_t cleared_at_  = 0x80000000u;
    uint32_t last_step_count_ = 0;
    uint32_t step_seen_at_    = 0;
};

// Out-of-class definitions for the arrays, which C++14 (the firmware's
// standard) needs when they are indexed.
constexpr int Ui::kStepKeys[8];
constexpr Rgb Ui::kRed;
constexpr Rgb Ui::kKnobColour[6];
constexpr Rgb Ui::kPageColour[8];

} // namespace x0x
