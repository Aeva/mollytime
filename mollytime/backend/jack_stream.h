
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

#include "audio_driver.h"

#include <map>
#include <vector>


struct _jack_port;      // Forward declares `typedef struct _jack_port jack_port_t` from <jack/types.h>
struct _jack_client;    // Forward declares `typedef struct _jack_client jack_client_t` from <jack/types.h>


struct JackThreadShared : AudioThreadShared
{
    std::vector<struct _jack_port*> OutputPorts;
    std::map<TileHandle, struct _jack_port*> InputPorts;
    std::map<TileHandle, struct _jack_port*> AuxOutPorts;
};


class JackRealTimeThread final : public RealTimeAudioThread
{
public:
    JackRealTimeThread(JackThreadShared* JackBufferState, int SampleRate);

    virtual void BeginFrame(FramePointers& Frame) override;
    int OnProcess(size_t FrameCount);
};


class JackStream final : public AudioStream
{
    const char* ClientName = "mollytime";

    struct _jack_client* JackClient;
    JackThreadShared BufferState;
    JackRealTimeThread RealTimeThread;

public:
    JackStream(int SampleRate);
    virtual ~JackStream() override;

    static constexpr bool IsAvailable()
    {
#if defined(ENABLE_JACK)
        return true;
#else
        return false;
#endif        
    }

    static constexpr std::string_view GetName()
    {
        return "Jack";
    }

    virtual bool ImplementsInput() override
    {
        return true;
    }

    virtual bool ImplementsAux() override
    {
        return true;
    }

    virtual float GetTemporalPressure() override;
    virtual void ProgramChange(ScratchUniquePtr&& NewProgram) override;
};
