
// Copyright 2025 Aeva Palecek
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifdef MIDI_ALSA

#include "alsa_midi.h"
#include <format>
#include <print>
#include <alsa/asoundlib.h>


AlsaMidiDriver::AlsaMidiDriver()
{
    if (snd_seq_open(&SeqHandle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) == 0)
    {
        snd_seq_set_client_name(SeqHandle, "mollytime");
        {
            const unsigned int Caps = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
            const unsigned int Type = SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_SOFTWARE | SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_SYNTHESIZER;
            MidiInPort = snd_seq_create_simple_port(SeqHandle, "in", Caps, Type);
        }
        {
            const unsigned int Caps = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
            const unsigned int Type = SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_SOFTWARE | SND_SEQ_PORT_TYPE_MIDI_GENERIC;
            MidiOutPort = snd_seq_create_simple_port(SeqHandle, "out", Caps, Type);
        }

#if 0
        {
            snd_seq_port_info_t* PortInfo;
            snd_seq_port_info_malloc(&PortInfo);
            snd_seq_get_port_info(SeqHandle, MidiInPort, PortInfo);
            unsigned int Caps = snd_seq_port_info_get_capability(PortInfo);
            std::print("SND_SEQ_PORT_CAP_NO_EXPORT: {}\n", (Caps & SND_SEQ_PORT_CAP_NO_EXPORT) == SND_SEQ_PORT_CAP_NO_EXPORT);
            std::print("midi channels: {}\n", snd_seq_port_info_get_midi_channels(PortInfo));
            std::print("midi voices: {}\n", snd_seq_port_info_get_midi_voices(PortInfo));
            std::print("time stamping: {}\n", snd_seq_port_info_get_timestamping(PortInfo));
            std::print("realtime stamps: {}\n", snd_seq_port_info_get_timestamp_real(PortInfo));
            std::print("timestamp queue id: {}\n", snd_seq_port_info_get_timestamp_queue(PortInfo));

            snd_seq_port_info_free(PortInfo);
        }
#endif
    }
    else
    {
        std::print("Unable to initialize ALSA.  No MIDI connections will be possible.\n");
        SeqHandle = nullptr;
    }
}


AlsaMidiDriver::~AlsaMidiDriver()
{
    if (SeqHandle)
    {
        if (MidiOutPort > -1)
        {
            snd_seq_delete_simple_port(SeqHandle, MidiOutPort);
            MidiOutPort = -1;
        }
        if (MidiInPort > -1)
        {
            snd_seq_delete_simple_port(SeqHandle, MidiInPort);
            MidiInPort = -1;
        }
        snd_seq_close(SeqHandle);
        SeqHandle = nullptr;
    }
}


void AlsaMidiDriver::ProcessEvents(MidiHandler* Handler)
{
    if (SeqHandle)
    {
        while (true)
        {
            snd_seq_event_t* Event = nullptr;
            snd_seq_event_input(SeqHandle, &Event);

            // Relevant API reference pages:
            // union struct thing:
            //  - https://www.alsa-project.org/alsa-doc/alsa-lib/unionsnd__seq__event__data__t.html
            // event type enums etc:
            //  - https://www.alsa-project.org/alsa-doc/alsa-lib/group___seq_events.html#gaef39e1f267006faf7abc91c3cb32ea40

            if (!Event)
            {
                return;
            }

            [[maybe_unused]] double TimeStamp = 0.0;
            {
                // Rosegarden sometimes sends time in ticks, but doesn't seem to do so during song playback.
                // My python experiments that generate MIDI events seem to produce packets with timestamps in
                // ticks and these timestamps are so coarse I'm not sure they're actually useful for scheduling.
                // I am unsure if that is the expected behavior, or a bug in my other programs.
                // Aplaymidi also sends ticks.
                const bool TimeIsReal = (Event->flags & SND_SEQ_TIME_STAMP_MASK) == SND_SEQ_TIME_STAMP_REAL;

                // It is unclear if or when an application can receive relative time stamps.
                // The docs (https://www.alsa-project.org/alsa-doc/alsa-lib/seq.html) make it sound like
                // relative mode is only of interest to the ALSA scheduling queue, and what another application
                // sees is probably the same regardless of whether or not the origin was sending in direct mode
                // or not, but I don't really know.
                // The docs also indicate (this time without ambiguity) that the "wall clock" reference point
                // which absolute time stamps are relative to is the start of the queue.  So for example,
                // Rosegarden time stamps are the absolute playback position relative to the start of the song,
                // whereas pads.py time stamps are relative to when the program started.
                const bool TimeIsAbsolute = (Event->flags & SND_SEQ_TIME_MODE_MASK) == SND_SEQ_TIME_MODE_ABS;

                // This will differentiate between different Rosegarden sessions, as well as Rosegarden vs
                // pads.py and Rosegarden vs aplaymidi, but for reasons unknown, it will NOT differentiate
                // between pads.py and aplaymidi despite these being different ports on different clients.
                [[maybe_unused]] const uint16_t Sender = (uint8_t(Event->source.client) << 8) | uint8_t(Event->source.port);

                if (TimeIsReal && TimeIsAbsolute)
                {
                    const double Seconds = double(Event->time.time.tv_sec);
                    const double NanoSeconds = double(Event->time.time.tv_nsec);
                    constexpr double NanoToSeconds = 1.0 / 1000000000.0;
                    TimeStamp = NanoSeconds * NanoToSeconds + Seconds;
                    //std::print("{:x} * timestamp {:.5f}\n", Sender, TimeStamp);
                }
                else
                {
                    //std::print("{:x} - tickstamp {}\n", Sender, Event->time.tick);
                }

                // TODO: TimeStamp behaves too eccentrically to be useful.  However, it appears that
                // `snd_seq_create_simple_port` does not setup timestamps or opt into realtime timestamps,
                // so the eccentric behavior of `TimeStamp` is a consequence of not setting up the port
                // manually.
            }

            if (Event->type == SND_SEQ_EVENT_NOTEON)
            {
                Handler->NoteOn(Event->data.note.note, Event->data.note.velocity, Event->data.note.channel);
            }
            else if (Event->type == SND_SEQ_EVENT_NOTEOFF)
            {
                Handler->NoteOff(Event->data.note.note, Event->data.note.channel);
            }
            else if (Event->type == SND_SEQ_EVENT_KEYPRESS)
            {
                Handler->NotePressure(Event->data.note.note, Event->data.note.velocity, Event->data.note.channel);
            }
            else if (Event->type == SND_SEQ_EVENT_CONTROLLER)
            {
                Handler->ControlChange7Bit(Event->data.control.param, Event->data.control.value, Event->data.control.channel);
            }
            else if (Event->type == SND_SEQ_EVENT_CONTROL14)
            {
                Handler->ControlChange14Bit(Event->data.control.param, Event->data.control.value, Event->data.control.channel);
            }
            else if (Event->type == SND_SEQ_EVENT_PGMCHANGE)
            {
                Handler->ProgramChange(Event->data.control.value, Event->data.control.channel);
            }
            else if (Event->type == SND_SEQ_EVENT_CHANPRESS)
            {
                Handler->ChannelPressure(Event->data.control.value, Event->data.control.channel);
            }
            else if (Event->type == SND_SEQ_EVENT_PITCHBEND)
            {
                // Alsa is cute about this and centers the value on zero for you despite the wire protocol not doing this,
                // which means we have to handle the conversion here instead of being able to perform it generically.
                // https://alsa-project.org/alsa-doc/alsa-lib/group___seq_middle.html#ga8da40bfd56e00ebec775e5241d86a3e3

                int16_t Value = Event->data.control.value;
                double Divisor = (Value <  0) ? 8192.0 : 8191.0;
                Handler->PitchBend(double(Value) / Divisor, Event->data.control.channel);
            }
#if 0
            else
            {
#define LOG_EVENT(EVENT_TYPE) if ( Event->type == EVENT_TYPE ) std::print("{}\n", #EVENT_TYPE);
                // None of these seem to be emitted by Rosegarden under normal playback conditions, nor while
                // seeking.  If you seek forward or backward by a fixed amount, the timestamp always resets to
                // zero.  The timestamps never move backward except when resetting to zero.  This raises the
                // interesting question of whether or not Rosegarden is using direct mode or a scheduling
                // queue.

                LOG_EVENT(SND_SEQ_EVENT_SONGPOS);
                LOG_EVENT(SND_SEQ_EVENT_SONGSEL);
                LOG_EVENT(SND_SEQ_EVENT_QFRAME);
                LOG_EVENT(SND_SEQ_EVENT_TIMESIGN);
                LOG_EVENT(SND_SEQ_EVENT_KEYSIGN);

                LOG_EVENT(SND_SEQ_EVENT_START);
                LOG_EVENT(SND_SEQ_EVENT_CONTINUE);
                LOG_EVENT(SND_SEQ_EVENT_STOP);
                LOG_EVENT(SND_SEQ_EVENT_SETPOS_TICK);
                LOG_EVENT(SND_SEQ_EVENT_SETPOS_TIME);
                LOG_EVENT(SND_SEQ_EVENT_TEMPO);
                LOG_EVENT(SND_SEQ_EVENT_CLOCK);
                LOG_EVENT(SND_SEQ_EVENT_TICK);
                LOG_EVENT(SND_SEQ_EVENT_QUEUE_SKEW);
                LOG_EVENT(SND_SEQ_EVENT_SYNC_POS);

                LOG_EVENT(SND_SEQ_EVENT_RESET);
                LOG_EVENT(SND_SEQ_EVENT_SENSING);
                LOG_EVENT(SND_SEQ_EVENT_ECHO);

                // Unclear when these would be received.
                LOG_EVENT(SND_SEQ_EVENT_CLIENT_START);
                LOG_EVENT(SND_SEQ_EVENT_CLIENT_EXIT);
                LOG_EVENT(SND_SEQ_EVENT_CLIENT_CHANGE);
                LOG_EVENT(SND_SEQ_EVENT_PORT_START);
                LOG_EVENT(SND_SEQ_EVENT_PORT_EXIT);
                LOG_EVENT(SND_SEQ_EVENT_PORT_CHANGE);

                // SND_SEQ_EVENT_PORT_SUBSCRIBED and SND_SEQ_EVENT_PORT_UNSUBSCRIBED are received when Rosegarden
                // connects and disconnects automatically, as well as when connections are made explicitly with
                // tools like helvum.
                LOG_EVENT(SND_SEQ_EVENT_PORT_SUBSCRIBED);
                LOG_EVENT(SND_SEQ_EVENT_PORT_UNSUBSCRIBED);

                // Unclear what these even are.
                LOG_EVENT(SND_SEQ_EVENT_UMP_EP_CHANGE);
                LOG_EVENT(SND_SEQ_EVENT_UMP_BLOCK_CHANGE);
#undef LOG_EVENT
            }
#endif
        }
    }
}

#endif
