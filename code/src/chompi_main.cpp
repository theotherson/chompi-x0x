/** @file chompi_main.cpp
 *  @brief CHOMPI x0x: a TB-303 / x0xb0x-style bass line machine.
 *
 *  Milestone 1 (scaffold): boots, mounts the SD card, reads every key and
 *  knob, lights every LED, outputs silence. No voice or sequencer yet.
 *
 *  Built on CHOMPI Club's TEMPO firmware: the hardware layer, LED driver,
 *  libraries and startup sequence are TEMPO's (MIT).
 *
 *  Two places code runs:
 *   1. AudioCallback(): the audio interrupt. Scans the controls, then
 *      renders audio (silence for now).
 *   2. The main loop: LEDs, the SD card and the battery.
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "fatfs.h"
#include "panel.h"
#include <cstdio>
#include <cstdlib>

using namespace daisy;
using namespace chompi;

Hardware  hw;
PanelTest panel;

SdmmcHandler sdmmc;
// The SD driver DMAs into FatFS's sector buffer inside this and then
// invalidates the data cache in whole 32-byte lines; owning its lines keeps
// that from wiping neighbouring variables (found in POLY).
struct alignas(32) AlignedFs
{
    FatFSInterface fsi;
};
AlignedFs       fs_holder;
FatFSInterface& fsi = fs_holder.fsi;

// File buffers likewise own their cache lines, and are globals: the stack is
// in DTCM, which the SD card's DMA cannot reach.
struct alignas(32) BootFile
{
    char buf[32];
    FIL  fil;
};
BootFile boot_file;

volatile bool running = false; // controls are left to main() until startup is done

/** Channels: out[0..1] headphones, out[2..3] main out. */
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    for(size_t i = 0; i < size; i++)
        out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;

    if(!running)
        return;

    hw.ProcessAllControls();
    panel.Poll();
}

/** Proves the card reads and writes: /X0X/boots.txt counts power-ons.
 *  @return true if the count was written */
static bool CardCheck()
{
    if(f_chdir("/X0X") != FR_OK)
    {
        f_mkdir("/X0X");
        if(f_chdir("/X0X") != FR_OK)
            return false;
    }

    long boots = 0;
    UINT n     = 0;
    if(f_open(&boot_file.fil, "boots.txt", FA_READ) == FR_OK)
    {
        f_read(&boot_file.fil, boot_file.buf, sizeof(boot_file.buf) - 1, &n);
        f_close(&boot_file.fil);
        boot_file.buf[n] = '\0';
        boots            = strtol(boot_file.buf, nullptr, 10);
    }

    const int len = snprintf(boot_file.buf, sizeof(boot_file.buf), "%ld\n", boots + 1);
    if(f_open(&boot_file.fil, "boots.txt", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return false;
    f_write(&boot_file.fil, boot_file.buf, len, &n);
    f_close(&boot_file.fil);
    return static_cast<int>(n) == len;
}

/** A sweep across the white keys in the three panel colours, so you know
 *  which firmware you booted. */
static void BootAnimation()
{
    static const Sw kWhite[15] = {Sw::KEY_1,  Sw::KEY_2,  Sw::KEY_3,  Sw::KEY_4,  Sw::KEY_5,
                                  Sw::KEY_6,  Sw::KEY_7,  Sw::KEY_8,  Sw::KEY_9,  Sw::KEY_10,
                                  Sw::KEY_11, Sw::KEY_12, Sw::KEY_13, Sw::KEY_14, Sw::KEY_15};
    for(int step = 0; step < 40; step++)
    {
        for(int s = 0; s < 15; s++)
        {
            const float d = fabsf(s - step * 0.5f);
            const float b = d < 3.f ? 1.f - d / 3.f : 0.f;
            const Rgb&  c = s < 8 ? kPitchColour : kFuncColour;
            SetSmtLedFloat(kKeyLed[static_cast<int>(kWhite[s])], c.r * b, c.g * b, c.b * b);
        }
        fill_led_data();
        System::Delay(12);
    }
    for(int i = 0; i < 25; i++)
        SetSmtLed(i, 0, 0, 0);
    fill_led_data();
}

int main(void)
{
    hw.Init();

    // Start the codecs' clocks straight away, playing silence: a codec set up
    // but left unclocked while the card is read makes a loud noise (found in
    // POLY). The callback outputs silence until `running`.
    hw.StartAudio(AudioCallback);

    hw.MpWrite(0x0c, 0B01010001); // BATT_LOW to 3 V
    hw.MpReadAll();
    for(size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    // SD card, as TEMPO sets it up.
    System::Delay(100);
    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::VERY_FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    sdmmc.Init(sd_cfg);
    System::Delay(100);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    System::Delay(100);
    const bool card_ok = f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1) == FR_OK && CardCheck();

    LedSetup();
    panel.Init(&hw, card_ok);

    BootAnimation();

    // Get any junk out of the shift registers (0.5 s), and the stock boot
    // combo: CHOMPI + PLAY + LOOP held at power-on = shipping mode.
    uint32_t sleep_state = 0;
    for(int i = 0; i < 5000; i++)
    {
        hw.ProcessAllControls();
        sleep_state += hw.button_sr.State(int(Sw::KEY_26)) && hw.button_sr.State(int(Sw::KEY_27))
                       && hw.button_sr.State(int(Sw::KEY_28));
        System::DelayUs(100);
    }
    if(sleep_state > 4000)
        hw.MpWrite(0x08, 0B10111111); // SHIPPING MODE

    hw.usb_sw.Write(false);       // give USB control
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100); // AutoDPDM
    System::Delay(1);
    hw.usb_sw.Write(true);        // take USB control

    running = true;

    uint32_t last_draw = 0, last_batt = 0;
    while(1)
    {
        const uint32_t now = System::GetNow();
        if(now - last_draw >= 16)
        {
            last_draw = now;
            panel.Draw();
        }
        if(now - last_batt > 20)
        {
            last_batt = now;
            hw.LowBatteryLockoutCheck();
        }
    }
}
