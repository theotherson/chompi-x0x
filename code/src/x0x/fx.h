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

/** The drums' filter LFO: off, triangle, ramp up, ramp down, sample and
 *  hold; its synced rates, slow to fast (beats per cycle): 4 bars, 2 bars,
 *  1 bar, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32. */
constexpr int   kLfoShapes = 5;
constexpr int   kLfoRates  = 11;
constexpr float kLfoBeats[kLfoRates] = {16.f, 8.f, 4.f, 2.f, 1.f, 2.f / 3, 0.5f, 1.f / 3, 0.25f, 1.f / 6, 0.125f};
constexpr float kLfoOctaves = 2.f; // its depth: the cutoff moves +-2 octaves

/** The LFO's value (-1..1) at phase 0..1; `held` for sample and hold. */
inline float LfoValue(int shape, float phase, float held)
{
    switch(shape)
    {
        case 1: return 1.f - 4.f * fabsf(phase - 0.5f); // triangle, from the bottom
        case 2: return 2.f * phase - 1.f;               // ramp up
        case 3: return 1.f - 2.f * phase;               // ramp down
        case 4: return held;                            // sample and hold
        default: return 0.f;
    }
}

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
        float dly_send   = 1.f; // the bass's share of the delay's input (the effects' balance)
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
        const float makeup  = DriveMakeup(s_.drive);
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
                if(!(fabsf(d.l) + fabsf(d.r) < 100.f))
                    d = {0.f, 0.f}, bad_++; // never pass on (or feed back) a broken value
                if(fading_)
                {
                    // Equal-power, so the level holds through the fade.
                    const Frame b  = DelayTap(head_b_ + wobble);
                    const float gb = FastSin2Pi(0.25f * fade_), ga = FastSin2Pi(0.25f - 0.25f * fade_);
                    d              = {d.l * ga + b.l * gb, d.r * ga + b.r * gb};
                }

                // Each repeat through the tape: low-pass, high-pass, saturation.
                const float in_d = s_.dly_send * dwet * (l + r) * 0.5f + (send ? send[i] : 0.f);
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
        // A broken value anywhere in the loops' state: start them afresh
        // (the delay's tape is guarded as it's read).
        if(!(fabsf(lp_l_) + fabsf(lp_r_) + fabsf(hpl_) + fabsf(hpr_) + fabsf(hp_state_) + fabsf(pre_state_)
                 + fabsf(tone1_) + fabsf(tone2_) + fabsf(dc_) + fabsf(held_) + fabsf(mod_buf_[mod_pos_])
             < 1e6f))
        {
            lp_l_ = lp_r_ = hpl_ = hpr_ = hp_state_ = pre_state_ = tone1_ = tone2_ = dc_ = held_ = 0.f;
            for(int i = 0; i < kModSize; i++)
                mod_buf_[i] = 0.f;
            bad_++;
        }
    }

    /** Broken values caught (and cleared) so far. */
    uint32_t BadCount() const { return bad_; }

  private:
    static constexpr int kModSize = 2048; // ~42 ms

    static float Sine(float ph) { return FastSin2Pi(ph); }

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
    uint32_t bad_ = 0;
};

/** A stereo plate reverb, after Jon Dattorro ("Effect Design, Part 1",
 *  1997): a pre-delay and a bandwidth low-pass, four allpass diffusers to
 *  smear each hit into a dense wash, then a figure-eight tank of two halves
 *  (each a slowly modulated allpass, a delay, damping, an allpass, a delay)
 *  that feed each other. Left and right are each summed from seven taps
 *  around the tank, so they're largely uncorrelated: wide.
 *
 *  Size sets everything together: the tank's length (0.75x to 1.25x), the
 *  decay (RT60 ~0.4 to ~6 s), the pre-delay (10 to 40 ms) and the tone
 *  (bigger is darker: damping ~6 to ~2.5 kHz, input bandwidth ~8 to
 *  ~5 kHz, two-pole), as in real rooms. The modulation
 *  keeps the tail from ringing. Memory: kReverbFrames floats. */
class Reverb
{
  public:
    static constexpr size_t kReverbFrames = 65536;

    void Init(float sample_rate, float* mem, size_t frames)
    {
        sr_ = sample_rate;
        k_  = sr_ / 29761.f; // the paper's lengths are at 29761 Hz
        mem_ = frames >= kReverbFrames ? mem : nullptr;
        if(!mem_)
            return;
        for(size_t i = 0; i < kReverbFrames; i++)
            mem_[i] = 0.f;
        // Every line at its longest (size 1.25, and the modulation's swing).
        size_t at = 0;
        auto take = [&](Line& l, float base) {
            l.size = static_cast<int>(base * k_ * 1.25f) + 64;
            l.buf  = mem_ + at;
            at += l.size;
        };
        take(pre_, 0.04f * 29761.f / 1.25f + 1.f);
        for(int d = 0; d < 4; d++)
            take(in_ap_[d], kInAp[d]);
        for(int h = 0; h < 2; h++)
        {
            take(tank_[h].mod_ap, kModAp[h]);
            take(tank_[h].d1, kDelay1[h]);
            take(tank_[h].ap, kTankAp[h]);
            take(tank_[h].d2, kDelay2[h]);
        }
        ok_ = at <= kReverbFrames;
        Set(0.5f);
    }

    /** 0..1: a small room, quick and bright, to a big hall, long and dark. */
    void Set(float size)
    {
        size = Clamp(size, 0.f, 1.f);
        if(fabsf(size - size_) < 1e-4f)
            return;
        size_            = size;
        const float sc   = 0.75f + 0.5f * size;
        const float rt60 = 0.4f * FastExp2(3.9f * size); // ~0.4 .. 6 s
        for(int d = 0; d < 4; d++)
            in_ap_[d].len = static_cast<int>(kInAp[d] * k_); // the diffusers stay put
        float loop = 0.f;
        for(int h = 0; h < 2; h++)
        {
            Half& t    = tank_[h];
            t.mod_ap.len = static_cast<int>(kModAp[h] * k_ * sc);
            t.d1.len     = static_cast<int>(kDelay1[h] * k_ * sc);
            t.ap.len     = static_cast<int>(kTankAp[h] * k_ * sc);
            t.d2.len     = static_cast<int>(kDelay2[h] * k_ * sc);
            loop += t.mod_ap.len + t.d1.len + t.ap.len + t.d2.len;
        }
        // Four decay gains around the loop: RT60 over the loop's length.
        decay_    = fminf(powf(10.f, -3.f * loop / (4.f * rt60 * sr_)), 0.97f);
        pre_.len  = static_cast<int>((0.01f + 0.03f * size) * sr_);
        // Bigger is darker: the damping in the tank and the input's
        // bandwidth both close down as the room grows.
        damp_     = TauToCoef(1.f / (2.f * kPi * (6000.f - 3500.f * size)), sr_);
        bw_coef_  = TauToCoef(1.f / (2.f * kPi * (8000.f - 3000.f * size)), sr_);
        tap_k_    = k_ * sc;
    }

    /** Adds the reverb of `in` to left / right. */
    void Process(const float* in, float* left, float* right, size_t n)
    {
        if(!mem_ || !ok_)
            return;
        const float lfo_inc0 = 0.6f / sr_, lfo_inc1 = 0.83f / sr_;
        const float swing    = 16.f * k_; // the modulation, samples
        for(size_t i = 0; i < n; i++)
        {
            // Pre-delay, bandwidth, diffusion.
            float x = pre_.Read(pre_.len);
            pre_.Write(in[i]);
            bw_ += (x - bw_) * bw_coef_; // two poles: a gentle 12 dB / octave
            bw2_ += (bw_ - bw2_) * bw_coef_;
            x = bw2_;
            for(int d = 0; d < 4; d++)
                x = in_ap_[d].Allpass(x, d < 2 ? 0.75f : 0.625f);

            // The tank: each half fed by the other's end.
            lfo_[0] += lfo_inc0, lfo_[1] += lfo_inc1;
            for(int h = 0; h < 2; h++)
                if(lfo_[h] >= 1.f)
                    lfo_[h] -= 1.f;
            const float ends[2] = {tank_[0].d2.Read(tank_[0].d2.len), tank_[1].d2.Read(tank_[1].d2.len)};
            for(int h = 0; h < 2; h++)
            {
                Half&       t = tank_[h];
                float       y = x + decay_ * ends[1 - h];
                // The modulated allpass: its length swings slowly.
                const float m = static_cast<float>(t.mod_ap.len) + swing * FastSin2Pi(lfo_[h]);
                const float d = t.mod_ap.ReadFrac(m);
                const float v = y + 0.7f * d;
                t.mod_ap.Write(v);
                y = d - 0.7f * v;
                t.d1.Write(y);
                y = t.d1.Read(t.d1.len);
                t.lp += (y - t.lp) * damp_;
                y = t.ap.Allpass(t.lp * decay_, -0.5f);
                t.d2.Write(y);
            }

            // Seven taps each side.
            const float kk = tap_k_;
            const Half& a  = tank_[0];
            const Half& b  = tank_[1];
            const float l  = b.d1.Tap(266 * kk) + b.d1.Tap(2974 * kk) - b.ap.Tap(1913 * kk) + b.d2.Tap(1996 * kk)
                            - a.d1.Tap(1990 * kk) - a.ap.Tap(187 * kk) - a.d2.Tap(1066 * kk);
            const float r  = a.d1.Tap(353 * kk) + a.d1.Tap(3627 * kk) - a.ap.Tap(1228 * kk) + a.d2.Tap(2673 * kk)
                            - b.d1.Tap(2111 * kk) - b.ap.Tap(335 * kk) - b.d2.Tap(121 * kk);
            left[i] += 0.5f * l;
            right[i] += 0.5f * r;
        }
    }

  private:
    struct Line
    {
        float* buf  = nullptr;
        int    size = 1, len = 1, pos = 0;
        float  Read(int delay) const
        {
            int p = pos - delay;
            while(p < 0)
                p += size;
            return buf[p];
        }
        float Tap(float delay) const { return Read(static_cast<int>(delay)); }
        float ReadFrac(float delay) const
        {
            const int   di = static_cast<int>(delay);
            const float f  = delay - di;
            const float a = Read(di), b = Read(di + 1);
            return a + (b - a) * f;
        }
        void Write(float v)
        {
            buf[pos] = v;
            if(++pos >= size)
                pos = 0;
        }
        float Allpass(float x, float g)
        {
            const float d = Read(len);
            const float v = x + g * d;
            Write(v);
            return d - g * v;
        }
    };
    struct Half
    {
        Line  mod_ap, d1, ap, d2;
        float lp = 0.f;
    };

    static constexpr float kInAp[4]    = {142.f, 107.f, 379.f, 277.f};
    static constexpr float kModAp[2]   = {672.f, 908.f};
    static constexpr float kDelay1[2]  = {4453.f, 4217.f};
    static constexpr float kTankAp[2]  = {1800.f, 2656.f};
    static constexpr float kDelay2[2]  = {3720.f, 3163.f};

    float  sr_ = 48000.f, k_ = 1.6f, tap_k_ = 1.6f;
    float* mem_ = nullptr;
    bool   ok_ = false;
    Line   pre_, in_ap_[4];
    Half   tank_[2];
    float  size_ = -1.f, decay_ = 0.5f, damp_ = 0.5f;
    float  bw_ = 0.f, bw2_ = 0.f, bw_coef_ = 0.5f; // the input's bandwidth (Set)
    float  lfo_[2] = {0.f, 0.25f};
};

/** The drums' own effects, in place: drive, a one-knob filter (low-pass
 *  turned left, high-pass turned right, off in the middle), bit crush.
 *  The distortion is the bass's drive's, with a dry / wet mix. */
class DrumFx
{
  public:
    void Init(float sample_rate) { sr_ = sample_rate; }

    struct Settings
    {
        float drive      = 0.f;
        float filter     = 0.5f;
        float filter_res = 0.f;
        float env_amount = 0.f; // the filter's envelope: how far each hit opens it
        float env_decay  = 0.4f;
        float crush_bits = 0.f;
        float crush_rate = 0.f;
    };

    void Set(const Settings& s) { s_ = s; }

    /** A drum hit, `at` samples into the next Process: the filter's envelope
     *  starts again (depth: 1, more for an accent). */
    void Trigger(size_t at, float depth)
    {
        trig_at_    = at;
        trig_depth_ = depth;
    }

    /** mod: if given, the LFO, in octaves, for each sample. */
    void Process(float* x, size_t n, const float* mod = nullptr)
    {
        // Distortion, as the bass's drive: a treble lift into a nearly hard
        // clip, then a tone low-pass (8 to 6 kHz).
        const bool  driving = s_.drive > 0.005f;
        const float gain    = 1.f + 60.f * s_.drive * s_.drive;
        const float makeup  = DrumDriveMakeup(s_.drive);
        const float pre_lp  = TauToCoef(1.f / (2.f * kPi * 1000.f), sr_);
        const float tone_lp = TauToCoef(1.f / (2.f * kPi * (8000.f - 2000.f * s_.drive)), sr_);
        const float f       = s_.filter - 0.5f;
        const bool  lp      = f < -0.02f, hp = f > 0.02f;
        // Low-pass from 20 kHz down to 200 Hz; high-pass from 20 Hz up to 5 kHz.
        const float hz = lp ? 200.f * FastExp2(6.64f * (1.f + 2.f * f)) : 20.f * FastExp2(7.97f * 2.f * f);
        // A state-variable filter, 12 dB / octave: Q from 0.5 (no peak) to
        // about 9, a little quieter as it rings.
        const float g    = tanf(kPi * Clamp(hz, 20.f, 0.45f * sr_) / sr_);
        const float res  = Clamp(s_.filter_res, 0.f, 1.f);
        const float kq   = 2.f * FastExp2(-4.2f * res); // 1 / Q
        float       a1   = 1.f / (1.f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
        const float trim = 1.f / (1.f + 1.5f * res);
        // The envelope: on each hit the cutoff jumps open (low-pass up,
        // high-pass down) by up to 6 octaves, then falls back, 10 ms to 1 s.
        const float env_oct = 6.f * Clamp(s_.env_amount, 0.f, 1.f) * (lp ? 1.f : -1.f);
        const float env_k   = TauToCoef(0.01f * FastExp2(6.64f * Clamp(s_.env_decay, 0.f, 1.f)), sr_);
        const bool  env_on  = (lp || hp) && (s_.env_amount > 0.005f || mod);
        const bool  crush = s_.crush_bits > 0.005f;
        const float levels = FastExp2(15.f - 12.f * s_.crush_bits);
        const int   hold   = 1 + static_cast<int>(31.f * s_.crush_rate * s_.crush_rate);
        for(size_t i = 0; i < n; i++)
        {
            if(i == trig_at_ || (i == n - 1 && trig_at_ != kNoTrigger))
                env_ = trig_depth_, trig_at_ = kNoTrigger; // (past the block: at its end)
            env_ -= env_ * env_k;
            if(env_on && (i & 3) == 0)
            {
                // The cutoff, moved: every 4 samples.
                const float hz_e = Clamp(hz * FastExp2(env_oct * env_ + (mod ? mod[i] : 0.f)), 20.f, 0.45f * sr_);
                const float ge   = tanf(kPi * hz_e / sr_);
                a1 = 1.f / (1.f + ge * (ge + kq)), a2 = ge * a1, a3 = ge * a2;
            }
            float y = x[i];
            if(driving)
            {
                pre_ += (y - pre_) * pre_lp;
                float d = (y + (y - pre_)) * gain; // highs doubled
                const float a = fabsf(d);
                if(a > 0.9f)
                    d = (d < 0.f ? -1.f : 1.f) * (0.9f + 0.1f * FastTanh((a - 0.9f) * 10.f));
                t1_ += (d - t1_) * tone_lp;
                t2_ += (t1_ - t2_) * tone_lp;
                y = t2_ * makeup;
            }
            if(lp || hp)
            {
                const float v3 = y - f2_;
                const float v1 = a1 * f1_ + a2 * v3, v2 = f2_ + a2 * f1_ + a3 * v3;
                f1_ = 2.f * v1 - f1_, f2_ = 2.f * v2 - f2_;
                y   = (lp ? v2 : y - kq * v1 - v2) * trim;
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
    static constexpr size_t kNoTrigger = ~static_cast<size_t>(0);
    float    f1_ = 0.f, f2_ = 0.f, held_ = 0.f, pre_ = 0.f, t1_ = 0.f, t2_ = 0.f;
    float    env_ = 0.f, trig_depth_ = 0.f;
    size_t   trig_at_ = kNoTrigger;
    int      hold_count_ = 0;
};

/** The master compressor: one knob, from off to heavy (threshold down and
 *  ratio up together, ~1.5:1 to ~8:1, a soft knee, automatic make-up gain),
 *  stereo-linked, ~8 ms attack and ~120 ms release. */
class Compressor
{
  public:
    void Init(float sample_rate)
    {
        att_ = TauToCoef(0.008f, sample_rate);
        rel_ = TauToCoef(0.12f, sample_rate);
    }

    void Set(float amount)
    {
        amount_ = Clamp(amount, 0.f, 1.f);
        thresh_ = -6.f - 24.f * amount_;           // dB
        ratio_  = 1.5f + 6.5f * amount_;
        makeup_ = -thresh_ * (1.f - 1.f / ratio_) * 0.45f; // most of the reduction at a typical level
    }

    /** The gain reduction now, dB (for the LEDs). */
    float Reduction() const { return gr_; }

    void Process(float* left, float* right, size_t n)
    {
        if(amount_ < 0.005f)
        {
            gr_ = 0.f;
            return;
        }
        for(size_t i = 0; i < n; i++)
        {
            const float peak = fmaxf(fabsf(left[i]), fabsf(right[i]));
            const float db   = 6.0206f * FastLog2(peak + 1e-9f); // 20 log10
            // Soft knee, 6 dB wide.
            const float over = db - thresh_;
            float       want = 0.f;
            if(over > 3.f)
                want = over * (1.f - 1.f / ratio_);
            else if(over > -3.f)
                want = (1.f - 1.f / ratio_) * (over + 3.f) * (over + 3.f) / 12.f;
            gr_ += (want - gr_) * (want > gr_ ? att_ : rel_);
            const float g = FastExp2((makeup_ - gr_) * 0.16609640f); // 10^(dB / 20)
            left[i] *= g;
            right[i] *= g;
        }
    }

  private:
    float amount_ = 0.f, thresh_ = 0.f, ratio_ = 1.f, makeup_ = 0.f;
    float att_ = 0.1f, rel_ = 0.01f, gr_ = 0.f;
};

constexpr float  Reverb::kInAp[4];
constexpr float  Reverb::kModAp[2];
constexpr float  Reverb::kDelay1[2];
constexpr float  Reverb::kTankAp[2];
constexpr float  Reverb::kDelay2[2];
constexpr size_t Reverb::kReverbFrames;

} // namespace x0x
