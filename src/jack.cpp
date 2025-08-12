
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

#ifdef ENABLE_JACK

#include <vector>
#include <atomic>
#include <mutex>
#include <print>
#include <format>
#include <cstring>
#include <string>
#include <chrono>

#include <signal.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <jack/jack.h>

#include "audio_backend.h"
#include "perf.h"


using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = Clock::duration;


static bool JackInitialized = false;
static AudioStream* StreamSingleton = nullptr;
jack_client_t* JackClient;
const char* ClientName = "mollytime";


void JackShutdown(void* UserData = nullptr)
{
    if (JackInitialized)
    {
        JackInitialized = false;
        jack_client_close(JackClient);
    }
}


struct ThreadShared
{
    DECLARE_TRACEABLE_MUTEX(Mutex);
    ScratchSharedPtr PendingProgram = nullptr;

    std::atomic<float> TemporalPressure = 0.0;
};


struct StreamRealTimeThread
{
    void SetupPorts(ThreadShared* InBufferState, int SampleRate);

    static int OnProcess(jack_nframes_t FrameCount, void *UserData);

private:
    jack_port_t* OutputMono = nullptr;

    ThreadShared* BufferState = nullptr;
    double SampleInterval = 0.0;

    ScratchSharedPtr Program = nullptr;
    TimePoint LastFrameStart;

    float TemporalPressure = 0.0f;
    std::vector<float> FramePressure;
    int FramePressureIndex = 0;
    int FramePressureCount = 0;

    void ResetFramePressure();
    int OnProcessInner(jack_nframes_t FrameCount);
};


void StreamRealTimeThread::SetupPorts(ThreadShared* InBufferState, int SampleRate)
{
    BufferState = InBufferState;
    SampleInterval = 1.0 / double(SampleRate);
    ResetFramePressure();

    OutputMono = jack_port_register(
        JackClient, "output_mono", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    if (OutputMono == nullptr)
    {
        JackShutdown();
        throw std::runtime_error("Unable to create jack ports!\n");
    }

    if (jack_activate(JackClient))
    {
        JackShutdown();
        throw std::runtime_error("Unable to activate jack client!\n");
    }

    const char** Ports = jack_get_ports(JackClient, nullptr, nullptr, JackPortIsPhysical|JackPortIsInput);
    if (Ports == nullptr)
    {
        JackShutdown();
        throw std::runtime_error("No physical playback ports!\n");
    }

    if (jack_connect(JackClient, jack_port_name(OutputMono), Ports[0]))
    {
        // unable to connect to physical port
    }

    if (jack_connect(JackClient, jack_port_name(OutputMono), Ports[1]))
    {
        // unable to connect to physical port
    }

    jack_free(Ports);
}


void StreamRealTimeThread::ResetFramePressure()
{
    FramePressure.resize(100, 0.0f);
    FramePressureIndex = 0;
    FramePressureCount = 0;
    TemporalPressure = 0.0f;
}


int StreamRealTimeThread::OnProcess(jack_nframes_t FrameCount, void *UserData)
{
    StreamRealTimeThread* RealTimeThread = (StreamRealTimeThread*)UserData;
    return RealTimeThread->OnProcessInner(FrameCount);
}


int StreamRealTimeThread::OnProcessInner(jack_nframes_t FrameCount)
{
    TRACEABLE_SCOPE;
    auto* WritePtr = (jack_default_audio_sample_t*)jack_port_get_buffer(OutputMono, FrameCount);

    TimePoint FrameStart = Clock::now();
    {
        TRACEABLE_LOCK_GUARD(BufferState->Mutex);
        if (BufferState->PendingProgram)
        {
            Program = BufferState->PendingProgram;
            BufferState->PendingProgram = nullptr;
            ResetFramePressure();
        }
        BufferState->TemporalPressure.store(TemporalPressure);
    }

    if (Program)
    {
        TRACEABLE_NAMED_SCOPE("MIDI PHASE");
        Midi::ProcessEvents(Program.get());
    }

    TimePoint EvalStart;
    TimePoint EvalEnd;
    if (Program)
    {
        EvalStart = Clock::now();
        for (int Frame = 0; Frame < FrameCount; ++Frame)
        {
            WritePtr[Frame] = float(Program->Eval(SampleInterval));
        }
        EvalEnd = Clock::now();
    }
    else
    {
        EvalStart = Clock::now();
        for (int Frame = 0; Frame < FrameCount; ++Frame)
        {
            WritePtr[Frame] = 0.0f;
        }
        EvalEnd = Clock::now();
    }

    Duration EvalDelta = Duration(EvalEnd - EvalStart);
    Duration FrameDelta = Duration(FrameStart - LastFrameStart);

    FramePressure[FramePressureIndex++] = float(EvalDelta.count()) / float(FrameDelta.count());

    FramePressureCount = std::max(FramePressureIndex, FramePressureCount);
    FramePressureIndex %= FramePressure.size();
    if (Program && FramePressureCount == FramePressure.size())
    {
        TemporalPressure = FramePressure[0];
        for (int Index = 1; Index < FramePressureCount; ++Index)
        {
            TemporalPressure += FramePressure[Index];
        }
        TemporalPressure /= float(FramePressureCount);
    }
    else
    {
        TemporalPressure = 0.0f;
    }
    LastFrameStart = FrameStart;

    return 0;
}


struct JackStream : public AudioStream
{
    JackStream();
    virtual float GetTemporalPressureInner() override;
    virtual void Setup(int SampleRate) override;
    virtual void ProgramChange(ScratchSharedPtr& NewProgram) override;
    virtual void Run() override;
    virtual void Reset() override;
    virtual ~JackStream();

private:
    StreamRealTimeThread RealTimeThread;
    ThreadShared BufferState;
};


JackStream::JackStream()
{
    jack_set_process_callback(JackClient, StreamRealTimeThread::OnProcess, &RealTimeThread);
    jack_on_shutdown(JackClient, JackShutdown, 0);
}


float JackStream::GetTemporalPressureInner()
{
    TRACEABLE_SCOPE;
    return BufferState.TemporalPressure.load();
}


void JackStream::Setup(int SampleRate)
{
    RealTimeThread.SetupPorts(&BufferState, SampleRate);
}


void JackStream::ProgramChange(ScratchSharedPtr& NewProgram)
{
    TRACEABLE_SCOPE;
    TRACEABLE_LOCK_GUARD(BufferState.Mutex);
    BufferState.PendingProgram = NewProgram;
}


void JackStream::Run()
{
    // TODO dead code?
}


void JackStream::Reset()
{
    // TODO dead code?
}


JackStream::~JackStream()
{
}


AudioStream* AudioStream::Get()
{
    if (!JackInitialized)
    {
        JackInitialized = true;

        jack_status_t JackStatus;
        jack_options_t JackOptions = JackNoStartServer;
        JackClient = jack_client_open(ClientName, JackOptions, &JackStatus, nullptr);
        if (JackClient == nullptr)
        {
            throw std::runtime_error(std::format("jack_client_open() failed, jack status = {}\n", (int)JackStatus));
        }
        if (JackStatus & JackNameNotUnique)
        {
            ClientName = jack_get_client_name(JackClient);
        }
    }
    if (StreamSingleton == nullptr)
    {
        StreamSingleton = new JackStream();
    }
    return StreamSingleton;
}


void AudioStream::Init(int SampleRate)
{
    TRACEABLE_SCOPE;
    Get()->Setup(SampleRate);
    Get()->Run();
}


void AudioStream::Shutdown()
{
    TRACEABLE_SCOPE;
    if (StreamSingleton != nullptr)
    {
        delete StreamSingleton;
        StreamSingleton = nullptr;
    }
    JackShutdown();
}


float AudioStream::GetTemporalPressure()
{
    return Get()->GetTemporalPressureInner();
}


#endif // ENABLE_JACK
