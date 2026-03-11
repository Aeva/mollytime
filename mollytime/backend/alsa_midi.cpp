
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

#include "alsa_midi.h"
#include <fmt/format.h>
#include <alsa/asoundlib.h>

constexpr bool EnableEventFilters = true;

// The goal of non-simple input port creation is to setup our own queue, which in
// theory should help us with pacing by giving us meaningful time stamps, and prevent
// notes from being dropped.  However, ALSA appears to take care of the actual pacing,
// so what happens instead is the time stamps are all zero, which indicates that the
// note is to be played immediately.  This, of course, does not fix the pacing problem
// that is likely caused by the batching that is inherent to doing sound generation
// directly on the audio thread.  However however, this does still mean we aren't
// as likely to drop notes as we would be if we didn't use a sequencer queue at all,
// so leaving this on is most likely worthwhile.
constexpr bool NonSimpleInputPort = true;


AlsaMidiDriver::AlsaMidiDriver()
{
    if (snd_seq_open(&SeqHandle, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) == 0)
    {
        snd_seq_nonblock(SeqHandle, 1);
        snd_seq_set_client_name(SeqHandle, "mollytime");
        //snd_seq_set_client_pool_input(SeqHandle, 1024 * 10); // 10 KiB aught to be enough?

        if (EnableEventFilters)
        {
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_NOTEON);
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_NOTEOFF);
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_KEYPRESS);
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_CONTROLLER);
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_CONTROL14);
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_PGMCHANGE);
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_CHANPRESS);
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_PITCHBEND);
            snd_seq_set_client_event_filter(SeqHandle, SND_SEQ_EVENT_PORT_UNSUBSCRIBED);
        }

        if (NonSimpleInputPort)
        {
            MidiQueue = snd_seq_alloc_named_queue(SeqHandle, "mollytime queue");
            if (MidiQueue < 0)
            {
                snd_seq_close(SeqHandle);
                SeqHandle = nullptr;
                fmt::print("Unable to create named ALSA sequencer queue.  No MIDI connections will be possible.\n");
                return;
            }
            snd_seq_start_queue(SeqHandle, MidiQueue, nullptr);
        }

        {
            const char* InputPortName = "in";
            const unsigned int Caps = SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE;
            const unsigned int Type = SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_SOFTWARE | SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_SYNTHESIZER;

            if (NonSimpleInputPort)
            {
                snd_seq_port_info_t* PortInfo = nullptr;
                snd_seq_port_info_alloca(&PortInfo);
                snd_seq_port_info_set_name(PortInfo, InputPortName);
                snd_seq_port_info_set_capability(PortInfo, Caps);
                snd_seq_port_info_set_type(PortInfo, Type);

                if (MidiQueue != -1)
                {
                    // This tells ALSA we want time stamps.
                    // TODO: If we leave this commented out, we get actual time stamps!  However, we get time stamps in a mix
                    // of realtime values relative to the song start (Rosegarden), tick values relative to song start (aplaymidi),
                    // or just zero (pads.py).  This is probably more useful to leave disabled, and do the sequencing in Mollytime,
                    // but that requires recreating all of the sequencing stuff ALSA would normally do.  The most ideal solution
                    // would be if it turns out there were  some way to programmatically tell alsa how much time has advanced in
                    // the input queue, and then do that every time the audio thread runs.  I do not think there is such a thing,
                    // however.
                    //snd_seq_port_info_set_timestamping(PortInfo, 1);

                    // This tells ALSA we want time stamps to expressed as time points not as ticks.
                    snd_seq_port_info_set_timestamp_real(PortInfo, 1);

                    // A queue seems to be required?
                    snd_seq_port_info_set_timestamp_queue(PortInfo, MidiQueue);
                }

                snd_seq_create_port(SeqHandle, PortInfo);
                MidiInPort = snd_seq_port_info_get_port(PortInfo);
            }
            else
            {
                MidiInPort = snd_seq_create_simple_port(SeqHandle, InputPortName, Caps, Type);
            }
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
            fmt::print("SND_SEQ_PORT_CAP_NO_EXPORT: {}\n", (Caps & SND_SEQ_PORT_CAP_NO_EXPORT) == SND_SEQ_PORT_CAP_NO_EXPORT);
            fmt::print("midi channels: {}\n", snd_seq_port_info_get_midi_channels(PortInfo));
            fmt::print("midi voices: {}\n", snd_seq_port_info_get_midi_voices(PortInfo));
            fmt::print("time stamping: {}\n", snd_seq_port_info_get_timestamping(PortInfo));
            fmt::print("realtime stamps: {}\n", snd_seq_port_info_get_timestamp_real(PortInfo));
            fmt::print("timestamp queue id: {}\n", snd_seq_port_info_get_timestamp_queue(PortInfo));

            snd_seq_port_info_free(PortInfo);
        }
#endif
    }
    else
    {
        fmt::print("Unable to initialize ALSA.  No MIDI connections will be possible.\n");
        SeqHandle = nullptr;
    }
}


AlsaMidiDriver::~AlsaMidiDriver()
{
    if (SeqHandle)
    {
        if (MidiQueue != -1 && MidiQueue == SND_SEQ_QUEUE_DIRECT)
        {
            snd_seq_free_queue(SeqHandle, MidiQueue);
            MidiQueue = -1;
        }
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
            int Error = snd_seq_event_input(SeqHandle, &Event);
            if (Error == -EAGAIN || Event == nullptr)
            {
                // Queue is empty.
                return;
            }
            else if (Error == -ENOSPC)
            {
                fmt::print("The MIDI input queue overflowed and events were lost.\n");
            }

            // Relevant API reference pages:
            // union struct thing:
            //  - https://www.alsa-project.org/alsa-doc/alsa-lib/unionsnd__seq__event__data__t.html
            // event type enums etc:
            //  - https://www.alsa-project.org/alsa-doc/alsa-lib/group___seq_events.html#gaef39e1f267006faf7abc91c3cb32ea40

#if 0
            {
                snd_seq_queue_status_t* QueueStatus;
                snd_seq_queue_status_alloca(&QueueStatus);
                snd_seq_get_queue_status(SeqHandle, MidiQueue, QueueStatus);
                auto* Time = snd_seq_queue_status_get_real_time(QueueStatus);

                const double Seconds = double(Time->tv_sec);
                const double NanoSeconds = double(Time->tv_nsec);
                constexpr double NanoToSeconds = 1.0 / 1000000000.0;
                double TimeStamp = NanoSeconds * NanoToSeconds + Seconds;

                // NOTE: This seems to always be zero.  I don't know what the point of this is.
                {
                    fmt::print("time stamp {:.5f}\n", TimeStamp);
                }
            }
#endif

#if 0
            [[maybe_unused]] double TimeStamp = 0.0;
            {
                // Rosegarden sometimes sends time in ticks, but doesn't seem to do so during song playback.
                // My python experiments that generate MIDI events seem to produce packets with time stamps in
                // ticks and these time stamps are so coarse I'm not sure they're actually useful for scheduling.
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
                    if (TimeStamp > 0.0)
                    {
                        fmt::print("{:x} * time stamp {:.5f}\n", Sender, TimeStamp);
                    }
                }
                else if (Event->time.tick > 0)
                {
                    fmt::print("{:x} - tick stamp {}\n", Sender, Event->time.tick);
                }

                // TODO: the ALSA time stamps do not seem to be terribyl useful?  When using a sequencer
                // queue via NonSimpleInputPort, the time stamps are always zero, because ALSA takes care
                // of the pacing such that they should be processed immediately when they are processed.
                // When using direct processing (when NonSimpleInputPort is false), we sometimes get
                // time stamps, and sometimes do not.  Rosegarden sends them, pads.py does not, and neither
                // does aplaymidi.  Rosegarden time stamps are relative to the playback queue start, and
                // they reset to zero when you seek during playback, stop the song, or pause/continue.
                // The event time stamps are also missmatched, because you are getting the time stamp that
                // was recorded when the event was enqueued, as apposed to one that was adjusted by ALSA.

                // What I want instead is for ALSA to let me specify a buffering interval where queued input
                // events are staged, and to let me look ahead in the queue and pick out impending events so
                // that they can be inserted into the patch in the same relative intervals for which they
                // were received.
            }
#endif

            // NOTE: Don't forget to add new events to the event filter list!

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
            else if (Event->type == SND_SEQ_EVENT_PORT_UNSUBSCRIBED)
            {
                // In theory it is possible and valid to have a setup that is dynamically subscribing and unsubscribing
                // ports while a song is playing, that such a system is somehow immaculately well behaved, and thus releasing
                // all held notes in response to each disconnect.  I'll believe it when I see it, and in the meantime this
                // event is the simplest way to detect when an aplaymidi process was terminated early (which is absolutely not
                // "immaculately well behaved").
                Handler->ReleaseHeldNotes();
            }

            // NOTE: Don't forget to add new events to the event filter list!


#if 0
            else
            {
#define LOG_EVENT(EVENT_TYPE) if ( Event->type == EVENT_TYPE ) fmt::print("{}\n", #EVENT_TYPE);
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
