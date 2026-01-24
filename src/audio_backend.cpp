
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
#include "midi.h"

#if defined(AUDIO_SDL)
#include "sdl_stream.h"
#elif defined(ENABLE_JACK)
#include "jack_stream.h"
#elif defined(AUDIO_WASAPI)
#include "wasapi_stream.h"

#endif

#include <fmt/format.h>

#include <memory>



static std::unique_ptr<AudioStream> Stream;


void RealTimeAudioThread::ResetFramePressure()
{
    FramePressure.resize(100, 0.0f);
    FramePressureIndex = 0;
    FramePressureCount = 0;
    BufferState->TemporalPressure.store(0.0f);
}


void RealTimeAudioThread::AdvanceFrames(FramePointers& Frame)
{
    {
        TRACEABLE_LOCK_GUARD(BufferState->Mutex);
        if (BufferState->PendingProgram)
        {
            if (Program && BufferState->PendingProgram->Identity == Program->Identity)
            {
                BufferState->PendingProgram->Migrate(*Program);
            }
            else
            {
                Midi::Reset();
            }
            Program = std::move(BufferState->PendingProgram);
            BufferState->PendingProgram = nullptr;
            ResetFramePressure();
        }

        // Hook for gathering the audio buffer read and write pointers:
        BeginFrame(Frame);
    }

    if (Program)
    {
        TRACEABLE_NAMED_SCOPE("MIDI PHASE");
        Midi::ProcessEvents(Program.get());
    }

    TimePoint EvalStart = Clock::now();
    if (Program && Program->Program.size() > 0)
    {
        for (int SampleIndex = 0; SampleIndex < Frame.SampleCount; ++SampleIndex)
        {
#if MIDI_ALSA
            // Pump MIDI events.  For some reason, ALSA gets backed up without this, but mmeapi doesn't?
            {
                TRACEABLE_NAMED_SCOPE("MIDI PHASE");
                Midi::ProcessEvents(Program.get());
            }
#endif

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
    }
    else
    {
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
    }
    TimePoint EvalEnd = Clock::now();

    // Hook for notifying the audio API that the data is ready, should it require such a thing.
    EndFrame(Frame);

    const std::chrono::duration<double> EvalDelta = EvalEnd - EvalStart;
    const std::chrono::duration<double> Interval(SampleInterval * double(Frame.SampleCount));
    const double Pressure = EvalDelta.count() / Interval.count();
    FramePressure[FramePressureIndex++] = float(Pressure);

    FramePressureCount = std::max(FramePressureIndex, FramePressureCount);
    FramePressureIndex %= FramePressure.size();
    if (FramePressureCount > 0)
    {
        if (Program && FramePressureCount == static_cast<int>(FramePressure.size()))
        {
            float TemporalPressure = FramePressure[0];
            for (int Index = 1; Index < FramePressureCount; ++Index)
            {
                TemporalPressure += FramePressure[Index];
            }
            TemporalPressure /= float(FramePressureCount);
            BufferState->TemporalPressure.store(TemporalPressure);
        }
        else
        {
            BufferState->TemporalPressure.store(0.0f);
        }
    }
}


// This no-op stub is used if no AudioStream is available.  This is
// primarily intended to aid in porting Mollytime to new platforms.
struct StubStream final : AudioStream
{
    virtual float GetTemporalPressure() override { return 0.0f; }
    virtual void ProgramChange(ScratchUniquePtr&& NewProgram) override {}
};


AudioStream* Audio::GetStream()
{
    return Stream.get();
}


void Audio::Init(int SampleRate)
{
#if defined(AUDIO_SDL)
    Stream = std::make_unique<SDLStream>(SampleRate);
#elif defined(ENABLE_JACK)
    Stream = std::make_unique<JackStream>(SampleRate);
#elif defined(AUDIO_WASAPI)
    Stream = std::make_unique<WasapiStream>(SampleRate);
#else
    fmt::println("No audio stream implementation is available.");
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
