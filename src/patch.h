
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

#include <unordered_map>
#include <set>
#include <tuple>
#include "tiles.h"

/* TODO:

The `Patch` class is meant to replace the part of `program_card` that describes
the wire connections, but it should be made to be the authoritative
representation of the graph program itself.

I've been writing with the assumption that the `MagicTile` is the basic unit of
the graph.  That was a bad idea because that will devolve into a mess of shared
pointers, were I to continue with that strat.

What I should do instead is have `Patch` manage everything as a collection of
metadata containers, have `program_card` subclass it, and have everything
accessed via accessors and handles.  If done correctly, this could simplify
serialization, and allow modifications to be sent to worker threads as copies,
removing the need for locking.  A separate `Scratch` class would track the
running state of the patch in a std::unordered_map.

 */

struct Patch
{
    std::unordered_map<uint32_t, OpCode> TilesById;

    std::set<std::tuple<uint64_t, uint64_t>> Wires;
    std::unordered_map<uint64_t, std::set<uint64_t>> WireByInput;
    std::unordered_map<uint64_t, std::set<uint64_t>> WireByOutput;
};
