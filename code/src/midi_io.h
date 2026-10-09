/** @file midi_io.h
 *  @brief MIDI in and out over DIN (UART) and USB.
 *
 *  In: Poll() runs in the audio interrupt before the machine renders, so
 *  clock, transport and notes take effect in the same block.
 *  Out: the machine queues messages from the audio interrupt. DIN goes out
 *  by DMA, kicked from the audio interrupt and chained from the transfer's
 *  end interrupt, so MIDI clock keeps time even while the main loop is busy
 *  with the card. USB is sent from the main loop.
 *
 *  The DMA queue follows TEMPO's MidiManager.
 */
#pragma once
#include "daisy_seed.h"
#include "x0x/machine.h"

namespace chompi
{

class MidiIo
{
  public:
    void Init()
    {
        MidiUartHandler::Config uart_cfg;
        uart_.Init(uart_cfg);
        uart_.StartReceive();
        uart_handle_ = uart_.GetMutableTransport().GetUartHandle();

        MidiUsbHandler::Config usb_cfg;
        usb_cfg.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
        usb_.Init(usb_cfg);
        usb_.Listen();
    }

    /** Audio interrupt: incoming MIDI to the machine. */
    void Poll(x0x::Machine& m)
    {
        uart_.Listen();
        while(uart_.HasEvents())
            Handle(m, uart_.PopEvent());
        while(usb_.HasEvents())
            Handle(m, usb_.PopEvent());
    }

    /** Audio interrupt: queued DIN messages out. */
    void PumpUart(x0x::Machine& m)
    {
        x0x::Machine::MidiOut msg;
        while(((head_ + 1) % kFifo) != tail_ && m.PopMidi(x0x::Machine::kUart, msg))
        {
            Entry& e = fifo_[head_];
            for(int i = 0; i < msg.len; i++)
                e.data[i] = msg.b[i];
            e.len = msg.len;
            head_ = (head_ + 1) % kFifo;
        }
        if(!busy_ && head_ != tail_)
            StartDma();
    }

    /** Main loop: queued USB messages out. */
    void PumpUsb(x0x::Machine& m)
    {
        x0x::Machine::MidiOut msg;
        while(m.PopMidi(x0x::Machine::kUsb, msg))
            usb_.SendMessage(msg.b, msg.len);
    }

  private:
    void Handle(x0x::Machine& m, const MidiEvent& ev)
    {
        if(ev.type == SystemRealTime)
        {
            switch(ev.srt_type)
            {
                case TimingClock: m.MidiClock(); break;
                case Start: m.MidiStart(); break;
                case Stop: m.MidiStop(); break;
                case Continue: m.MidiContinue(); break;
                default: break;
            }
            return;
        }
        // The drum channel: its GM drum notes play the drums (their note-offs
        // are ignored: a drum hit has no length). Other notes on it, if it's
        // also the bass's channel, go on to the bass.
        if(m.options.drums_in && ev.channel == m.options.drum_channel - 1
           && (ev.type == NoteOn || ev.type == NoteOff) && x0x::GmToDrum(ev.data[0]) >= 0)
        {
            if(ev.type == NoteOn)
                m.MidiDrumNote(ev.data[0], ev.data[1]);
            return;
        }
        if(ev.channel != m.options.channel_in - 1)
            return;
        switch(ev.type)
        {
            case NoteOn:
                if(ev.data[1] == 0)
                    m.LiveNoteOff(ev.data[0]);
                else
                    m.LiveNoteOn(ev.data[0], ev.data[1] >= 112);
                break;
            case NoteOff: m.LiveNoteOff(ev.data[0]); break;
            case ControlChange: m.MidiCc(ev.data[0], ev.data[1]); break;
            default: break;
        }
    }

    /** Interrupts are either masked or this is the DMA's own end interrupt. */
    void StartDma()
    {
        busy_          = true;
        Entry&    e    = fifo_[tail_];
        uintptr_t from = reinterpret_cast<uintptr_t>(e.data) & ~static_cast<uintptr_t>(31);
        SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(from), 32);
        uart_handle_.DmaTransmit(e.data, e.len, nullptr, TxEnd, this);
    }

    static void TxEnd(void* ctx, UartHandler::Result)
    {
        MidiIo* self = static_cast<MidiIo*>(ctx);
        self->tail_  = (self->tail_ + 1) % kFifo;
        self->busy_  = false;
        if(self->head_ != self->tail_)
            self->StartDma();
    }

    // Each message owns a 32-byte cache line, so cleaning it for the DMA
    // never touches anything else.
    struct alignas(32) Entry
    {
        uint8_t data[3];
        uint8_t len;
    };
    static constexpr int kFifo = 64;

    MidiUartHandler uart_;
    MidiUsbHandler  usb_;
    UartHandler     uart_handle_;
    Entry           fifo_[kFifo];
    volatile int    head_ = 0, tail_ = 0;
    volatile bool   busy_ = false;
};

} // namespace chompi
