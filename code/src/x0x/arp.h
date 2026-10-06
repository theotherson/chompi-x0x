/** @file arp.h
 *  @brief The arpeggiator: the keys held in live mode, played one at a time
 *  in sixteenths, up, down, up-down, at random or as played, over 1-3
 *  octaves. Latch keeps the chord going after the keys are let go; while
 *  latched, each key pressed adds its note to the chord, or takes it out if
 *  it is already there.
 *
 *  It only decides which note comes next; Machine times the steps (locked to
 *  the pattern when it runs) and plays them.
 */
#pragma once
#include "dsp.h"

namespace x0x
{

enum class ArpMode : uint8_t
{
    UP,
    DOWN,
    UP_DOWN,
    RANDOM,
    PLAYED,
    COUNT,
};

class Arp
{
  public:
    static constexpr int kMaxNotes = 16;

    void SetMode(ArpMode m) { mode_ = m; }
    void SetOctaves(int o) { octaves_ = ClampInt(o, 1, 3); }

    void SetLatch(bool on)
    {
        latch_ = on;
        if(!on && pressed_ == 0)
            count_ = 0; // letting go of latch with no keys down ends it
    }
    bool Latch() const { return latch_; }

    bool Active() const { return count_ > 0; }

    void Clear()
    {
        count_ = pressed_ = 0;
        pos_   = -1;
        dir_   = 1;
    }

    void NoteOn(int note)
    {
        pressed_++;
        for(int i = 0; i < count_; i++)
        {
            if(notes_[i] == note)
            {
                if(latch_)
                    Remove(i); // latched: a second press takes it out
                return;
            }
        }
        if(count_ < kMaxNotes)
            notes_[count_++] = note;
        if(count_ == 1)
            pos_ = -1, dir_ = 1;
    }

    void NoteOff(int note)
    {
        if(pressed_ > 0)
            pressed_--;
        if(latch_)
            return;
        for(int i = 0; i < count_; i++)
        {
            if(notes_[i] == note)
            {
                Remove(i);
                return;
            }
        }
    }

    /** The notes in the chord now (for the LEDs). */
    int Count() const { return count_; }
    int NoteAt(int i) const { return notes_[i]; }

    /** The held note the last Next() came from, before its octave. */
    int LastSource() const { return last_source_; }

    /** The next note to play; only call while Active(). */
    int Next()
    {
        // Pitch order, or press order for PLAYED.
        int order[kMaxNotes];
        for(int i = 0; i < count_; i++)
            order[i] = notes_[i];
        if(mode_ != ArpMode::PLAYED)
            for(int i = 1; i < count_; i++)
                for(int j = i; j > 0 && order[j] < order[j - 1]; j--)
                {
                    const int t  = order[j];
                    order[j]     = order[j - 1];
                    order[j - 1] = t;
                }

        const int len = count_ * octaves_;
        int       idx;
        switch(mode_)
        {
            case ArpMode::DOWN:
                pos_ = (pos_ + 1) % len;
                idx  = len - 1 - pos_;
                break;
            case ArpMode::UP_DOWN:
                if(len == 1)
                    idx = pos_ = 0;
                else
                {
                    pos_ += dir_;
                    if(pos_ >= len)
                        pos_ = len - 2, dir_ = -1;
                    else if(pos_ < 0)
                        pos_ = 1, dir_ = 1;
                    idx = pos_;
                }
                break;
            case ArpMode::RANDOM:
                rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
                idx = static_cast<int>(rng_ % static_cast<uint32_t>(len));
                break;
            default: // UP, PLAYED
                pos_ = (pos_ + 1) % len;
                idx  = pos_;
                break;
        }
        last_source_ = order[idx % count_];
        return last_source_ + 12 * (idx / count_);
    }

  private:
    void Remove(int i)
    {
        for(int j = i; j < count_ - 1; j++)
            notes_[j] = notes_[j + 1];
        count_--;
    }

    int      notes_[kMaxNotes];
    int      count_   = 0;
    int      pressed_ = 0;
    int      pos_     = -1;
    int      dir_     = 1;
    int      octaves_ = 1;
    int      last_source_ = -1;
    bool     latch_   = false;
    ArpMode  mode_    = ArpMode::UP;
    uint32_t rng_     = 0x9E3779B9u;
};

} // namespace x0x
