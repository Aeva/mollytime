
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

#include <cstdint>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <set>
#include <vector>
#include <string>
#include <memory>


// TileId
using TileHandle = uint32_t;


// TileId is the upper DWORD, Port Index lower DWORD
using PortHandle = uint64_t;


using WireHandle = std::tuple<PortHandle, PortHandle>;


enum class OpCode : uint32_t
{
    CONST = 0,
    OUT,
    SIN,
    ADD,
    MUL,
    MIN,
    MAX,
    Count
};


TileHandle PortHandleTilePart(PortHandle Handle);
uint32_t PortHandlePortIndexPart(PortHandle Handle);


struct RunningState
{
    RunningState(double InSample)
        : Sample(InSample)
    {
    }
    double Get()
    {
        return Sample;
    }
    void Set(double NewSample)
    {
        Sample = NewSample;
    }

private:
    double Sample;
};

using RunningStateSharedPtr = std::shared_ptr<RunningState>;


struct InstructionThunk
{
    virtual void Crank(double SampleInterval) = 0;
    virtual ~InstructionThunk() {};
};


struct Scratch
{
    std::vector<std::shared_ptr<InstructionThunk>> Program;
    RunningStateSharedPtr Output;

    double Eval(double SampleInterval);
};

using ScratchSharedPtr = std::shared_ptr<Scratch>;


struct Patch
{
    std::unordered_map<TileHandle, OpCode> TileSymbols;
    std::unordered_map<TileHandle, double> TileConstants;
    std::unordered_map<TileHandle, std::string> TileNames;

    std::set<WireHandle> Wires;
    std::unordered_map<PortHandle, std::set<PortHandle>> ByInput;
    std::unordered_map<PortHandle, std::set<PortHandle>> ByOutput;

    Patch();

    TileHandle MakeTile(OpCode Symbol);
    TileHandle MakeTile(double Constant);
    void EraseTile(TileHandle Tile);

    OpCode GetTileSymbol(TileHandle Tile);

    std::string GetTileName(TileHandle Tile);
    void SetTileName(TileHandle Tile, std::string NewName);

    double GetConstant(TileHandle Tile);
    void SetConstant(TileHandle Tile, double NewValue);

    std::string GetTileLabel(TileHandle Tile);

    std::vector<PortHandle> GetTileInputPorts(TileHandle Tile);
    std::vector<PortHandle> GetTileOutputPorts(TileHandle Tile);
    std::string GetTileInputName(PortHandle Port);
    std::string GetTileOutputName(PortHandle Port);

    void Connect(PortHandle OutputPort, PortHandle InputPort);
    void Disconnect(PortHandle OutputPort, PortHandle InputPort);
    void ToggleConnection(PortHandle OutputPort, PortHandle InputPort);

    bool CanConnect(TileHandle OutputTile, TileHandle InputTile);
    std::optional<WireHandle> GetImplicitWire(TileHandle OutputTile, TileHandle InputTile);

private:
    void ReplaceConstantOutput(TileHandle Tile, double NewValue);

    TileHandle LastAssignedTileHandle;
    std::unordered_map<PortHandle, RunningStateSharedPtr> ActiveOutputs;

    void Recompile();
    ScratchSharedPtr Compile();
};
