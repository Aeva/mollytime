
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
#define FORCE_SAMPLE_PROFILING 0

#if defined(TRACY_ENABLE) && !FORCE_SAMPLE_PROFILING
#pragma warning(push)
#pragma warning(disable : 4464)
#include "tracy/Tracy.hpp"
#pragma warning(pop)

#define DECLARE_TRACEABLE_MUTEX(NAME) TracyLockable(std::mutex, NAME)
#define TRACEABLE_LOCK_GUARD(LOCK_VAR) std::lock_guard<LockableBase(std::mutex)> LOCK_GUARD_##__LINE__(LOCK_VAR)
#define TRACEABLE_SCOPE ZoneScoped
#define TRACEABLE_NAMED_SCOPE(NAME) ZoneScopedN(NAME)

inline bool IsProfilingEnabled()
{
    return true;
}

inline void PerfTrampoline(const char* Name, auto& Function, auto& ReturnVal)
{
    ZoneScoped;
    ZoneName(Name, strlen(Name));
    ReturnVal = Function();
}

#else

#define DECLARE_TRACEABLE_MUTEX(NAME) std::mutex NAME
#define TRACEABLE_LOCK_GUARD(LOCK_VAR) std::lock_guard<std::mutex> LOCK_GUARD_##__LINE__(LOCK_VAR)
#define TRACEABLE_SCOPE
#define TRACEABLE_NAMED_SCOPE(NAME)

inline bool IsProfilingEnabled()
{
    return false;
}

inline void PerfTrampoline(const char* Name, auto& Function, auto& ReturnVal)
{
    ReturnVal = Function();
}

#endif
