
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

#include <cstdint>
#include <vector>

struct MidiHandler
{
    virtual void NoteOn(uint8_t Note, uint8_t Velocity, uint8_t Channel)
    {
    }
    virtual void NoteOff(uint8_t Note, uint8_t Channel)
    {
        NoteOn(Note, 0, Channel);
    }
    virtual void NotePressure(uint8_t Note, uint8_t Pressure, uint8_t Channel)
    {
    }
};


struct MidiDriver
{
    virtual ~MidiDriver() {}
    virtual void ProcessEvents(MidiHandler* Handler) = 0;
};


namespace Midi
{
    void ProcessEvents(MidiHandler* Handler);
    void Init();
    void Shutdown();
}