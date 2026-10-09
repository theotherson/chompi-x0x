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
    /** reverb_mem: Reverb::kReverbFrames floats (SDRAM on the hardware);
     *  without it there's no reverb. */
    void Init(float sample_rate, Fx::Frame* delay_mem, size_t delay_frames, float* reverb_mem = nullptr,
              size_t reverb_frames = 0)
    {
        sr_ = sample_rate;
        voice_.Init(sample_rate);
        drums_.Init(sample_rate);
        drum_fx_.Init(sample_rate);
        drum_fx_send_.Init(sample_rate);
        comp_.Init(sample_rate);
        reverb_.Init(sample_rate, reverb_mem, reverb_frames);
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
        drum_pat_ = -1;
        lfo_phase_ = 0.f, lfo_held_ = NextLfoRandom(); // the drums' LFO from the top
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
        drum_pat_        = -1;
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
     *  with the pattern running (to the nearest step), unless `record` is
     *  false (the pitched live keyboard: a step has no pitch). */
    void DrumHit(int voice, bool accent, int semitones = 0, bool record = true)
    {
        const int next = (hit_head_ + 1) % kHitQueue;
        if(next == hit_tail_ || voice < 0 || voice >= kDrumVoices)
            return; // full: dropped
        hits_[hit_head_]     = static_cast<uint8_t>(voice | (accent ? kDrumAccent : 0));
        hit_semis_[hit_head_] = static_cast<int8_t>(ClampInt(semitones, -36, 36));
        hit_rec_[hit_head_]   = record;
        hit_head_            = next;
    }

    /** The drum part's step playing now (its own length), -1 when stopped. */
    int CurrentDrumStep() const { return Running() ? drum_pos_ : -1; }

    void SetDrumLength(int len)
    {
        Current().drum_length = static_cast<uint8_t>(ClampInt(len, 1, kSteps));
        pattern_changes++;
    }

    /** Mute and solo, per voice (bits), for the drum part's playback: a
     *  muted voice is silent; while any voice is soloed only soloed ones
     *  play. Live hits always sound. Not saved. */
    void SetDrumMute(int v, bool on) { drum_mute_ = Bit(drum_mute_, v, on); }
    void SetDrumSolo(int v, bool on) { drum_solo_ = Bit(drum_solo_, v, on); }
    bool DrumMuted(int v) const { return (drum_mute_ >> v) & 1; }
    bool DrumSoloed(int v) const { return (drum_solo_ >> v) & 1; }
    bool DrumAudible(int v) const { return !DrumMuted(v) && (!drum_solo_ || DrumSoloed(v)); }

    /** For the LEDs: the compressor's gain reduction (dB) and the sidechain's
     *  duck now (0..1). */
    float CompReduction() const { return comp_.Reduction(); }
    /** Erase (A#4 held + a voice, live): the voice's hits go as they pass. */
    void SetDrumErase(int v, bool on) { erase_mask_ = Bit(erase_mask_, v, on); }
    bool DrumErasing(int v) const { return (erase_mask_ >> v) & 1; }
    void StopErasing() { erase_mask_ = 0; }
    /** Roll (G#4 held + a voice, live): the voice repeats at the roll rate. */
    void SetDrumRoll(int v, bool on, bool accent)
    {
        roll_accent_ = accent;
        roll_mask_   = Bit(roll_mask_, v, on);
    }
    bool DrumRolling(int v) const { return (roll_mask_ >> v) & 1; }
    void StopRolls() { roll_mask_ = 0; }

    /** The drums' filter LFO now, -1..1 (0 when off). */
    float DrumLfo() const { return lfo_now_; }

    /** Whether drum voice v goes to the reverb and delay (saved). */
    bool DrumInFx(int v) const { return (StepIndex(settings.params[DRUM_FX_SENDS], 128) >> v) & 1; }
    void SetDrumInFx(int v, bool on)
    {
        int m = StepIndex(settings.params[DRUM_FX_SENDS], 128);
        m     = on ? m | (1 << v) : m & ~(1 << v);
        SetParam(DRUM_FX_SENDS, StepValue(m, 128));
    }
    float Duck() const { return duck_; }

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
        drum_pat_ = -1;
        lfo_phase_ = 0.f, lfo_held_ = NextLfoRandom(); // the drums' LFO from the top
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
            dp.tune        = p[DRUM_TUNE + v];
            dp.fm          = p[DRUM_FM + v];
        }
        const uint8_t sends = static_cast<uint8_t>(StepIndex(p[DRUM_FX_SENDS], 128));
        drums_.SetSendMask(sends);
        const bool split = (sends & 0x7f) != 0x7f && (sends & 0x7f) != 0; // some voices in, some out
        float      drum[64], dsrc[64];
        for(size_t i = 0; i < n; i++)
            drum[i] = dsrc[i] = 0.f;
        // Live drum hits from the panel.
        block_pos_ = 0;
        while(hit_tail_ != hit_head_)
        {
            const uint8_t h     = hits_[hit_tail_];
            const int     semis = hit_semis_[hit_tail_];
            const bool    rec   = hit_rec_[hit_tail_];
            hit_tail_           = (hit_tail_ + 1) % kHitQueue;
            const int v         = h & 0x7f;
            PlayDrum(v, (h & kDrumAccent) != 0, semis);
            if(Recording() && Running() && rec)
                RecordDrum(v, (h & kDrumAccent) != 0);
        }
        // Rolls: a voice's first hit came with its key (above); then on the
        // roll's grid while running, or every roll step from the key when
        // stopped. A grid hit too soon after the key's is skipped.
        {
            const uint8_t rolls = roll_mask_;
            const int     rt    = kRollTicks[StepIndex(p[DRUM_ROLL_RATE], kRollRates)];
            const float   spt   = sr_ * 60.f / (seq_.Tempo() * 24.f);
            if(rolls & ~roll_prev_)
                roll_since_ = 0.f, roll_clock_ = rt * spt;
            roll_prev_ = rolls;
            seq_.SetRoll(rolls ? rt : 0);
            roll_skip_ = roll_since_ < rt * spt * 0.5f;
            if(rolls && !Running())
            {
                roll_clock_ -= static_cast<float>(n);
                if(roll_clock_ <= 0.f)
                {
                    roll_clock_ += rt * spt;
                    RollHits();
                }
            }
            roll_since_ += static_cast<float>(n);
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
                drums_.Process(drum + pos, at - pos, split ? dsrc + pos : nullptr);
                pos = at;
            }
            block_pos_ = at;
            Handle(ev[i]);
        }
        if(pos < n)
        {
            voice_.Process(vp_, mono + pos, n - pos);
            drums_.Process(drum + pos, n - pos, split ? dsrc + pos : nullptr);
        }
        if(arp_gate_left_ >= 0.0)
            arp_gate_left_ -= n;
        if(arp_clock_ > 0.0)
            arp_clock_ -= n;

        // The mix (bass / drums) first, so a side's echoes follow it.
        float gb, gd;
        MixGains(p[MIX], p[MIX_MUTE], &gb, &gd);
        mix_bass_ += (gb - mix_bass_) * 0.2f; // a little smoothing, per block
        mix_drums_ += (gd - mix_drums_) * 0.2f;
        for(size_t i = 0; i < n; i++)
            mono[i] *= mix_bass_, drum[i] *= mix_drums_, dsrc[i] *= mix_drums_;

        // The drums' own effects, then their sends to the shared delay and
        // the reverb.
        DrumFx::Settings ds;
        ds.drive      = p[DRUM_DRIVE];
        ds.filter_res = p[DRUM_FILTER_RES];
        ds.env_amount = p[DRUM_FENV];
        ds.env_decay  = p[DRUM_FENV_DECAY];
        ds.filter     = p[DRUM_FILTER];
        ds.crush_bits = p[DRUM_CRUSH];
        ds.crush_rate = p[DRUM_CRUSH_RATE];
        // The filter's LFO, synced to the tempo (from the start when PLAY).
        const int    shape = StepIndex(p[DRUM_LFO_SHAPE], kLfoShapes);
        float        lfo[64];
        const float* mod = nullptr;
        if(shape > 0)
        {
            const float inc = seq_.Tempo() / (60.f * sr_ * kLfoBeats[StepIndex(p[DRUM_LFO_RATE], kLfoRates)]);
            for(size_t i = 0; i < n; i++)
            {
                lfo_phase_ += inc;
                if(lfo_phase_ >= 1.f)
                    lfo_phase_ -= 1.f, lfo_held_ = NextLfoRandom();
                lfo[i] = kLfoOctaves * LfoValue(shape, lfo_phase_, lfo_held_);
            }
            mod = lfo;
        }
        lfo_now_ = shape > 0 ? LfoValue(shape, lfo_phase_, lfo_held_) : 0.f;
        drum_fx_.Set(ds);
        drum_fx_.Process(drum, n, mod);
        // The sends: the voices in them, through the same filter, crush and
        // distortion (a second copy: they aren't linear); all in, the bus.
        const float* send_src = drum;
        if(split)
        {
            drum_fx_send_.Set(ds);
            drum_fx_send_.Process(dsrc, n, mod);
            send_src = dsrc;
        }
        else if((sends & 0x7f) == 0)
        {
            for(size_t i = 0; i < n; i++)
                dsrc[i] = 0.f;
            send_src = dsrc;
        }
        reverb_.Set(p[REVERB_SIZE]);
        const float dly_send = p[DRUM_DELAY] * p[DRUM_DELAY];
        const float rev_send = p[DRUM_REVERB] * p[DRUM_REVERB];
        float       dsend[64], rin[64];
        for(size_t i = 0; i < n; i++)
            dsend[i] = send_src[i] * dly_send, rin[i] = send_src[i] * rev_send;

        fx_.Process(mono, dly_send > 0.0001f ? dsend : nullptr, left, right, n);
        float rl[64], rr[64];
        for(size_t i = 0; i < n; i++)
            rl[i] = rr[i] = 0.f;
        reverb_.Process(rin, rl, rr, n);
        // The kick's sidechain: each BD hit ducks the bass, the echoes and
        // the reverb (not the drums), recovering by the next beat.
        const float depth   = 0.9f * p[SIDECHAIN];
        const float rel     = TauToCoef(0.25f * 60.f / seq_.Tempo(), sr_);
        const float smooth  = TauToCoef(0.002f, sr_);
        for(size_t i = 0; i < n; i++)
        {
            duck_ -= duck_ * rel;
            duck_gain_ += ((1.f - depth * duck_) - duck_gain_) * smooth;
            left[i]  = (left[i] + rl[i]) * duck_gain_ + drum[i];
            right[i] = (right[i] + rr[i]) * duck_gain_ + drum[i];
        }
        // The master compressor, on everything.
        comp_.Set(p[COMP]);
        comp_.Process(left, right, n);
        const float vol = settings.params[VOLUME] * settings.params[VOLUME] * 1.5f;
        for(size_t i = 0; i < n; i++)
        {
            left[i]  = SoftLimit(left[i] * vol);
            right[i] = SoftLimit(right[i] * vol);
            // Never send the codec a broken value (it plays as a crackle).
            if(!(fabsf(left[i]) + fabsf(right[i]) <= 4.f))
                left[i] = right[i] = 0.f, bad_out_++;
        }
    }

    /** A side's mute (the bass, or the drums): silent, effects and all. */
    bool SideMuted(bool drums) const { return (StepIndex(settings.params[MIX_MUTE], 4) >> (drums ? 1 : 0)) & 1; }
    void SetSideMuted(bool drums, bool on)
    {
        const int m = StepIndex(settings.params[MIX_MUTE], 4), bit = drums ? 2 : 1;
        SetParam(MIX_MUTE, StepValue(on ? m | bit : m & ~bit, 4));
    }

    /** Broken values caught: at the output, and in the bass's effects. */
    uint32_t BadOutCount() const { return bad_out_; }
    uint32_t BadFxCount() const { return fx_.BadCount(); }

  private:
    static constexpr int kArpEvent = -2; // Event::step of an arpeggiator event
    static constexpr int kHitQueue = 16;

    /** GM drum notes, for MIDI out (channel 10). */
    static constexpr uint8_t kDrumMidi[kDrumVoices] = {36, 38, 45, 50, 49, 46, 42};

    void PlayDrum(int v, bool accent, int semitones = 0)
    {
        const float acc = accent ? settings.params[DRUM_ACCENT] : 0.f;
        drums_.Trigger(static_cast<Drum>(v), acc, semitones);
        drum_fx_.Trigger(block_pos_, 1.f + 0.5f * acc); // the filter's envelope
        drum_fx_send_.Trigger(block_pos_, 1.f + 0.5f * acc);
        if(v == BD)
            duck_ = 1.f; // the sidechain: duck from now
        drum_hit_count_[v]++;
        if(options.notes_out)
        {
            PushMidi(0x99, kDrumMidi[v], accent ? 127 : 100);
            PushMidi(0x89, kDrumMidi[v], 0);
        }
    }

    /** A roll's hit: every rolling voice (recorded, if recording). */
    void RollHits()
    {
        const uint8_t rolls = roll_mask_;
        if(!rolls || (Running() && roll_skip_))
            return;
        for(int v = 0; v < kDrumVoices; v++)
            if((rolls >> v) & 1)
            {
                PlayDrum(v, roll_accent_);
                if(Recording() && Running())
                    RecordDrum(v, roll_accent_, true);
            }
    }

    /** The pattern the drums play: the next one already from its first
     *  step's place on the grid, before a late first bass note starts it. */
    Pattern& DrumPattern() { return patterns[drum_pat_ >= 0 ? drum_pat_ : settings.pattern]; }

    /** A step's place on the grid: the drum part's next step (its own
     *  length). Hits recorded late are scheduled (quantize on: on the grid);
     *  voices held for erasing lose theirs. */
    void DrumStep()
    {
        Pattern&   pat   = DrumPattern();
        drum_pos_        = (drum_pos_ + 1) % ClampInt(pat.drum_length, 1, kSteps);
        const bool quant = StepIndex(settings.params[DRUM_QUANTIZE], 2) == 1;
        const uint8_t erase = erase_mask_;
        if(erase & pat.drums[drum_pos_])
        {
            for(int v = 0; v < kDrumVoices; v++)
                if((erase >> v) & 1)
                    pat.SetDrumHit(drum_pos_, v, false);
            if(!(pat.drums[drum_pos_] & 0x7f))
                pat.drums[drum_pos_] = 0; // no hits left: no accent either
            pattern_changes++;
        }
        const uint8_t s    = pat.drums[drum_pos_];
        const uint8_t skip = rec_skip_;
        rec_skip_          = 0;
        for(int v = 0; v < kDrumVoices; v++)
            if(((s >> v) & 1) && DrumAudible(v) && !((skip >> v) & 1))
            {
                const bool acc   = (s & kDrumAccent) != 0;
                const int  nudge = quant ? 0 : pat.drum_nudge[drum_pos_][v];
                if(nudge == 0)
                    PlayDrum(v, acc);
                else
                    seq_.ScheduleDrum(nudge, v | (acc ? kDrumAccent : 0));
            }
    }

    static uint8_t Bit(uint8_t bits, int v, bool on)
    {
        return static_cast<uint8_t>(on ? bits | (1 << v) : bits & ~(1 << v));
    }

    /** A live hit into the drum part. Quantize on: to the nearest point of
     *  the grid (every 1, 2 or 4 steps). Off: to the step it falls in, late
     *  by as many ticks as it was played (or the next step, on time). A roll
     *  keeps the first of its hits in a step. */
    void RecordDrum(int v, bool accent, bool roll = false)
    {
        if(drum_pos_ < 0)
            return;
        Pattern&    pat   = DrumPattern();
        const int   len   = ClampInt(pat.drum_length, 1, kSteps);
        const float phase = seq_.GridPhase();
        int         step, nudge = 0;
        if(StepIndex(settings.params[DRUM_QUANTIZE], 2) == 1)
        {
            const int g = QuantGridSteps(settings.params[DRUM_QUANT_GRID]);
            step        = (static_cast<int>((drum_pos_ + phase) / g + 0.5f) * g) % len;
        }
        else
        {
            step  = drum_pos_;
            nudge = static_cast<int>(phase * kStepTicks + 0.5f);
            if(nudge >= kStepTicks)
                step = (step + 1) % len, nudge = 0;
        }
        if(roll && roll_rec_step_[v] == step && pat.DrumHit(step, v))
            return;
        roll_rec_step_[v] = static_cast<int8_t>(step); // (a roll's key hit counts as its first)
        pat.drums[step]   = static_cast<uint8_t>(pat.drums[step] | (1 << v) | (accent ? kDrumAccent : 0));
        pat.drum_nudge[step][v] = static_cast<uint8_t>(nudge);
        if(step != drum_pos_)
            rec_skip_ = static_cast<uint8_t>(rec_skip_ | (1 << v)); // played just now: not again at its step
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
            case Sequencer::Event::GRID:
                // The drums step on the grid; at the top, a queued pattern's.
                if(e.step == 0 && queued_ >= 0)
                    drum_pat_ = queued_, drum_pos_ = -1;
                DrumStep();
                break;
            case Sequencer::Event::DRUM:
            {
                const int v = e.step & 0x7f;
                if(DrumAudible(v) && !((erase_mask_ >> v) & 1))
                    PlayDrum(v, (e.step & kDrumAccent) != 0);
                break;
            }
            case Sequencer::Event::ROLL:
                RollHits();
                break;
            case Sequencer::Event::STEP:
                step_count_++;
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
                queued_ = -1;
                if(drum_pat_ < 0)
                    drum_pos_ = -1; // the new pattern's drums from their top
                drum_pat_ = -1;     // (they already are, from the grid)
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
    DrumFx      drum_fx_, drum_fx_send_;
    size_t      block_pos_ = 0; // where in the block a drum hit lands
    uint32_t    bad_out_   = 0;
    float       lfo_phase_ = 0.f, lfo_held_ = 0.f, lfo_now_ = 0.f;
    uint32_t    lfo_seed_  = 12345;
    float       NextLfoRandom()
    {
        lfo_seed_ = lfo_seed_ * 1664525u + 1013904223u;
        return static_cast<int32_t>(lfo_seed_) * (1.f / 2147483648.f);
    }
    Compressor  comp_;
    float       duck_ = 0.f, duck_gain_ = 1.f;
    Reverb      reverb_;
    int         drum_pos_ = -1;
    int         drum_pat_ = -1;      // the drums' pattern, ahead of the bass's
    uint8_t     rec_skip_ = 0;       // voices recorded into the next step
    int8_t      roll_rec_step_[kDrumVoices] = {-1, -1, -1, -1, -1, -1, -1}; // each voice's last recorded step
    volatile uint8_t erase_mask_ = 0; // voices held for erasing
    volatile uint8_t roll_mask_  = 0; // voices rolling
    volatile bool    roll_accent_ = false;
    uint8_t     roll_prev_  = 0;
    float       roll_since_ = 0.f;   // samples since a roll started
    float       roll_clock_ = 0.f;   // stopped: samples to its next hit
    bool        roll_skip_  = false; // too soon after the key's own hit
    uint8_t     hits_[kHitQueue] = {};
    int8_t      hit_semis_[kHitQueue] = {};
    bool        hit_rec_[kHitQueue] = {};
    volatile int hit_head_ = 0, hit_tail_ = 0;
    uint32_t    drum_hit_count_[kDrumVoices] = {};
    uint8_t     drum_mute_ = 0, drum_solo_ = 0;
    float       mix_bass_ = 1.f, mix_drums_ = 1.f;
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
