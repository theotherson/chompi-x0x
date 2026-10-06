/** @file storage.h
 *  @brief The x0x's files on the SD card, all in /X0X:
 *
 *    patterns.txt   the 16 patterns (format in x0x/pattern.h)
 *    current.txt    knobs, waveform, the selected pattern
 *    options.txt    MIDI options, written with the defaults if missing
 *
 *  Writes go to a temporary file renamed over the old one, so a power cut
 *  never leaves half a file. Main loop only: these block on the card.
 */
#pragma once
#include "fatfs.h"
#include "x0x/machine.h"

namespace chompi
{

class Storage
{
  public:
    /** @return true if the card is there and /X0X is the working folder */
    bool Mount(FatFSInterface& fsi)
    {
        ok_ = f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1) == FR_OK;
        if(ok_ && f_chdir("/X0X") != FR_OK)
        {
            f_mkdir("/X0X");
            ok_ = f_chdir("/X0X") == FR_OK;
        }
        return ok_;
    }

    bool Ok() const { return ok_; }

    /** Everything, at startup (before the audio runs the machine). */
    void Load(x0x::Machine& m)
    {
        if(!ok_)
            return;
        if(Read("patterns.txt"))
            x0x::ReadPatterns(buf_, m.patterns, x0x::kPatterns);
        if(Read("current.txt"))
            x0x::ReadSettings(buf_, m.settings);
        if(Read("options.txt"))
            x0x::ReadOptions(buf_, m.options);
        else
            SaveOptions(m.options);
    }

    /** The patterns as they are now. The copy is taken with interrupts off,
     *  so a step the audio interrupt is writing never saves half-changed. */
    bool SavePatterns(const x0x::Machine& m)
    {
        if(!ok_)
            return false;
        __disable_irq();
        for(int i = 0; i < x0x::kPatterns; i++)
            snapshot_[i] = m.patterns[i];
        __enable_irq();
        const size_t n = x0x::WritePatterns(snapshot_, x0x::kPatterns, buf_, sizeof(buf_));
        return n > 0 && Write("patterns.txt", n);
    }

    bool SaveSettings(const x0x::Machine& m)
    {
        if(!ok_)
            return false;
        __disable_irq();
        const x0x::Settings s = m.settings;
        __enable_irq();
        const size_t n = x0x::WriteSettings(s, buf_, sizeof(buf_));
        return n > 0 && Write("current.txt", n);
    }

  private:
    bool SaveOptions(const x0x::Options& o)
    {
        const size_t n = x0x::WriteOptions(o, buf_, sizeof(buf_));
        return n > 0 && Write("options.txt", n);
    }

    /** Reads a whole file into buf_, NUL-terminated. */
    bool Read(const char* name)
    {
        if(f_open(&file_.fil, name, FA_READ) != FR_OK)
            return false;
        UINT n = 0;
        f_read(&file_.fil, buf_, sizeof(buf_) - 1, &n);
        f_close(&file_.fil);
        buf_[n] = '\0';
        return true;
    }

    bool Write(const char* name, size_t len)
    {
        if(f_open(&file_.fil, "save.tmp", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
            return false;
        UINT n = 0;
        f_write(&file_.fil, buf_, len, &n);
        f_close(&file_.fil);
        if(n != len)
            return false;
        f_unlink(name);
        return f_rename("save.tmp", name) == FR_OK;
    }

    // The SD driver DMAs straight into these and then invalidates the data
    // cache over them in whole 32-byte lines, so each owns its lines (found
    // in POLY). The Storage must be a global: the stack is in DTCM, which the
    // card's DMA cannot reach.
    struct alignas(32) File
    {
        FIL fil;
    };
    alignas(32) char buf_[16384];
    File             file_;
    x0x::Pattern     snapshot_[x0x::kPatterns];
    bool             ok_ = false;
};

} // namespace chompi
