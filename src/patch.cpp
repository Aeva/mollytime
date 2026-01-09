
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

#include <algorithm>
#include <cassert>
#include <print>

#include "errors.h"
#include "patch.h"
#include "audio_backend.h"

extern SymbolInfo SymbolInfoMap;


PortHandle MakePortHandle(TileHandle TileId, uint32_t PortNumber)
{
    return (uint64_t(TileId) << 32) | uint64_t(PortNumber);
}


PortHandle MakeClosureHandle(TileHandle TileId, uint32_t PortNumber)
{
    PortNumber = std::numeric_limits<uint32_t>::max() - PortNumber;
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


double EncodeSampleHandle(uint32_t SampleHandle)
{
#if 0
    const uint64_t NaN = (0xffful << 51);
    uint64_t Encoded = NaN | uint64_t(SampleHandle);
    return std::bit_cast<double, uint64_t>(Encoded);
#endif
    return std::bit_cast<double, uint64_t>(uint64_t(SampleHandle));
}


std::string GetDefaultName(OpCode Symbol)
{
    return SymbolInfoMap.DefaultNames[(int)Symbol];
}


static int GetClosureCount(OpCode Symbol)
{
    return SymbolInfoMap.Closures[(int)Symbol];
}


static bool IsOutputSymbol(const OpCode Symbol)
{
    return (Symbol == OpCode::OUT || Symbol == OpCode::AUX || Symbol == OpCode::SCOPE);
};


static bool IsLaneJoinSymbol(const OpCode Symbol)
{
    return (Symbol == OpCode::LEAD_LANE || Symbol == OpCode::ADD_LANES);
};


static uint32_t DefaultPolyphony = 4;
void SetDefaultPolyphony(int Polyphony)
{
    DefaultPolyphony = uint32_t(std::max(1, Polyphony));
}


Patch::Patch()
    : LastAssignedTileHandle(0)
{
    static uint64_t NextPatchIdentity = 0;
    Identity = ++NextPatchIdentity;
    MidiPolyphony = DefaultPolyphony;
    // This forces the playing patch to clear, which is useful for the editor, but
    // probably not something we want in a future stand-alone runtime.
    Recompile();
}


void Patch::SetPolyphony(int NewPolyphony)
{
    MidiPolyphony = NewPolyphony;
    Recompile();
}


int Patch::GetPolyphony()
{
    return MidiPolyphony;
}


bool Patch::GetChannelMask(int Channel)
{
    uint16_t Mask = uint16_t(1 << Channel);
    return (ChannelMask & Mask) == Mask;
}


void Patch::SetChannelMask(int Channel, bool Listen)
{
    uint16_t Mask = uint16_t(1 << Channel);
    if (Listen)
    {
        ChannelMask |= Mask;
    }
    else
    {
        ChannelMask &= ~Mask;
    }
    Recompile();
}


TileHandle Patch::MakeTile(OpCode Symbol)
{
    TRACEABLE_SCOPE;
    TileHandle AllocatedHandle;
    {
        do
        {
            AllocatedHandle = ++LastAssignedTileHandle;
        }
        while (AllocatedHandle == 0 || AllocatedHandle == uint32_t(-1));
    }
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

    // TODO: Move this into Patch::Compile somehow?
    if (Symbol == OpCode::BOOP || Symbol == OpCode::TWEAK)
    {
        SpecialInputs[AllocatedHandle] = std::make_shared<AtomicRunningState>(0.0);
    }
    if (Symbol == OpCode::IN || Symbol == OpCode::OUT || Symbol == OpCode::AUX)
    {
        Recompile();
    }
    return AllocatedHandle;
}


TileHandle Patch::MakeTile(double Constant)
{
    TRACEABLE_SCOPE;
    const TileHandle AllocatedHandle = MakeTile(OpCode::CONST);
    auto Result = TileConstants.try_emplace(AllocatedHandle, Constant);
    if (!Result.second)
    {
        throw std::runtime_error(std::format("Unusual fatal error: cannot initialize constant tile {}!\n", AllocatedHandle));
    }
    ReplaceConstantOutput(AllocatedHandle, Constant);
    return AllocatedHandle;
}


void Patch::EraseTile(TileHandle Tile)
{
    TRACEABLE_SCOPE;
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
        Disconnect(std::get<0>(Wire), std::get<1>(Wire));
    }

    // TODO: Move this into Patch::Compile somehow?
    OpCode Symbol = GetTileSymbol(Tile);
    if (Symbol == OpCode::BOOP || Symbol == OpCode::TWEAK)
    {
        SpecialInputs.erase(Tile);
    }

    TileSymbols.erase(Tile);
    TileConstants.erase(Tile);
    TileNames.erase(Tile);
    ErasedTiles.push_back(Tile);
    Recompile();
}


std::vector<TileHandle> Patch::GetAllTileHandles()
{
    TRACEABLE_SCOPE;
    std::vector<TileHandle> Handles;
    for (const auto& Entry : TileSymbols)
    {
        Handles.push_back(Entry.first);
    }
    return Handles;
}


OpCode Patch::GetTileSymbol(TileHandle Tile)
{
    TRACEABLE_SCOPE;
    return TileSymbols.at(Tile);
}


std::string Patch::GetTileName(TileHandle Tile)
{
    TRACEABLE_SCOPE;
    OpCode Symbol = GetTileSymbol(Tile);

    if (Symbol == OpCode::IN)
    {
        return std::format("in {}", Tile);
    }
    else if (Symbol == OpCode::AUX)
    {
        return std::format("aux {}", Tile);
    }
    else if (Symbol == OpCode::OUT)
    {
        auto Found = OutputTileNames.find(Tile);
        if (Found != OutputTileNames.end())
        {
            return Found->second;
        }
    }
    {
        auto Found = TileNames.find(Tile);
        if (Found == TileNames.end())
        {
            return SymbolInfoMap.DefaultNames[(int)Symbol];
        }
        else
        {
            return Found->second;
        }
    }
}


void Patch::SetTileName(TileHandle Tile, std::string NewName)
{
    TRACEABLE_SCOPE;
    TileNames[Tile] = NewName;
}


double Patch::GetConstant(TileHandle Tile)
{
    TRACEABLE_SCOPE;
    return TileConstants.at(Tile);
}


void Patch::SetConstant(TileHandle Tile, double NewValue)
{
    TRACEABLE_SCOPE;
    OpCode Symbol = GetTileSymbol(Tile);
    if (Symbol == OpCode::CONST)
    {
        TileConstants[Tile] = NewValue;
        ReplaceConstantOutput(Tile, NewValue);
    }
    else
    {
        throw std::runtime_error(std::format("Attempted to assign a value to non-constant tile {}!\n", Tile));
    }
}


void Patch::ReplaceConstantOutput(TileHandle Tile, double NewValue)
{
    TRACEABLE_SCOPE;
    Recompile();
}


std::string Patch::GetTileLabel(TileHandle Tile)
{
    TRACEABLE_SCOPE;
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
    TRACEABLE_SCOPE;
    OpCode Symbol = GetTileSymbol(Tile);
    size_t Count = SymbolInfoMap.InputNames[(int)Symbol].size();
    std::vector<PortHandle> Handles;
    Handles.reserve(Count);
    for (int PortIndex = 0; PortIndex < static_cast<int>(Count); ++PortIndex)
    {
        Handles.push_back(MakePortHandle(Tile, PortIndex));
    }
    return Handles;
}


std::vector<PortHandle> Patch::GetTileOutputPorts(TileHandle Tile)
{
    TRACEABLE_SCOPE;
    OpCode Symbol = GetTileSymbol(Tile);
    size_t Count = SymbolInfoMap.OutputNames[(int)Symbol].size();
    std::vector<PortHandle> Handles;
    Handles.reserve(Count);
    for (uint32_t PortIndex = 0; PortIndex < static_cast<uint32_t>(Count); ++PortIndex)
    {
        Handles.push_back(MakePortHandle(Tile, PortIndex));
    }
    return Handles;
}


std::string Patch::GetTileInputName(PortHandle Port)
{
    TRACEABLE_SCOPE;
    OpCode Symbol = GetTileSymbol(PortHandleTilePart(Port));
    uint32_t PortIndex = PortHandlePortIndexPart(Port);
    return SymbolInfoMap.InputNames[(int)Symbol][PortIndex];
}


std::string Patch::GetTileOutputName(PortHandle Port)
{
    TRACEABLE_SCOPE;
    OpCode Symbol = GetTileSymbol(PortHandleTilePart(Port));
    uint32_t PortIndex = PortHandlePortIndexPart(Port);
    return SymbolInfoMap.OutputNames[(int)Symbol][PortIndex];
}


int Patch::GetTilePolyphony(TileHandle Tile)
{
    auto Found = TilePolyphony.find(Tile);
    if (Found != TilePolyphony.end())
    {
        return Found->second;
    }
    else
    {
        return 0;
    }
}


void Patch::Freeze()
{
    Frozen = true;
}


void Patch::Unfreeze()
{
    Frozen = false;
    Recompile();
}


bool Patch::GetFrozen()
{
    return Frozen;
}


void Patch::Connect(PortHandle OutputPort, PortHandle InputPort)
{
    TRACEABLE_SCOPE;
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

    Recompile();
}


void Patch::Disconnect(PortHandle OutputPort, PortHandle InputPort)
{
    TRACEABLE_SCOPE;
    Wires.erase({OutputPort, InputPort});
    ByInput[InputPort].erase(OutputPort);
    ByOutput[OutputPort].erase(InputPort);

    Recompile();
}


void Patch::ToggleConnection(PortHandle OutputPort, PortHandle InputPort)
{
    TRACEABLE_SCOPE;
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
    TRACEABLE_SCOPE;
    std::vector<PortHandle> OutputPorts = GetTileOutputPorts(OutputTile);
    std::vector<PortHandle> InputPorts = GetTileInputPorts(InputTile);
    return (OutputPorts.size() > 0 && InputPorts.size() > 0);
}


std::optional<WireHandle> Patch::GetImplicitWire(TileHandle OutputTile, TileHandle InputTile)
{
    TRACEABLE_SCOPE;
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


std::tuple<double, double> Patch::ReadOutputProbe()
{
    TRACEABLE_SCOPE;
    return OutputProbe->Get();
}


std::tuple<double, double> Patch::ReadScopeProbe()
{
    TRACEABLE_SCOPE;
    return ScopeProbe->Get();
}


void Patch::SetActiveProbe(TileHandle Tile)
{
    if (ActiveProbeTile != Tile)
    {
        ActiveProbeTile = Tile;
        Recompile();
    }
}


void Patch::ClearActiveProbe()
{
    TileHandle Tile = -1;
    Patch::SetActiveProbe(Tile);
}


void Patch::SetSpecialInput(TileHandle Tile, double Value)
{
    TRACEABLE_SCOPE;
    SpecialInputs[Tile]->Set(Value);
}


void Patch::AddSpecialInput(TileHandle Tile, double Value)
{
    TRACEABLE_SCOPE;
    SpecialInputs[Tile]->Add(Value);
}


void Patch::AddRangeSpecialInput(TileHandle Tile, double Value, double LimitLow, double LimitHigh)
{
    TRACEABLE_SCOPE;
    SpecialInputs[Tile]->Add(Value, LimitLow, LimitHigh);
}


double Patch::GetSpecialInput(TileHandle Tile)
{
    TRACEABLE_SCOPE;
    return SpecialInputs[Tile]->Get();
}


ScratchUniquePtr Patch::Compile()
{
    TRACEABLE_SCOPE;

    std::set<TileHandle> BreadCrumbs;
    std::map<TileHandle, std::vector<PortHandle>> InputSequences;

    struct TilePartial
    {
        TileHandle Tile;
        bool DynamicPolyphony; // True indicates the tile's polyphony is determined via propagation, not by its symbol.
        uint32_t Polyphony = 1;

        // This is NOT redundant to Patch::ByInput because its elements are ordered,
        // and that ordering is determined at compile time (e.g. by OpCode::GO).
        std::vector<std::vector<PortHandle>> Inputs;
    };
    std::vector<TilePartial> FlatGraph;
    std::unordered_map<TileHandle, TilePartial*> PartialByTile;
    FlatGraph.reserve(TileSymbols.size());

    assert(MidiPolyphony > 0);
    MidiPolyphony = std::max(MidiPolyphony, 1u);

    ScratchUniquePtr Program = std::make_unique<Scratch>();
    Program->Identity = Identity;
    Program->OutputProbe = OutputProbe;
    Program->ScopeProbe = ScopeProbe;
    Program->Polyphony = MidiPolyphony;
    Program->ChannelMask = ChannelMask;
    Program->MidiLanes.resize(MidiPolyphony);
    Program->Retriggerables.resize(MidiPolyphony);

    auto VisitTile = [&](TileHandle Tile) -> TilePartial&
    {
        TilePartial& Partial = FlatGraph.emplace_back();
        Partial.Tile = Tile;
        PartialByTile[Tile] = &Partial;
        return Partial;
    };

    bool GraphHasCycles = false;

    std::function<void(TileHandle)> Step = [&](const TileHandle Tile) -> void
    {
        // Recursively solve the order in which tiles are to be cranked, assign polyphony hints,
        // and solve each tile's ordered inputs.

        if (!BreadCrumbs.insert(Tile).second)
        {
            GraphHasCycles = true;
            return;
        }

        const OpCode Symbol = GetTileSymbol(Tile);
        if (Symbol == OpCode::CONST || Symbol == OpCode::IN || Symbol == OpCode::LANE_COUNT || Symbol == OpCode::BOOP || Symbol == OpCode::TWEAK)
        {
            TilePartial& Partial = VisitTile(Tile);
            Partial.DynamicPolyphony = false;
            Partial.Polyphony = 1;
            return;
        }

        const size_t InputCount = SymbolInfoMap.InputNames[(int)Symbol].size();

        // Recurse first to populate everything sequentally.
        for (int PortIndex = 0; PortIndex < static_cast<int>(InputCount); ++PortIndex)
        {
            PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
            for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
            {
                Step(PortHandleTilePart(ConnectedOutput));
            }
        }

        if (Symbol == OpCode::GO)
        {
            // Go tiles only build input lists for other tiles, and do not emit any thunks.
            auto Result = InputSequences.try_emplace(Tile);
            if (Result.second)
            {
                std::vector<PortHandle>& Sequence = (Result.first->second);
                for (int PortIndex = 0; PortIndex < static_cast<int>(InputCount); ++PortIndex)
                {
                    PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
                    for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
                    {
                        TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                        OpCode ConnectedSymbol = GetTileSymbol(ConnectedTile);
                        if (ConnectedSymbol == OpCode::GO)
                        {
                            std::vector<PortHandle>& Append = InputSequences.at(ConnectedTile);
                            Sequence.insert(Sequence.end(), Append.cbegin(), Append.cend());
                        }
                        else
                        {
                            Sequence.push_back(ConnectedOutput);
                        }
                    }
                }
            }
        }
        else if (IsOutputSymbol(Symbol))
        {
            TilePartial& Partial = VisitTile(Tile);
            Partial.DynamicPolyphony = false;
            Partial.Polyphony = 1;
            std::vector<PortHandle>& Input0 = Partial.Inputs.emplace_back();

            const PortHandle InputHandle = MakePortHandle(Tile, 0);
            for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
            {
                Input0.push_back(ConnectedOutput);
            }
        }
        else
        {
            TilePartial& Partial = VisitTile(Tile);

            if (Symbol == OpCode::GATE || Symbol == OpCode::NOTE || Symbol == OpCode::VELO ||
                Symbol == OpCode::PRES || Symbol == OpCode::CTRL || Symbol == OpCode::KIKI ||
                Symbol == OpCode::BEND)
            {
                Partial.DynamicPolyphony = false;
                Partial.Polyphony = MidiPolyphony;
            }
            else if (IsLaneJoinSymbol(Symbol))
            {
                Partial.DynamicPolyphony = false;
                Partial.Polyphony = 1;
            }
            else
            {
                // Inherit from inputs.
                Partial.DynamicPolyphony = true;
                Partial.Polyphony = 1;
            }

            for (int PortIndex = 0; PortIndex < static_cast<int>(InputCount); ++PortIndex)
            {
                std::vector<PortHandle>& PortInputs = Partial.Inputs.emplace_back();
                PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
                for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
                {
                    TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                    OpCode ConnectedSymbol = GetTileSymbol(ConnectedTile);
                    if (ConnectedSymbol == OpCode::GO)
                    {
                        for (PortHandle ForwardedOutput : InputSequences.at(ConnectedTile))
                        {
                            PortInputs.push_back(ForwardedOutput);
                        }
                    }
                    else
                    {
                        PortInputs.push_back(ConnectedOutput);
                    }
                }
            }
        }
    };

    Program->Outputs.clear();

    // The handles for all the different types of output tiles are staged here so
    // they can be sorted and stepped deterministically.  The outputs are evaluated
    // in ascending order of TileID magnitude.  If there are multiple OpCode::OUT
    // tiles, they will renamed to indicate which semantic output they represent.
    // OpCode::AUX tiles are always renamed in accordance of their tile handles.
    // Everything else retains its default name.
    // OpCode::SCOPE is the only exception: it will only ever be included in the
    // graph when the operator sets it as the active probe tile.
    // Tape tiles aren't actually outputs, but they're solved before anything else
    // to ensure consistent ordering and latency in the event that they're used to
    // create feedback loops.
    std::vector<TileHandle> TapeTiles;
    std::vector<TileHandle> OutputTiles;
    std::vector<TileHandle> AuxTiles;
    {
        for (const auto& [Tile, Symbol] : TileSymbols)
        {
            switch (Symbol)
            {
                case OpCode::OUT:
                    OutputTiles.push_back(Tile);
                    break;
                case OpCode::AUX:
                    AuxTiles.push_back(Tile);
                    break;
                case OpCode::TAPE_LOOP:
                    TapeTiles.push_back(Tile);
                    break;
                default:
                    break;
            }
        }
        auto SortAndStep = [Step](std::vector<TileHandle>& OutputSet) -> void
        {
            // You are filled with determinism.
            std::sort(OutputSet.begin(), OutputSet.end());
            for (TileHandle Tile : OutputSet)
            {
                Step(Tile);
            }
        };
        SortAndStep(TapeTiles);
        SortAndStep(OutputTiles);
        SortAndStep(AuxTiles);
    }

    Program->ProbeConnected = false;
    if (ActiveProbeTile != TileHandle(-1))
    {
        auto Found = TileSymbols.find(ActiveProbeTile);
        if (Found != TileSymbols.end())
        {
            Program->ProbeConnected = true;
            const OpCode Symbol = Found->second;
            if (Symbol == OpCode::SCOPE)
            {
                Step(ActiveProbeTile);
            }
        }
    }
    if (!Program->ProbeConnected)
    {
        // The scope tile does appear to be disconnected, so force the probe values to zero
        // since they most likely will not be updated when the patch runs.
        OutputProbe->Set(0.0);
        ScopeProbe->Set(0.0);
        Program->ProbeInput = -1;
    }

    // From this point on, `FlatGraph` contains entries for everything that will contribute to the compiled patch program.

    {
        // Remove persistence information for recently erased tiles.
        for (TileHandle Tile : ErasedTiles)
        {
            TileLanes.erase(Tile);
        }
        ErasedTiles.clear();
    }

    // Solve tile polyphony via propagation.
    TilePolyphony.clear();
    {
        // We need to run multiple times when there are graph cycles to fully propagate the required
        // polyphony.  It is unclear if there is any situation where more than one retry would be needed
        // to fully solve the graph correctly.
        uint32_t IterationCount = GraphHasCycles ? 2 : 1;
        // It is possible to create valid graphs with cycles where it is not possible to determine
        // the lane width because none of the tiles have constants or midi inputs.  The correct thing
        // to do in such a situation is to default to a lane width of one.
        for (uint32_t Iteration = 0; Iteration < IterationCount; ++Iteration)
        {
            for (TilePartial& Partial : FlatGraph)
            {
                if (Partial.DynamicPolyphony)
                {
                    for (std::vector<PortHandle>& ConnectedOutputs : Partial.Inputs)
                    {
                        for (PortHandle ConnectedPort : ConnectedOutputs)
                        {
                            TileHandle ConnectedTile = PortHandleTilePart(ConnectedPort);
                            TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                            Partial.Polyphony = std::max(Partial.Polyphony, ConnectedPartial->Polyphony);
                        }
                    }
                }
            }
        }
        for (TilePartial& Partial : FlatGraph)
        {
            TileLanes.insert_or_assign(Partial.Tile, Partial.Polyphony);
            TilePolyphony[Partial.Tile] = Partial.Polyphony;
        }
    }

    std::unordered_map<PortHandle, PortHandle> LaneMergePorts;
    {
        uint32_t NextVirtualPortIndex = 0;
        for (TilePartial& Partial : FlatGraph)
        {
            const OpCode Symbol = GetTileSymbol(Partial.Tile);
            if (IsLaneJoinSymbol(Symbol))
            {
                // No virtual ports needed.
            }
            else if (Partial.Polyphony == 1)
            {
                for (std::vector<PortHandle>& ConnectedOutputs : Partial.Inputs)
                {
                    for (PortHandle ConnectedPort : ConnectedOutputs)
                    {
                        TileHandle ConnectedTile = PortHandleTilePart(ConnectedPort);
                        TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                        if (ConnectedPartial->Polyphony > 1)
                        {
                            // We have an output from a polyphonic tile connected to a input port on
                            // at least one monophonic tile.  To make this work, we create an alias
                            // to a virtual port handle.  The presence of this alias will automatically
                            // result in a lane add fixup tile being introduced later on.
                            PortHandle VirtualPort = MakePortHandle(0, NextVirtualPortIndex);
                            if (LaneMergePorts.insert({ConnectedPort, VirtualPort}).second)
                            {
                                ++NextVirtualPortIndex;
                            }
                        }
                    }
                }
            }
        }
    }

    std::map<PortHandle, std::ptrdiff_t> RegisterMap;
    auto AllocateRegister = [&](PortHandle Port, uint32_t Lanes, double InitialValue = 0.0) -> std::ptrdiff_t
    {
        std::ptrdiff_t Offset = Program->RegisterFile.size();
        Program->RegisterFile.insert(Program->RegisterFile.end(), Lanes, InitialValue);
        RegisterMap[Port] = Offset;
        return Offset;
    };
    auto AllocatePersistentRegister = [&](PortHandle Port, uint32_t Lanes, double InitialValue = 0.0) -> std::ptrdiff_t
    {
        std::ptrdiff_t Offset = AllocateRegister(Port, Lanes, InitialValue);
        Program->PersistentRegisters[Port] = { Offset, Lanes };
        return Offset;
    };

    // Output registers can't be allocated in tandem with thunk generation, as graphs can have cycles.
    // Likewise, we need to allocate registers for everything we want to persist between patch generations,
    // not just the registers needed for the current version of a patch.
    for (auto& TileAndLanes : TileLanes)
    {
        const TileHandle Tile = TileAndLanes.first;
        const uint32_t Lanes = TileAndLanes.second;
        const OpCode Symbol = GetTileSymbol(Tile);

        if (Symbol == OpCode::CONST)
        {
            // A temporary register is fine here, because this should never be overwritten.
            const double ConstantValue = GetConstant(Tile);
            AllocateRegister(MakePortHandle(Tile, 0), Lanes, ConstantValue);
        }
        else if (Symbol == OpCode::LANE_COUNT)
        {
            // A temporary register is fine here, because this should never be overwritten.
            AllocateRegister(MakePortHandle(Tile, 0), Lanes, double(MidiPolyphony));
        }
        else if (Symbol == OpCode::IN)
        {
            // If the audio backend is not guaranteed to write to this input every frame, a persistent register
            // might be better, depending on whether or not resseting to zero is more or less ideal than holding
            // the last known value.
            Program->Inputs[Tile] = AllocateRegister(MakePortHandle(Tile, 0), Lanes);
        }
        else if (IsOutputSymbol(Symbol))
        {
            const size_t InputCount = SymbolInfoMap.InputNames[(int)Symbol].size();
            const size_t ConnectedInputCount = (InputCount > 0) ? ByInput.at(MakePortHandle(Tile, 0)).size() : 0;
            if (ConnectedInputCount != 1)
            {
                // If we have zero connected inputs, then we make a register so we have something to output.
                // If we have exactly one, we'll reuse the output of the connection.
                // If we have more than one, we'll need to emit a thunk to add them, and that thunk will need
                // a register to output to.  This all happens elsewhere, we just need to allocate or not allocate
                // for now.
                // A temporary register is fine here because this will be written every frame in any
                // situation where it is possible to observe its effect, output tiles have no persistent
                // state, and cannot be used to construct graph cycles.
                AllocateRegister(MakePortHandle(Tile, 0), Lanes);
            }
        }
        else
        {
            const size_t OutputCount = SymbolInfoMap.OutputNames[(int)Symbol].size();
            const size_t ClosureCount = GetClosureCount(Symbol);

            for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
            {
                // Persistent registers are used here, because there may be graph cycles, in which
                // case we need to be able to read values from the previous frame, which may have come
                // from a different patch.  Additionally, thunks are free to assume their outputs are
                // persistent, which can be used to save on allocating extra closure registers.
                PortHandle OutputPort = MakePortHandle(Tile, PortIndex);
                AllocatePersistentRegister(OutputPort, Lanes);

                auto Found = LaneMergePorts.find(OutputPort);
                if (Found != LaneMergePorts.end())
                {
                    // A monophonic tile requires a temporary register for the merged output.
                    PortHandle VirtualPort = Found->second;
                    AllocateRegister(VirtualPort, 1);
                }
            }

            for (int ClosureIndex = 0; ClosureIndex < static_cast<int>(ClosureCount); ++ClosureIndex)
            {
                // Closure registers represent a thunk's internal state, and as such they must be
                // persistent across patch revisions.
                PortHandle ClosurePort = MakeClosureHandle(Tile, ClosureIndex);
                AllocatePersistentRegister(ClosurePort, Lanes);
            }
        }
    }

    // Make sure we have all our tapes.
    {
        std::ptrdiff_t TapeCount = 0;
        for (TileHandle Tile : TapeTiles)
        {
            PortHandle Port = MakePortHandle(Tile, 0);
            TilePartial* Partial = PartialByTile.at(Tile);

            std::ptrdiff_t BaseOffset = TapeCount;
            uint32_t LaneCount = Partial->Polyphony;

            [[maybe_unused]] auto Result = Program->PersistentTapes.try_emplace(Port, BaseOffset, LaneCount);
            assert(Result.second == true);

            TapeCount += LaneCount;
        }
        Program->TapeFile.resize(TapeCount);
        for (MagicTapeUniquePtr& Tape : Program->TapeFile)
        {
            Tape = std::make_unique<BlankTape>();
        }
    }

    // Emit thunks, and adjust default values.
    for (TilePartial& Partial : FlatGraph)
    {
        const OpCode Symbol = GetTileSymbol(Partial.Tile);
        std::vector<double>* RegisterFile = &(Program->RegisterFile);
        std::vector<MagicTapeUniquePtr>* TapeFile = &(Program->TapeFile);

        std::vector<std::vector<std::ptrdiff_t>> Inputs;
        Inputs.reserve(Partial.Inputs.size());

        std::vector<std::vector<uint32_t>> InputWidths;
        InputWidths.reserve(Partial.Inputs.size());

        for (std::vector<PortHandle>& ConnectedOutputs : Partial.Inputs)
        {
            std::vector<std::ptrdiff_t>& InputRegisters = Inputs.emplace_back();
            InputRegisters.reserve(ConnectedOutputs.size());

            std::vector<uint32_t>& Widths = InputWidths.emplace_back();
            Widths.reserve(ConnectedOutputs.size());

            if (IsLaneJoinSymbol(Symbol))
            {
                if (Symbol == OpCode::LEAD_LANE)
                {
                    for (PortHandle ConnectedOutput : ConnectedOutputs)
                    {
                        TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                        TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                        std::ptrdiff_t BaseAddress = RegisterMap.at(ConnectedOutput);
                        // LeadLaneThunk will read the inputs with a stride.
                        uint32_t Width = ConnectedPartial->Polyphony;
                        if (Width == 1)
                        {
                            // Monophonic inputs need to be copied to fill the full lane width.
                            for (uint32_t Offset = 0; Offset < MidiPolyphony; ++Offset)
                            {
                                InputRegisters.push_back(BaseAddress);
                                Widths.push_back(1);
                            }
                        }
                        else
                        {
                            // Polyphonic inputs have each register connected as different input.
                            assert(Width == MidiPolyphony);
                            for (uint32_t Offset = 0; Offset < Width; ++Offset)
                            {
                                InputRegisters.push_back(BaseAddress + Offset);
                                Widths.push_back(1);
                            }
                        }
                    }
                }
                else if (Symbol == OpCode::ADD_LANES)
                {
                    for (PortHandle ConnectedOutput : ConnectedOutputs)
                    {
                        TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                        TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                        std::ptrdiff_t BaseAddress = RegisterMap.at(ConnectedOutput);
                        uint32_t Width = ConnectedPartial->Polyphony;
                        for (uint32_t Offset = 0; Offset < Width; ++Offset)
                        {
                            InputRegisters.push_back(BaseAddress + Offset);
                            Widths.push_back(1);
                        }
                    }
                }
            }
            else if (Partial.Polyphony == 1)
            {
                for (PortHandle ConnectedOutput : ConnectedOutputs)
                {
                    auto Found = LaneMergePorts.find(ConnectedOutput);
                    if (Found != LaneMergePorts.end())
                    {
                        ConnectedOutput = Found->second;
                    }
                    InputRegisters.push_back(RegisterMap.at(ConnectedOutput));
                    Widths.push_back(1);
                }
            }
            else
            {
                for (PortHandle ConnectedOutput : ConnectedOutputs)
                {
                    InputRegisters.push_back(RegisterMap.at(ConnectedOutput));
                    TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                    TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                    Widths.push_back(ConnectedPartial->Polyphony);
                }
            }
        }

        std::vector<std::ptrdiff_t> Outputs;
        std::vector<std::ptrdiff_t> Closures;

        if (Symbol == OpCode::CONST || Symbol == OpCode::IN || Symbol == OpCode::LANE_COUNT)
        {
            // No thunks are created for these symbols.
            continue;
        }
        else if (IsOutputSymbol(Symbol))
        {
            std::ptrdiff_t OutputRegister;

            if (Inputs[0].size() == 1)
            {
                OutputRegister = Inputs[0][0];
            }
            else
            {
                OutputRegister = RegisterMap.at(MakePortHandle(Partial.Tile, 0));
                Outputs = { OutputRegister };
                if (Inputs[0].size() > 1)
                {
                    static const BasicCreateAndConnectFn AddCreateAndConnect = SymbolInfoMap.BasicCreateAndConnect.at((int)OpCode::ADD);
                    Program->Program.push_back(AddCreateAndConnect(RegisterFile, Inputs, Outputs, Closures));
                }
            }

            // Collect the patch output registers.
            if (Symbol == OpCode::OUT)
            {
                Program->Outputs.push_back(OutputRegister);
            }
            else if (Symbol == OpCode::AUX)
            {
                Program->AuxOutputs[Partial.Tile] = OutputRegister;
            }

            // This tile is also the current active probe.
            if (Program->ProbeConnected && Partial.Tile == ActiveProbeTile)
            {
                Program->ProbeInput = OutputRegister;
            }
        }
        else
        {
            const size_t OutputCount = SymbolInfoMap.OutputNames[(int)Symbol].size();
            const size_t ClosureCount = GetClosureCount(Symbol);

            Outputs.reserve(OutputCount);
            for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
            {
                PortHandle OutputPort = MakePortHandle(Partial.Tile, PortIndex);
                Outputs.push_back(RegisterMap.at(OutputPort));
            }

            Closures.reserve(ClosureCount);
            for (int ClosureIndex = 0; ClosureIndex < static_cast<int>(ClosureCount); ++ClosureIndex)
            {
                PortHandle ClosurePort = MakeClosureHandle(Partial.Tile, ClosureIndex);
                Closures.push_back(RegisterMap.at(ClosurePort));
            }

            for (uint32_t Lane = 0; Lane < Partial.Polyphony; ++Lane)
            {
                if (Lane > 0)
                {
                    for (uint32_t InputIndex = 0; InputIndex < Inputs.size(); ++InputIndex)
                    {
                        std::vector<std::ptrdiff_t>& InputRegisters = Inputs[InputIndex];
                        for (uint32_t Connection = 0; Connection < InputRegisters.size(); ++Connection)
                        {
                            // TODO: assert that this matches if the width is not 1
                            if (InputWidths[InputIndex][Connection] == Partial.Polyphony)
                            {
                                ++(InputRegisters[Connection]);
                            }
                        }
                    }
                    for (std::ptrdiff_t& OutputRegister : Outputs)
                    {
                        ++OutputRegister;
                    }
                    for (std::ptrdiff_t& ClosureRegister : Closures)
                    {
                        ++ClosureRegister;
                    }
                }

                InstructionThunkSharedPtr Thunk = nullptr;
                {
                    auto Found = SymbolInfoMap.BasicCreateAndConnect.find((int)Symbol);
                    if (Found != SymbolInfoMap.BasicCreateAndConnect.end())
                    {
                        Thunk = Found->second(RegisterFile, Inputs, Outputs, Closures);
                        Program->Program.push_back(Thunk);
                    }
                }

                if (Thunk == nullptr)
                {
                    auto Found = SymbolInfoMap.WidgetCreateAndConnect.find((int)Symbol);
                    if (Found != SymbolInfoMap.WidgetCreateAndConnect.end())
                    {
                        Thunk = Found->second(RegisterFile, Inputs, Outputs, Closures, SpecialInputs[Partial.Tile]);
                        Program->Program.push_back(Thunk);
                    }
                }

                if (Thunk == nullptr)
                {
                    auto Found = SymbolInfoMap.MidiCreateAndConnect.find((int)Symbol);
                    if (Found != SymbolInfoMap.MidiCreateAndConnect.end())
                    {
                        Thunk = Found->second(RegisterFile, Program.get(), Inputs, Outputs, Closures, Lane);
                        Program->Program.push_back(Thunk);
                    }
                }

                if (Thunk == nullptr)
                {
                    auto Found = SymbolInfoMap.TapeCreateAndConnect.find((int)Symbol);
                    if (Found != SymbolInfoMap.TapeCreateAndConnect.end())
                    {
                        PortHandle Port = MakePortHandle(Partial.Tile, 0);
                        RegisterAllocation& TapeAllocation = Program->PersistentTapes.at(Port);
                        std::ptrdiff_t TapeIndex = TapeAllocation.BaseOffset + Lane;
                        Thunk = Found->second(RegisterFile, TapeFile, TapeIndex, Inputs, Outputs, Closures);
                        Program->Program.push_back(Thunk);
                    }
                }

                assert(Thunk != nullptr);
                if (Partial.Polyphony > 1 && Symbol == OpCode::ADSR)
                {
                    Thunk->Retriggerable = true;
                    uint32_t ThunkIndex = Program->Program.size() - 1;
                    assert(Program->Program[ThunkIndex] == Thunk);
                    // TODO: figure out some means of determining if the trigger is directly or indirectly
                    // connected to a gate tile inntead of using the ADSR's polyphony as a proxy for this.
                    Program->Retriggerables[Lane].push_back(ThunkIndex);
                }

                if (Symbol == OpCode::LEAD_LANE)
                {
                    break;
                }
            }

            if (Partial.Polyphony > 1)
            {
                for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
                {
                    PortHandle OutputPort = MakePortHandle(Partial.Tile, PortIndex);

                    auto Found = LaneMergePorts.find(OutputPort);
                    if (Found != LaneMergePorts.end())
                    {
                        PortHandle VirtualPort = Found->second;

                        std::ptrdiff_t PolyphonicBaseAddress = RegisterMap.at(OutputPort);
                        std::ptrdiff_t ResultAddress = RegisterMap.at(VirtualPort);

                        std::vector<std::ptrdiff_t> MergeLanes;
                        MergeLanes.reserve(Partial.Polyphony);
                        for (std::ptrdiff_t Lane = 0; Lane < (std::ptrdiff_t)Partial.Polyphony; ++Lane)
                        {
                            MergeLanes.push_back(PolyphonicBaseAddress + Lane);
                        }
                        std::vector<std::vector<std::ptrdiff_t>> JoinInputs = { MergeLanes };
                        std::vector<std::ptrdiff_t> JoinOutputs = { ResultAddress };
                        std::vector<std::ptrdiff_t> JoinClosures;

                        static const BasicCreateAndConnectFn AddCreateAndConnect = SymbolInfoMap.BasicCreateAndConnect.at((int)OpCode::ADD);
                        Program->Program.push_back(AddCreateAndConnect(RegisterFile, JoinInputs, JoinOutputs, JoinClosures));
                    }
                }
            }
        }
    }

#ifndef NDEBUG
    for (InstructionThunkSharedPtr& Thunk : Program->Program)
    {
        assert(Thunk != nullptr);
    }
#endif

    OutputTileNames.clear();
    if (OutputTiles.size() >= 2)
    {
        int OutputIndex = 0;
        for (const TileHandle& Tile : OutputTiles)
        {
            if (OutputIndex == 0)
            {
                OutputTileNames[Tile] = "out\nleft";
            }
            else if (OutputIndex == 1)
            {
                OutputTileNames[Tile] = "out\nright";
            }
            else
            {
                OutputTileNames[Tile] = "ignored\nout";
            }
            ++OutputIndex;
        }
    }

    return Program;
}


void Patch::Recompile()
{
    TRACEABLE_SCOPE;
    if (!Frozen)
    {
        ScratchUniquePtr NewProgram = Compile();
        Audio::GetStream()->ProgramChange(std::move(NewProgram));
    }
}


void Scratch::PrintRegisters() const
{
    std::unordered_map<uint32_t, PortHandle> PortsByRegisterOffset;
    for (auto const& [Port, Allocation] : PersistentRegisters)
    {
        PortsByRegisterOffset[Allocation.BaseOffset] = Port;
    }
    for (std::ptrdiff_t Register = 0; Register < (std::ptrdiff_t)RegisterFile.size(); ++Register)
    {
        const double Value = RegisterFile.at(Register);
        auto Found = PortsByRegisterOffset.find(Register);
        if (Found != PortsByRegisterOffset.end())
        {
            PortHandle Port = Found->second;
            TileHandle Tile = PortHandleTilePart(Port);
            int32_t Index = (int32_t)PortHandlePortIndexPart(Port);
            std::print("    {:>4}: {:^8.4} ({}:{})\n", Register, Value, Tile, Index);
        }
        else
        {
            std::print("    {:>4}: {:^8.4}\n", Register, Value);
        }
    }
}


void Scratch::Migrate(Scratch& Old)
{
    TRACEABLE_SCOPE;

    if (Polyphony == Old.Polyphony)
    {
        assert(Old.MidiLanes.size() == Old.Polyphony);
        MidiLanes = Old.MidiLanes;
    }
    // TODO: probably should always do this regardless of matching identity.
    // The midi scheduler's running state should probably be a persistent mixin.
    ChannelPrograms = Old.ChannelPrograms;
    ChannelPitchBend = Old.ChannelPitchBend;
    ChannelControls = Old.ChannelControls;
    assert(MidiLanes.size() == Polyphony);

#ifndef NDEBUG
    for (std::vector<uint32_t>& ThunkIndices : Retriggerables)
    {
        for (uint32_t ThunkIndex : ThunkIndices)
        {
            InstructionThunkSharedPtr& Thunk = Program.at(ThunkIndex);
            assert(Thunk != nullptr);
            assert(Thunk->Retriggerable);
        }
    }
#endif

    constexpr bool EnableDebugLogging = false;

    if (EnableDebugLogging)
    {
        std::print("\n\n==============================================================================\n");
        std::print("Old register file:\n");
        Old.PrintRegisters();
        std::print("\nRunning migration:\n");
    }

    for (auto const& [Port, NewAllocation] : PersistentRegisters)
    {
        auto Found = Old.PersistentRegisters.find(Port);
        if (Found != Old.PersistentRegisters.end())
        {
            if (EnableDebugLogging)
            {
                std::print(" + MATCH: {}:{}", PortHandleTilePart(Port), (int32_t)PortHandlePortIndexPart(Port));
            }

            const RegisterAllocation& OldAllocation = Found->second;
            if (OldAllocation.LaneCount == NewAllocation.LaneCount)
            {
                if (EnableDebugLogging)
                {
                    std::print(" (copy, no resize)\n");
                }

                for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                {
                    const double MigratedValue = Old.RegisterFile.at(OldAllocation.BaseOffset + Lane);
                    if (EnableDebugLogging)
                    {
                        const double StompedValue = RegisterFile.at(NewAllocation.BaseOffset + Lane);
                        const uint32_t WriteOffset = NewAllocation.BaseOffset + Lane;
                        std::print("    > Register[{}] = {:.4} -> {:.4}\n", WriteOffset, StompedValue, MigratedValue);
                    }
                    RegisterFile.at(NewAllocation.BaseOffset + Lane) = MigratedValue;
                }
            }
            else if (OldAllocation.LaneCount == 1)
            {
                if (EnableDebugLogging)
                {
                    std::print(" (mono -> poly resize)\n");
                }
                for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                {
                    const double MigratedValue = Old.RegisterFile.at(OldAllocation.BaseOffset);
                    if (EnableDebugLogging)
                    {
                        const double StompedValue = RegisterFile.at(NewAllocation.BaseOffset + Lane);
                        const uint32_t WriteOffset = NewAllocation.BaseOffset + Lane;
                        std::print("    | Register[{}] = {:.4} -> {:.4}\n", WriteOffset, StompedValue, MigratedValue);
                    }
                    RegisterFile.at(NewAllocation.BaseOffset + Lane) = MigratedValue;
                }
            }
            else
            {
                // If the register is migrating from polyphonic to monophonic, the correct default value for
                // that port is expected to already be in the new register file.  Usually this value will be
                // zero, but some thunks will set different default values for their ports.
                if (EnableDebugLogging)
                {
                    std::print(" (poly->mono RESET)\n");
                }
            }

            if (EnableDebugLogging)
            {
                std::print("\n");
            }
        }
        else
        {
            // The register is totally new, and already has the correct starting value.  No additional
            // handling is required.
            if (EnableDebugLogging)
            {
                std::print(" - no match: {}:{}\n", PortHandleTilePart(Port), (int32_t)PortHandlePortIndexPart(Port));
            }
        }
    }
    for (auto const& [Port, NewAllocation] : PersistentTapes)
    {
        auto Found = Old.PersistentTapes.find(Port);
        if (Found != Old.PersistentTapes.end())
        {
            const RegisterAllocation& OldAllocation = Found->second;
            if (OldAllocation.LaneCount == NewAllocation.LaneCount)
            {
                for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                {
                    std::ptrdiff_t Read = OldAllocation.BaseOffset + Lane;
                    std::ptrdiff_t Write = NewAllocation.BaseOffset + Lane;
                    TapeFile.at(Write) = std::move(Old.TapeFile.at(Read));
                }
            }
        }
    }

    if (EnableDebugLogging)
    {
        std::print("\nMigration complete!\n");
        std::print("\nNew register file:\n");
        PrintRegisters();
    }
}


void Scratch::Crank(double SampleInterval, float& OutLeft, float& OutRight)
{
    TRACEABLE_SCOPE;
    assert(MidiLanes.size() == Polyphony);
    {
        TRACEABLE_NAMED_SCOPE("MIDI PHASE");

        MidiMessage Message;
        if (PopMidiMessage(Message) && (ChannelMask & (1 << Message.Channel)))
        {
            int32_t AssignedLane = -1;
            if (Message.Type == MidiMessageType::Reset)
            {
                for (MidiNoteState& State : MidiLanes)
                {
                    double OldNote = State.Note;
                    State = MidiNoteState();
                    State.Note = OldNote;
                }
            }
            else if (Message.Type == MidiMessageType::ControlChange)
            {
                uint8_t Control = uint8_t(Message.Param1);
                ChannelControls[Message.Channel][Control] = Message.Param2;
                if (Control == 123 && Message.Param2 == 0.0)
                {
                    // All notes off.  See: http://midi.teragonaudio.com/tech/midispec/ntnoff.htm
                    for (MidiNoteState& State : MidiLanes)
                    {
                        double OldNote = State.Note;
                        State = MidiNoteState();
                        State.Note = OldNote;
                        State.Gate = 0.0;
                        State.Pressure = 0.0;
                    }
                }
            }
            else if (Message.Type == MidiMessageType::ProgramChange)
            {
                ChannelPrograms[Message.Channel] = uint8_t(Message.Param1);
                for (MidiNoteState& State : MidiLanes)
                {
                    if (State.Channel == Message.Channel)
                    {
                        State.Gate = 0.0;
                        State.Velocity = 0.0;
                        State.Pressure = 0.0;
                    }
                }
            }
            else if (Message.Type == MidiMessageType::ChannelPressure)
            {
                for (MidiNoteState& State : MidiLanes)
                {
                    if (State.Channel == Message.Channel)
                    {
                        State.Pressure = Message.Param1;
                    }
                }
            }
            else if (Message.Type == MidiMessageType::PitchBend)
            {
                ChannelPitchBend[Message.Channel] = Message.Param1;
            }
            else if (Message.Type == MidiMessageType::Note || Message.Type == MidiMessageType::PolyPress)
            {
                bool LaneReset = false;
                const double Note = Message.Param1;
                const int Channel = int(Message.Channel);
                if (MostRecentLane == -1)
                {
                    AssignedLane = 0;
                    LaneReset = true;
                }
                else
                {
                    // If we have a lane that matches the note and mask, use that.
                    for (uint32_t Lane = 0; Lane < Polyphony; ++Lane)
                    {
                        const double LaneNote = MidiLanes.at(Lane).Note;
                        const int LaneChannel = int(MidiLanes.at(Lane).Channel);
                        if (Note == LaneNote && Channel == LaneChannel)
                        {
                            LaneReset = MidiLanes.at(Lane).Gate == 0.0;
                            AssignedLane = Lane;
                            break;
                        }
                    }
                    if (AssignedLane == -1)
                    {
                        // Otherwise select the oldest lane, prioritizing inactive lanes over active lanes.
                        int32_t OldestLane = -1;
                        int64_t OldestAge = -1;
                        int32_t OldestInactiveLane = -1;
                        int64_t OldestInactiveAge = -1;
                        for (uint32_t Lane = 0; Lane < Polyphony; ++Lane)
                        {
                            double Gate = MidiLanes.at(Lane).Gate;
                            int64_t Age = MidiLanes.at(Lane).Age;
                            if (Gate == 0.0 && Age > OldestInactiveAge)
                            {
                                OldestInactiveLane = Lane;
                                OldestInactiveAge = Age;
                            }
                            if (Age > OldestAge)
                            {
                                OldestLane = Lane;
                                OldestAge = Age;
                            }
                        }
                        if (OldestInactiveLane > 0)
                        {
                            LaneReset = true;
                            AssignedLane = OldestInactiveLane;
                        }
                        else if (OldestLane > 0)
                        {
                            LaneReset = true;
                            AssignedLane = OldestLane;
                        }
                    }
                    if (AssignedLane == -1)
                    {
                        // In the event that we somehow selected nothing, just pick "the next one".
                        LaneReset = true;
                        AssignedLane = (MostRecentLane + 1) % Polyphony;
                    }
                }
                if (LaneReset)
                {
                    MostRecentLane = AssignedLane;
                    for (MidiNoteState& State : MidiLanes)
                    {
                        ++State.Age;
                    }

                    MidiNoteState& State = MidiLanes.at(AssignedLane);
                    State.Note = Note;
                    State.Gate = 0.0;
                    State.Velocity = 0.0;
                    State.Pressure = 0.0;
                    State.Channel = double(Channel);
                    State.Age = 0;

                    for (uint32_t ThunkIndex : Retriggerables[AssignedLane])
                    {
                        InstructionThunkSharedPtr& Thunk = Program.at(ThunkIndex);
                        assert(Thunk != nullptr);
                        assert(Thunk->Retriggerable);
                        Thunk->Retrigger();
                    }
                }
                if (Message.Type == MidiMessageType::Note)
                {
                    MidiNoteState& State = MidiLanes.at(AssignedLane);
                    const double Velocity = Message.Param2;
                    if (Velocity > 0.0)
                    {
                        State.Gate = 1.0;
                        State.Velocity = Velocity;
                    }
                    else
                    {
                        // Leave the velocity alone so the adsr can ring out instead.
                        State.Gate = 0.0;
                        State.Pressure = 0.0;
                    }
                }
                else if (Message.Type == MidiMessageType::PolyPress)
                {
                    MidiNoteState& State = MidiLanes.at(AssignedLane);
                    const double Pressure = Message.Param2;
                    State.Pressure = Pressure;
                }
            }
        }
    }
    {
        TRACEABLE_NAMED_SCOPE("CRANK PHASE");
        for (InstructionThunkSharedPtr& Thunk : Program)
        {
            assert(Thunk != nullptr);
            Thunk->Crank(SampleInterval);
        }
    }
    {
        if (Outputs.size() == 1)
        {
            OutLeft = RegisterFile.at(Outputs[0]);
            OutRight = RegisterFile.at(Outputs[0]);
        }
        else if (Outputs.size() > 1)
        {
            OutLeft = RegisterFile.at(Outputs[0]);
            OutRight = RegisterFile.at(Outputs[1]);
        }
    }
    if (ProbeConnected)
    {
        TRACEABLE_NAMED_SCOPE("UPDATE PROBES");
        ScopeProbe->Set(RegisterFile.at(ProbeInput));
        if (Outputs.size() > 0)
        {
            // TODO : per-output probes
            OutputProbe->Set(RegisterFile.at(Outputs[0]));
        }
    }
    else if (Outputs.size() > 0)
    {
        TRACEABLE_NAMED_SCOPE("UPDATE PROBES");
        // TODO : per-output probes
        ScopeProbe->Set(RegisterFile.at(Outputs[0]));
        OutputProbe->Set(RegisterFile.at(Outputs[0]));
    }
}
