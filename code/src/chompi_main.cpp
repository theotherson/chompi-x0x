/** @file chompi_main.cpp
 *  @brief CHOMPI x0x: a TB-303 / x0xb0x-style bass line machine.
 *
 *  Built on CHOMPI Club's TEMPO firmware: the hardware layer, LED driver,
 *  libraries and startup sequence are TEMPO's (MIT). The instrument itself
 *  (voice, sequencer, panel logic) is in x0x/, plain C++ that also builds and
 *  is tested on the desktop (host/).
 *
 *  Two places code runs:
 *   1. AudioCallback(): the audio interrupt, every block. Scans the controls,
 *      reads MIDI, runs the sequencer and voice, queues MIDI out. Everything
 *      that changes notes happens here, so no locking is needed.
 *   2. The main loop: LEDs, USB MIDI out, the SD card (autosave), battery.
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "fatfs.h"
#include "panel.h"
#include "midi_io.h"
#include "storage.h"
#include "x0x/machine.h"
#include "x0x/ui.h"

using namespace daisy;
using namespace chompi;

Hardware     hw;
x0x::Machine machine;
x0x::Ui      ui;
Panel        panel;
MidiIo       midi;
Storage      storage;

// The delay's memory: 2 s of stereo, in SDRAM.
static constexpr size_t kDelayFrames = 96000;
x0x::Fx::Frame DSY_SDRAM_BSS delay_mem[kDelayFrames];

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

volatile bool running = false; // controls are left to main() until startup is done

/** Channels: out[0..1] headphones, out[2..3] main out. */
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    if(!running)
    {
        for(size_t i = 0; i < size; i++)
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
        return;
    }

    const uint32_t now = System::GetNow();
    hw.ProcessAllControls();
    panel.Poll(now);
    midi.Poll(machine);

    // The machine renders at most 64 samples at a time.
    for(size_t pos = 0; pos < size; pos += 64)
    {
        const size_t n = size - pos < 64 ? size - pos : 64;
        machine.Process(out[0] + pos, out[1] + pos, n);
    }
    for(size_t i = 0; i < size; i++)
    {
        out[2][i] = out[0][i];
        out[3][i] = out[1][i];
    }

    midi.PumpUart(machine);
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

    machine.Init(hw.seed.AudioSampleRate(), delay_mem, kDelayFrames);
    ui.Init(&machine);

    // SD card, as TEMPO sets it up; patterns, settings and MIDI options.
    System::Delay(100);
    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::VERY_FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    sdmmc.Init(sd_cfg);
    System::Delay(100);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    System::Delay(100);
    if(storage.Mount(fsi))
        storage.Load(machine);
    machine.Loaded();

    LedSetup();
    panel.Init(&hw, &ui);
    midi.Init();

    panel.BootAnimation();
    if(!storage.Ok())
        panel.CardError();

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
    // Clear edges collected during the boot wait, so nothing fires at once.
    for(int sw = 0; sw < 40; sw++)
    {
        hw.button_sr.RisingEdge(sw);
        hw.button_sr.FallingEdge(sw);
    }

    hw.usb_sw.Write(false);       // give USB control
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100); // AutoDPDM
    System::Delay(1);
    hw.usb_sw.Write(true);        // take USB control

    running = true;

    uint32_t last_draw = 0, last_batt = 0;
    uint32_t saved_patterns = machine.pattern_changes, saved_settings = machine.settings_changes;
    uint32_t pattern_seen = saved_patterns, settings_seen = saved_settings;
    uint32_t pattern_at = 0, settings_at = 0;
    while(1)
    {
        const uint32_t now = System::GetNow();

        if(now - last_draw >= 16)
        {
            last_draw = now;
            panel.Draw(now);
        }

        midi.PumpUsb(machine);

        // Save a few seconds after the last change, not on every turn.
        if(machine.pattern_changes != pattern_seen)
        {
            pattern_seen = machine.pattern_changes;
            pattern_at   = now;
        }
        if(machine.settings_changes != settings_seen)
        {
            settings_seen = machine.settings_changes;
            settings_at   = now;
        }
        if(pattern_seen != saved_patterns && now - pattern_at > 2000)
        {
            saved_patterns = pattern_seen;
            storage.SavePatterns(machine);
        }
        if(settings_seen != saved_settings && now - settings_at > 3000)
        {
            saved_settings = settings_seen;
            storage.SaveSettings(machine);
        }

        if(now - last_batt > 20)
        {
            last_batt = now;
            hw.LowBatteryLockoutCheck();
        }
    }
}
