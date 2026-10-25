#include "encoder.h"

using namespace chompi;

void ChompiEncoder::Init(dsy_gpio_pin a,
                   dsy_gpio_pin b,
                   dsy_gpio_pin click,
                   float        update_rate)
{
    last_update_ = daisy::System::GetNow();
    updated_     = false;

    // Init GPIO for A, and B
    hw_a_.pin  = a;
    hw_a_.mode = DSY_GPIO_MODE_INPUT;
    hw_a_.pull = DSY_GPIO_PULLUP;
    hw_b_.pin  = b;
    hw_b_.mode = DSY_GPIO_MODE_INPUT;
    hw_b_.pull = DSY_GPIO_PULLUP;
    dsy_gpio_init(&hw_a_);
    dsy_gpio_init(&hw_b_);
    // Default Initialization for Switch
    sw_.Init(click);
    // Set initial states, etc.
    inc_  = 0;
    quad_ = x0x::Quadrature{};
}

void ChompiEncoder::Debounce()
{
    // update no faster than 1kHz
    uint32_t now = daisy::System::GetNow();
    updated_     = false;

    if(now - last_update_ >= 1)
    {
        last_update_ = now;
        updated_     = true;

        inc_ = quad_.Sample(dsy_gpio_read(&hw_a_), dsy_gpio_read(&hw_b_));
    }

    // Debounce built-in switch
    sw_.Debounce();
}

void ChompiEncoder::Debounce(bool a_state, bool b_state)
{
    
    // update no faster than 1kHz
    uint32_t now = daisy::System::GetNow();
    updated_     = false;

    if(now - last_update_ >= 1)
    {
        last_update_ = now;
        updated_     = true;

        inc_ = quad_.Sample(a_state, b_state);
    }

    // Debounce built-in switch
    // sw_.Debounce();
}
