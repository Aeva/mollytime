
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

#include <vector>
#include <atomic>
#include <mutex>
#include <print>
#include <cstring>
#include <chrono>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include "pipewire.h"
#include "perf.h"


using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = Clock::duration;


static bool PipeWireInitialized = false;
static AudioStream* StreamSingleton = nullptr;


struct ThreadShared
{
    DECLARE_TRACEABLE_MUTEX(Mutex);
    ScratchSharedPtr PendingProgram = nullptr;

    std::atomic<float> TemporalPressure = 0.0;
};


struct StreamRealTimeThread
{
    void SetupPorts(ThreadShared* InBufferState, pw_stream* InStream, int SampleRate);

    static void OnProcess(void *UserData);

private:
    ThreadShared* BufferState = nullptr;
    pw_stream* Stream = nullptr;
    double SampleInterval = 0.0;

    ScratchSharedPtr Program = nullptr;
    TimePoint LastFrameStart;

    float TemporalPressure = 0.0f;
    std::vector<float> FramePressure;
    int FramePressureIndex = 0;
    int FramePressureCount = 0;

    void ResetFramePressure();
    void OnProcessInner();
};


void StreamRealTimeThread::SetupPorts(ThreadShared* InBufferState, pw_stream* InStream, int SampleRate)
{
    BufferState = InBufferState;
    Stream = InStream;
    SampleInterval = 1.0 / double(SampleRate);
    ResetFramePressure();
}


void StreamRealTimeThread::ResetFramePressure()
{
    FramePressure.resize(100, 0.0f);
    FramePressureIndex = 0;
    FramePressureCount = 0;
    TemporalPressure = 0.0f;
}


void StreamRealTimeThread::OnProcess(void *UserData)
{
    StreamRealTimeThread* RealTimeThread = (StreamRealTimeThread*)UserData;
    RealTimeThread->OnProcessInner();
}


void StreamRealTimeThread::OnProcessInner()
{
    TRACEABLE_SCOPE;
    pw_buffer* StreamBuffer = pw_stream_dequeue_buffer(Stream);
    if (!StreamBuffer)
    {
        return;
    }

    TimePoint FrameStart = Clock::now();

    struct spa_data& StreamMetaData = StreamBuffer->buffer->datas[0];

    int Stride = sizeof(float); // multiply by channel count if you convert this to stereo
    int FrameCount = StreamMetaData.maxsize / Stride;

    if (StreamBuffer->requested)
    {
        FrameCount = SPA_MIN(StreamBuffer->requested, FrameCount);
    }

    float* WritePtr = (float*)StreamMetaData.data;

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

    StreamMetaData.chunk->offset = 0;
    StreamMetaData.chunk->stride = Stride;
    StreamMetaData.chunk->size = FrameCount * Stride;

    pw_stream_queue_buffer(Stream, StreamBuffer);

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
}


struct PipeWireStream : public AudioStream
{
    PipeWireStream();
    virtual float GetTemporalPressureInner() override;
    virtual void Setup(int SampleRate) override;
    virtual void ProgramChange(ScratchSharedPtr& NewProgram) override;
    virtual void Run() override;
    virtual void Reset() override;
    virtual ~PipeWireStream();

private:
    struct pw_thread_loop* Loop = nullptr;
    struct pw_stream* Stream = nullptr;

    StreamRealTimeThread RealTimeThread;
    ThreadShared BufferState;
};


PipeWireStream::PipeWireStream()
{
}


float PipeWireStream::GetTemporalPressureInner()
{
    TRACEABLE_SCOPE;
    return BufferState.TemporalPressure.load();
}


void PipeWireStream::Setup(int SampleRate)
{
    std::vector<const spa_pod*> Params;

    uint8_t BuilderBuffer[1024];
    spa_pod_builder PodBuilder = SPA_POD_BUILDER_INIT(BuilderBuffer, sizeof(BuilderBuffer));

    Loop = pw_thread_loop_new("mollytime", nullptr);
    pw_thread_loop_lock(Loop);

    static const pw_stream_events StreamEvents =
    {
        .version = PW_VERSION_STREAM_EVENTS,
        .process = StreamRealTimeThread::OnProcess,
    };

    Stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(Loop),
        "mollytime",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Music",
            nullptr),
        &StreamEvents,
        &RealTimeThread);

    RealTimeThread.SetupPorts(&BufferState, Stream, SampleRate);

    {
        spa_audio_info_raw StreamFormat =
        {
            .format = SPA_AUDIO_FORMAT_DSP_F32,
            .rate = (uint32_t)SampleRate,
            .channels = 1
        };
        Params.push_back(spa_format_audio_raw_build(&PodBuilder, SPA_PARAM_EnumFormat, &StreamFormat));
    }

    pw_thread_loop_unlock(Loop);

    {
        int Flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS;
        int StatusCode = pw_stream_connect(
            Stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, (enum pw_stream_flags)Flags, Params.data(), Params.size());

        if (StatusCode < 0)
        {
            std::print("Can't connect pipewire stream :(\n");
            Reset();
        }
    }
}


void PipeWireStream::ProgramChange(ScratchSharedPtr& NewProgram)
{
    TRACEABLE_SCOPE;
    TRACEABLE_LOCK_GUARD(BufferState.Mutex);
    BufferState.PendingProgram = NewProgram;
}


void PipeWireStream::Run()
{
    TRACEABLE_SCOPE;
    if (Loop && Stream)
    {
        pw_thread_loop_lock(Loop);
        pw_thread_loop_start(Loop);
        pw_thread_loop_unlock(Loop);
    }
}


void PipeWireStream::Reset()
{
    TRACEABLE_SCOPE;
    if (Loop)
    {
        pw_thread_loop_lock(Loop);
    }
    if (Stream)
    {
        pw_stream_destroy(Stream);
        Stream = nullptr;
    }
    if (Loop)
    {
        pw_thread_loop_unlock(Loop);
        pw_thread_loop_stop(Loop);
        pw_thread_loop_destroy(Loop);
        Loop = nullptr;
    }
}


PipeWireStream::~PipeWireStream()
{
    Reset();
}


AudioStream* AudioStream::Get()
{
    if (!PipeWireInitialized)
    {
        PipeWireInitialized = true;
        int argc = 0;
        char*** argv = nullptr;
        pw_init(&argc, argv);
    }
    if (StreamSingleton == nullptr)
    {
        StreamSingleton = new PipeWireStream();
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
    if (PipeWireInitialized)
    {
        PipeWireInitialized = false;
        pw_deinit();
    }
}


float AudioStream::GetTemporalPressure()
{
    return Get()->GetTemporalPressureInner();
}
