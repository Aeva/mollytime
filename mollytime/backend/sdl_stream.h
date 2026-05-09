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

struct SDL_AudioStream;


struct SDLThreadShared final : AudioThreadShared
{
    std::vector<float> OutputSamplesLeft;
    std::vector<float> OutputSamplesRight;
};


class SDLRealTimeThread final : public RealTimeAudioThread
{
    SDL_AudioStream* SDLStream;

    static void OnAudioRequested(void* UserData, SDL_AudioStream* Stream, int AdditionalAmount, int TotalAmount);

public:
    SDLRealTimeThread(SDLThreadShared* WasapiBufferState, int SampleRate);
    virtual ~SDLRealTimeThread() override;

    virtual void BeginFrame(FramePointers& Frame) override;
    virtual void EndFrame(FramePointers& Frame) override;
};


class SDLStream final : public AudioStream
{
    SDLThreadShared BufferState;
    SDLRealTimeThread RealTimeThread;

public:
    SDLStream(int SampleRate);

    static constexpr bool IsAvailable()
    {
#if defined(AUDIO_SDL)
        return true;
#else
        return false;
#endif        
    }

    static constexpr std::string_view GetName()
    {
        return "SDL";
    }

    virtual float GetTemporalPressure() override;
    virtual void ProgramChange(ScratchUniquePtr&& NewProgram) override;
};
