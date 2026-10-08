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
#include "arp.h"
#include "drums.h"
#include "fx.h"
#include "params.h"
#include "pattern.h"
#include "sequencer.h"
#include "voice.h"

namespace x0x
{

static_assert(kParams[DELAY_TIME].steps == kDelayDivisions, "delay_time's positions are the delay's divisions");

class Machine
{
  public:
    Pattern  patterns[kPatterns];
    Settings settings;
    Options  options;

    /** The filter's character, fixed in the firmware (set here so the
     *  desktop can compare versions). */
    float max_loop_gain = VoiceParams{}.max_loop_gain;
    float stage_drive   = VoiceParams{}.stage_drive;
    float bass_makeup   = VoiceParams{}.bass_makeup;
    FilterFit filter_fit;
    float post_hp_hz    = VoiceParams{}.post_hp_hz;

    /** Bumped on every change, so the main loop can save a few seconds
     *  after the last one. */
    volatile uint32_t pattern_changes  = 0;
    volatile uint32_t settings_changes = 0;

    /** delay_mem: the delay's memory (2 s = 96000 frames at 48 kHz); on the
     *  hardware it lives in SDRAM. */
    void Init(float sample_rate, Fx::Frame* delay_mem, size_t delay_frames)
    {
        sr_ = sample_rate;
        voice_.Init(sample_rate);
        drums_.Init(sample_rate);
        fx_.Init(sample_rate, delay_mem, delay_frames);
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
        queued_   = -1;
        drum_pos_ = -1; // the first step is the drums' first
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

    /** Counts notes recorded in real time (keys or the arpeggiator), for
     *  the LEDs. */
    uint32_t RecordCount() const { return record_count_; }

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

    /** MIDI file export: the panel asks, the main loop writes the files (the
     *  card blocks) and reports back. */
    volatile uint32_t export_requests = 0;
    volatile uint32_t exports_done    = 0;
    volatile bool     export_ok       = false;
    void RequestExport() { export_requests++; }

    /** MIDI files imported at power-on (read once by the panel, to flash). */
    int imported      = 0;
    int import_failed = 0;
    void ExportDone(bool ok)
    {
        export_ok = ok;
        exports_done++;
    }

    /** Write protect: the patterns can still be edited, but the edits
     *  aren't saved to the card (the main loop checks this), so the next
     *  start-up loads them as they were. Turning it off keeps what's in
     *  memory, which then saves as usual. */
    bool Protected() const { return settings.protect; }
    void SetProtected(bool on)
    {
        settings.protect = on;
        settings_changes++;
    }

    /** Quantize for good: every recorded note moves to where it plays with
     *  quantize on at this grid (Pattern::PlayedStep), its timing dropped. */
    void QuantizePattern(int grid_steps)
    {
        Pattern&      p   = Current();
        const Pattern was = p;
        for(int i = 0; i < p.length; i++)
            p.steps[i] = was.PlayedStep(i, grid_steps);
        PatternEdited();
    }

    // ------------------------------------------------------------ drums

    /** A drum hit from the panel (main loop): played at the start of the
     *  next audio block, and recorded into the drum part when recording
     *  with the pattern running (to the nearest step). */
    void DrumHit(int voice, bool accent)
    {
        const int next = (hit_head_ + 1) % kHitQueue;
        if(next == hit_tail_ || voice < 0 || voice >= kDrumVoices)
            return; // full: dropped
        hits_[hit_head_] = static_cast<uint8_t>(voice | (accent ? kDrumAccent : 0));
        hit_head_        = next;
    }

    /** The drum part's step playing now (its own length), -1 when stopped. */
    int CurrentDrumStep() const { return Running() ? drum_pos_ : -1; }

    void SetDrumLength(int len)
    {
        Current().drum_length = static_cast<uint8_t>(ClampInt(len, 1, kSteps));
        pattern_changes++;
    }

    /** Counts drum hits as they play (pattern or live), for the LEDs. */
    uint32_t DrumHitCount(int voice) const { return drum_hit_count_[voice]; }

    /** Clears this pattern's bassline (its drum part stays). */
    void ClearPattern()
    {
        Current().ClearBass();
        pattern_changes++;
    }

    void SetTranspose(int st) { seq_.SetTranspose(st); }
    int  Transpose() const { return seq_.Transpose(); }

    // ------------------------------------------------------------ knobs

    void SetParam(int p, float v)
    {
        v = Clamp(v, 0.f, 1.f);
        if(kParams[p].steps)
            v = StepValue(StepIndex(v, kParams[p].steps), kParams[p].steps);
        settings.params[p] = v;
        settings_changes++;
        if(options.cc_out && kParams[p].cc)
            PushMidi(0xB0 | (options.channel_out - 1), kParams[p].cc, static_cast<uint8_t>(v * 127.f + 0.5f));
    }

    /** The current pattern's length, 1-16. */
    void SetLength(int n)
    {
        Current().length = static_cast<uint8_t>(ClampInt(n, 1, kSteps));
        pattern_changes++;
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

    // ------------------------------------------------------------ arpeggiator

    bool ArpOn() const { return StepIndex(settings.params[ARP_ON], 2) == 1; }

    void SetArpOn(bool on)
    {
        SetParam(ARP_ON, on ? 1.f : 0.f);
        if(!on)
        {
            arp_.Clear();
            ArpGateOff();
        }
    }

    bool ArpLatch() const { return arp_.Latch(); }

    /** The arpeggio's own transpose, separate from the pattern's. */
    void SetArpTranspose(int st) { arp_transpose_ = ClampInt(st, -24, 24); }
    int  ArpTranspose() const { return arp_transpose_; }

    /** For the LEDs: the chord, and a count of the notes played, with the
     *  held note the last one came from. */
    const Arp& GetArp() const { return arp_; }
    uint32_t   ArpNoteCount() const { return arp_count_; }
    int        ArpLastSource() const { return arp_last_source_; }
    void SetArpLatch(bool on) { arp_.SetLatch(on); }

    /** Live notes go to the arpeggiator when it's on, except in note entry
     *  (record armed, stopped), where each key is a step. */
    bool ArpEngaged() const { return ArpOn() && !(recording_ && !Running()); }

    // ------------------------------------------------------------ live notes

    /** A key (or MIDI note) played live: it takes over the voice from the
     *  pattern while held. Overlapping notes slide. While recording with the
     *  pattern running it is written to the nearest step. With the
     *  arpeggiator on, it joins the chord the arpeggiator plays instead. */
    void LiveNoteOn(int note, bool accent = false)
    {
        note = ClampInt(note, 0, 127);
        if(ArpEngaged())
        {
            const bool was = arp_.Active();
            arp_.NoteOn(note);
            if(!was && !Running())
                arp_clock_ = 0.0; // start straight away
            return;
        }
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
        if(ArpEngaged())
        {
            arp_.NoteOff(note);
            return;
        }
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

    /** The step a note played now would land on (the nearest sixteenth),
     *  -1 when stopped. */
    int NearestStep() const
    {
        const int cur = seq_.CurrentStep();
        if(cur < 0)
            return -1;
        return seq_.StepPhase() < 0.5f ? cur : seq_.NextStep();
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
        queued_   = -1;
        drum_pos_ = -1; // the first step is the drums' first
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
                const int steps    = kParams[i].steps;
                const float v      = value / 127.f;
                settings.params[i] = steps ? StepValue(StepIndex(v, steps), steps) : v;
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
        ToVoiceParams(settings.params, vp_);
        vp_.max_loop_gain = max_loop_gain;
        vp_.stage_drive   = stage_drive;
        vp_.bass_makeup   = bass_makeup;
        vp_.post_hp_hz    = post_hp_hz;
        FilterToVoice(settings.params[CUTOFF], settings.params[ENV_MOD], filter_fit, vp_);
        seq_.SetSwing(settings.params[SWING]);
        seq_.SetQuantize(StepIndex(settings.params[QUANTIZE], 2) == 1 ? QuantGridSteps(settings.params[QUANT_GRID]) : 0);
        const bool ext = ExternalClock();
        seq_.SetTempo(ext ? ext_bpm_ : TempoBpm(settings.params[TEMPO]));
        const float* p = settings.params;
        Fx::Settings fs;
        fs.drive      = p[DRIVE];
        fs.crush_bits = p[CRUSH];
        fs.crush_rate = p[CRUSH_RATE];
        fs.mod        = p[MOD];
        fs.mod_width  = p[MOD_WIDTH];
        fs.dly_mix    = p[DELAY];
        fs.dly_div    = StepIndex(p[DELAY_TIME], kDelayDivisions);
        fs.dly_free   = StepIndex(p[DELAY_FREE_ON], 2) == 1;
        fs.dly_free_ms = DelayFreeMs(p[DELAY_FREE]);
        fs.dly_fb     = p[DELAY_FB];
        fs.dly_tone   = p[DELAY_TONE];
        fs.bpm        = seq_.Tempo();
        fx_.Set(fs);
        if(!seq_.Queued())
            queued_ = -1;
        for(int v = 0; v < kDrumVoices; v++)
        {
            DrumParams& dp = drums_.Params(static_cast<Drum>(v));
            dp.level       = p[DRUM_PARAMS + 3 * v];
            dp.attack      = p[DRUM_PARAMS + 3 * v + 1];
            dp.decay       = p[DRUM_PARAMS + 3 * v + 2];
        }
        float drum[64];
        for(size_t i = 0; i < n; i++)
            drum[i] = 0.f;
        // Live drum hits from the panel.
        while(hit_tail_ != hit_head_)
        {
            const uint8_t h = hits_[hit_tail_];
            hit_tail_       = (hit_tail_ + 1) % kHitQueue;
            const int v     = h & 0x7f;
            PlayDrum(v, (h & kDrumAccent) != 0);
            if(Recording() && Running())
                RecordDrum(v, (h & kDrumAccent) != 0);
        }

        Sequencer::Event ev[Sequencer::kMaxEvents + 4];
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

        // The arpeggiator: its gate ends half a step after it starts; while
        // the pattern runs its steps come with the pattern's, otherwise from
        // its own clock at the tempo.
        arp_.SetMode(static_cast<ArpMode>(StepIndex(p[ARP_MODE], static_cast<int>(ArpMode::COUNT))));
        arp_.SetOctaves(StepIndex(p[ARP_OCT_DOWN], 3), StepIndex(p[ARP_OCT_UP], 3));
        arp_step_samples_ = sr_ * 60.f / (seq_.Tempo() * 4.f);
        if(arp_gate_left_ >= 0.0 && arp_gate_left_ < n)
            ev[count++] = ArpEvent(Sequencer::Event::NOTE_OFF, arp_gate_left_);
        if(!Running() && ArpEngaged() && arp_.Active() && arp_clock_ < n)
        {
            ev[count++] = ArpEvent(Sequencer::Event::NOTE_ON, arp_clock_);
            arp_clock_ += arp_step_samples_;
        }
        SortByOffset(ev, count);

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
                drums_.Process(drum + pos, at - pos);
                pos = at;
            }
            Handle(ev[i]);
        }
        if(pos < n)
        {
            voice_.Process(vp_, mono + pos, n - pos);
            drums_.Process(drum + pos, n - pos);
        }
        if(arp_gate_left_ >= 0.0)
            arp_gate_left_ -= n;
        if(arp_clock_ > 0.0)
            arp_clock_ -= n;

        fx_.Process(mono, left, right, n);
        // The drums join after the bass's effects (their own come later).
        const float vol = settings.params[VOLUME] * settings.params[VOLUME] * 1.5f;
        for(size_t i = 0; i < n; i++)
        {
            left[i]  = SoftLimit((left[i] + drum[i]) * vol);
            right[i] = SoftLimit((right[i] + drum[i]) * vol);
        }
    }

  private:
    static constexpr int kArpEvent = -2; // Event::step of an arpeggiator event
    static constexpr int kHitQueue = 16;

    /** GM drum notes, for MIDI out (channel 10). */
    static constexpr uint8_t kDrumMidi[kDrumVoices] = {36, 38, 45, 50, 49, 46, 42};

    void PlayDrum(int v, bool accent)
    {
        drums_.Trigger(static_cast<Drum>(v), accent ? settings.params[DRUM_ACCENT] : 0.f);
        drum_hit_count_[v]++;
        if(options.notes_out)
        {
            PushMidi(0x99, kDrumMidi[v], accent ? 127 : 100);
            PushMidi(0x89, kDrumMidi[v], 0);
        }
    }

    /** A step of the pattern: the drum part's next step (its own length). */
    void DrumStep()
    {
        const Pattern& pat = Current();
        drum_pos_          = (drum_pos_ + 1) % ClampInt(pat.drum_length, 1, kSteps);
        const uint8_t s    = pat.drums[drum_pos_];
        for(int v = 0; v < kDrumVoices; v++)
            if((s >> v) & 1)
                PlayDrum(v, (s & kDrumAccent) != 0);
    }

    /** A live hit into the drum part: the step playing, or the next one if
     *  it's more than halfway through. */
    void RecordDrum(int v, bool accent)
    {
        if(drum_pos_ < 0)
            return;
        Pattern&  pat  = Current();
        const int len  = ClampInt(pat.drum_length, 1, kSteps);
        int       step = drum_pos_;
        if(seq_.StepPhase() > 0.5f)
            step = (step + 1) % len;
        pat.drums[step] = static_cast<uint8_t>(pat.drums[step] | (1 << v) | (accent ? kDrumAccent : 0));
        pattern_changes++;
        record_count_++;
    }

    Sequencer::Event ArpEvent(Sequencer::Event::Type type, double offset) const
    {
        Sequencer::Event e{type, static_cast<uint32_t>(offset > 0.0 ? offset : 0.0)};
        e.step = kArpEvent;
        return e;
    }

    /** Stable, by sample offset (the lists are a handful long). */
    static void SortByOffset(Sequencer::Event* ev, int count)
    {
        for(int i = 1; i < count; i++)
            for(int j = i; j > 0 && ev[j].offset < ev[j - 1].offset; j--)
            {
                const Sequencer::Event t = ev[j];
                ev[j]                    = ev[j - 1];
                ev[j - 1]                = t;
            }
    }

    /** The pattern is silent while live keys or the arpeggiator play. */
    bool LiveSounding() const { return live_count_ > 0 || (ArpEngaged() && (arp_.Active() || arp_note_ >= 0)); }

    /** The arpeggiator's next note; recorded into `step` (when >= 0) while
     *  recording with the pattern running. */
    void ArpStep(uint32_t offset, int step)
    {
        ArpGateOff();
        if(!ArpEngaged() || !arp_.Active())
            return;
        const int note = ClampInt(arp_.Next() + arp_transpose_, 0, 127);
        arp_last_source_ = arp_.LastSource();
        arp_count_++;
        voice_.NoteOn(note, false, false);
        voice_note_ = note;
        arp_note_   = note;
        if(options.notes_out)
            PushMidi(0x90 | (options.channel_out - 1), note, 100);
        arp_gate_left_ = offset + 0.5 * arp_step_samples_;
        if(recording_ && Running() && step >= 0)
            RecordAt(step, note, 0);
    }

    void ArpGateOff()
    {
        if(arp_note_ < 0)
            return;
        if(voice_note_ == arp_note_ && live_count_ == 0)
            voice_.NoteOff();
        if(options.notes_out)
            PushMidi(0x80 | (options.channel_out - 1), arp_note_, 0);
        arp_note_      = -1;
        arp_gate_left_ = -1.0;
    }

    void Handle(const Sequencer::Event& e)
    {
        const uint8_t ch = options.channel_out - 1;
        if(e.step == kArpEvent)
        {
            if(e.type == Sequencer::Event::NOTE_ON)
                ArpStep(e.offset, -1);
            else
                ArpGateOff();
            return;
        }
        switch(e.type)
        {
            case Sequencer::Event::NOTE_ON:
                if(!LiveSounding())
                {
                    voice_.NoteOn(e.note, e.accent, e.slide);
                    voice_note_ = e.note;
                }
                if(options.notes_out)
                    PushMidi(0x90 | ch, e.note, e.accent ? 127 : 100);
                break;
            case Sequencer::Event::NOTE_OFF:
                if(!LiveSounding() && e.note == voice_note_)
                    voice_.NoteOff();
                if(options.notes_out)
                    PushMidi(0x80 | ch, e.note, 0);
                break;
            case Sequencer::Event::STEP:
                step_count_++;
                DrumStep();
                if(ArpEngaged() && (arp_.Active() || arp_note_ >= 0))
                    ArpStep(e.offset, e.step);
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
                queued_   = -1;
                drum_pos_ = -1; // the new pattern's drums from their top
                settings_changes++;
                break;
        }
    }

    /** Writes a live note to the pattern. Quantize on: to the nearest point
     *  of the grid (every 1, 2 or 4 steps). Off: to the step it falls in,
     *  late by as many ticks as it was played. */
    void Record(int midi)
    {
        const int cur = seq_.CurrentStep();
        if(cur < 0)
            return;
        const int   len   = Current().length;
        const float phase = seq_.StepPhase();
        int         step, nudge = 0;
        if(StepIndex(settings.params[QUANTIZE], 2) == 1)
        {
            const int g = QuantGridSteps(settings.params[QUANT_GRID]);
            step        = static_cast<int>((cur + phase) / g + 0.5f) * g;
            step %= len;
        }
        else
        {
            step  = cur;
            nudge = static_cast<int>(phase * Sequencer::kTicksPerStep + 0.5f);
            if(nudge >= Sequencer::kTicksPerStep)
                step = seq_.NextStep(), nudge = 0;
        }
        RecordAt(step, midi, nudge);
        rec_key_  = midi;
        rec_step_ = step;
    }

    /** Writes `midi` into `step` as a plain note. */
    void RecordAt(int step, int midi, int nudge)
    {
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
        s.nudge  = static_cast<uint8_t>(nudge);
        pattern_changes++;
        record_count_++;
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
    Drums       drums_;
    int         drum_pos_ = -1;
    uint8_t     hits_[kHitQueue] = {};
    volatile int hit_head_ = 0, hit_tail_ = 0;
    uint32_t    drum_hit_count_[kDrumVoices] = {};
    VoiceParams vp_;
    Fx          fx_;
    Arp         arp_;
    int         arp_note_         = -1;   // the arpeggiator's sounding note
    double      arp_gate_left_    = -1.0; // samples until its gate ends
    double      arp_clock_        = 0.0;  // samples until its next step (own clock)
    float       arp_step_samples_ = 6000.f;
    uint32_t    arp_count_        = 0;
    int         arp_transpose_    = 0;
    int         arp_last_source_  = -1;
    Sequencer   seq_;
    int         queued_     = -1;
    int         voice_note_ = -1;
    uint32_t    step_count_ = 0;
    uint32_t    record_count_ = 0;

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

constexpr uint8_t Machine::kDrumMidi[kDrumVoices];

} // namespace x0x
