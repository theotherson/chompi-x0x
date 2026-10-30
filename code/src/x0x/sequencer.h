/** @file sequencer.h
 *  @brief Plays a pattern as the TB-303 does: each step that is on plays its
 *  note, a tie holds the note before, slide holds the gate into the next note,
 *  which glides; the gate is otherwise half a step long.
 *
 *  Time is counted in MIDI clock ticks, 24 per beat, 6 per step (4 or 8 in
 *  the pattern's triplet scales). On the
 *  internal clock the ticks come from the sample count, so every event lands
 *  on its exact sample within the audio block. On an external MIDI clock each
 *  incoming clock advances one tick. Swing delays every second step by up to
 *  2 ticks (a triplet feel at full swing); triplet scales aren't swung.
 *
 *  A ratcheted step plays its note 2-4 times, evenly through the step, each
 *  with a gate half its share; a slide or tie out of it holds the last.
 *
 *  Events come out in time order with their sample offset in the block. The
 *  caller (Machine) turns them into voice notes and MIDI.
 *
 *  GRID marks each step's place on the grid (swing included, but not the
 *  bass note's own timing). The drums have a grid of their own, with its
 *  own step length (SetDrumTicks: the drum part's scale), from the same
 *  start: DRUM_GRID, which they step on. Drum hits played late (or early,
 *  from the step before) are scheduled from the last DRUM_GRID
 *  (ScheduleDrum) and come back as DRUM events.
 */
#pragma once
#include "pattern.h"

namespace x0x
{

class Sequencer
{
  public:
    static constexpr int kMaxEvents    = 32;

    struct Event
    {
        enum Type : uint8_t
        {
            NOTE_ON,
            NOTE_OFF,
            STEP,           // a step started (step = its index)
            TICK,           // an internal clock tick, for MIDI clock out
            PATTERN_CHANGE, // the queued pattern took over
            GRID,           // a step's place on the grid (step = its index)
            DRUM,           // a drum hit scheduled late (step = its payload)
            ROLL,           // a drum roll's next hit (SetRoll)
            DRUM_GRID,      // the drums' next step's place on their own grid
        };
        Type     type;
        uint32_t offset; // sample in the block
        int      note   = 0;
        bool     accent = false;
        bool     slide  = false; // NOTE_ON: arrive legato from the sounding note
        int      step   = 0;
    };

    void Init(float sample_rate) { sr_ = sample_rate; }

    void SetTempo(float bpm) { bpm_ = Clamp(bpm, 20.f, 300.f); }
    float Tempo() const { return bpm_; }
    /** 0..1 */
    void SetSwing(float s) { swing_ticks_ = 2.f * Clamp(s, 0.f, 1.f); }
    void SetTranspose(int st) { transpose_ = ClampInt(st, -24, 24); }
    /** Play recorded notes on a grid of this many steps, 0 = as recorded. */
    void SetQuantize(int grid_steps) { quant_grid_ = grid_steps; }
    int  Transpose() const { return transpose_; }

    /** The pattern to play now (also when stopped). */
    void SetPattern(const Pattern* p) { pat_ = p; queued_ = nullptr; }
    /** The pattern to switch to when the current one ends; nullptr cancels. */
    void Queue(const Pattern* p) { queued_ = p; }
    const Pattern* Queued() const { return queued_; }

    bool Running() const { return running_; }
    int  CurrentStep() const { return running_ ? step_ : -1; }
    int  NextStep() const { return next_step_; }
    /** MIDI clock ticks a step, by the pattern's scale. A new scale (or a
     *  pattern with another) takes over at the top of the pattern. */
    int StepTicks() const { return ticks_; }

    /** The drums' grid: ticks a step (by the drum part's scale). Called at
     *  the drum part's first step (its DRUM_GRID), it takes over from there,
     *  and the swing counts from there. Before Start: from the start. */
    void SetDrumTicks(int ticks)
    {
        ticks = ticks > 0 ? ticks : kStepTicks;
        if(running_)
            dgrid_base_ = dgrid_last_unswung_, dgrid_n_ = 1;
        dticks_    = ticks;
        dgrid_pos_ = running_ ? DrumGridAt(dgrid_n_) : 0.0;
    }
    int DrumTicks() const { return dticks_; }

    /** The drums' grid from the last GRID (a pattern played on both sides
     *  starting together): its next step now, unless one just came with it. */
    void SyncDrumGrid()
    {
        if(!running_ || fabs(dgrid_last_ - last_grid_pos_) < 1e-6)
            return;
        dgrid_base_ = dgrid_last_unswung_ = last_grid_pos_;
        dgrid_n_    = 0;
        dgrid_pos_  = last_grid_pos_;
    }

    /** Ticks from the last DRUM_GRID to the next (for drum hits early to it). */
    double GridSpacing() const { return dgrid_pos_ - dgrid_last_; }

    /** How far from the last DRUM_GRID to the next, 0..1 (recording drums). */
    float GridPhase() const
    {
        if(!running_)
            return 0.f;
        const double len = dgrid_pos_ - dgrid_last_;
        return len > 0.0 ? Clamp(static_cast<float>((pos_ - dgrid_last_) / len), 0.f, 1.f) : 0.f;
    }

    /** A DRUM event `ticks` after the last DRUM_GRID, carrying `payload`. */
    void ScheduleDrum(double ticks, int payload)
    {
        if(!running_ || drum_count_ >= kMaxDrums)
            return;
        drum_at_[drum_count_]  = dgrid_last_ + ticks;
        drum_pay_[drum_count_] = payload;
        drum_count_++;
    }

    /** Drum rolls: a ROLL event every `ticks`, on that grid from the top of
     *  the pattern; 0 stops them. */
    void SetRoll(int ticks)
    {
        if(ticks == roll_ticks_)
            return;
        roll_ticks_ = ticks;
        if(ticks > 0)
            roll_next_ = (static_cast<int>(pos_ / ticks + 1e-6) + 1) * static_cast<double>(ticks);
    }

    /** How far through the current step on the grid, 0..1 (for recording). */
    float StepPhase() const
    {
        if(!running_ || step_ < 0)
            return 0.f;
        const double start = GridStart(step_);
        const double len   = NextGridStart() - start;
        return len > 0.0 ? Clamp(static_cast<float>((pos_ - start) / len), 0.f, 1.f) : 0.f;
    }

    /** From the top of the pattern. */
    void Start()
    {
        running_   = true;
        pos_       = 0.0;
        step_      = -1;
        next_step_ = 0;
        next_tick_ = 0.0;
        gate_off_  = -1.0;
        hold_      = false;
        grid_step_ = 0, grid_pos_ = 0.0, grid_base_ = 0.0, last_grid_pos_ = 0.0;
        drum_count_ = 0;
        roll_next_  = 0.0;
        rat_left_   = 0;
        dgrid_base_ = dgrid_last_ = dgrid_last_unswung_ = 0.0, dgrid_n_ = 0, dgrid_pos_ = 0.0;
        ticks_      = pat_ ? pat_->StepTicks() : kStepTicks;
        ScheduleStep();
        if(step_pos_ < 0.0)
            step_pos_ = 0.0; // an early first step: at PLAY
    }

    /** Carry on from where it stopped (MIDI Continue). */
    void Continue()
    {
        if(pat_)
            running_ = true;
    }

    /** Stop; the sounding note is released at the next Process(). */
    void Stop() { running_ = false; }

    /** Internal clock: advance by n samples. @return events written */
    int Process(size_t n, Event* ev)
    {
        int count = 0;
        if(!running_ || !pat_)
        {
            ReleaseIfPlaying(0, ev, count);
            return count;
        }
        const double spt  = sr_ * 60.0 / (bpm_ * 24.0); // samples per tick
        double       left = static_cast<double>(n);
        double       done = 0.0;
        while(true)
        {
            const double next  = NextEventPos();
            const double until = (next - pos_) * spt;
            // An event a hair before the block's end (rounding) belongs to
            // the next block's first sample.
            if(until > left - 1e-6)
            {
                pos_ += left / spt;
                break;
            }
            if(next > pos_)
                pos_ = next; // (an early step can fall due before now: never back)
            done += until > 0.0 ? until : 0.0;
            left -= until > 0.0 ? until : 0.0;
            const double   r   = done + 0.5;
            const uint32_t off = r < n ? static_cast<uint32_t>(r) : static_cast<uint32_t>(n - 1);
            Fire(off, true, ev, count);
            if(count > kMaxEvents - 4)
                break;
        }
        return count;
    }

    /** External clock: one MIDI clock pulse (the first after Start is tick
     *  0). Events get `offset`. */
    int ExternalTick(uint32_t offset, Event* ev)
    {
        int count = 0;
        if(!running_ || !pat_)
        {
            ReleaseIfPlaying(offset, ev, count);
            return count;
        }
        // A clock pulse plays what is due at its tick, then time moves on.
        while(NextEventPos() <= pos_ + 1e-9 && count < kMaxEvents - 4)
            Fire(offset, false, ev, count);
        pos_ += 1.0;
        return count;
    }

  private:
    double NextEventPos() const
    {
        double next = next_tick_;
        if(step_pos_ < next)
            next = step_pos_;
        if(grid_pos_ < next)
            next = grid_pos_;
        if(dgrid_pos_ < next)
            next = dgrid_pos_;
        for(int i = 0; i < drum_count_; i++)
            if(drum_at_[i] < next)
                next = drum_at_[i];
        if(roll_ticks_ > 0 && roll_next_ < next)
            next = roll_next_;
        if(rat_left_ > 0 && rat_pos_ < next)
            next = rat_pos_;
        if(gate_off_ >= 0.0 && gate_off_ < next)
            next = gate_off_;
        return next;
    }

    /** Handles everything due at pos_, in order: gate off, then the step,
     *  then the clock tick. */
    void Fire(uint32_t off, bool internal, Event* ev, int& count)
    {
        if(gate_off_ >= 0.0 && gate_off_ <= pos_ + 1e-9)
        {
            gate_off_ = -1.0;
            ReleaseIfPlaying(off, ev, count);
        }
        if(grid_pos_ <= pos_ + 1e-9)
        {
            // The grid first: a step's note is never before its place. At
            // the top, the scale of the pattern this loop plays.
            if(grid_step_ == 0 && (queued_ || pat_))
                ticks_ = (queued_ ? queued_ : pat_)->StepTicks();
            Event e{Event::GRID, off};
            e.step         = grid_step_;
            ev[count++]    = e;
            last_grid_pos_ = grid_pos_;
            grid_step_++;
            if(grid_step_ >= pat_->length)
                grid_step_ = 0, grid_base_ += pat_->length * StepTicks(); // a wrap is never swung
            grid_pos_ = grid_base_ + GridStart(grid_step_);
        }
        if(dgrid_pos_ <= pos_ + 1e-9)
        {
            // The drums' grid, after the bass's at the same moment.
            ev[count++]         = {Event::DRUM_GRID, off};
            dgrid_last_         = dgrid_pos_;
            dgrid_last_unswung_ = dgrid_base_ + dgrid_n_ * static_cast<double>(dticks_);
            dgrid_n_++;
            dgrid_pos_ = DrumGridAt(dgrid_n_);
        }
        if(step_pos_ <= pos_ + 1e-9)
            StartStep(off, ev, count);
        if(rat_left_ > 0 && rat_pos_ <= pos_ + 1e-9 && count < kMaxEvents - 4)
            RatchetHit(off, ev, count);
        for(int i = 0; i < drum_count_ && count < kMaxEvents - 4;)
            if(drum_at_[i] <= pos_ + 1e-9)
            {
                Event e{Event::DRUM, off};
                e.step      = drum_pay_[i];
                ev[count++] = e;
                drum_count_--;
                drum_at_[i]  = drum_at_[drum_count_];
                drum_pay_[i] = drum_pay_[drum_count_];
            }
            else
                i++;
        if(roll_ticks_ > 0 && roll_next_ <= pos_ + 1e-9 && count < kMaxEvents - 4)
        {
            ev[count++] = {Event::ROLL, off};
            roll_next_ += roll_ticks_;
        }
        if(next_tick_ <= pos_ + 1e-9)
        {
            if(internal)
                ev[count++] = {Event::TICK, off};
            next_tick_ += 1.0;
        }
    }

    /** The drums' grid point n from its base, swing included (1/16 only). */
    double DrumGridAt(int n) const
    {
        const double swing = dticks_ == kScaleTicks[kScale16] && n % 2 == 1 ? swing_ticks_ : 0.0;
        return dgrid_base_ + n * static_cast<double>(dticks_) + swing;
    }

    /** Swing, in ticks: the 1/16 scale only. */
    double Swing() const { return ticks_ == kScaleTicks[kScale16] ? swing_ticks_ : 0.0; }

    /** A step's timing, within its step: late, or set by hand early too
     *  (the step's note before its place on the grid). */
    int Nudge(const Step& s) const
    {
        const int lim = StepTicks() - 1;
        return ClampInt(s.nudge, s.timed ? -lim : 0, lim);
    }

    /** Where step `step` sits on the grid, swing included. */
    double GridStart(int step) const
    {
        return step * StepTicks() + (step % 2 == 1 ? Swing() : 0.0);
    }

    /** When step `step` actually starts: its grid position plus its nudge. */
    double StepStart(int step) const
    {
        const double nudge = pat_ && step >= 0 && step < kSteps ? Nudge(At(step)) : 0;
        return GridStart(step) + nudge;
    }

    /** Where the step after the current one sits on the grid, counting on
     *  past the end when it wraps (so a wrap is never swung). */
    double NextGridStart() const
    {
        return (step_ + 1) * StepTicks() + (next_step_ % 2 == 1 ? Swing() : 0.0);
    }

    void ScheduleStep() { step_pos_ = StepStart(next_step_); }

    /** Step i of the pattern as it plays (quantized or not). */
    Step At(int i) const { return pat_->PlayedStep(i, quant_grid_); }

    void StartStep(uint32_t off, Event* ev, int& count)
    {
        // A ratchet cut short (this step early, the last one late): its
        // slide or tie still holds.
        if(rat_left_ > 0)
            hold_ = rat_hold_, rat_left_ = 0;
        // Wrap: back to the top, and a queued pattern takes over.
        if(next_step_ == 0 && step_ >= 0)
        {
            // Back by the pattern's length: a late first step stays late,
            // and the loop keeps its length.
            const double l = static_cast<double>(pat_length_ticks_);
            pos_ -= l;
            next_tick_ -= l;
            if(gate_off_ >= 0.0)
                gate_off_ -= l;
            grid_base_ -= l, grid_pos_ -= l, last_grid_pos_ -= l;
            dgrid_base_ -= l, dgrid_pos_ -= l, dgrid_last_ -= l, dgrid_last_unswung_ -= l;
            for(int i = 0; i < drum_count_; i++)
                drum_at_[i] -= l;
            roll_next_ -= l;
            if(queued_)
            {
                pat_    = queued_;
                queued_ = nullptr;
                Event e{Event::PATTERN_CHANGE, off};
                ev[count++] = e;
            }
        }
        step_             = next_step_;
        pat_length_ticks_ = pat_->length * StepTicks();
        next_step_        = step_ + 1 < pat_->length ? step_ + 1 : 0;
        // The next step's start, in this pattern's ticks (a wrap counts on).
        step_pos_ = NextGridStart() + Nudge(At(next_step_));

        {
            Event e{Event::STEP, off};
            e.step      = step_;
            ev[count++] = e;
        }

        const Step  s       = At(step_);
        const bool  held_in = hold_ && playing_ >= 0;
        hold_               = false;
        if(s.on && !s.tie)
        {
            const int note = ClampInt(s.Midi() + transpose_, 0, 127);
            if(held_in)
            {
                Event e{Event::NOTE_ON, off};
                e.note = note, e.accent = s.accent, e.slide = true;
                ev[count++] = e;
                if(note != playing_)
                {
                    Event o{Event::NOTE_OFF, off};
                    o.note      = playing_;
                    ev[count++] = o;
                }
            }
            else
            {
                ReleaseIfPlaying(off, ev, count);
                Event e{Event::NOTE_ON, off};
                e.note = note, e.accent = s.accent;
                ev[count++] = e;
            }
            playing_ = note;
        }
        else if(s.on && s.tie)
        {
            // Holds the note before; with nothing held in, it's a rest.
            if(!held_in)
                ReleaseIfPlaying(off, ev, count);
        }
        else
            ReleaseIfPlaying(off, ev, count);

        // Keep the gate open into the next step if it's tied to this one, or
        // this step slides into it.
        const Step next  = At(next_step_);
        hold_            = playing_ >= 0 && next.on && (next.tie || s.slide);
        // Half a step (of a ratchet: half its share), but never past the
        // next step's start (a late step leaves less room).
        const int    hits   = s.on && !s.tie && playing_ >= 0 ? s.Hits() : 1;
        const double share  = StepTicks() / static_cast<double>(hits);
        const double off_at = StepStart(step_) + share / 2.0;
        if(hits > 1)
        {
            // The rest of the ratchet; the hold waits for its last hit.
            rat_left_ = hits - 1, rat_share_ = share, rat_pos_ = StepStart(step_) + share;
            rat_note_ = playing_, rat_accent_ = s.accent, rat_hold_ = hold_;
            hold_     = false;
        }
        gate_off_ = hold_ || playing_ < 0 ? -1.0 : (off_at < step_pos_ ? off_at : step_pos_);
    }

    /** A ratchet's next hit: the step's note again. */
    void RatchetHit(uint32_t off, Event* ev, int& count)
    {
        const double at = rat_pos_;
        rat_left_--;
        rat_pos_ += rat_share_;
        ReleaseIfPlaying(off, ev, count);
        Event e{Event::NOTE_ON, off};
        e.note = rat_note_, e.accent = rat_accent_;
        ev[count++] = e;
        playing_    = rat_note_;
        if(rat_left_ == 0)
            hold_ = rat_hold_;
        const double off_at = at + rat_share_ / 2.0;
        gate_off_ = hold_ ? -1.0 : (off_at < step_pos_ ? off_at : step_pos_);
    }

    void ReleaseIfPlaying(uint32_t off, Event* ev, int& count)
    {
        if(playing_ < 0)
            return;
        Event e{Event::NOTE_OFF, off};
        e.note      = playing_;
        ev[count++] = e;
        playing_    = -1;
        hold_       = false;
    }

    float          sr_          = 48000.f;
    float          bpm_         = 120.f;
    double         swing_ticks_ = 0.0;
    int            transpose_   = 0;
    int            quant_grid_  = 0;
    const Pattern* pat_         = nullptr;
    const Pattern* queued_      = nullptr;

    bool       running_          = false;
    double     pos_              = 0.0; // ticks since the top of the pattern
    double     step_pos_         = 0.0; // when the next step starts
    double     next_tick_        = 0.0;
    double     gate_off_         = -1.0;
    int        pat_length_ticks_ = kSteps * kStepTicks;
    int        ticks_            = kStepTicks; // a step, this loop (StepTicks)
    int        step_             = -1;
    int        next_step_        = 0;
    int        playing_          = -1;
    bool       hold_             = false;
    // The grid, for the drums: the next step's place, the last one's, and
    // where this pass of the pattern started (ahead of pos_'s while the
    // bass's first step is late).
    int        grid_step_     = 0;
    double     grid_pos_      = 0.0;
    double     grid_base_     = 0.0;
    double     last_grid_pos_ = 0.0;
    static constexpr int kMaxDrums = 32; // late hits and drum ratchets
    double     drum_at_[kMaxDrums]  = {};
    int        drum_pay_[kMaxDrums] = {};
    int        drum_count_          = 0;
    int        roll_ticks_          = 0;
    double     roll_next_           = 0.0;
    // The drums' grid: ticks a step, where its count started, its count
    // next, its next point, the last one (and that one unswung).
    int        dticks_             = kStepTicks;
    double     dgrid_base_         = 0.0;
    int        dgrid_n_            = 0;
    double     dgrid_pos_          = 0.0;
    double     dgrid_last_         = 0.0;
    double     dgrid_last_unswung_ = 0.0;
    // The ratchet playing: hits to come, the next one's place, the spacing.
    int        rat_left_   = 0;
    double     rat_pos_    = 0.0;
    double     rat_share_  = 0.0;
    int        rat_note_   = -1;
    bool       rat_accent_ = false;
    bool       rat_hold_   = false; // the gate holds after the last hit

};

} // namespace x0x
