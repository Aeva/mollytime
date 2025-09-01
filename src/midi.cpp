
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
#include "alsa_midi.h"

#include <utility>
#include <memory>
#include <print>


static std::unique_ptr<MidiDriver> Driver;


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
    MidiMessage Event;
    Event.Type = MidiMessageType::PolyPress;
    Event.Channel = Channel;
    Event.Param1 = double(Note);
    Event.Param2 = double(Pressure) / 127.0;
    EnqueueMidiMessage(Event);
}


void MidiHandler::EnqueueMidiMessage(MidiMessage& Message)
{
    TRACEABLE_SCOPE;

    TRACEABLE_LOCK_GUARD(PendingMidiCrit);

    // See notes about reallocation in MidiHandler::SwapMidiMessageQueue.
    PendingMidiMessages.push_back(Message);
}


void MidiHandler::SwapMidiMessageQueue(std::vector<MidiMessage>& MessageQueue)
{
    TRACEABLE_SCOPE;

    TRACEABLE_LOCK_GUARD(PendingMidiCrit);

    // NOTE: This swap pattern *allows* for the frequency of implicit array resizes to be
    // reduced.  Because std::vector::clear is not supposed to change the capacity of the
    // vector, if the vectors being swapped are both persistent, then both vectors will
    // eventually reach equilibrium at some high water mark capacity.  Doing so has the
    // tradeoff that the memory is never or only rarely freed.  If the caller does a full
    // reset or passes a new vector in every time, then over-allocation is not a concern,
    // but MidiHandler::EnqueueMidiMessage becomes more expensive.
    MessageQueue.clear();
    std::swap(PendingMidiMessages, MessageQueue);
}


void Midi::ProcessEvents(MidiHandler* Handler)
{
    if (Driver)
    {
        Driver->ProcessEvents(Handler);
    }
}


void Midi::Init()
{
#ifdef MIDI_ALSA
    Driver = std::make_unique<AlsaMidiDriver>();
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
