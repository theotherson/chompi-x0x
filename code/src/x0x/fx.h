/** @file fx.h
 *  @brief The effects after the voice, in this order:
 *
 *   drive -> bit crusher -> chorus / flanger -> tape delay
 *
 *  Each is off at zero. All take the voice's mono signal to stereo.
 *   - Drive: a pedal-style hard clipper (after the DS-1 and friends): a
 *     high-pass tightens the low end, up to ~60x gain hits a nearly hard,
 *     slightly asymmetric clip, then a low-pass tone takes the fizz off.
 *     The level is evened out as it turns up.
 *   - Bit crusher: bit depth (16 down to 4) and sample-rate reduction (down
 *     to 1/32), separately.
 *   - Modulation: one knob morphs chorus (first half: two voices swinging
 *     against each other) into flanger (second half: very short, with
 *     feedback). Width spreads the two sides apart and deepens it.
 *   - Tape delay: tempo-synced ping-pong. Each repeat goes through tape-like
 *     EQ (a low-pass set by tone, a high-pass) and saturation, so repeats
 *     darken as they fade; a little wow and flutter on the time. Feedback
 *     goes just past self-oscillation, where the saturation holds it.
 */
#pragma once
#include "dsp.h"

namespace x0x
{

/** Delay times, as fractions of a beat. */
constexpr int   kDelayDivisions              = 6;
constexpr float kDelayBeats[kDelayDivisions] = {0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f};

class Fx
{
  public:
    struct Frame
    {
        float l, r;
    };

    struct Settings
    {
        float drive      = 0.f; // 0..1
        float crush_bits = 0.f; // 0..1
        float crush_rate = 0.f; // 0..1
        float mod        = 0.f; // 0 off, chorus to flanger
        float mod_width  = 0.5f;
        float dly_mix    = 0.f; // dry/wet
        int   dly_div    = 2;   // index into kDelayBeats
        float dly_fb     = 0.35f;
        float dly_tone   = 0.4f; // dark .. bright
        float bpm        = 120.f;
    };

    /** delay_mem: frames for the delay (2 s at 48 kHz = 96000); on the
     *  hardware it lives in SDRAM. */
    void Init(float sample_rate, Frame* delay_mem, size_t delay_frames)
    {
        sr_         = sample_rate;
        delay_      = delay_mem;
        delay_size_ = delay_frames;
        for(size_t i = 0; i < delay_size_; i++)
            delay_[i] = {0.f, 0.f};
        for(int i = 0; i < kModSize; i++)
            mod_buf_[i] = 0.f;
        delay_pos_ = mod_pos_ = 0;
    }

    void Set(const Settings& s)
    {
        s_ = s;
        const float beats = kDelayBeats[ClampInt(s.dly_div, 0, kDelayDivisions - 1)];
        target_delay_     = Clamp(beats * 60.f / s.bpm * sr_, 64.f, static_cast<float>(delay_size_ - 400));
    }

    void Process(const float* in, float* left, float* right, size_t n)
    {
        // Drive
        const bool  driving = s_.drive > 0.005f;
        const float gain    = 1.f + 60.f * s_.drive * s_.drive;
        const float bias    = 0.08f * s_.drive; // a touch of asymmetry
        const float makeup  = 1.f / (1.f + 2.5f * s_.drive);
        const float hp_in   = TauToCoef(1.f / (2.f * kPi * 120.f), sr_);
        const float tone_lp = TauToCoef(1.f / (2.f * kPi * (5000.f - 2500.f * s_.drive)), sr_);

        // Bit crusher
        const bool  crush_bits = s_.crush_bits > 0.005f;
        const float levels     = FastExp2(15.f - 12.f * s_.crush_bits);
        const int   hold       = 1 + static_cast<int>(31.f * s_.crush_rate * s_.crush_rate);

        // Modulation: chorus (0-0.5) into flanger (0.5-1).
        const bool modding = s_.mod > 0.005f;
        const float w      = s_.mod_width;
        float base_ms, depth_ms, rate_hz, fb, wet;
        if(s_.mod < 0.5f)
        {
            const float t = s_.mod * 2.f;
            base_ms = 8.f, depth_ms = 2.f + 3.f * w, rate_hz = 0.6f, fb = 0.f, wet = 0.4f + 0.3f * t;
        }
        else
        {
            const float t = (s_.mod - 0.5f) * 2.f;
            base_ms = 1.5f, depth_ms = 1.f + 1.f * w, rate_hz = 0.18f, fb = 0.3f + 0.5f * t, wet = 0.7f;
        }
        const float base    = base_ms * 0.001f * sr_;
        const float depth   = depth_ms * 0.001f * sr_;
        const float lfo_inc = rate_hz / sr_;
        const float phase_r = 0.5f * w; // width: the right side's LFO offset

        // Tape delay
        const bool  delaying = s_.dly_mix > 0.005f || delay_tail_ > 0;
        float       dry, dwet;
        MixGains(s_.dly_mix, &dry, &dwet);
        const float dfb     = 1.05f * s_.dly_fb;
        const float tape_lp = TauToCoef(1.f / (2.f * kPi * (900.f + 6000.f * s_.dly_tone * s_.dly_tone)), sr_);
        const float tape_hp = TauToCoef(1.f / (2.f * kPi * 90.f), sr_);

        for(size_t i = 0; i < n; i++)
        {
            float x = in[i];

            if(driving)
            {
                hp_state_ += (x - hp_state_) * hp_in;
                float d = (x - hp_state_) * gain + bias;
                d       = HardClip(d);
                dc_ += (d - dc_) * 0.0005f;
                d -= dc_;
                tone1_ += (d - tone1_) * tone_lp;
                tone2_ += (tone1_ - tone2_) * tone_lp;
                x = tone2_ * makeup;
            }

            if(crush_bits || hold > 1)
            {
                if(hold_count_-- <= 0)
                {
                    hold_count_ = hold - 1;
                    held_       = crush_bits ? floorf(Clamp(x, -1.f, 1.f) * levels + 0.5f) / levels : x;
                }
                x = held_;
            }

            float l = x, r = x;
            if(modding)
            {
                lfo_ += lfo_inc;
                if(lfo_ >= 1.f)
                    lfo_ -= 1.f;
                float pr = lfo_ + phase_r;
                if(pr >= 1.f)
                    pr -= 1.f;
                const float tl = ModTap(base + depth * (0.5f + 0.5f * Sine(lfo_)));
                const float tr = ModTap(base + depth * (0.5f + 0.5f * Sine(pr)));
                mod_buf_[mod_pos_] = x + fb * 0.5f * (tl + tr);
                mod_pos_           = (mod_pos_ + 1) % kModSize;
                const float norm   = 1.f / (1.f + 0.5f * wet);
                l                  = (x + wet * tl) * norm;
                r                  = (x + wet * tr) * norm;
            }

            if(delaying)
            {
                // Glide to the set time, plus a little wow and flutter.
                cur_delay_ += (target_delay_ - cur_delay_) * 0.0005f;
                wow_ += 0.55f / sr_;
                if(wow_ >= 1.f)
                    wow_ -= 1.f;
                flutter_ += 6.5f / sr_;
                if(flutter_ >= 1.f)
                    flutter_ -= 1.f;
                const float wobble = sr_ * (0.0012f * Sine(wow_) + 0.00015f * Sine(flutter_));
                const Frame d      = DelayTap(cur_delay_ + wobble);

                // Each repeat through the tape: low-pass, high-pass, saturation.
                const float in_d = s_.dly_mix > 0.005f ? (l + r) * 0.5f : 0.f;
                float       fl   = in_d + dfb * d.r;
                float       fr   = dfb * d.l;
                lp_l_ += (fl - lp_l_) * tape_lp;
                lp_r_ += (fr - lp_r_) * tape_lp;
                hpl_ += (lp_l_ - hpl_) * tape_hp;
                hpr_ += (lp_r_ - hpr_) * tape_hp;
                delay_[delay_pos_] = {FastTanh(lp_l_ - hpl_), FastTanh(lp_r_ - hpr_)};
                delay_pos_         = (delay_pos_ + 1) % delay_size_;

                l = l * dry + dwet * d.l;
                r = r * dry + dwet * d.r;
                // Keep running a while after the mix goes to zero, so the
                // echoes die away instead of stopping.
                if(s_.dly_mix > 0.005f)
                    delay_tail_ = static_cast<int>(sr_ * 6.f);
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

    /** Nearly hard: straight up to 0.9, then a short knee into 1. */
    static float HardClip(float x)
    {
        const float a = fabsf(x);
        if(a <= 0.9f)
            return x;
        const float y = 0.9f + 0.1f * FastTanh((a - 0.9f) * 10.f);
        return x < 0.f ? -y : y;
    }

    /** Dry/wet law: the bottom half brings the effect up to full under a
     *  full dry signal (the middle is 50/50), the top half fades the dry. */
    static void MixGains(float m, float* dry, float* wet)
    {
        m    = Clamp(m, 0.f, 1.f);
        *wet = m < 0.5f ? 2.f * m : 1.f;
        *dry = m > 0.5f ? 2.f * (1.f - m) : 1.f;
    }

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

    float    sr_ = 48000.f;
    Settings s_;

    float hp_state_ = 0.f, dc_ = 0.f, tone1_ = 0.f, tone2_ = 0.f;
    float held_     = 0.f;
    int   hold_count_ = 0;
    float lfo_      = 0.f;
    float mod_buf_[kModSize];
    int   mod_pos_  = 0;

    Frame* delay_        = nullptr;
    size_t delay_size_   = 1;
    size_t delay_pos_    = 0;
    float  target_delay_ = 12000.f;
    float  cur_delay_    = 12000.f;
    int    delay_tail_   = 0;
    float  wow_ = 0.f, flutter_ = 0.f;
    float  lp_l_ = 0.f, lp_r_ = 0.f, hpl_ = 0.f, hpr_ = 0.f;
};

} // namespace x0x
