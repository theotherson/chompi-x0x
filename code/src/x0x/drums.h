/** @file drums.h
 *  @brief A TR-606-style drum machine's voices, built as the 606 builds them
 *  (Roland's service notes, block diagram):
 *
 *   - BD: the trigger rings two resonators (twin-T), mixed: a ~62 Hz body
 *     and a short, quieter one about an octave up.
 *   - SD: one resonator (~217 Hz) and an attack click, plus white noise
 *     through its own envelope and a high-pass: by 80 ms it's mostly noise.
 *   - LT / HT: a resonator each (~165 / ~216 Hz) with an attack click; the
 *     low tom's pitch sinks a little as it rings.
 *   - CY / OH / CH: six square waves (~245-627 Hz) as a "metal" source,
 *     through a bandpass at ~7.1 kHz (and, for the cymbal, a second at
 *     ~3.4 kHz), each through a VCA on an envelope, then high-passed. Open
 *     and closed hats share one VCA: a closed hat chokes an open one.
 *   - Accent hits every voice harder: up to 3x as loud, the same shape.
 *
 *  Fitted to clean single hits from six 606 sample kits (the consensus
 *  where units differ: the BD is 54-62 Hz across them).
 *
 *  Per voice: level, attack (the click / snap), decay. Plain C++, no
 *  libDaisy, so the desktop can test and render it.
 */
#pragma once
#include "dsp.h"
#include <cstdint>

namespace x0x
{

enum Drum : uint8_t
{
    BD,
    SD,
    LT,
    HT,
    CY,
    OH,
    CH,
    NUM_DRUMS
};

/** Each voice's knobs, 0..1 (0.5 = the 606's own). */
struct DrumParams
{
    float level  = 0.75f;
    float attack = 0.5f; // the click / snap
    float decay  = 0.5f;
};

/** A resonator, pinged: a two-pole filter whose impulse response is a
 *  decaying sine of amplitude `amp` (the 606's twin-T, rung by a pulse). */
class Resonator
{
  public:
    void Set(float hz, float tau_s, float sr)
    {
        w_ = 2.f * kPi * hz / sr;
        r_ = expf(-1.f / (tau_s * sr));
        a1_ = 2.f * r_ * cosf(w_);
        a2_ = -r_ * r_;
    }
    /** Re-tune while ringing (the decay unchanged). */
    void Retune(float hz, float sr)
    {
        w_  = 2.f * kPi * hz / sr;
        a1_ = 2.f * r_ * cosf(w_);
    }
    void Ping(float amp) { y1_ += amp * sinf(w_); } // starts the sine at `amp`
    float Process()
    {
        const float y = a1_ * y1_ + a2_ * y2_;
        y2_ = y1_, y1_ = y;
        return y;
    }
    void Reset() { y1_ = y2_ = 0.f; }

  private:
    float w_ = 0.f, r_ = 0.f, a1_ = 0.f, a2_ = 0.f, y1_ = 0.f, y2_ = 0.f;
};

/** A state-variable filter (Chamberlin, 2x per sample for stability up
 *  high): low, band and high outputs. */
class Svf
{
  public:
    void Set(float hz, float q, float sr)
    {
        f_ = 2.f * sinf(kPi * Clamp(hz, 10.f, sr * 0.2f) / (2.f * sr));
        q_ = 1.f / q;
    }
    void Process(float x)
    {
        for(int i = 0; i < 2; i++)
        {
            low_ += f_ * band_;
            high_ = x - low_ - q_ * band_;
            band_ += f_ * high_;
        }
    }
    float Low() const { return low_; }
    float Band() const { return band_; }
    float High() const { return high_; }

  private:
    float f_ = 0.1f, q_ = 1.f, low_ = 0.f, band_ = 0.f, high_ = 0.f;
};

class Drums
{
  public:
    void Init(float sample_rate)
    {
        *this = Drums{}; // nothing left ringing
        sr_   = sample_rate;
        for(int i = 0; i < 6; i++)
            metal_ph_[i] = 0.17f * i;
        hat_bp_.Set(7100.f, 1.6f, sr_);
        cy_bp_lo_.Set(3440.f, 1.6f, sr_);
        cy_bp_hi_.Set(7100.f, 1.6f, sr_);
        hat_hp_.Set(6000.f, 1.0f, sr_);
        hat_hp2_.Set(6000.f, 0.8f, sr_);
        cy_hp_hi2_.Set(6000.f, 0.8f, sr_);
        cy_hp_lo_.Set(3000.f, 0.7f, sr_);
        cy_hp_hi_.Set(6000.f, 1.0f, sr_);
        sd_hp_.Set(1400.f, 0.9f, sr_);
        sd_lp_.Set(5000.f, 0.7f, sr_);
        tom_lp_.Set(1000.f, 0.7f, sr_);
        for(int d = 0; d < NUM_DRUMS; d++)
            params_[d] = DrumParams{};
    }

    DrumParams& Params(Drum d) { return params_[d]; }

    /** A hit; accent 0 (none) .. 1 (full, at the accent knob's level);
     *  semitones: played pitched (the live keyboard), 0 = as the 606. */
    void Trigger(Drum d, float accent, int semitones = 0)
    {
        const float pr = semitones ? FastExp2(semitones / 12.f) : 1.f;
        const DrumParams& p = params_[d];
        // Accent raises the trigger voltage: up to 3x as loud (the service
        // notes: 2 Vp-p at accent minimum, 6 at maximum), the shape the same
        // (plain and accented hits of one 606 decay alike).
        const float       hit = 1.f + 2.f * Clamp(accent, 0.f, 1.f);
        const float       dk  = DecayScale(p.decay);
        const float       mk  = dk;
        const float       ck  = 2.f * p.attack; // click: 0..2
        switch(d)
        {
            case BD:
                bd_body_.Set(60.f * pr, 0.040f * dk, sr_);
                bd_knock_.Set(124.f * pr, 0.007f * dk, sr_);
                bd_body_.Ping(hit);
                bd_knock_.Ping(0.3f * hit);
                bd_click_ = 0.35f * ck * hit;
                break;
            case SD:
                sd_tone_.Set(212.f * pr, 0.024f * dk, sr_);
                sd_tone_.Ping(0.55f * hit);
                sd_noise_env_ = hit;
                sd_noise_tau_ = 0.033f * dk;
                sd_click_     = 0.4f * ck * hit;
                break;
            case LT:
                lt_.Set(176.f * pr, 0.047f * dk, sr_);
                lt_.Ping(hit);
                lt_glide_ = 1.f;
                lt_pitch_ = pr;
                tom_click_ = 0.3f * ck * hit;
                tom_noise_ = 0.06f * hit;
                break;
            case HT:
                ht_.Set(208.f * pr, 0.035f * dk, sr_);
                ht_.Ping(hit);
                tom_click_ = 0.3f * ck * hit;
                tom_noise_ = 0.06f * hit;
                break;
            case CY:
                metal_pitch_ = pr;
                cy_env_fast_ = 0.78f * hit, cy_env_slow_ = 0.22f * hit;
                cy_time_     = 0.f;
                cy_tau_      = mk;
                cy_click_    = 0.3f * ck * hit;
                break;
            case OH:
                metal_pitch_ = pr;
                oh_env_   = hit;
                oh_tau_   = 0.25f * mk;
                oh_time_  = 0.f;
                hat_click_ = 0.3f * ck * hit;
                break;
            case CH:
                metal_pitch_ = pr;
                oh_env_    = 0.f; // the choke: a closed hat cuts the open one
                ch_env_    = hit;
                ch_tau_    = 0.017f * mk;
                ch_time_   = 0.f;
                hat_click_ = 0.3f * ck * hit;
                break;
            default: break;
        }
    }

    /** Adds n samples of all the voices, mixed by their levels, to out. */
    void Process(float* out, size_t n)
    {
        const float lv[NUM_DRUMS] = {
            Level(BD), Level(SD), Level(LT), Level(HT), Level(CY), Level(OH), Level(CH)};
        const float sd_nk   = TauToCoef(sd_noise_tau_, sr_);
        const float ch_k    = TauToCoef(ch_tau_, sr_);
        const float oh_k    = TauToCoef(oh_tau_, sr_);
        const float oh_cut  = TauToCoef(0.25f, sr_); // the 606's shut-off, after ~0.55 s
        const float cy_kf   = TauToCoef(0.015f * cy_tau_, sr_);
        const float cy_ks   = TauToCoef(0.24f * cy_tau_, sr_);
        const float tom_nk  = TauToCoef(0.025f, sr_);
        const float click_k = TauToCoef(0.0008f, sr_);
        const float glide_k = TauToCoef(0.02f, sr_);
        for(size_t i = 0; i < n; i++)
        {
            // Bass drum: the two resonators and a click.
            float bd = bd_body_.Process() + bd_knock_.Process() + bd_click_;
            bd_click_ -= bd_click_ * click_k;

            // Snare: the tone and click, and the noise high-passed.
            const float noise = Noise();
            sd_hp_.Process(noise * sd_noise_env_);
            sd_lp_.Process(sd_hp_.High());
            sd_noise_env_ -= sd_noise_env_ * sd_nk;
            float sd = sd_tone_.Process() + 0.7f * sd_lp_.Low() + sd_click_ * noise;
            sd_click_ -= sd_click_ * click_k;

            // Toms: the low one's pitch sinks as it rings.
            if(lt_glide_ > 0.001f)
            {
                lt_glide_ -= lt_glide_ * glide_k;
                if((i & 15) == 0)
                    lt_.Retune((153.f + 23.f * lt_glide_) * lt_pitch_, sr_);
            }
            // The toms' shared noise: a short, low-passed burst.
            tom_lp_.Process(noise * tom_noise_);
            tom_noise_ -= tom_noise_ * tom_nk;
            const float tclick = tom_click_ * noise + tom_lp_.Low();
            tom_click_ -= tom_click_ * click_k;
            const float lt = lt_.Process() + tclick, ht = ht_.Process() + tclick;

            // The metal: six squares, band-limited (PolyBLEP).
            float metal = 0.f;
            for(int k = 0; k < 6; k++)
            {
                const float inc = kMetalHz[k] * metal_pitch_ / sr_;
                float&      ph  = metal_ph_[k];
                ph += inc;
                if(ph >= 1.f)
                    ph -= 1.f;
                float sq = ph < 0.5f ? 1.f : -1.f;
                sq += Blep(ph, inc) - Blep(fmodf(ph + 0.5f, 1.f), inc);
                metal += sq;
            }
            metal *= 1.f / 6.f;

            // Hats: the upper band through one VCA (open and closed
            // envelopes), then the high-pass.
            hat_bp_.Process(metal);
            oh_time_ += 1.f / sr_;
            oh_env_ -= oh_env_ * (oh_time_ > 0.55f * oh_tau_ / 0.25f ? oh_cut + oh_k : oh_k);
            ch_time_ += 1.f / sr_;
            if(ch_time_ > 0.004f) // a moment's hold
                ch_env_ -= ch_env_ * ch_k;
            const float hat_vca = lv[OH] * oh_env_ + lv[CH] * ch_env_;
            hat_hp_.Process(hat_bp_.Band() * hat_vca + hat_click_ * noise * 0.2f);
            hat_hp2_.Process(hat_hp_.High()); // steep: the squares' low end stays out
            hat_click_ -= hat_click_ * click_k;

            // Cymbal: both bands, one envelope (a fast part and a tail).
            cy_bp_lo_.Process(metal);
            cy_bp_hi_.Process(metal);
            const float cy_env = cy_env_fast_ + cy_env_slow_;
            cy_time_ += 1.f / sr_;
            if(cy_time_ > 0.008f) // holds a moment, then falls fast
                cy_env_fast_ -= cy_env_fast_ * cy_kf;
            cy_env_slow_ -= cy_env_slow_ * cy_ks;
            cy_hp_lo_.Process(cy_bp_lo_.Band() * cy_env);
            cy_hp_hi_.Process(cy_bp_hi_.Band() * cy_env + cy_click_ * noise * 0.2f);
            cy_hp_hi2_.Process(cy_hp_hi_.High());
            cy_click_ -= cy_click_ * click_k;
            const float cy = 0.02f * cy_hp_lo_.High() + cy_hp_hi2_.High();

            out[i] += kGain * (lv[BD] * bd + lv[SD] * sd + lv[LT] * lt + lv[HT] * ht + lv[CY] * 1.6f * cy
                               + 1.6f * hat_hp2_.High());
        }
    }

  private:
    static constexpr float kMetalHz[6] = {245.f, 308.f, 367.f, 418.f, 440.f, 627.f};
    static constexpr float kGain       = 0.5f;

    /** Decay knob: 0.25x .. 4x the 606's, 0.5 = as it is. */
    static float DecayScale(float v) { return FastExp2(4.f * (v - 0.5f)); }
    float        Level(Drum d) const { return params_[d].level * params_[d].level * 1.78f; } // 0.75 = 1

    float Noise()
    {
        rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
        return static_cast<float>(static_cast<int32_t>(rng_)) * (1.f / 2147483648.f);
    }

    /** PolyBLEP: smooths a square's step so it doesn't alias. */
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

    float      sr_ = 48000.f;
    DrumParams params_[NUM_DRUMS];
    uint32_t   rng_ = 0x12345678u;

    Resonator bd_body_, bd_knock_, sd_tone_, lt_, ht_;
    float     bd_click_ = 0.f, sd_click_ = 0.f, tom_click_ = 0.f, hat_click_ = 0.f, cy_click_ = 0.f;
    float     sd_noise_env_ = 0.f, sd_noise_tau_ = 0.05f;
    float     lt_glide_ = 0.f, lt_pitch_ = 1.f, metal_pitch_ = 1.f;
    float     tom_noise_ = 0.f;
    Svf       hat_hp2_, cy_hp_hi2_;
    Svf       sd_hp_, sd_lp_, tom_lp_, hat_bp_, hat_hp_, cy_bp_lo_, cy_bp_hi_, cy_hp_lo_, cy_hp_hi_;
    float     metal_ph_[6] = {};
    float     oh_env_ = 0.f, oh_tau_ = 0.25f, oh_time_ = 0.f;
    float     ch_env_ = 0.f, ch_tau_ = 0.01f, ch_time_ = 1.f;
    float     cy_env_fast_ = 0.f, cy_env_slow_ = 0.f, cy_tau_ = 1.f, cy_time_ = 1.f;
};

constexpr float Drums::kMetalHz[6];

} // namespace x0x
