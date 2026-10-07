/** @file voice.h
 *  @brief The 303-style voice: one oscillator (saw, or square), a 3-pole
 *  resonant ladder low-pass, a decay-only filter envelope, a gated amp
 *  envelope, the accent circuit and slide. (Drive is in fx.h.)
 *
 *  Not a circuit model, but built around the TB-303's behaviour:
 *   - Filter: the TB-303's 4-pole diode ladder, after Tim Stinchcombe's
 *     analysis. Its core transfer function,
 *       1 / (s^4 + 2^(11/4) s^3 + 10 sqrt(2) s^2 + 2^(13/4) s + 1 + k),
 *     factors into four one-pole low-passes at 0.128, 1.04, 2.33 and 3.24 x
 *     the cutoff, inside one feedback loop. The spread poles give the
 *     303's slope: about 18 dB/octave above the cutoff, 24 only far above,
 *     and a broader resonance than equal stages. It self-oscillates at
 *     k = 17 (at 1.19 x the cutoff); resonance stops well short. A high-pass
 *     in the loop and tanh at its input, as on the 303. Run at 2x.
 *   - Filter envelope: instant attack, exponential decay set by DECAY, no
 *     sustain. ENV MOD sets how many octaves it opens the cutoff.
 *   - Accent: louder, the filter envelope at its shortest decay, and an
 *     accent capacitor charged from the filter envelope that drains slowly,
 *     so accents in a row build up into the 303's "wow". Higher resonance
 *     slows the drain, as on the original.
 *   - Slide: the next note arrives without retriggering the envelopes and
 *     glides to its pitch in the slide time.
 */
#pragma once
#include "dsp.h"

namespace x0x
{

/** Everything the voice needs from the panel, in real units. */
struct VoiceParams
{
    float cutoff_hz  = 500.f;
    float resonance  = 0.5f;  // 0..1
    float env_oct    = 2.f;   // octaves the filter envelope opens the cutoff
    float decay_s    = 0.6f;  // filter envelope decay, unaccented
    float accent     = 0.5f;  // 0..1
    float tuning_st  = 0.f;   // semitones
    float slide_s    = 0.06f; // slide time
    bool  square     = false;
    float pulse_width = 0.5f; // square only: 0.05..0.95
};

/** The TB-303's diode ladder (see the file comment): four one-pole
 *  low-passes at kStagePoles x the cutoff, feedback k around them through a
 *  high-pass, tanh at the input. Steps at the rate given to Init (the voice
 *  runs it at twice the sample rate). */
class DiodeLadder
{
  public:
    /** The poles, as multiples of the cutoff (the roots of Stinchcombe's
     *  denominator; their product is 1). */
    static constexpr float kStagePoles[4] = {0.128f, 1.0382f, 2.3254f, 3.2356f};

    void Init(float rate)
    {
        rate_ = rate;
        Reset();
        SetLoopHighPass(kLoopHighPassHz);
    }

    void Reset()
    {
        for(int i = 0; i < 4; i++)
            s_[i] = 0.f;
        y_ = hp_ = 0.f;
    }

    void SetCutoff(float wc_hz)
    {
        for(int i = 0; i < 4; i++)
        {
            const float f = Clamp(wc_hz * kStagePoles[i], 10.f, 0.45f * rate_);
            const float g = tanf(kPi * f / rate_);
            G_[i]         = g / (1.f + g);
        }
    }

    void SetLoopHighPass(float hz) { hp_coef_ = TauToCoef(1.f / (2.f * kPi * hz), rate_); }

    /** k: loop gain (17 self-oscillates). */
    float Process(float x, float k)
    {
        // Feedback from the last output (a one-step delay).
        hp_ += (y_ - hp_) * hp_coef_;
        const float in = FastTanh(x - k * (y_ - hp_));
        const float a  = Stage(in, G_[0], s_[0]);
        const float b  = Stage(a, G_[1], s_[1]);
        const float c  = Stage(b, G_[2], s_[2]);
        y_             = Stage(c, G_[3], s_[3]);
        return y_;
    }

    static constexpr float kLoopHighPassHz = 20.f;

  private:
    /** One trapezoidal (TPT) one-pole low-pass stage. */
    static float Stage(float x, float G, float& z)
    {
        const float v = G * (x - z);
        const float y = v + z;
        z             = y + v;
        return y;
    }

    float rate_    = 96000.f;
    float s_[4]    = {};
    float G_[4]    = {0.5f, 0.5f, 0.5f, 0.5f};
    float y_       = 0.f;
    float hp_      = 0.f;
    float hp_coef_ = 0.f;
};

class Voice
{
  public:
    /** Resonance at full: this much loop gain (17 would self-oscillate).
     *  To be fitted to a real 303. */
    static constexpr float kMaxLoopGain = 12.2f;
    /** The cutoff knob's frequency to the ladder's cutoff: the resonant
     *  peak lands where the earlier filter's did. */
    static constexpr float kCutoffScale = 1.6f;

    void Init(float sample_rate)
    {
        sr_ = sample_rate;
        ladder_.Init(2.f * sample_rate);
        Reset();
    }

    void Reset()
    {
        phase_ = 0.f;
        ladder_.Reset();
        coef_count_ = 0;
        fenv_ = aenv_ = acc_cap_ = 0.f;
        gate_ = accent_ = sliding_ = false;
    }

    /** slide: arrive legato from the sounding note and glide to this one. */
    void NoteOn(int note, bool accent, bool slide)
    {
        target_ = static_cast<float>(note);
        accent_ = accent;
        if(slide && gate_)
        {
            sliding_ = true; // envelopes carry on
        }
        else
        {
            pitch_   = target_;
            sliding_ = false;
            fenv_    = 1.f;  // instant attack
            amp_on_  = true;
        }
        gate_ = true;
    }

    void NoteOff()
    {
        gate_ = false;
    }

    bool Gate() const { return gate_; }
    bool Sounding() const { return gate_ || aenv_ > 0.0001f; }

    /** Adds n mono samples to out. */
    void Process(const VoiceParams& p, float* out, size_t n)
    {
        // Control rate: once per call (the callers use blocks of 48 or fewer).
        const float glide = sliding_ ? TauToCoef(p.slide_s / 3.f, sr_) : 1.f;
        const float decay = accent_ ? 0.2f : p.decay_s;
        const float fdec  = TauToCoef(decay / 4.6f, sr_);   // ~1 % after `decay`
        const float att   = TauToCoef(0.0015f, sr_);
        const float hold  = TauToCoef(2.5f, sr_);           // slow sag while the gate is held
        const float rel   = TauToCoef(0.004f, sr_);
        // Resonance slows the accent capacitor's drain, as on the 303.
        const float acc_charge = TauToCoef(0.012f, sr_);
        const float acc_drain  = TauToCoef(0.08f + 0.35f * p.resonance, sr_);
        const float k          = kMaxLoopGain * p.resonance;
        const float amp_acc    = accent_ ? 1.f + 0.9f * p.accent : 1.f;

        for(size_t i = 0; i < n; i++)
        {
            pitch_ += (target_ - pitch_) * glide;
            const float inc = MidiToHz(pitch_ + p.tuning_st) / sr_;

            // Envelopes
            fenv_ -= fenv_ * fdec;
            if(gate_)
            {
                if(amp_on_)
                {
                    aenv_ += (1.05f - aenv_) * att;
                    if(aenv_ >= 1.f)
                    {
                        aenv_  = 1.f;
                        amp_on_ = false;
                    }
                }
                else
                    aenv_ -= aenv_ * hold * 0.5f;
            }
            else
                aenv_ -= aenv_ * rel;

            const float acc_in = accent_ ? fenv_ : 0.f;
            acc_cap_ += (acc_in - acc_cap_) * (acc_in > acc_cap_ ? acc_charge : acc_drain);

            // Cutoff, at 2x the sample rate; the four stages' coefficients
            // are worked out every fourth sample (the envelope moves slowly).
            if(coef_count_-- <= 0)
            {
                coef_count_     = 3;
                const float oct = p.env_oct * fenv_ + 3.f * p.accent * acc_cap_;
                ladder_.SetCutoff(p.cutoff_hz * kCutoffScale * FastExp2(oct));
            }

            const float x = Osc(inc, p.square, p.pulse_width);

            // Two filter steps per sample, the input held; the output is
            // their average (a simple decimator).
            float y = 0.f;
            for(int os = 0; os < 2; os++)
            {
                y += ladder_.Process(x, k);
            }
            // Make up some of the level resonance takes (a ladder's gain at
            // low frequencies is 1 / (1 + k)); the rest of the bass loss is
            // the 303's own.
            y *= 0.4f * (1.f + 0.5f * k); // (0.4: headroom for the resonant peaks)

            out[i] += y * aenv_ * amp_acc * 0.5f;
        }
    }

  private:
    /** polyBLEP saw, or a pulse from two offset saws (`pw` = its width). */
    float Osc(float inc, bool square, float pw)
    {
        phase_ += inc;
        if(phase_ >= 1.f)
            phase_ -= 1.f;
        float saw = 2.f * phase_ - 1.f - Blep(phase_, inc);
        if(!square)
            return saw;
        float p2 = phase_ + pw;
        if(p2 >= 1.f)
            p2 -= 1.f;
        const float saw2 = 2.f * p2 - 1.f - Blep(p2, inc);
        // The difference of the saws is a pulse between -2pw and 2 - 2pw,
        // whose average is zero at any width.
        return 0.7f * (saw - saw2);
    }


    static float Blep(float t, float dt)
    {
        if(t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.f;
        }
        if(t > 1.f - dt)
        {
            t = (t - 1.f) / dt;
            return t * t + t + t + 1.f;
        }
        return 0.f;
    }

    float sr_      = 48000.f;
    float phase_   = 0.f;
    float pitch_   = 36.f;
    float target_  = 36.f;
    DiodeLadder ladder_;
    int         coef_count_ = 0;
    float fenv_    = 0.f;
    float aenv_    = 0.f;
    float acc_cap_ = 0.f;
    bool  gate_    = false;
    bool  amp_on_  = false;
    bool  accent_  = false;
    bool  sliding_ = false;
};

constexpr float DiodeLadder::kStagePoles[4];

} // namespace x0x
