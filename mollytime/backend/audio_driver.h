
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

#include "common.h"
#include "patch.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#define BOOST_ATOMIC_NO_LIB
#include <boost/atomic.hpp>
#undef BOOST_ATOMIC_NO_LIB
#pragma clang diagnostic pop


using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;


struct AudioThreadShared
{
    DECLARE_TRACEABLE_MUTEX(Mutex);
    ScratchUniquePtr PendingProgram = nullptr;

    boost::atomic_float_t TemporalPressure = 0.0;
};


struct FramePointers
{
    int SampleCount = 0;
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

    ScratchUniquePtr Program = nullptr;

    std::vector<float> FramePressure;
    int FramePressureIndex = 0;
    int FramePressureCount = 0;

    virtual ~RealTimeAudioThread() {}

    virtual void BeginFrame(FramePointers& Frame) = 0;
    virtual void EndFrame(FramePointers& Frame) {};

    void ResetFramePressure();
    void AdvanceFrames(FramePointers& Frame);
};


struct AudioStream
{
    virtual ~AudioStream() {}

    virtual float GetTemporalPressure() = 0;
    virtual void ProgramChange(ScratchUniquePtr&& NewProgram) = 0;
};


namespace Audio
{
    AudioStream* GetStream();

    MOLLY_API void Init(int SampleRate);
    MOLLY_API void Shutdown();
    MOLLY_API float GetTemporalPressure();
};
