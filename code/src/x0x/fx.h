/** @file fx.h
 *  @brief The effects after the voice, in this order:
 *
 *   bit crusher -> doubler / chorus / flanger -> delay
 *
 *  Each is off at zero. All take the voice's mono signal to stereo.
 *   - Bit crusher: one knob takes bit depth from 16 down to 4 and the sample
 *     rate down to 1/16 together.
 *   - Modulation: one knob morphs doubler (0-1/3: a short, slowly drifting
 *     copy), chorus (1/3-2/3: deeper, two voices swinging left and right) and
 *     flanger (2/3-1: very short, with feedback), louder as it turns.
 *   - Delay: tempo-synced ping-pong. Amount sets the mix and the feedback
 *     together; time is a note length.
 */
#pragma once
#include "dsp.h"

namespace x0x
{

/** Delay times, as fractions of a beat. */
constexpr int   kDelayDivisions               = 6;
constexpr float kDelayBeats[kDelayDivisions]  = {0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f};

class Fx
{
  public:
    struct Frame
    {
        float l, r;
    };

    /** delay_mem: frames for the delay (2 s at 48 kHz = 96000); on the
     *  hardware it lives in SDRAM. */
    void Init(float sample_rate, Frame* delay_mem, size_t delay_frames)
    {
        sr_          = sample_rate;
        delay_       = delay_mem;
        delay_size_  = delay_frames;
        for(size_t i = 0; i < delay_size_; i++)
            delay_[i] = {0.f, 0.f};
        for(int i = 0; i < kModSize; i++)
            mod_buf_[i] = 0.f;
        delay_pos_ = mod_pos_ = 0;
    }

    /** crush, mod, delay_amount: 0..1. delay_div: index into kDelayBeats. */
    void Set(float crush, float mod, float delay_amount, int delay_div, float bpm)
    {
        crush_ = crush;
        mod_   = mod;
        dly_   = delay_amount;
        const float beats   = kDelayBeats[ClampInt(delay_div, 0, kDelayDivisions - 1)];
        target_delay_       = Clamp(beats * 60.f / bpm * sr_, 64.f, static_cast<float>(delay_size_ - 4));
    }

    void Process(const float* in, float* left, float* right, size_t n)
    {
        // Bit crusher settings.
        const bool  crushing = crush_ > 0.001f;
        const float bits     = 16.f - 12.f * crush_;
        const float levels   = FastExp2(bits - 1.f);
        const int   hold     = 1 + static_cast<int>(15.f * crush_ * crush_);

        // Modulation settings by region.
        const bool modding = mod_ > 0.001f;
        float      base_ms, depth_ms, rate_hz, fb, wet;
        if(mod_ < 1.f / 3.f) // doubler
        {
            const float t = mod_ * 3.f;
            base_ms = 18.f, depth_ms = 1.5f, rate_hz = 0.2f, fb = 0.f, wet = 0.6f * t;
        }
        else if(mod_ < 2.f / 3.f) // chorus
        {
            const float t = (mod_ - 1.f / 3.f) * 3.f;
            base_ms = 9.f, depth_ms = 3.f + 3.f * t, rate_hz = 0.7f, fb = 0.f, wet = 0.6f + 0.2f * t;
        }
        else // flanger
        {
            const float t = (mod_ - 2.f / 3.f) * 3.f;
            base_ms = 2.5f, depth_ms = 2.f, rate_hz = 0.2f, fb = 0.4f + 0.4f * t, wet = 0.7f;
        }
        const float base  = base_ms * 0.001f * sr_;
        const float depth = depth_ms * 0.001f * sr_;
        const float lfo_inc = rate_hz / sr_;

        // Delay settings: mix and feedback rise together.
        const bool  delaying = dly_ > 0.001f || delay_tail_ > 0;
        const float dmix     = Clamp(dly_ * 1.4f, 0.f, 0.7f);
        const float dfb      = 0.75f * dly_;

        for(size_t i = 0; i < n; i++)
        {
            float x = in[i];

            if(crushing)
            {
                if(hold_count_-- <= 0)
                {
                    hold_count_ = hold - 1;
                    held_       = floorf(Clamp(x, -1.f, 1.f) * levels + 0.5f) / levels;
                }
                x = held_;
            }

            float l = x, r = x;
            if(modding)
            {
                lfo_ += lfo_inc;
                if(lfo_ >= 1.f)
                    lfo_ -= 1.f;
                const float sl = Sine(lfo_), sr = Sine(lfo_ + 0.25f > 1.f ? lfo_ - 0.75f : lfo_ + 0.25f);
                const float tl = ModTap(base + depth * (0.5f + 0.5f * sl));
                const float tr = ModTap(base + depth * (0.5f + 0.5f * sr));
                mod_buf_[mod_pos_] = x + fb * 0.5f * (tl + tr);
                mod_pos_           = (mod_pos_ + 1) % kModSize;
                l                  = x + wet * tl;
                r                  = x + wet * tr;
                const float norm   = 1.f / (1.f + 0.5f * wet);
                l *= norm, r *= norm;
            }

            if(delaying)
            {
                // Glide the delay time to its target, so turning it doesn't click.
                cur_delay_ += (target_delay_ - cur_delay_) * 0.0005f;
                const Frame d  = DelayTap(cur_delay_);
                // Ping-pong: each side feeds the other.
                const float inl = (l + r) * 0.5f * (dly_ > 0.001f ? 1.f : 0.f);
                delay_[delay_pos_] = {SoftClip(inl + dfb * d.r), SoftClip(dfb * d.l)};
                delay_pos_         = (delay_pos_ + 1) % delay_size_;
                l += dmix * d.l;
                r += dmix * d.r;
                // Keep running a little after the knob goes to zero, so the
                // echoes die away instead of stopping.
                if(dly_ > 0.001f)
                    delay_tail_ = static_cast<int>(sr_ * 4.f);
                else if(delay_tail_ > 0)
                    delay_tail_--;
            }

            left[i]  = l;
            right[i] = r;
        }
    }

  private:
    static constexpr int kModSize = 2048; // ~42 ms

    static float Sine(float ph) { return sinf(2.f * kPi * ph); }
    static float SoftClip(float x) { return FastTanh(x); }

    float ModTap(float delay) const
    {
        float pos = static_cast<float>(mod_pos_) - delay;
        while(pos < 0.f)
            pos += kModSize;
        const int   i0 = static_cast<int>(pos);
        const float f  = pos - i0;
        const float a = mod_buf_[i0 % kModSize], b = mod_buf_[(i0 + 1) % kModSize];
        return a + (b - a) * f;
    }

    Frame DelayTap(float delay) const
    {
        float pos = static_cast<float>(delay_pos_) - delay;
        while(pos < 0.f)
            pos += static_cast<float>(delay_size_);
        const size_t i0 = static_cast<size_t>(pos);
        const float  f  = pos - i0;
        const Frame& a  = delay_[i0 % delay_size_];
        const Frame& b  = delay_[(i0 + 1) % delay_size_];
        return {a.l + (b.l - a.l) * f, a.r + (b.r - a.r) * f};
    }

    float  sr_         = 48000.f;
    float  crush_      = 0.f;
    float  mod_        = 0.f;
    float  dly_        = 0.f;
    float  held_       = 0.f;
    int    hold_count_ = 0;
    float  lfo_        = 0.f;
    float  mod_buf_[kModSize];
    int    mod_pos_    = 0;
    Frame* delay_      = nullptr;
    size_t delay_size_ = 1;
    size_t delay_pos_  = 0;
    float  target_delay_ = 12000.f;
    float  cur_delay_    = 12000.f;
    int    delay_tail_   = 0;
};

} // namespace x0x
