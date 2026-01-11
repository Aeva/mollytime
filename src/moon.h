
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

#include "thunks.h"


struct MoonThunk : public InstructionThunk
{
    static constexpr InstructionInfo<4, 1, 2> Info = \
    {
        OpCode::MOON, "moon",
        {{
            {"lat", 41.881944, InputCombiner::ADD},
            {"long", -87.627778, InputCombiner::ADD},
            {"julian\ndate", -1.0, InputCombiner::ADD},
            {"speed", 1.0, InputCombiner::ADD},
        }},
        {"altitude"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override;

    virtual ~MoonThunk() {};
};
