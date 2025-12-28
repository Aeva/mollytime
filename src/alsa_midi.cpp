
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
            const unsigned int Type = SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_SOFTWARE | SND_SEQ_PORT_TYPE_SYNTHESIZER;
            MidiInPort = snd_seq_create_simple_port(SeqHandle, "in", Caps, Type);
        }
        {
            const unsigned int Caps = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ;
            const unsigned int Type = SND_SEQ_PORT_TYPE_APPLICATION | SND_SEQ_PORT_TYPE_SOFTWARE;
            MidiOutPort = snd_seq_create_simple_port(SeqHandle, "out", Caps, Type);
        }
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
            else if (Event->type == SND_SEQ_EVENT_NOTEON)
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
        }
    }
}

#endif
