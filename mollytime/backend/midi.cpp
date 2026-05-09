
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

#include <fmt/format.h>

#include <cassert>
#include <atomic>
#include <memory>
#include <utility>


static std::unique_ptr<MidiDriver> Driver;
static std::atomic_bool SendPatchReset = false;
static std::atomic<uint16_t> SendReleaseHeldNotes = 0;


void MidiHandler::NoteOn(uint8_t Note, uint8_t Velocity, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::Note;
    Event.Channel = Channel;
    Event.Param1 = double(Note);
    Event.Param2 = double(Velocity) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::NotePressure(uint8_t Note, uint8_t Pressure, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::PolyPress;
    Event.Channel = Channel;
    Event.Param1 = double(Note);
    Event.Param2 = double(Pressure) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::ControlChange7Bit(uint8_t Control, uint8_t Value, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::ControlChange;
    Event.Channel = Channel;
    Event.Param1 = double(Control);
    Event.Param2 = double(Value) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::ControlChange14Bit(uint8_t Control, uint16_t Value, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::ControlChange;
    Event.Channel = Channel;
    Event.Param1 = double(Control);
    Event.Param2 = std::min(double(Value) / 16383.0, 1.0); // educated guess
    EnqueueMidiMessage(Event);
}


void MidiHandler::ProgramChange(uint8_t Program, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::ProgramChange;
    Event.Channel = Channel;
    Event.Param1 = double(Program);
    EnqueueMidiMessage(Event);
}


void MidiHandler::ChannelPressure(uint8_t Pressure, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::ChannelPressure;
    Event.Channel = Channel;
    Event.Param1 = double(Pressure) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::PitchBend(double Value, uint8_t Channel)
{
    TRACEABLE_SCOPE;

    MidiMessage Event;
    Event.Type = MidiMessageType::PitchBend;
    Event.Channel = Channel;
    Event.Param1 = Value;
    EnqueueMidiMessage(Event);
}


void MidiHandler::PatchReset()
{
    TRACEABLE_SCOPE;

#if MIDI_NEEDS_LOCKS
    TRACEABLE_LOCK_GUARD(PendingMidiCrit);
#endif

    PendingMidiMessages.clear();
    PendingMidiMessages.push_back({ MidiMessageType::PatchReset, 0, 0.0, 0.0 });
}


void MidiHandler::ReleaseHeldNotes(uint16_t ChannelMask)
{
    TRACEABLE_SCOPE;

    #if MIDI_NEEDS_LOCKS
    TRACEABLE_LOCK_GUARD(PendingMidiCrit);
    #endif

    PendingMidiMessages.push_back({ MidiMessageType::ReleaseHeldNotes, ChannelMask, 0.0, 0.0 });
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


void Midi::PatchReset()
{
    SendPatchReset = true;
}


void Midi::ReleaseHeldNotes(uint16_t ChannelMask)
{
    SendReleaseHeldNotes.fetch_or(ChannelMask);
}


void Midi::ProcessEvents(MidiHandler* Handler)
{
    if (Driver)
    {
        Driver->ProcessEvents(Handler);
        if (SendPatchReset.exchange(false))
        {
            Handler->PatchReset();
        }
        if (const uint16_t ReleaseMask = SendReleaseHeldNotes.exchange(0))
        {
            Handler->ReleaseHeldNotes(ReleaseMask);
        }
    }
}


void Midi::Init(std::unique_ptr<MidiDriver>&& InDriver)
{
    assert(InDriver);
    Driver = std::move(InDriver);
}


void Midi::Shutdown()
{
    if (Driver)
    {
        Driver.reset();
    }
}
