
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

#pragma once

#ifdef MIDI_ALSA
// Polling for MIDI events happens on the audio thread.
#define MIDI_NEEDS_LOCKS 0
#else
// MIDI events are recorded wherever the OS wants.
#define MIDI_NEEDS_LOCKS 1
#endif

#include <cstdint>
#include <deque>
#if MIDI_NEEDS_LOCKS
#include <mutex>
#endif

#include "perf.h"

using AudioSample = float;


enum class MidiMessageType : uint8_t
{
    Note,
    PolyPress,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend,
    Reset,
};


struct MidiMessage
{
    MidiMessageType Type;
    uint8_t Channel;
    AudioSample Param1;
    AudioSample Param2;
};


struct MidiHandler
{
    void NoteOn(uint8_t Note, uint8_t Velocity, uint8_t Channel);

    void NoteOff(uint8_t Note, uint8_t Channel)
    {
        NoteOn(Note, 0, Channel);
    }

    void NotePressure(uint8_t Note, uint8_t Pressure, uint8_t Channel);

    /* Handles a regular 7-bit control change event.
     */
    void ControlChange7Bit(uint8_t Control, uint8_t Value, uint8_t Channel);

    /* Handles a 14-bit control change event.
     */
    void ControlChange14Bit(uint8_t Control, uint16_t Value, uint8_t Channel);

    void ProgramChange(uint8_t Program, uint8_t Channel);

    void ChannelPressure(uint8_t Value, uint8_t Channel);

    void PitchBend(AudioSample Value, uint8_t Channel);

    /* Drop all pending midi events and generate some a fake one to tell the running audio
     * thread to reset all polyphony voices.
     */
    void Reset();

    void EnqueueMidiMessage(MidiMessage& Message);

    /* MIDI serial connections run at 31250 baud, which means there's a maximum throughput of
     * almost exactly 1302 3-byte packets per second (e.g. note and CC events).  Since the audio
     * thread currently drives all processing, and the audio thread runs at 48 khz, we can get
     * away with processing exactly one midi packet per audio frame.  This prevents accidentally
     * dropping note events and removes the need for explicit caching of values for things like
     * CC codes.
     *
     * As such, PopMidiMessage is intended to only be called ONCE per audio frame.
     */
    bool PopMidiMessage(MidiMessage& Message);

private:
#if MIDI_NEEDS_LOCKS
    DECLARE_TRACEABLE_MUTEX(PendingMidiCrit);
#endif
    std::deque<MidiMessage> PendingMidiMessages;
};


struct MidiDriver
{
    virtual ~MidiDriver() {}
    virtual void ProcessEvents(MidiHandler* Handler) = 0;
};


namespace Midi
{
    void Reset();
    void ProcessEvents(MidiHandler* Handler);
    void Init();
    void Shutdown();
}
