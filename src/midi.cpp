
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
// limitations under the License.#include "midi.h"

#include "midi.h"

#ifdef MIDI_ALSA
#include "alsa_midi.h"
#elifdef MIDI_MMEAPI
#include "mmeapi_midi.h"
#endif

#include <utility>
#include <atomic>
#include <memory>
#include <print>


static std::unique_ptr<MidiDriver> Driver;
static std::atomic_bool SendMidiReset = false;


void MidiHandler::NoteOn(uint8_t Note, uint8_t Velocity, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::Note;
    Event.Channel = Channel;
    Event.Param1 = AudioSample(Note);
    Event.Param2 = AudioSample(Velocity) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::NotePressure(uint8_t Note, uint8_t Pressure, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::PolyPress;
    Event.Channel = Channel;
    Event.Param1 = AudioSample(Note);
    Event.Param2 = AudioSample(Pressure) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::ControlChange7Bit(uint8_t Control, uint8_t Value, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::ControlChange;
    Event.Channel = Channel;
    Event.Param1 = AudioSample(Control);
    Event.Param2 = AudioSample(Value) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::ControlChange14Bit(uint8_t Control, uint16_t Value, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::ControlChange;
    Event.Channel = Channel;
    Event.Param1 = AudioSample(Control);
    Event.Param2 = std::min(AudioSample(Value) / 16383.0, 1.0); // educated guess
    EnqueueMidiMessage(Event);
}


void MidiHandler::ProgramChange(uint8_t Program, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::ProgramChange;
    Event.Channel = Channel;
    Event.Param1 = AudioSample(Program);
    EnqueueMidiMessage(Event);
}


void MidiHandler::ChannelPressure(uint8_t Pressure, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::ChannelPressure;
    Event.Channel = Channel;
    Event.Param1 = AudioSample(Pressure) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::PitchBend(AudioSample Value, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::PitchBend;
    Event.Channel = Channel;
    Event.Param1 = Value;
    EnqueueMidiMessage(Event);
}


void MidiHandler::Reset()
{
    TRACEABLE_SCOPE;

#if MIDI_NEEDS_LOCKS
    TRACEABLE_LOCK_GUARD(PendingMidiCrit);
#endif

    PendingMidiMessages.clear();
    PendingMidiMessages.emplace_back(MidiMessageType::Reset, 0, 0.0, 0.0);
}


void MidiHandler::EnqueueMidiMessage(MidiMessage& Message)
{
    TRACEABLE_SCOPE;

#if MIDI_NEEDS_LOCKS
    TRACEABLE_LOCK_GUARD(PendingMidiCrit);
#endif

    PendingMidiMessages.push_back(Message);
}


bool MidiHandler::PopMidiMessage(MidiMessage& Message)
{
    TRACEABLE_SCOPE;

#if MIDI_NEEDS_LOCKS
    TRACEABLE_LOCK_GUARD(PendingMidiCrit);
#endif

    if (PendingMidiMessages.empty())
    {
        return false;
    }
    else
    {
        Message = PendingMidiMessages.front();
        PendingMidiMessages.pop_front();
        return true;
    }
}


void Midi::Reset()
{
    SendMidiReset = true;
}


void Midi::ProcessEvents(MidiHandler* Handler)
{
    if (Driver)
    {
        Driver->ProcessEvents(Handler);
        if (SendMidiReset.exchange(false))
        {
            Handler->Reset();
        }
    }
}


void Midi::Init()
{
#ifdef MIDI_ALSA
    Driver = std::make_unique<AlsaMidiDriver>();
#elifdef MIDI_MMEAPI
    Driver = std::make_unique<MmeApiMidiDriver>();
#else
    std::println("No MIDI driver is available.");
#endif
}


void Midi::Shutdown()
{
    if (Driver)
    {
        Driver.reset();
    }
}
