/** @file storage.h
 *  @brief The x0x's files on the SD card, all in /X0X:
 *
 *    patterns.txt   the 16 patterns (format in x0x/pattern.h)
 *    current.txt    knobs, waveform, the selected pattern
 *    options.txt    MIDI options, written with the defaults if missing
 *    MIDI/01A.mid   each pattern as a MIDI file, when exported (16B.mid the
 *                   last); empty patterns' files are removed
 *    IMPORT/3B.mid  read into that pattern at power-on, then renamed
 *                   3B.done; a file it can't read is renamed 3B.bad and
 *                   changes nothing. Each is renamed 3B.try before it's
 *                   read, so a file can never be read at every power-on
 *
 *  Writes go to a temporary file renamed over the old one, so a power cut
 *  never leaves half a file. Main loop only: these block on the card.
 */
#pragma once
#include "fatfs.h"
#include "x0x/defaults.h"
#include "x0x/machine.h"
#include "x0x/midifile.h"

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
        // A new card (no files yet): the demo set (x0x/defaults.h), saved
        // to the card at once so it's there from then on.
        if(Read("patterns.txt"))
            x0x::ReadPatterns(buf_, m.patterns, x0x::kPatterns);
        else if(UseDefault(x0x::kDefaultPatterns, sizeof(x0x::kDefaultPatterns)))
        {
            for(int i = 0; i < x0x::kPatterns; i++)
                m.patterns[i].Clear();
            x0x::ReadPatterns(buf_, m.patterns, x0x::kPatterns);
            SavePatterns(m);
        }
        if(Read("current.txt"))
            x0x::ReadSettings(buf_, m.settings);
        else if(UseDefault(x0x::kDefaultSettings, sizeof(x0x::kDefaultSettings)))
        {
            x0x::ReadSettings(buf_, m.settings);
            SaveSettings(m);
        }
        if(Read("options.txt"))
            x0x::ReadOptions(buf_, m.options);
        else
            SaveOptions(m.options);
        ImportMidi(m);
    }

    /** MIDI files in /X0X/IMPORT into their patterns (startup only). Saved
     *  at once, write protect or not: an import is meant to stay. */
    void ImportMidi(x0x::Machine& m)
    {
        f_mkdir("IMPORT"); // there to drop files into; fails harmlessly if it's there
        // The names first: renaming while reading the folder could skip some.
        static constexpr int kMax = 2 * x0x::kPatterns;
        char                 names[kMax][16];
        int                  count = 0;
        DIR                  dir;
        FILINFO              info;
        if(f_opendir(&dir, "IMPORT") != FR_OK)
            return;
        while(count < kMax && f_readdir(&dir, &info) == FR_OK && info.fname[0])
            if(!(info.fattrib & AM_DIR) && x0x::ParseImportName(info.fname) >= 0)
            {
                // A name it takes is at most 7 characters ("16B.mid").
                char* to = names[count++];
                for(int c = 0; c < 15 && info.fname[c]; c++)
                    to[c] = info.fname[c], to[c + 1] = '\0';
            }
        f_closedir(&dir);

        for(int i = 0; i < count; i++)
        {
            const int  idx  = x0x::ParseImportName(names[i]);
            const int  num  = x0x::PatternNumber(idx) + 1;
            const char side = x0x::PatternSide(idx) ? 'B' : 'A';
            char       src[32] = "IMPORT/", tried[32], done[32];
            for(int c = 0; c < 15 && names[i][c]; c++)
                src[7 + c] = names[i][c], src[8 + c] = '\0';
            snprintf(tried, sizeof tried, "IMPORT/%d%c.try", num, side);
            f_unlink(tried);
            if(f_rename(src, tried) != FR_OK)
            {
                m.import_failed++;
                continue;
            }
            x0x::Pattern p;
            UINT         n  = 0;
            bool         ok = f_open(&file_.fil, tried, FA_READ) == FR_OK;
            if(ok)
            {
                ok = f_size(&file_.fil) <= sizeof(buf_) && f_read(&file_.fil, buf_, sizeof(buf_), &n) == FR_OK;
                f_close(&file_.fil);
            }
            ok = ok && x0x::ReadMidiFile(reinterpret_cast<const uint8_t*>(buf_), n, p);
            if(ok)
                m.patterns[idx] = p, m.imported++;
            else
                m.import_failed++;
            snprintf(done, sizeof done, "IMPORT/%d%c.%s", num, side, ok ? "done" : "bad");
            f_unlink(done);
            f_rename(tried, done);
        }
        if(m.imported)
            SavePatterns(m);
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

    /** Every pattern as a MIDI file in /X0X/MIDI, as it plays now (tempo,
     *  quantize). @return false if the card failed */
    bool ExportMidi(const x0x::Machine& m)
    {
        if(!ok_)
            return false;
        __disable_irq();
        for(int i = 0; i < x0x::kPatterns; i++)
            snapshot_[i] = m.patterns[i];
        const float bpm   = m.TempoBpmNow();
        const bool  quant = x0x::StepIndex(m.settings.params[x0x::QUANTIZE], 2) == 1;
        const int   grid  = quant ? x0x::QuantGridSteps(m.settings.params[x0x::QUANT_GRID]) : 0;
        const bool  dquant = x0x::StepIndex(m.settings.params[x0x::DRUM_QUANTIZE], 2) == 1;
        __enable_irq();
        f_mkdir("MIDI"); // fails harmlessly if it's there
        bool ok = true;
        for(int i = 0; i < x0x::kPatterns; i++)
        {
            char      path[16], name[16];
            const int num = x0x::PatternNumber(i) + 1;
            const char side = x0x::PatternSide(i) ? 'B' : 'A';
            snprintf(path, sizeof path, "MIDI/%02d%c.mid", num, side);
            if(snapshot_[i].Empty())
            {
                f_unlink(path);
                continue;
            }
            snprintf(name, sizeof name, "x0x %d%c", num, side);
            const size_t n = x0x::WriteMidiFile(snapshot_[i], bpm, grid, name,
                                                reinterpret_cast<uint8_t*>(buf_), sizeof(buf_), dquant);
            UINT wrote = 0;
            if(n == 0 || f_open(&file_.fil, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
            {
                ok = false;
                continue;
            }
            f_write(&file_.fil, buf_, n, &wrote);
            ok &= f_close(&file_.fil) == FR_OK && wrote == n;
        }
        return ok;
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

    /** load.txt: CPU load in percent of the audio's time (now, peak since
     *  power-on) and broken values caught. A diagnostic. */
    bool SaveLoad(uint32_t avg, uint32_t peak, uint32_t bad_out, uint32_t bad_fx)
    {
        if(!ok_)
            return false;
        const int n = snprintf(buf_, sizeof(buf_), "cpu_avg %lu%%\ncpu_peak %lu%%\nbad_out %lu\nbad_fx %lu\n",
                               static_cast<unsigned long>(avg), static_cast<unsigned long>(peak),
                               static_cast<unsigned long>(bad_out), static_cast<unsigned long>(bad_fx));
        return n > 0 && Write("load.txt", static_cast<size_t>(n));
    }

  private:
    bool SaveOptions(const x0x::Options& o)
    {
        const size_t n = x0x::WriteOptions(o, buf_, sizeof(buf_));
        return n > 0 && Write("options.txt", n);
    }

    /** A default (text) into buf_, to parse like a file. */
    bool UseDefault(const char* text, size_t size)
    {
        if(size > sizeof(buf_))
            return false;
        memcpy(buf_, text, size);
        return true;
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
    alignas(32) char buf_[32768]; // 32 patterns: ~14 KB of text, ~23 KB with every hit late and pitched
    File             file_;
    x0x::Pattern     snapshot_[x0x::kPatterns];
    bool             ok_ = false;
};

} // namespace chompi
