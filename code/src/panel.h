/** @file panel.h
 *  @brief The x0x front panel: which CHOMPI control is which x0x control,
 *  their LEDs, and (for milestone 1) a panel test that lights every control
 *  as you use it.
 *
 *  Layout (see the design doc):
 *    white keys 1-8 + black keys 1-5   pitch keys, C3 to C4
 *    white keys 9-15                   DOWN UP ACCENT SLIDE BACK CLEAR COPY
 *                                      (time mode: DOWN = note, UP = tie,
 *                                      ACCENT = rest)
 *    black keys 6-10                   PITCH TIME PATTERN STEP KEYBOARD modes
 *    PLAY                              run / stop
 *    LOOP                              TAP / NEXT
 *    CHOMPI                            FUNCTION
 *    toggle                            saw / square
 *
 *  Poll() runs in the audio interrupt, once per block, right after the
 *  controls are scanned. Draw() runs from the main loop and only reads.
 *
 *  Key and LED tables are from POLY (sfaber02/chompi-poly, MIT), which
 *  checked them on hardware.
 */
#pragma once
#include "hardware.h"
#include "temp_led_stuff.h"

namespace chompi
{

using Sw = Hardware::SwId;

// ------------------------------------------------------------------ keys

constexpr int kNumPitchKeys = 13;

/** Pitch keys C3..C4, in pitch order. */
static const Sw kPitchKeys[kNumPitchKeys] = {
    Sw::KEY_1,  Sw::KEY_16, Sw::KEY_2, Sw::KEY_17, Sw::KEY_3,  Sw::KEY_4, Sw::KEY_18,
    Sw::KEY_5,  Sw::KEY_19, Sw::KEY_6, Sw::KEY_20, Sw::KEY_7,  Sw::KEY_8,
};

enum class Func : uint8_t
{
    DOWN,   // time mode: NOTE
    UP,     // time mode: TIE
    ACCENT, // time mode: REST
    SLIDE,
    BACK,
    CLEAR,
    COPY,
    COUNT,
};
static const Sw kFuncKeys[static_cast<int>(Func::COUNT)] = {
    Sw::KEY_9, Sw::KEY_10, Sw::KEY_11, Sw::KEY_12, Sw::KEY_13, Sw::KEY_14, Sw::KEY_15,
};

enum class Mode : uint8_t
{
    PITCH,
    TIME,
    PATTERN,
    STEP,
    KEYBOARD,
    COUNT,
};
static const Sw kModeKeys[static_cast<int>(Mode::COUNT)] = {
    Sw::KEY_21, Sw::KEY_22, Sw::KEY_23, Sw::KEY_24, Sw::KEY_25,
};

constexpr Sw kKeyPlay     = Sw::KEY_27;
constexpr Sw kKeyTap      = Sw::KEY_28; // LOOP
constexpr Sw kKeyFunction = Sw::KEY_26; // CHOMPI

/** Keybed LED (SMT chain index) for each switch id; -1 = not a keybed key. */
static const int8_t kKeyLed[40] = {
    -1, -1, -1, -1, -1, -1, -1, 0,  // ENC1-4 SW, NC6, KEY_26, SW_TOG, KEY_16
    23, 22, 21, 20, 1,  2,  3,  24, // KEY_2..5, 17, 18, 19, KEY_1
    19, 18, 17, 16, 15, 4,  5,  6,  // KEY_6..10, 20, 21, 22
    14, 13, 12, 11, 10, 7,  8,  9,  // KEY_11..15, 23, 24, 25
    -1, -1, -1, -1, -1, -1, -1, -1, // ENC6 SW, KEY_27, KEY_28, NC
};

// ------------------------------------------------------------------ knobs

/** Knobs left to right: knobs 1-4, the big purple knob, volume. */
enum class Knob : uint8_t
{
    K1,  // resonance (FUNCTION: tuning)
    K2,  // env mod (FUNCTION: swing)
    K3,  // decay (FUNCTION: drive)
    K4,  // accent (FUNCTION: slide time)
    BIG, // cutoff (FUNCTION: pattern length)
    VOL, // volume (FUNCTION: tempo)
    COUNT,
};
constexpr int kNumKnobs = static_cast<int>(Knob::COUNT);

/** Physical encoder -> knob, and each knob's LED(s) and click switch. */
static const uint8_t kEncoderKnob[kNumKnobs] = {1, 2, 3, 0, 4, 5};
static const uint8_t kKnobLed[kNumKnobs]     = {1, 2, 3, 4, 5, 9};
static const uint8_t kKnobLed2[kNumKnobs]    = {1, 2, 3, 4, 6, 9}; // the big knob has two
static const Sw      kKnobClick[kNumKnobs]   = {Sw::ENC_4_SW, Sw::ENC_1_SW, Sw::ENC_2_SW,
                                              Sw::ENC_3_SW, Sw::NC_6, Sw::ENC_6_SW};
// (the big knob's click is not on the shift register: read from enc[4])

constexpr int kLedFunction = 0; // CHOMPI
constexpr int kLedPlay     = 7;
constexpr int kLedTap      = 8; // LOOP

// ------------------------------------------------------------------ colours

struct Rgb
{
    float r, g, b;
};
constexpr Rgb kPitchColour = {0.f, .35f, 1.f};  // blue
constexpr Rgb kFuncColour  = {1.f, .35f, 0.f};  // orange
constexpr Rgb kModeColour  = {0.f, 1.f, .45f};  // green
constexpr Rgb kSawColour   = {1.f, .55f, 0.f};  // amber
constexpr Rgb kSqrColour   = {0.f, .8f, 1.f};   // cyan

// ------------------------------------------------------------------ panel test

/** Milestone 1: proves every control reads and every LED lights. Keys glow
 *  dim in their group's colour and bright while held; knobs show their value
 *  as brightness (click resets to half); PLAY, LOOP and CHOMPI light while
 *  held; flipping the toggle flashes the keybed amber (saw) or cyan (square).
 *  CHOMPI's LED is green if the SD card mounted and wrote, red if not. */
class PanelTest
{
  public:
    void Init(Hardware* hw, bool card_ok)
    {
        hw_      = hw;
        card_ok_ = card_ok;
        for(int k = 0; k < kNumKnobs; k++)
            knob_[k] = 0.5f;
        tog_ = hw_->GetToggleState();
    }

    void Poll()
    {
        const uint32_t now = System::GetNow();
        auto&          sr  = hw_->button_sr;

        for(int k = 0; k < kNumKnobs; k++)
        {
            const bool clicked = k == static_cast<int>(Knob::BIG) ? hw_->enc[4].RisingEdge()
                                                                  : Pressed(kKnobClick[k]);
            if(clicked)
                knob_[k] = 0.5f;
        }
        for(int e = 0; e < kNumKnobs; e++)
        {
            const int inc = hw_->enc[e].Increment();
            if(inc)
            {
                float& v = knob_[kEncoderKnob[e]];
                v        = v + inc * 0.02f;
                v        = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
            }
        }

        for(int i = 0; i < 40; i++)
            held_[i] = sr.State(i);

        const bool tog = hw_->GetToggleState();
        if(tog != tog_)
        {
            tog_         = tog;
            tog_flash_at_ = now;
        }
    }

    void Draw()
    {
        const uint32_t now = System::GetNow();

        // Keybed: group colour, dim at rest, full while held.
        for(int i = 0; i < kNumPitchKeys; i++)
            KeyLed(kPitchKeys[i], kPitchColour);
        for(int i = 0; i < static_cast<int>(Func::COUNT); i++)
            KeyLed(kFuncKeys[i], kFuncColour);
        for(int i = 0; i < static_cast<int>(Mode::COUNT); i++)
            KeyLed(kModeKeys[i], kModeColour);

        // Toggle flip: the whole keybed flashes its waveform's colour.
        if(now - tog_flash_at_ < 400)
        {
            const Rgb   c = tog_ ? kSqrColour : kSawColour;
            const float b = 1.f - (now - tog_flash_at_) / 400.f;
            for(int i = 0; i < 25; i++)
                SetSmtLedFloat(i, c.r * b, c.g * b, c.b * b);
        }

        // Knobs: white, brightness = value.
        for(int k = 0; k < kNumKnobs; k++)
        {
            const float b = 0.05f + 0.95f * knob_[k];
            SetPthLedFloat(kKnobLed[k], b, b, b);
            SetPthLedFloat(kKnobLed2[k], b, b, b);
        }

        const bool fn = held_[static_cast<int>(kKeyFunction)];
        if(fn)
            SetPthLedFloat(kLedFunction, 1.f, 1.f, 1.f);
        else if(card_ok_)
            SetPthLedFloat(kLedFunction, 0.f, 0.6f, 0.f);
        else
            SetPthLedFloat(kLedFunction, 0.8f, 0.f, 0.f);
        const float play = held_[static_cast<int>(kKeyPlay)] ? 1.f : 0.f;
        const float tap  = held_[static_cast<int>(kKeyTap)] ? 1.f : 0.f;
        SetPthLedFloat(kLedPlay, play, play * .85f, 0.f);
        SetPthLedFloat(kLedTap, tap, 0.f, 0.f);

        fill_led_data();
    }

  private:
    void KeyLed(Sw sw, const Rgb& c)
    {
        const int   i = static_cast<int>(sw);
        const float b = held_[i] ? 1.f : 0.08f;
        SetSmtLedFloat(kKeyLed[i], c.r * b, c.g * b, c.b * b);
    }

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

    Hardware* hw_      = nullptr;
    bool      card_ok_ = false;
    bool      held_[40] = {};
    float     knob_[kNumKnobs];
    bool      tog_          = false;
    uint32_t  tog_flash_at_ = 0;
};

} // namespace chompi
