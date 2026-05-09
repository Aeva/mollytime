
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

#include "midi.h"

#include <string_view>

struct _snd_seq;    // Forward declares `typedef struct _snd_seq snd_seq_t` from <alsa/seq.h>.

class AlsaMidiDriver final : public MidiDriver
{
    struct _snd_seq *SeqHandle = nullptr;
    int MidiQueue = -1;
    int MidiInPort = -1;
    int MidiOutPort = -1;

public:
    AlsaMidiDriver();
    virtual ~AlsaMidiDriver() override;

    static constexpr bool IsAvailable()
    {
#if defined(MIDI_ALSA)
        return true;
#else
        return false;
#endif
    }

    static constexpr std::string_view GetName()
    {
        return "Alsa";
    }

    void ProcessEvents(MidiHandler* Handler) override;
};
