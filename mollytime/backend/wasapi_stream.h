
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

#include <vector>


struct WasapiThreadShared final : AudioThreadShared
{
    std::vector<float> OutputSamplesLeft;
    std::vector<float> OutputSamplesRight;
};


class WasapiStream final : public AudioStream
{
    WasapiThreadShared BufferState;
    std::unique_ptr<class WasapiRealTimeThread> RealTimeThread;

public:
    WasapiStream(int SampleRate);
    ~WasapiStream();

    static constexpr bool IsAvailable()
    {
#if defined(AUDIO_WASAPI)
        return true;
#else
        return false;
#endif        
    }

    static constexpr std::string_view GetName()
    {
        return "Wasapi";
    }

    virtual float GetTemporalPressure() override;
    virtual void ProgramChange(ScratchUniquePtr&& NewProgram) override;
};
