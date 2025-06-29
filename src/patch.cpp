
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

#include <stdexcept>
#include <format>

#include "patch.h"


PortHandle MakePortHandle(TileHandle TileId, uint32_t PortNumber)
{
    return (uint64_t(TileId) << 32) | uint64_t(PortNumber);
}


TileHandle PortHandleTilePart(PortHandle Handle)
{
    return uint32_t(Handle >> 32);
}


uint32_t PortHandlePortIndexPart(PortHandle Handle)
{
    return uint32_t(Handle & 0xFFFFFFFF);
}


struct SymbolInfo
{
    std::vector<OpCode> Combiners;
    std::vector<std::string> DefaultNames;
    std::vector<std::vector<std::string>> InputNames;
    std::vector<std::vector<std::string>> OutputNames;

    SymbolInfo()
    {
        Combiners.resize((int)OpCode::Count);
        DefaultNames.resize((int)OpCode::Count);
        InputNames.resize((int)OpCode::Count);
        OutputNames.resize((int)OpCode::Count);

        Set(OpCode::CONST, OpCode::ADD, "const", {}, {"#"});
        Set(OpCode::OUT, OpCode::ADD, "out", {"out"}, {});
        Set(OpCode::SIN, OpCode::ADD, "sin", {"hz"}, {"amp"});
        Set(OpCode::ADD, OpCode::ADD, "add", {"+"}, {"="});
        Set(OpCode::MUL, OpCode::MUL, "mul", {"*"}, {"="});
        Set(OpCode::MIN, OpCode::MIN, "min", {"min"}, {"="});
        Set(OpCode::MAX, OpCode::MAX, "max", {"max"}, {"="});
    }

    void Set(OpCode Symbol, OpCode Combiner, std::string Name, std::vector<std::string> Inputs, std::vector<std::string> Outputs)
    {
        Combiners[(int)Symbol] = Combiner;
        DefaultNames[(int)Symbol] = Name;
        InputNames[(int)Symbol] = Inputs;
        OutputNames[(int)Symbol] = Outputs;
    }
};

const SymbolInfo SymbolInfoMap;


Patch::Patch()
    : LastAssignedTileHandle(0)
{
}


TileHandle Patch::MakeTile(OpCode Symbol)
{
    TileHandle AllocatedHandle = ++LastAssignedTileHandle;
    {
        auto Result = TileSymbols.try_emplace(AllocatedHandle, Symbol);
        if (!Result.second)
        {
            // 32 bit TileId values allow 4294967295 calls to MakeTile before the patch becomes uneditable.
            // If you hit MakeTile every second, it would take about 136 years before it becomes cashed out.
            // Should this somehow prove to be a problem, consider adding an id recycling system.
            throw std::runtime_error(std::format("Fatal error: TileId collision on {}!\n", AllocatedHandle));
        }
    }
    for (PortHandle Port : GetTileInputPorts(AllocatedHandle))
    {
        ByInput[Port] = std::set<PortHandle>();
    }
    for (PortHandle Port : GetTileOutputPorts(AllocatedHandle))
    {
        ByOutput[Port] = std::set<PortHandle>();
    }
    return AllocatedHandle;
}


TileHandle Patch::MakeTile(float Constant)
{
    const TileHandle AllocatedHandle = MakeTile(OpCode::CONST);
    auto Result = TileConstants.try_emplace(AllocatedHandle, Constant);
    if (!Result.second)
    {
        throw std::runtime_error(std::format("Unusual fatal error: cannot initialize constant tile {}!\n", AllocatedHandle));
    }
    return AllocatedHandle;
}


void Patch::EraseTile(TileHandle Tile)
{
    std::vector<WireHandle> MatchingWires;
    for (WireHandle Wire : Wires)
    {
        if (PortHandleTilePart(std::get<0>(Wire)) == Tile || PortHandleTilePart(std::get<1>(Wire)) == Tile)
        {
            MatchingWires.push_back(Wire);
        }
    }
    for (WireHandle Wire : MatchingWires)
    {
        Wires.erase(Wire);
    }
    for (PortHandle Port : GetTileOutputPorts(Tile))
    {
        ByOutput.erase(Port);
    }
    for (PortHandle Port : GetTileInputPorts(Tile))
    {
        ByInput.erase(Port);
    }
    TileSymbols.erase(Tile);
    TileConstants.erase(Tile);
    TileNames.erase(Tile);
}


OpCode Patch::GetTileSymbol(TileHandle Tile)
{
    return TileSymbols.at(Tile);
}


std::string Patch::GetTileName(TileHandle Tile)
{
    auto Found = TileNames.find(Tile);
    if (Found == TileNames.end())
    {
        OpCode Symbol = GetTileSymbol(Tile);
        return SymbolInfoMap.DefaultNames[(int)Symbol];
    }
    else
    {
        return Found->second;
    }
}


void Patch::SetTileName(TileHandle Tile, std::string NewName)
{
    TileNames[Tile] = NewName;
}


float Patch::GetConstant(TileHandle Tile)
{
    return TileConstants.at(Tile);
}


void Patch::SetConstant(TileHandle Tile, float NewValue)
{
    OpCode Symbol = GetTileSymbol(Tile);
    if (Symbol == OpCode::CONST)
    {
        TileConstants[Tile] = NewValue;
    }
    else
    {
        throw std::runtime_error(std::format("Attempted to assign a value to non-constant tile {}!\n", Tile));
    }
}


std::string Patch::GetTileLabel(TileHandle Tile)
{
    OpCode Symbol = GetTileSymbol(Tile);
    if (Symbol == OpCode::CONST)
    {
        return std::format("{}", GetConstant(Tile));
    }
    else
    {
        return GetTileName(Tile);
    }
}


std::vector<PortHandle> Patch::GetTileInputPorts(TileHandle Tile)
{
    OpCode Symbol = GetTileSymbol(Tile);
    int Count = SymbolInfoMap.InputNames[(int)Symbol].size();
    std::vector<PortHandle> Handles;
    Handles.reserve(Count);
    for (int PortIndex = 0; PortIndex < Count; ++PortIndex)
    {
        Handles.push_back(MakePortHandle(Tile, PortIndex));
    }
    return Handles;
}


std::vector<PortHandle> Patch::GetTileOutputPorts(TileHandle Tile)
{
    OpCode Symbol = GetTileSymbol(Tile);
    int Count = SymbolInfoMap.OutputNames[(int)Symbol].size();
    std::vector<PortHandle> Handles;
    Handles.reserve(Count);
    for (uint32_t PortIndex = 0; PortIndex < Count; ++PortIndex)
    {
        Handles.push_back(MakePortHandle(Tile, PortIndex));
    }
    return Handles;
}


std::string Patch::GetTileInputName(PortHandle Port)
{
    OpCode Symbol = GetTileSymbol(PortHandleTilePart(Port));
    uint32_t PortIndex = PortHandlePortIndexPart(Port);
    return SymbolInfoMap.InputNames[(int)Symbol][PortIndex];
}


std::string Patch::GetTileOutputName(PortHandle Port)
{
    OpCode Symbol = GetTileSymbol(PortHandleTilePart(Port));
    uint32_t PortIndex = PortHandlePortIndexPart(Port);
    return SymbolInfoMap.OutputNames[(int)Symbol][PortIndex];
}


void Patch::Connect(PortHandle OutputPort, PortHandle InputPort)
{
    if (!ByOutput.contains(OutputPort))
    {
        throw std::range_error(std::format("Fatal error: {} is not a known output port!\n", OutputPort));
    }
    if (!ByInput.contains(InputPort))
    {
        throw std::range_error(std::format("Fatal error: {} is not a known input port!\n", InputPort));
    }
    ByInput[InputPort].insert(OutputPort);
    ByOutput[OutputPort].insert(InputPort);
    Wires.emplace(OutputPort, InputPort);
}


void Patch::Disconnect(PortHandle OutputPort, PortHandle InputPort)
{
    Wires.erase({OutputPort, InputPort});
    ByInput[InputPort].erase(OutputPort);
    ByOutput[OutputPort].erase(InputPort);
}


void Patch::ToggleConnection(PortHandle OutputPort, PortHandle InputPort)
{
    if (Wires.contains({OutputPort, InputPort}))
    {
        Disconnect(OutputPort, InputPort);
    }
    else
    {
        Connect(OutputPort, InputPort);
    }
}


bool Patch::CanConnect(TileHandle OutputTile, TileHandle InputTile)
{
    std::vector<PortHandle> OutputPorts = GetTileOutputPorts(OutputTile);
    std::vector<PortHandle> InputPorts = GetTileInputPorts(InputTile);
    return (OutputPorts.size() > 0 && InputPorts.size() > 0);
}


std::optional<WireHandle> Patch::GetImplicitWire(TileHandle OutputTile, TileHandle InputTile)
{
    std::vector<PortHandle> OutputPorts = GetTileOutputPorts(OutputTile);
    std::vector<PortHandle> InputPorts = GetTileInputPorts(InputTile);
    if (OutputPorts.size() == 1 && InputPorts.size() == 1)
    {
        return WireHandle(OutputPorts[0], InputPorts[0]);
    }
    else
    {
        return {};
    }
}
