
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

#include "audio_backend.h"


void RealTimeAudioThread::ResetFramePressure()
{
    FramePressure.resize(100, 0.0f);
    FramePressureIndex = 0;
    FramePressureCount = 0;
    TemporalPressure = 0.0f;
}


void RealTimeAudioThread::AdvanceFrames(FramePointers& Frame)
{
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

        // Hook for gathering the audio buffer read and write pointers:
        BeginFrame(Frame);
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
        for (int SampleIndex = 0; SampleIndex < Frame.SampleCount; ++SampleIndex)
        {
            // Copy the applicable input samples into the patch's input registers:
            for (auto [ReadPtr, WritePtr] : Frame.InPtrs)
            {
                *WritePtr = double(ReadPtr[SampleIndex]);
            }

            // Advance the program by one frame:
            Program->Crank(SampleInterval, Frame.OutLeft[SampleIndex], Frame.OutRight[SampleIndex]);

            // Copy the applicable output samples from the patch's output registers:
            for (auto [ReadPtr, WritePtr] : Frame.AuxPtrs)
            {
                WritePtr[SampleIndex] = float(*ReadPtr);
            }
        }
        EvalEnd = Clock::now();
    }
    else
    {
        EvalStart = Clock::now();
        for (int SampleIndex = 0; SampleIndex < Frame.SampleCount; ++SampleIndex)
        {
            Frame.OutLeft[SampleIndex] = 0.0f;
            Frame.OutRight[SampleIndex] = 0.0f;
        }
        for (auto [ReadPtr, WritePtr] : Frame.AuxPtrs)
        {
            for (int SampleIndex = 0; SampleIndex < Frame.SampleCount; ++SampleIndex)
            {
                WritePtr[SampleIndex] = 0.0f;
            }
        }
        EvalEnd = Clock::now();
    }

    // Hook for notifying the audio API that the data is ready, should it require such a thing.
    EndFrame(Frame);

    Duration EvalDelta = Duration(EvalEnd - EvalStart);
    Duration FrameDelta = Duration(FrameStart - LastFrameStart);

    FramePressure[FramePressureIndex++] = float(EvalDelta.count()) / float(FrameDelta.count());

    FramePressureCount = std::max(FramePressureIndex, FramePressureCount);
    FramePressureIndex %= FramePressure.size();
    if (Program && FramePressureCount == static_cast<int>(FramePressure.size()))
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

// ---

#include "jack_stream.h"

#include <memory>

static std::unique_ptr<AudioStream> Stream;


struct StubStream final : AudioStream
{
    virtual float GetTemporalPressure() override { return 0.0f; }
    virtual void ProgramChange(ScratchSharedPtr& NewProgram) override {}
};


AudioStream* Audio::GetStream()
{
    return Stream.get();
}


void Audio::Init(int SampleRate)
{
#ifdef ENABLE_JACK
    Stream = std::make_unique<JackStream>(SampleRate);
#else
    Stream = std::make_unique<StubStream>();
#endif
}


void Audio::Shutdown()
{
    if(Stream)
    {
        Stream.reset();
    }
}


float Audio::GetTemporalPressure()
{
    if(!Stream)
    {
        return 0.0f;
    }
    
    return Stream->GetTemporalPressure();
}
