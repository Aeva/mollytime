
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

#include "patch.h"


struct MoonThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Latitude;
    std::vector<RunningStateSharedPtr> Longitude;
    std::vector<RunningStateSharedPtr> JulianDate;
    std::vector<RunningStateSharedPtr> Speed;
    RunningStateSharedPtr Altitude = nullptr;

    // If JulianDate was unset, then this will cache the current Julian Date
    // at the time the tile was activated.
    RunningStateSharedPtr OriginDate = nullptr;
    RunningStateSharedPtr ElapsedSeconds = nullptr;

    virtual void Crank(double SampleInterval) override;

    virtual ~MoonThunk() {};
};
