/** @file machine.h
 *  @brief The whole instrument without its panel: the patterns, the knob
 *  settings, the sequencer, the voice, live playing and real-time recording,
 *  tap tempo, MIDI clock in and the MIDI out queue.
 *
 *  Threading: everything that changes notes runs in the audio interrupt
 *  (the panel and MIDI input are polled there, before Process()). The main
 *  loop only reads: patterns and settings to save them, the MIDI out queue,
 *  and state for the LEDs.
 */
#pragma once
#include "params.h"
#include "pattern.h"
#include "sequencer.h"
#include "voice.h"

namespace x0x
{

class Machine
{
  public:
    Pattern  patterns[kPatterns];
    Settings settings;
    Options  options;

    /** Bumped on every change, so the main loop can save a few seconds
     *  after the last one. */
    volatile uint32_t pattern_changes  = 0;
    volatile uint32_t settings_changes = 0;

    void Init(float sample_rate)
    {
        sr_ = sample_rate;
        voice_.Init(sample_rate);
        seq_.Init(sample_rate);
        for(int i = 0; i < kPatterns; i++)
            patterns[i].Clear();
        DemoPattern(patterns[0]);
        seq_.SetPattern(&patterns[settings.pattern]);
    }

    /** After patterns and settings were loaded from the card. */
    void Loaded()
    {
        settings.pattern = ClampInt(settings.pattern, 0, kPatterns - 1);
        seq_.SetPattern(&patterns[settings.pattern]);
    }

    // ------------------------------------------------------------ transport

    bool Running() const { return seq_.Running(); }
    void TogglePlay() { Running() ? Stop() : Play(); }

    void Play()
    {
        if(Running())
            return;
        seq_.SetPattern(&patterns[settings.pattern]);
        queued_ = -1;
        seq_.Start();
        if(options.transport_out && !ExternalClock())
            PushMidi(0xFA);
    }

    void Stop()
    {
        if(!Running())
            return;
        seq_.Stop();
        rec_key_ = -1;
        if(options.transport_out && !ExternalClock())
            PushMidi(0xFC);
    }

    /** The step playing now, -1 when stopped. */
    int CurrentStep() const { return seq_.CurrentStep(); }

    /** Counts steps as they play, for the LEDs. */
    uint32_t StepCount() const { return step_count_; }

    // ------------------------------------------------------------ patterns

    int CurrentPattern() const { return settings.pattern; }
    int QueuedPattern() const { return queued_; }
    Pattern& Current() { return patterns[settings.pattern]; }

    /** While running, the new pattern waits for the end of the current one,
     *  unless `now`. */
    void SelectPattern(int i, bool now)
    {
        i = ClampInt(i, 0, kPatterns - 1);
        if(Running() && !now && i != settings.pattern)
        {
            seq_.Queue(&patterns[i]);
            queued_ = i;
            return;
        }
        settings.pattern = i;
        queued_          = -1;
        seq_.SetPattern(&patterns[i]);
        settings_changes++;
    }

    void PatternEdited() { pattern_changes++; }

    void SetTranspose(int st) { seq_.SetTranspose(st); }
    int  Transpose() const { return seq_.Transpose(); }

    // ------------------------------------------------------------ knobs

    void SetParam(int p, float v)
    {
        v                  = Clamp(v, 0.f, 1.f);
        settings.params[p] = v;
        settings_changes++;
        if(options.cc_out && kParams[p].cc)
            PushMidi(0xB0 | (options.channel_out - 1), kParams[p].cc, static_cast<uint8_t>(v * 127.f + 0.5f));
    }

    void SetSquare(bool sq)
    {
        settings.square = sq;
        settings_changes++;
    }

    float TempoBpmNow() const { return ExternalClock() ? ext_bpm_ : TempoBpm(settings.params[TEMPO]); }

    /** Tap tempo: the average of the last few taps, if they came within 2 s. */
    void Tap(uint32_t now_ms)
    {
        if(tap_count_ > 0 && now_ms - last_tap_ms_ < 2000)
        {
            const uint32_t dt = now_ms - last_tap_ms_;
            tap_sum_ += dt;
            tap_n_++;
            if(tap_n_ > 4)
            {
                tap_sum_ = tap_sum_ / tap_n_ * 4;
                tap_n_   = 4;
            }
            SetParam(TEMPO, TempoKnob(60000.f / (static_cast<float>(tap_sum_) / tap_n_)));
        }
        else
        {
            tap_sum_ = 0;
            tap_n_   = 0;
        }
        tap_count_++;
        last_tap_ms_ = now_ms;
    }

    // ------------------------------------------------------------ live notes

    /** A key (or MIDI note) played live: it takes over the voice from the
     *  pattern while held. Overlapping notes slide. While recording with the
     *  pattern running it is written to the nearest step. */
    void LiveNoteOn(int note, bool accent = false)
    {
        note = ClampInt(note, 0, 127);
        for(int i = 0; i < live_count_; i++)
            if(live_[i] == note)
                return;
        if(live_count_ < kLive)
            live_[live_count_++] = note;
        voice_.NoteOn(note, accent, live_count_ > 1);
        voice_note_ = note;
        if(options.notes_out)
            PushMidi(0x90 | (options.channel_out - 1), note, accent ? 127 : 100);
        if(recording_ && Running())
            Record(note);
    }

    void LiveNoteOff(int note)
    {
        int i = 0;
        for(; i < live_count_; i++)
            if(live_[i] == note)
                break;
        if(i == live_count_)
            return;
        for(; i < live_count_ - 1; i++)
            live_[i] = live_[i + 1];
        live_count_--;
        if(live_count_ == 0)
            voice_.NoteOff();
        else
        {
            voice_note_ = live_[live_count_ - 1];
            voice_.NoteOn(voice_note_, false, true);
        }
        if(note == rec_key_)
            rec_key_ = -1;
        if(options.notes_out)
            PushMidi(0x80 | (options.channel_out - 1), note, 0);
    }

    void AllLiveOff()
    {
        while(live_count_ > 0)
            LiveNoteOff(live_[live_count_ - 1]);
    }

    bool Recording() const { return recording_; }
    void SetRecording(bool r)
    {
        recording_ = r;
        if(!r)
            rec_key_ = -1;
    }

    /** The note the voice is playing, -1 if silent. */
    int SoundingNote() const { return voice_.Gate() ? voice_note_ : -1; }

    // ------------------------------------------------------------ MIDI in

    void MidiClock()
    {
        if(!options.clock_in)
            return;
        // Tempo from the spacing of clocks, smoothed.
        if(since_ext_ < static_cast<uint32_t>(sr_) && since_ext_ > 0)
        {
            const float bpm = sr_ * 60.f / (since_ext_ * 24.f);
            ext_bpm_ += (bpm - ext_bpm_) * 0.1f;
        }
        since_ext_ = 0;
        ext_ticks_++;
    }

    void MidiStart()
    {
        if(!options.transport_in)
            return;
        seq_.SetPattern(&patterns[settings.pattern]);
        queued_ = -1;
        seq_.Start();
    }

    void MidiStop()
    {
        if(options.transport_in)
            seq_.Stop();
    }

    void MidiContinue()
    {
        if(options.transport_in)
            seq_.Continue();
    }

    void MidiCc(int cc, int value)
    {
        if(!options.cc_in)
            return;
        for(int i = 0; i < NUM_PARAMS; i++)
        {
            if(kParams[i].cc == cc)
            {
                settings.params[i] = value / 127.f;
                settings_changes++;
                return;
            }
        }
    }

    /** MIDI clock is arriving (within the last half second). */
    bool ExternalClock() const { return options.clock_in && since_ext_ < static_cast<uint32_t>(sr_ / 2); }

    // ------------------------------------------------------------ MIDI out

    struct MidiOut
    {
        uint8_t b[3];
        uint8_t len;
    };

    /** Every message goes to both outputs, each with its own queue: DIN is
     *  sent from the audio interrupt, USB from the main loop. */
    static constexpr int kUart = 0;
    static constexpr int kUsb  = 1;

    /** The next message for output `which`. */
    bool PopMidi(int which, MidiOut& m)
    {
        OutQueue& q = out_[which];
        if(q.tail == q.head)
            return false;
        m      = q.msg[q.tail];
        q.tail = (q.tail + 1) % kOut;
        return true;
    }

    // ------------------------------------------------------------ audio

    /** Renders n samples (n <= 64) to both outputs. */
    void Process(float* left, float* right, size_t n)
    {
        ToVoiceParams(settings.params, settings.square, vp_);
        seq_.SetSwing(settings.params[SWING]);
        const bool ext = ExternalClock();
        seq_.SetTempo(ext ? ext_bpm_ : TempoBpm(settings.params[TEMPO]));
        if(!seq_.Queued())
            queued_ = -1;

        Sequencer::Event ev[Sequencer::kMaxEvents];
        int              count = 0;
        if(ext)
        {
            while(ext_ticks_ > 0 && count < Sequencer::kMaxEvents - 8)
            {
                count += seq_.ExternalTick(0, ev + count);
                ext_ticks_--;
            }
            if(!seq_.Running())
                count += seq_.ExternalTick(0, ev + count); // releases a held note
        }
        else
        {
            ext_ticks_ = 0;
            count      = seq_.Process(n, ev);
        }
        if(since_ext_ < 0x7FFFFFFF)
            since_ext_ += static_cast<uint32_t>(n);

        float  mono[64];
        size_t pos = 0;
        for(size_t i = 0; i < n; i++)
            mono[i] = 0.f;
        for(int i = 0; i < count; i++)
        {
            const size_t at = ev[i].offset < n ? ev[i].offset : n - 1;
            if(at > pos)
            {
                voice_.Process(vp_, mono + pos, at - pos);
                pos = at;
            }
            Handle(ev[i]);
        }
        if(pos < n)
            voice_.Process(vp_, mono + pos, n - pos);

        const float vol = settings.params[VOLUME] * settings.params[VOLUME] * 1.5f;
        for(size_t i = 0; i < n; i++)
            left[i] = right[i] = mono[i] * vol;
    }

  private:
    void Handle(const Sequencer::Event& e)
    {
        const uint8_t ch = options.channel_out - 1;
        switch(e.type)
        {
            case Sequencer::Event::NOTE_ON:
                if(live_count_ == 0)
                {
                    voice_.NoteOn(e.note, e.accent, e.slide);
                    voice_note_ = e.note;
                }
                if(options.notes_out)
                    PushMidi(0x90 | ch, e.note, e.accent ? 127 : 100);
                break;
            case Sequencer::Event::NOTE_OFF:
                if(live_count_ == 0 && e.note == voice_note_)
                    voice_.NoteOff();
                if(options.notes_out)
                    PushMidi(0x80 | ch, e.note, 0);
                break;
            case Sequencer::Event::STEP:
                step_count_++;
                // A key held while recording ties through the steps it covers.
                if(rec_key_ >= 0 && e.step != rec_step_)
                {
                    Step& s = patterns[settings.pattern].steps[e.step];
                    s.on    = true;
                    s.tie   = true;
                    rec_step_ = e.step;
                    pattern_changes++;
                }
                break;
            case Sequencer::Event::TICK:
                if(options.clock_out)
                    PushMidi(0xF8);
                break;
            case Sequencer::Event::PATTERN_CHANGE:
                if(queued_ >= 0)
                    settings.pattern = queued_;
                queued_ = -1;
                settings_changes++;
                break;
        }
    }

    /** Writes a live note to the step it is nearest. */
    void Record(int midi)
    {
        const int step = seq_.StepPhase() < 0.5f || seq_.CurrentStep() < 0 ? seq_.CurrentStep()
                                                                           : seq_.NextStep();
        if(step < 0)
            return;
        int rel = midi - kBaseNote, oct = 0;
        while(rel < 0 && oct > -1)
            rel += 12, oct--;
        while(rel >= kKeyNotes && oct < 1)
            rel -= 12, oct++;
        if(rel < 0 || rel >= kKeyNotes)
            return; // out of range even with the octave flags
        Step& s  = patterns[settings.pattern].steps[step];
        s        = Step{};
        s.note   = static_cast<uint8_t>(rel);
        s.octave = static_cast<int8_t>(oct);
        s.on     = true;
        rec_key_  = midi;
        rec_step_ = step;
        pattern_changes++;
    }

    void PushMidi(uint8_t a, int b = -1, int c = -1)
    {
        MidiOut m;
        m.b[0] = a;
        m.len  = 1;
        if(b >= 0)
        {
            m.b[1] = static_cast<uint8_t>(b & 0x7F);
            m.len  = 2;
        }
        if(c >= 0)
        {
            m.b[2] = static_cast<uint8_t>(c & 0x7F);
            m.len  = 3;
        }
        for(auto& q : out_)
        {
            const int next = (q.head + 1) % kOut;
            if(next == q.tail)
                continue; // full: drop
            q.msg[q.head] = m;
            q.head        = next;
        }
    }

    static constexpr int kLive = 8;
    static constexpr int kOut  = 128;

    float       sr_ = 48000.f;
    Voice       voice_;
    VoiceParams vp_;
    Sequencer   seq_;
    int         queued_     = -1;
    int         voice_note_ = -1;
    uint32_t    step_count_ = 0;

    int  live_[kLive];
    int  live_count_ = 0;
    bool recording_  = false;
    int  rec_key_    = -1;
    int  rec_step_   = -1;

    uint32_t since_ext_ = 0x7FFFFFFF; // samples since the last MIDI clock
    int      ext_ticks_ = 0;
    float    ext_bpm_   = 120.f;

    uint32_t last_tap_ms_ = 0;
    uint32_t tap_sum_     = 0;
    int      tap_n_       = 0;
    int      tap_count_   = 0;

    struct OutQueue
    {
        MidiOut      msg[kOut];
        volatile int head = 0;
        volatile int tail = 0;
    };
    OutQueue out_[2];
};

} // namespace x0x
