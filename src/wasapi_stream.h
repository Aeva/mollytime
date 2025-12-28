
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

#ifdef AUDIO_WASAPI

#include "audio_backend.h"

#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#define NOMINMAX
#include <Audioclient.h>
#include <mmdeviceapi.h>
#include <winrt/base.h>
#undef NOMINMAX
#undef VC_EXTRALEAN
#undef WIN32_LEAN_AND_MEAN


template<typename T>
using ComPtr = winrt::com_ptr<T>;


struct WasapiThreadShared final : AudioThreadShared
{
    std::vector<float> OutputSamplesLeft;
    std::vector<float> OutputSamplesRight;
};


class WasapiRealTimeThread final : public RealTimeAudioThread
{
    ComPtr<IMMDevice> Device;
    ComPtr<IAudioClient> AudioClient;
    ComPtr<IAudioRenderClient> RenderClient;
    ComPtr<IAudioClock> AudioClock;
    UINT32 BufferSize;
    HANDLE EventHandle = nullptr;

    std::thread LoopThread;
    std::atomic<bool> IsRunning;
    void Loop();

public:
    WasapiRealTimeThread(WasapiThreadShared* WasapiBufferState, int SampleRate);
    virtual ~WasapiRealTimeThread() override;

    virtual void BeginFrame(FramePointers& Frame) override;
    virtual void EndFrame(FramePointers& Frame) override;
};


class WasapiStream final : public AudioStream
{
    WasapiThreadShared BufferState;
    WasapiRealTimeThread RealTimeThread;

public:
    WasapiStream(int SampleRate);

    virtual float GetTemporalPressure() override;
    virtual void ProgramChange(ScratchUniquePtr&& NewProgram) override;
};

#endif
