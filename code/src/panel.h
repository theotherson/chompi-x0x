/** @file panel.h
 *  @brief The CHOMPI's switches and LEDs, wired to the x0x panel logic
 *  (x0x/ui.h, which says what every control does).
 *
 *  Poll() runs in the audio interrupt, once per block, right after the
 *  controls are scanned. Draw() runs from the main loop and only reads.
 *
 *  Key, LED and knob tables are from POLY (sfaber02/chompi-poly, MIT), which
 *  checked them on hardware.
 */
#pragma once
#include "hardware.h"
#include "temp_led_stuff.h"
#include "x0x/ui.h"

namespace chompi
{

using Sw = Hardware::SwId;

/** Keybed key (0-24, pitch order C3..C5) for each switch id; -1 = not a key. */
static const int8_t kSwKey[40] = {
    -1, -1, -1, -1, -1, -1, -1, 1,  // ENC1-4 SW, NC6, KEY_26, SW_TOG, KEY_16
    2,  4,  5,  7,  3,  6,  8,  0,  // KEY_2..5, 17, 18, 19, KEY_1
    9,  11, 12, 14, 16, 10, 13, 15, // KEY_6..10, 20, 21, 22
    17, 19, 21, 23, 24, 18, 20, 22, // KEY_11..15, 23, 24, 25
    -1, -1, -1, -1, -1, -1, -1, -1, // ENC6 SW, KEY_27, KEY_28, NC
};

/** Keybed LED (SMT chain index) for each switch id. */
static const int8_t kSwLed[40] = {
    -1, -1, -1, -1, -1, -1, -1, 0,
    23, 22, 21, 20, 1,  2,  3,  24,
    19, 18, 17, 16, 15, 4,  5,  6,
    14, 13, 12, 11, 10, 7,  8,  9,
    -1, -1, -1, -1, -1, -1, -1, -1,
};

/** Physical encoder -> knob (0-3 knobs 1-4, 4 the big knob, 5 volume), and
 *  each knob's LED(s) and click switch. */
static const uint8_t kEncoderKnob[6] = {1, 2, 3, 0, 4, 5};
static const uint8_t kKnobLed[6]     = {1, 2, 3, 4, 5, 9};
static const uint8_t kKnobLed2[6]    = {1, 2, 3, 4, 6, 9}; // the big knob has two
static const Sw      kKnobClick[6]   = {Sw::ENC_4_SW, Sw::ENC_1_SW, Sw::ENC_2_SW,
                                      Sw::ENC_3_SW, Sw::NC_6, Sw::ENC_6_SW};
// (the big knob's click is not on the shift register: read from enc[4])

constexpr Sw  kSwChompi = Sw::KEY_26;
constexpr Sw  kSwPlay   = Sw::KEY_27;
constexpr Sw  kSwLoop   = Sw::KEY_28;
constexpr int kLedChompi = 0;
constexpr int kLedPlay   = 7;
constexpr int kLedLoop   = 8;

class Panel
{
  public:
    void Init(Hardware* hw, x0x::Ui* ui)
    {
        hw_ = hw;
        ui_ = ui;
        for(int i = 0; i < 25; i++)
            key_led_[i] = -1;
        for(int sw = 0; sw < 40; sw++)
            if(kSwKey[sw] >= 0)
                key_led_[kSwKey[sw]] = kSwLed[sw];
    }

    void Poll(uint32_t now)
    {
        auto& sr = hw_->button_sr;

        // Toggle up = step mode, down = pitch mode.
        ui_->SetMode(hw_->GetToggleState() ? x0x::Ui::Mode::PITCH : x0x::Ui::Mode::STEP);
        ui_->Chompi(sr.State(static_cast<int>(kSwChompi)));

        for(int sw = 0; sw < 40; sw++)
        {
            const int k = kSwKey[sw];
            if(k < 0)
                continue;
            if(sr.RisingEdge(sw))
                ui_->KeyDown(k, now);
            else if(sr.FallingEdge(sw))
                ui_->KeyUp(k, now);
        }

        if(Pressed(kSwPlay))
            ui_->Play();
        // LOOP: both edges (a tap and a 2 s hold do different things).
        if(sr.RisingEdge(static_cast<int>(kSwLoop)))
            ui_->LoopDown(now);
        else if(sr.FallingEdge(static_cast<int>(kSwLoop)))
            ui_->LoopUp(now);

        // Knob clicks: the big knob's at once (tap tempo); the others when
        // let go, unless turned while pushed (push and turn: Ui::KnobDown).
        if(hw_->enc[4].RisingEdge())
            ui_->KnobClick(4, now);
        for(int k = 0; k < 6; k++)
        {
            if(k == 4)
                continue;
            const int i = static_cast<int>(kKnobClick[k]);
            if(sr.RisingEdge(i))
                ui_->KnobDown(k);
            else if(sr.FallingEdge(i))
                ui_->KnobUp(k, now);
        }
        for(int e = 0; e < 6; e++)
        {
            const int inc = hw_->enc[e].Increment();
            if(inc)
            {
                const int knob  = kEncoderKnob[e];
                const bool fast = now - last_turn_[knob] < 25;
                last_turn_[knob] = now;
                ui_->KnobTurn(knob, inc, fast);
            }
        }
        ui_->Tick(now);
    }

    void Draw(uint32_t now)
    {
        ui_->NoteStep(now);
        x0x::LedFrame f;
        ui_->Draw(f, now);
        for(int k = 0; k < 25; k++)
            SetSmtLedFloat(key_led_[k], f.key[k].r, f.key[k].g, f.key[k].b);
        for(int k = 0; k < 6; k++)
        {
            SetPthLedFloat(kKnobLed[k], f.knob[k].r, f.knob[k].g, f.knob[k].b);
            const x0x::Rgb& c2 = k == 4 ? f.big_right : f.knob[k]; // the big knob has two LEDs
            SetPthLedFloat(kKnobLed2[k], c2.r, c2.g, c2.b);
        }
        SetPthLedFloat(kLedChompi, f.chompi.r, f.chompi.g, f.chompi.b);
        SetPthLedFloat(kLedPlay, f.play.r, f.play.g, f.play.b);
        SetPthLedFloat(kLedLoop, f.loop.r, f.loop.g, f.loop.b);
        fill_led_data();
    }

    /** Lights the keys in a sweep, so you know which firmware you booted. */
    void BootAnimation()
    {
        for(int step = 0; step < 40; step++)
        {
            for(int k = 0; k < 25; k++)
            {
                const float d = fabsf(k - step * 0.7f);
                const float b = d < 4.f ? 1.f - d / 4.f : 0.f;
                SetSmtLedFloat(key_led_[k], b, 0.f, b * 0.15f);
            }
            fill_led_data();
            System::Delay(12);
        }
        for(int k = 0; k < 25; k++)
            SetSmtLed(key_led_[k], 0, 0, 0);
        fill_led_data();
    }

    /** A red flash of the CHOMPI key when the card isn't working. */
    void CardError()
    {
        for(int i = 0; i < 3; i++)
        {
            SetPthLedFloat(kLedChompi, 1.f, 0.f, 0.f);
            fill_led_data();
            System::Delay(150);
            SetPthLedFloat(kLedChompi, 0.f, 0.f, 0.f);
            fill_led_data();
            System::Delay(150);
        }
    }

  private:
    /** A fresh press of a button that isn't a key. libDaisy's 4021 driver
     *  only reports a new rising edge after the falling edge has been read,
     *  so the release is consumed too (POLY found this on hardware). */
    bool Pressed(Sw sw)
    {
        auto&     sr = hw_->button_sr;
        const int i  = static_cast<int>(sw);
        if(sr.RisingEdge(i))
            return true;
        sr.FallingEdge(i);
        return false;
    }

    Hardware* hw_ = nullptr;
    x0x::Ui*  ui_ = nullptr;
    int8_t    key_led_[25];
    uint32_t  last_turn_[6] = {};
};

} // namespace chompi
