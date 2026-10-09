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
    float tune   = 0.5f; // +-24 semitones
    float pan    = 0.5f; // left .. right
    float fm     = 0.f;  // feedback FM: the voice's pitch pushed by its own output
};

/** A resonator, pinged: a two-pole filter whose impulse response is a
 *  decaying sine of amplitude `amp` (the 606's twin-T, rung by a pulse). */
class Resonator
{
  public:
    void Set(float hz, float tau_s, float sr)
    {
        r_ = expf(-1.f / (tau_s * sr));
        a2_ = -r_ * r_;
        Retune(hz, sr);
    }
    /** Re-tune while ringing (the decay unchanged). */
    void Retune(float hz, float sr)
    {
        w_  = 2.f * kPi * Clamp(hz, 10.f, 0.2f * sr) / sr;
        c_  = cosf(w_), s_ = sinf(w_);
        a1_ = 2.f * r_ * c_;
    }
    /** A sample with feedback FM: the frequency times (1 + depth * output),
     *  its coefficient from cos(w + u) to second order in u; bounded. */
    float ProcessFm(float depth)
    {
        const float m  = Clamp(depth * y1_, -0.9f, 4.f);
        const float u  = w_ * m;
        const float a1 = Clamp(2.f * r_ * (c_ * (1.f - 0.5f * u * u) - s_ * u), -2.f * r_, 2.f * r_);
        float       y  = a1 * y1_ + a2_ * y2_;
        y              = Clamp(y, -8.f, 8.f); // a modulated resonator can pump itself up
        y2_ = y1_, y1_ = y;
        return y;
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
    float w_ = 0.f, r_ = 0.f, a1_ = 0.f, a2_ = 0.f, y1_ = 0.f, y2_ = 0.f, c_ = 1.f, s_ = 0.f;
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
        hat_hp_s_.Set(6000.f, 1.0f, sr_);
        hat_hp2_s_.Set(6000.f, 0.8f, sr_);
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
        const DrumParams& p  = params_[d];
        const float       st = semitones + 48.f * (Clamp(p.tune, 0.f, 1.f) - 0.5f); // +-24 from the knob
        const float       pr = fabsf(st) > 0.01f ? FastExp2(st / 12.f) : 1.f;
        const float       fm = 3.f * p.fm * p.fm; // feedback FM depth
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
                bd_fm_    = fm;
                break;
            case SD:
                sd_tone_.Set(212.f * pr, 0.024f * dk, sr_);
                sd_tone_.Ping(0.55f * hit);
                sd_noise_env_ = hit;
                sd_noise_tau_ = 0.033f * dk;
                sd_click_     = 0.4f * ck * hit;
                sd_fm_        = fm;
                break;
            case LT:
                lt_.Set(176.f * pr, 0.047f * dk, sr_);
                lt_.Ping(hit);
                lt_glide_ = 1.f;
                lt_pitch_ = pr;
                tom_click_ = 0.3f * ck * hit;
                click_lt_  = true;
                tom_noise_ = 0.06f * hit;
                lt_fm_     = fm;
                break;
            case HT:
                ht_.Set(208.f * pr, 0.035f * dk, sr_);
                ht_.Ping(hit);
                tom_click_ = 0.3f * ck * hit;
                click_lt_  = false;
                tom_noise_ = 0.06f * hit;
                ht_fm_     = fm;
                break;
            case CY:
                metal_pitch_ = pr, metal_fm_ = fm;
                cy_env_fast_ = 0.78f * hit, cy_env_slow_ = 0.22f * hit;
                cy_time_     = 0.f;
                cy_tau_      = mk;
                cy_click_    = 0.3f * ck * hit;
                break;
            case OH:
                metal_pitch_ = pr, metal_fm_ = fm;
                oh_env_   = hit;
                oh_tau_   = 0.25f * mk;
                oh_time_  = 0.f;
                hat_click_ = 0.3f * ck * hit;
                click_oh_  = true;
                break;
            case CH:
                metal_pitch_ = pr, metal_fm_ = fm;
                oh_env_    = 0.f; // the choke: a closed hat cuts the open one
                ch_env_    = hit;
                ch_tau_    = 0.017f * mk;
                ch_time_   = 0.f;
                hat_click_ = 0.3f * ck * hit;
                click_oh_  = false;
                break;
            default: break;
        }
    }

    /** Adds n samples of all the voices, mixed by their levels, to out. */
    /** Which voices go to the effects' sends (bits, as Drum); the rest stay
     *  out of them. */
    void SetSendMask(uint8_t m) { send_mask_ = m; }

    /** Adds n samples of all the voices, mixed by their levels, to out; and
     *  to send (if given), the voices in the send mask. */
    /** Mono: the left and right, averaged (centred voices as they are). */
    void Process(float* out, size_t n, float* send = nullptr)
    {
        float l[64], r[64];
        for(size_t pos = 0; pos < n; pos += 64)
        {
            const size_t m = n - pos < 64 ? n - pos : 64;
            for(size_t i = 0; i < m; i++)
                l[i] = r[i] = 0.f;
            Process(l, r, m, send ? send + pos : nullptr);
            for(size_t i = 0; i < m; i++)
                out[pos + i] += 0.5f * (l[i] + r[i]);
        }
    }

    /** Stereo: each voice panned (equal power, a centred one at full in
     *  both); the send is mono, unpanned. */
    void Process(float* out_l, float* out_r, size_t n, float* send = nullptr)
    {
        const uint8_t sm      = send_mask_;
        const bool    in[NUM_DRUMS] = {(sm & 1) != 0, (sm & 2) != 0, (sm & 4) != 0, (sm & 8) != 0,
                                       (sm & 16) != 0, (sm & 32) != 0, (sm & 64) != 0};
        const float lv[NUM_DRUMS] = {
            Level(BD), Level(SD), Level(LT), Level(HT), Level(CY), Level(OH), Level(CH)};
        float gl[NUM_DRUMS], gr[NUM_DRUMS];
        for(int v = 0; v < NUM_DRUMS; v++)
        {
            const float pan = Clamp(params_[v].pan, 0.f, 1.f);
            if(fabsf(pan - 0.5f) < 0.002f)
                gl[v] = gr[v] = 1.f;
            else
                gl[v] = 1.41421356f * FastSin2Pi(0.25f - 0.25f * pan), gr[v] = 1.41421356f * FastSin2Pi(0.25f * pan);
        }
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
            float bd = (bd_fm_ > 0.f ? bd_body_.ProcessFm(bd_fm_) + bd_knock_.ProcessFm(bd_fm_)
                                     : bd_body_.Process() + bd_knock_.Process())
                       + bd_click_;
            bd_click_ -= bd_click_ * click_k;

            // Snare: the tone and click, and the noise high-passed.
            const float noise = Noise();
            sd_hp_.Process(noise * sd_noise_env_);
            sd_lp_.Process(sd_hp_.High());
            sd_noise_env_ -= sd_noise_env_ * sd_nk;
            float sd = (sd_fm_ > 0.f ? sd_tone_.ProcessFm(sd_fm_) : sd_tone_.Process()) + 0.7f * sd_lp_.Low()
                       + sd_click_ * noise;
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
            const float lt_r = lt_fm_ > 0.f ? lt_.ProcessFm(lt_fm_) : lt_.Process();
            const float ht_r = ht_fm_ > 0.f ? ht_.ProcessFm(ht_fm_) : ht_.Process();
            // The send: the click and noise with the last tom, if it's in.
            const float t_send = in[click_lt_ ? LT : HT] ? tclick * (lv[LT] + lv[HT]) : 0.f;

            // The metal: six squares, band-limited (PolyBLEP).
            // Feedback FM: their pitch pushed by their own last output.
            const float metal_mod = metal_fm_ > 0.f ? Clamp(1.f + 2.f * metal_fm_ * metal_last_, 0.1f, 4.f) : 1.f;
            float       metal     = 0.f;
            for(int k = 0; k < 6; k++)
            {
                const float inc = kMetalHz[k] * metal_pitch_ * metal_mod / sr_;
                float&      ph  = metal_ph_[k];
                ph += inc;
                if(ph >= 1.f)
                    ph -= 1.f;
                float sq = ph < 0.5f ? 1.f : -1.f;
                sq += Blep(ph, inc) - Blep(fmodf(ph + 0.5f, 1.f), inc);
                metal += sq;
            }
            metal *= 1.f / 6.f;
            metal_last_ = metal;

            // Hats: the upper band through one VCA (open and closed
            // envelopes), then the high-pass.
            hat_bp_.Process(metal);
            oh_time_ += 1.f / sr_;
            oh_env_ -= oh_env_ * (oh_time_ > 0.55f * oh_tau_ / 0.25f ? oh_cut + oh_k : oh_k);
            ch_time_ += 1.f / sr_;
            if(ch_time_ > 0.004f) // a moment's hold
                ch_env_ -= ch_env_ * ch_k;
            // The 606 has one VCA and filter for both; here each has its
            // own filters (they're linear: together, the same sound), so
            // they can be panned and sent apart. The click goes with the
            // hat that made it.
            const float hclick = hat_click_ * noise * 0.2f;
            hat_hp_.Process(hat_bp_.Band() * lv[OH] * oh_env_ + (click_oh_ ? hclick : 0.f));
            hat_hp2_.Process(hat_hp_.High()); // steep: the squares' low end stays out
            hat_hp_s_.Process(hat_bp_.Band() * lv[CH] * ch_env_ + (click_oh_ ? 0.f : hclick));
            hat_hp2_s_.Process(hat_hp_s_.High());
            const float oh       = hat_hp2_.High(), ch = hat_hp2_s_.High();
            const float hat_send = (in[OH] ? oh : 0.f) + (in[CH] ? ch : 0.f);
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

            const int   tv    = click_lt_ ? LT : HT; // the toms' click and noise: the last tom's
            const float t_all = tclick * (lv[LT] + lv[HT]);
            const float vb = lv[BD] * bd, vs = lv[SD] * sd, vl = lv[LT] * lt_r, vh = lv[HT] * ht_r,
                        vc = lv[CY] * 1.6f * cy, vo = 1.6f * oh, vx = 1.6f * ch;
            out_l[i] += kGain * (gl[BD] * vb + gl[SD] * vs + gl[LT] * vl + gl[HT] * vh + gl[tv] * t_all + gl[CY] * vc
                                 + gl[OH] * vo + gl[CH] * vx);
            out_r[i] += kGain * (gr[BD] * vb + gr[SD] * vs + gr[LT] * vl + gr[HT] * vh + gr[tv] * t_all + gr[CY] * vc
                                 + gr[OH] * vo + gr[CH] * vx);
            if(send)
                send[i] += kGain * ((in[BD] ? lv[BD] * bd : 0.f) + (in[SD] ? lv[SD] * sd : 0.f)
                                    + (in[LT] ? lv[LT] * lt_r : 0.f) + (in[HT] ? lv[HT] * ht_r : 0.f) + t_send
                                    + (in[CY] ? lv[CY] * 1.6f * cy : 0.f) + 1.6f * hat_send);
        }
    }

  private:
    static constexpr float kMetalHz[6] = {245.f, 308.f, 367.f, 418.f, 440.f, 627.f};
    static constexpr float kGain       = 0.56f; // the level the drums' distortion is fitted to

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
    float     bd_fm_ = 0.f, sd_fm_ = 0.f, lt_fm_ = 0.f, ht_fm_ = 0.f, metal_fm_ = 0.f, metal_last_ = 0.f;
    float     tom_noise_ = 0.f;
    Svf       hat_hp2_, cy_hp_hi2_, hat_hp_s_, hat_hp2_s_;
    uint8_t   send_mask_ = 0x7f;
    bool      click_oh_  = false; // the hats' click is the open one's
    bool      click_lt_  = false; // the toms' click and noise are the low one's
    Svf       sd_hp_, sd_lp_, tom_lp_, hat_bp_, hat_hp_, cy_bp_lo_, cy_bp_hi_, cy_hp_lo_, cy_hp_hi_;
    float     metal_ph_[6] = {};
    float     oh_env_ = 0.f, oh_tau_ = 0.25f, oh_time_ = 0.f;
    float     ch_env_ = 0.f, ch_tau_ = 0.01f, ch_time_ = 1.f;
    float     cy_env_fast_ = 0.f, cy_env_slow_ = 0.f, cy_tau_ = 1.f, cy_time_ = 1.f;
};

constexpr float Drums::kMetalHz[6];

} // namespace x0x
