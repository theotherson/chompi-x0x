/** @file voice.h
 *  @brief The 303-style voice: one oscillator (saw, or square), a 3-pole
 *  resonant ladder low-pass, a decay-only filter envelope, a gated amp
 *  envelope, the accent circuit, slide, and a soft drive.
 *
 *  Not a circuit model, but built around the TB-303's behaviour:
 *   - Filter: three one-pole stages with tanh feedback, about 18 dB/octave,
 *     run at 2x the sample rate. A 3-pole loop self-oscillates at a loop gain
 *     of 8; resonance stops short of that.
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
    float drive      = 0.f;   // 0..1
    float slide_s    = 0.06f; // slide time
    bool  square     = false;
};

class Voice
{
  public:
    void Init(float sample_rate)
    {
        sr_ = sample_rate;
        Reset();
    }

    void Reset()
    {
        phase_ = 0.f;
        s1_ = s2_ = s3_ = y3_ = 0.f;
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
        const float k          = 7.2f * p.resonance;       // loop gain; 8 would self-oscillate
        const float amp_acc    = accent_ ? 1.f + 0.9f * p.accent : 1.f;
        const float drive      = 1.f + 5.f * p.drive * p.drive;
        const float drive_norm = 1.f / FastTanh(drive * 0.5f);
        const float sr2        = 2.f * sr_;

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

            // Cutoff, at 2x the sample rate.
            const float oct = p.env_oct * fenv_ + 3.f * p.accent * acc_cap_;
            float       fc  = p.cutoff_hz * FastExp2(oct);
            fc              = Clamp(fc, 20.f, 0.45f * sr2 * 0.5f);
            const float g   = tanf(kPi * fc / sr2);
            const float G   = g / (1.f + g);

            const float x = Osc(inc, p.square);

            // Two filter steps per sample, the input held; the output is
            // their average (a simple decimator).
            float y = 0.f;
            for(int os = 0; os < 2; os++)
            {
                // Feedback from the last output (a one-sample delay at 2x).
                const float in = FastTanh(x - k * y3_);
                const float y1 = Stage(in, G, s1_);
                const float y2 = Stage(y1, G, s2_);
                y3_            = Stage(y2, G, s3_);
                y += y3_;
            }
            y *= 0.5f * (1.f + 0.35f * k); // make up the level resonance takes

            float v = y * aenv_ * amp_acc;
            v       = FastTanh(v * drive * 0.5f) * drive_norm;
            out[i] += v * 0.5f;
        }
    }

  private:
    /** polyBLEP saw, or square from two offset saws. */
    float Osc(float inc, bool square)
    {
        phase_ += inc;
        if(phase_ >= 1.f)
            phase_ -= 1.f;
        float saw = 2.f * phase_ - 1.f - Blep(phase_, inc);
        if(!square)
            return saw;
        float p2 = phase_ + 0.5f;
        if(p2 >= 1.f)
            p2 -= 1.f;
        const float saw2 = 2.f * p2 - 1.f - Blep(p2, inc);
        return 0.5f * (saw - saw2) * 1.4f;
    }

    /** One trapezoidal (TPT) one-pole low-pass stage. */
    static float Stage(float x, float G, float& z)
    {
        const float v = G * (x - z);
        const float y = v + z;
        z             = y + v;
        return y;
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
    float s1_ = 0.f, s2_ = 0.f, s3_ = 0.f, y3_ = 0.f;
    float fenv_    = 0.f;
    float aenv_    = 0.f;
    float acc_cap_ = 0.f;
    bool  gate_    = false;
    bool  amp_on_  = false;
    bool  accent_  = false;
    bool  sliding_ = false;
};

} // namespace x0x
