
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

#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>

#include "patch.h"


using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = Clock::duration;


struct AudioThreadShared
{
    DECLARE_TRACEABLE_MUTEX(Mutex);
    ScratchSharedPtr PendingProgram = nullptr;

    std::atomic<float> TemporalPressure = 0.0;
};


struct FramePointers
{
    size_t SampleCount = 0;
    float* OutLeft = nullptr;
    float* OutRight = nullptr;
    std::vector<std::tuple<float*, double*>> InPtrs;
    std::vector<std::tuple<double*, float*>> AuxPtrs;
};


struct RealTimeAudioThread
{
protected:
    AudioThreadShared* BufferState = nullptr;
    double SampleInterval = 0.0;

    ScratchSharedPtr Program = nullptr;
    TimePoint LastFrameStart;

    float TemporalPressure = 0.0f;
    std::vector<float> FramePressure;
    int FramePressureIndex = 0;
    int FramePressureCount = 0;

    virtual void BeginFrame(FramePointers& Frame) = 0;
    virtual void EndFrame(FramePointers& Frame) {};

    void ResetFramePressure();
    void AdvanceFrames(FramePointers& Frame);
};


struct AudioStream
{
    static AudioStream* Get();
    static void Init(int SampleRate);
    static void Shutdown();
    static float GetTemporalPressure();
    virtual float GetTemporalPressureInner() = 0;

    virtual void Setup(int SampleRate) = 0;
    virtual void ProgramChange(ScratchSharedPtr& NewProgram) = 0;

    virtual ~AudioStream() {};
};
