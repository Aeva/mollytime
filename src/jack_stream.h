
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

#include "audio_backend.h"

#include <map>
#include <vector>


struct _jack_client;
struct _jack_port;


struct JackThreadShared : AudioThreadShared
{
    std::vector<struct _jack_port*> OutputPorts;
    std::map<TileHandle, struct _jack_port*> InputPorts;
    std::map<TileHandle, struct _jack_port*> AuxOutPorts;
};


class JackRealTimeThread final : public RealTimeAudioThread
{
public:
    JackRealTimeThread(struct _jack_client* JackClient, JackThreadShared* JackBufferState, int SampleRate);

    virtual void BeginFrame(FramePointers& Frame) override;

    static int OnProcess(uint32_t FrameCount, void *UserData);
    int OnProcessInner(uint32_t FrameCount);
};


class JackStream final : public AudioStream
{
    const char* ClientName = "mollytime";

    struct _jack_client* JackClient;
    JackRealTimeThread RealTimeThread;
    JackThreadShared BufferState;

public:
    JackStream(int SampleRate);
    virtual ~JackStream() override;
    
    virtual float GetTemporalPressure() override;
    virtual void ProgramChange(ScratchSharedPtr& NewProgram) override;
};