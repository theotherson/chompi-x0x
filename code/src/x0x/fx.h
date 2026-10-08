/** @file fx.h
 *  @brief The effects after the voice, in this order:
 *
 *   drive -> bit crusher -> chorus / flanger -> tape delay
 *
 *  Each is off at zero. All take the voice's mono signal to stereo.
 *   - Drive: a pedal-style hard clipper (after the DS-1 and friends): a
 *     high-pass tightens the low end and a treble lift puts the highs in
 *     front, up to ~60x gain hits a nearly hard, slightly asymmetric clip,
 *     then a low-pass tone (8 to 6 kHz) takes only the harshest fizz off.
 *     The level is evened out as it turns up.
 *   - Bit crusher: bit depth (16 down to 4) and sample-rate reduction (down
 *     to 1/32), separately.
 *   - Modulation: one knob morphs chorus (first half: two voices swinging
 *     against each other) into flanger (second half: very short, with
 *     feedback). Width spreads the two sides apart and deepens it.
 *   - Tape delay: ping-pong, tempo-synced (nine settings, triplets and
 *     dotted ones too) or free (30 ms to 1.9 s). Each repeat goes through
 *     tape-like EQ (a low-pass set by tone, a high-pass) and saturation, so
 *     repeats darken as they fade; a little wow and flutter on the time.
 *     Feedback goes just past self-oscillation, where the saturation holds
 *     it. A new synced time crossfades between two read heads (no pitch
 *     bend); the free time glides there slowly, like a tape machine's rate
 *     control, bending the repeats' pitch. Small changes (the tempo moving)
 *     glide too.
 */
#pragma once
#include "dsp.h"

namespace x0x
{

/** Synced delay times, as fractions of a beat: 1/16T, 1/16, 1/8T, 1/8,
 *  1/4T, 3/16, 1/4, 3/8, 1/2. */
constexpr int   kDelayDivisions              = 9;
constexpr float kDelayBeats[kDelayDivisions] = {1.f / 6, 0.25f, 1.f / 3, 0.5f, 2.f / 3, 0.75f, 1.f, 1.5f, 2.f};

/** The free delay time's range, in ms. */
constexpr float kDelayFreeMinMs = 30.f;
constexpr float kDelayFreeMaxMs = 1900.f;

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
        int   dly_div    = 5;   // index into kDelayBeats
        bool  dly_free   = false; // free time instead of synced
        float dly_free_ms = 375.f;
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
        heads_set_ = false;
    }

    void Set(const Settings& s)
    {
        s_ = s;
        float samples;
        if(s.dly_free)
            samples = Clamp(s.dly_free_ms, kDelayFreeMinMs, kDelayFreeMaxMs) * 0.001f * sr_;
        else
            samples = kDelayBeats[ClampInt(s.dly_div, 0, kDelayDivisions - 1)] * 60.f / s.bpm * sr_;
        target_delay_ = Clamp(samples, 64.f, static_cast<float>(delay_size_ - 400));
        if(!heads_set_)
            head_a_ = head_b_ = target_delay_, heads_set_ = true;
    }

    /** in: the bass (mono); send: the drums' send into the delay (mono),
     *  nullptr for none. The delay is shared: the bass reaches it through
     *  its delay knob (lower half sends, upper half also fades the dry), the
     *  drums through their send; its return joins left / right. */
    void Process(const float* in, const float* send, float* left, float* right, size_t n)
    {
        // Drive
        const bool  driving = s_.drive > 0.005f;
        const float gain    = 1.f + 60.f * s_.drive * s_.drive;
        const float bias    = 0.08f * s_.drive; // a touch of asymmetry
        const float makeup  = 1.f / (1.f + 2.5f * s_.drive);
        const float hp_in   = TauToCoef(1.f / (2.f * kPi * 120.f), sr_);
        // Treble lift before the clip (+6 dB above ~1 kHz) so the highs
        // distort hardest, and a brighter tone filter after it (8 kHz down
        // to 6 kHz at full drive): a trebly, DS-1-like edge.
        const float pre_lp  = TauToCoef(1.f / (2.f * kPi * 1000.f), sr_);
        const float tone_lp = TauToCoef(1.f / (2.f * kPi * (8000.f - 2000.f * s_.drive)), sr_);

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
        const bool  sending  = s_.dly_mix > 0.005f || send != nullptr;
        const bool  delaying = sending || delay_tail_ > 0;
        float       dry, dwet;
        MixGains(s_.dly_mix, &dry, &dwet);
        const float dfb     = 1.05f * s_.dly_fb;
        const float tape_lp = TauToCoef(1.f / (2.f * kPi * (900.f + 6000.f * s_.dly_tone * s_.dly_tone)), sr_);
        const float tape_hp = TauToCoef(1.f / (2.f * kPi * 90.f), sr_);
        const float glide   = TauToCoef(s_.dly_free ? 0.15f : 0.05f, sr_); // free: tape-slow
        const float fade_in = 1.f / (0.08f * sr_);                             // crossfade: 80 ms

        for(size_t i = 0; i < n; i++)
        {
            float x = in[i];

            if(driving)
            {
                hp_state_ += (x - hp_state_) * hp_in;
                const float tight = x - hp_state_;
                pre_state_ += (tight - pre_state_) * pre_lp;
                const float bright = tight + (tight - pre_state_); // highs doubled
                float d = bright * gain + bias;
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
                // To the set time: a big synced change crossfades to a second
                // read head (one at a time; a newer change waits for it);
                // the free time and small changes glide the head there.
                const bool far = fabsf(target_delay_ - head_a_) > 0.03f * head_a_;
                if(fading_)
                {
                    fade_ += fade_in;
                    if(fade_ >= 1.f)
                        head_a_ = head_b_, fade_ = 0.f, fading_ = false;
                }
                else if(far && !s_.dly_free)
                    head_b_ = target_delay_, fade_ = 0.f, fading_ = true;
                else
                {
                    // At most a third faster or slower than the tape's
                    // speed: a swoop of the repeats' pitch, never backwards.
                    const float step = Clamp((target_delay_ - head_a_) * glide, -0.33f, 0.33f);
                    head_a_ += step;
                }
                wow_ += 0.55f / sr_;
                if(wow_ >= 1.f)
                    wow_ -= 1.f;
                flutter_ += 6.5f / sr_;
                if(flutter_ >= 1.f)
                    flutter_ -= 1.f;
                const float wobble = sr_ * (0.0012f * Sine(wow_) + 0.00015f * Sine(flutter_));
                Frame       d      = DelayTap(head_a_ + wobble);
                if(fading_)
                {
                    // Equal-power, so the level holds through the fade.
                    const Frame b  = DelayTap(head_b_ + wobble);
                    const float gb = sinf(0.5f * kPi * fade_), ga = cosf(0.5f * kPi * fade_);
                    d              = {d.l * ga + b.l * gb, d.r * ga + b.r * gb};
                }

                // Each repeat through the tape: low-pass, high-pass, saturation.
                const float in_d = dwet * (l + r) * 0.5f + (send ? send[i] : 0.f);
                float       fl   = in_d + dfb * d.r;
                float       fr   = dfb * d.l;
                lp_l_ += (fl - lp_l_) * tape_lp;
                lp_r_ += (fr - lp_r_) * tape_lp;
                hpl_ += (lp_l_ - hpl_) * tape_hp;
                hpr_ += (lp_r_ - hpr_) * tape_hp;
                delay_[delay_pos_] = {FastTanh(lp_l_ - hpl_), FastTanh(lp_r_ - hpr_)};
                delay_pos_         = (delay_pos_ + 1) % delay_size_;

                l = l * dry + d.l;
                r = r * dry + d.r;
                // Keep running a while after the sends go to zero, so the
                // echoes die away instead of stopping.
                if(sending)
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

    float hp_state_ = 0.f, pre_state_ = 0.f, dc_ = 0.f, tone1_ = 0.f, tone2_ = 0.f;
    float held_     = 0.f;
    int   hold_count_ = 0;
    float lfo_      = 0.f;
    float mod_buf_[kModSize];
    int   mod_pos_  = 0;

    Frame* delay_        = nullptr;
    size_t delay_size_   = 1;
    size_t delay_pos_    = 0;
    float  target_delay_ = 12000.f;
    float  head_a_       = 12000.f; // the read head playing
    float  head_b_       = 12000.f; // the one fading in
    float  fade_         = 0.f;
    bool   fading_       = false;
    bool   heads_set_    = false;
    int    delay_tail_   = 0;
    float  wow_ = 0.f, flutter_ = 0.f;
    float  lp_l_ = 0.f, lp_r_ = 0.f, hpl_ = 0.f, hpr_ = 0.f;
};

/** A stereo reverb: four delay lines feeding back through a Householder
 *  matrix (a feedback delay network), each damped by a low-pass, after two
 *  allpass diffusers. Size sets the room (the lines' lengths) and the
 *  decay (RT60 ~0.3 to ~6 s) together. Memory: kReverbFrames floats. */
class Reverb
{
  public:
    static constexpr size_t kReverbFrames = 16384;

    void Init(float sample_rate, float* mem, size_t frames)
    {
        sr_  = sample_rate;
        mem_ = frames >= kReverbFrames ? mem : nullptr;
        if(mem_)
            for(size_t i = 0; i < kReverbFrames; i++)
                mem_[i] = 0.f;
        // The memory, split: two diffusers, then the four lines.
        size_t at = 0;
        for(int k = 0; k < 2; k++)
            ap_[k] = mem_ + at, at += kApLen[k];
        for(int k = 0; k < 4; k++)
            line_[k] = mem_ + at, at += kLineMax;
        damp_ = TauToCoef(1.f / (2.f * kPi * 6000.f), sr_);
        Set(0.5f);
    }

    /** 0..1: a small room, quick, to a big hall, long. */
    void Set(float size)
    {
        const float scale = 0.7f + 0.6f * Clamp(size, 0.f, 1.f);
        const float rt60  = 0.3f * FastExp2(4.3f * Clamp(size, 0.f, 1.f));
        for(int k = 0; k < 4; k++)
        {
            len_[k] = static_cast<int>(kLineLen[k] * scale * sr_ / 48000.f);
            if(len_[k] > static_cast<int>(kLineMax) - 1)
                len_[k] = static_cast<int>(kLineMax) - 1;
            g_[k] = powf(10.f, -3.f * len_[k] / (rt60 * sr_));
        }
    }

    /** Adds the reverb of `in` to left / right. */
    void Process(const float* in, float* left, float* right, size_t n)
    {
        if(!mem_)
            return;
        for(size_t i = 0; i < n; i++)
        {
            // Diffuse the input.
            float x = in[i];
            for(int k = 0; k < 2; k++)
            {
                float*      b = ap_[k];
                const float d = b[ap_pos_[k]];
                const float v = x + 0.6f * d;
                b[ap_pos_[k]] = v;
                x             = d - 0.6f * v;
                ap_pos_[k]    = (ap_pos_[k] + 1) % kApLen[k];
            }
            // The lines' outputs, damped.
            float o[4], sum = 0.f;
            for(int k = 0; k < 4; k++)
            {
                int rd = pos_ - len_[k];
                if(rd < 0)
                    rd += static_cast<int>(kLineMax);
                lp_[k] += (line_[k][rd] - lp_[k]) * damp_;
                o[k] = lp_[k] * g_[k];
                sum += o[k];
            }
            // Householder: each line gets its own output less half the sum,
            // plus the input (signs alternating).
            const float h = 0.5f * sum;
            for(int k = 0; k < 4; k++)
                line_[k][pos_] = o[k] - h + ((k & 1) ? -x : x);
            pos_ = (pos_ + 1) % static_cast<int>(kLineMax);
            left[i] += 0.5f * (o[0] + o[2]);
            right[i] += 0.5f * (o[1] - o[3]);
        }
    }

  private:
    static constexpr int    kApLen[2]   = {225, 556};
    static constexpr int    kLineLen[4] = {1557, 1871, 2311, 2803}; // at 48 kHz
    static constexpr size_t kLineMax    = 3880;                      // 2803 x 1.3, and to spare

    float  sr_ = 48000.f;
    float* mem_ = nullptr;
    float* ap_[2] = {};
    float* line_[4] = {};
    int    ap_pos_[2] = {};
    int    pos_ = 0;
    int    len_[4] = {};
    float  g_[4] = {};
    float  lp_[4] = {};
    float  damp_ = 0.5f;
};

/** The drums' own effects, in place: drive, a one-knob filter (low-pass
 *  turned left, high-pass turned right, off in the middle), bit crush. */
class DrumFx
{
  public:
    void Init(float sample_rate) { sr_ = sample_rate; }

    struct Settings
    {
        float drive      = 0.f;
        float filter     = 0.5f;
        float crush_bits = 0.f;
        float crush_rate = 0.f;
    };

    void Set(const Settings& s) { s_ = s; }

    void Process(float* x, size_t n)
    {
        const bool  driving = s_.drive > 0.005f;
        const float gain    = 1.f + 20.f * s_.drive * s_.drive;
        const float makeup  = 1.f / (1.f + 1.5f * s_.drive);
        const float f       = s_.filter - 0.5f;
        const bool  lp      = f < -0.02f, hp = f > 0.02f;
        // Low-pass from 20 kHz down to 200 Hz; high-pass from 20 Hz up to 5 kHz.
        const float hz = lp ? 200.f * FastExp2(6.64f * (1.f + 2.f * f)) : 20.f * FastExp2(7.97f * 2.f * f);
        const float k  = TauToCoef(1.f / (2.f * kPi * Clamp(hz, 20.f, 0.45f * sr_)), sr_);
        const bool  crush = s_.crush_bits > 0.005f;
        const float levels = FastExp2(15.f - 12.f * s_.crush_bits);
        const int   hold   = 1 + static_cast<int>(31.f * s_.crush_rate * s_.crush_rate);
        for(size_t i = 0; i < n; i++)
        {
            float y = x[i];
            if(driving)
                y = FastTanh(y * gain) * makeup;
            // Two one-pole stages: 12 dB / octave.
            if(lp)
            {
                f1_ += (y - f1_) * k;
                f2_ += (f1_ - f2_) * k;
                y = f2_;
            }
            else if(hp)
            {
                f1_ += (y - f1_) * k;
                const float h1 = y - f1_;
                f2_ += (h1 - f2_) * k;
                y = h1 - f2_;
            }
            if(crush || hold > 1)
            {
                if(hold_count_-- <= 0)
                {
                    hold_count_ = hold - 1;
                    held_       = crush ? floorf(Clamp(y, -1.f, 1.f) * levels + 0.5f) / levels : y;
                }
                y = held_;
            }
            x[i] = y;
        }
    }

  private:
    float    sr_ = 48000.f;
    Settings s_;
    float    f1_ = 0.f, f2_ = 0.f, held_ = 0.f;
    int      hold_count_ = 0;
};

constexpr int    Reverb::kApLen[2];
constexpr int    Reverb::kLineLen[4];
constexpr size_t Reverb::kLineMax;
constexpr size_t Reverb::kReverbFrames;

} // namespace x0x
