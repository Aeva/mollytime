
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

#include <fmt/format.h>

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
    return bit_cast<double, uint64_t>(Encoded);
#endif
    return bit_cast<double, uint64_t>(uint64_t(SampleHandle));
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


static uint32_t DefaultPolyphony = 32;
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
            throw std::runtime_error(fmt::format("Fatal error: TileId collision on {}!\n", AllocatedHandle));
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
        throw std::runtime_error(fmt::format("Unusual fatal error: cannot initialize constant tile {}!\n", AllocatedHandle));
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
        return fmt::format("in {}", Tile);
    }
    else if (Symbol == OpCode::AUX)
    {
        return fmt::format("aux {}", Tile);
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
        throw std::runtime_error(fmt::format("Attempted to assign a value to non-constant tile {}!\n", Tile));
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
    if (Tile == 0)
    {
        return "virtual tile";
    }
    else
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


bool Patch::GetTileIsConstant(TileHandle Tile)
{
    auto Found = TileIsConstant.find(Tile);
    if (Found != TileIsConstant.end())
    {
        return Found->second;
    }
    else
    {
        return false;
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
    if (ByOutput.find(OutputPort) == ByOutput.end())
    {
        throw std::range_error(fmt::format("Fatal error: {} is not a known output port!\n", OutputPort));
    }
    if (ByInput.find(InputPort) == ByInput.end())
    {
        throw std::range_error(fmt::format("Fatal error: {} is not a known input port!\n", InputPort));
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
    if (Wires.find({OutputPort, InputPort}) != Wires.end())
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
    TileHandle Tile = static_cast<uint32_t>(-1);
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


struct LaneScatterThunk : public InstructionThunk
{
    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("LaneScatterThunk");
        double Value = *Registers.InputPtr(0);
        double* BaseAddress = Registers.OutputPtr(0);
        for (uint32_t Lane = 0; Lane < Registers.Polyphony; ++Lane)
        {
            BaseAddress[Lane] = Value;
        }
    };

    virtual ~LaneScatterThunk() {};
};


struct LaneMergeThunk : public InstructionThunk
{
    virtual bool LaneJoiner() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("LaneMergeThunk");
        double* BaseAddress = Registers.InputPtr(0);
        double& Value = Registers.OutputRef(0, 0);
        Value = BaseAddress[0];
        for (uint32_t Lane = 1; Lane < Registers.Polyphony; ++Lane)
        {
            Value += BaseAddress[Lane];
        }
    };

    virtual void Reset() override
    {
        const uint32_t Lane = 0;
        Registers.ZeroOut(Lane);
    }

    virtual ~LaneMergeThunk() {};
};


ScratchUniquePtr Patch::Compile()
{
    TRACEABLE_SCOPE;

    std::set<TileHandle> BreadCrumbs;
    std::map<TileHandle, std::vector<PortHandle>> InputSequences;

    enum class PartialType
    {
        TILE = 0,
        ADD_TILE,
        LANE_SCATTER,
        LANE_MERGE,
    };

    enum class EvalFrequency : uint8_t
    {
        UNKNOWN,
        ONCE,
        LIVE,
    };

    struct TilePartial
    {
        PartialType Type;
        TileHandle Tile; // Must be non-zero if the type is PartialType::TILE, and zero otherwise.
        bool DynamicPolyphony; // True indicates the tile's polyphony is determined via propagation, not by its symbol.
        EvalFrequency Frequency = EvalFrequency::UNKNOWN;
        uint32_t Polyphony = 1;

        // This is NOT redundant to Patch::ByInput, because its elements are ordered,
        // that ordering is determined at compile time (e.g. by OpCode::GO), and because
        // lane merges and input combiners can replace entries in this list.
        std::vector<std::vector<PortHandle>> Inputs;

        // This is NOT redundant to Patch::ByOutput, because polyphonic lane merges,
        // and input combiner thunks will
        std::vector<PortHandle> Outputs;
    };
    using TilePartialSharedPtr = std::shared_ptr<TilePartial>;

    std::vector<TilePartialSharedPtr> FlatGraph;
    std::unordered_map<TileHandle, TilePartialSharedPtr> PartialByTile;
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
    Program->Retriggerables = {};

    auto VisitTile = [&](TileHandle Tile) -> TilePartialSharedPtr
    {
        TilePartialSharedPtr Partial = std::make_shared<TilePartial>();
        FlatGraph.push_back(Partial);
        PartialByTile[Tile] = Partial;
        Partial->Type = PartialType::TILE;
        Partial->Tile = Tile;
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
            TilePartialSharedPtr Partial = VisitTile(Tile);
            Partial->DynamicPolyphony = false;
            Partial->Polyphony = 1;
            Partial->Outputs = { MakePortHandle(Tile, 0) };

            if (Symbol == OpCode::CONST || Symbol == OpCode::LANE_COUNT)
            {
                Partial->Frequency = EvalFrequency::ONCE;
            }
            else
            {
                Partial->Frequency = EvalFrequency::LIVE;
            }
            return;
        }

        const size_t InputCount = SymbolInfoMap.InputNames[(int)Symbol].size();
        const size_t OutputCount = SymbolInfoMap.OutputNames[(int)Symbol].size();

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
            TilePartialSharedPtr Partial = VisitTile(Tile);
            Partial->Frequency = EvalFrequency::LIVE;
            Partial->DynamicPolyphony = false;
            Partial->Polyphony = 1;
            Partial->Outputs = {};
            std::vector<PortHandle>& Input0 = Partial->Inputs.emplace_back();

            const PortHandle InputHandle = MakePortHandle(Tile, 0);
            for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
            {
                Input0.push_back(ConnectedOutput);
            }
        }
        else
        {
            TilePartialSharedPtr Partial = VisitTile(Tile);

            if (Symbol == OpCode::GATE || Symbol == OpCode::NOTE || Symbol == OpCode::VELO ||
                Symbol == OpCode::PRES || Symbol == OpCode::CTRL || Symbol == OpCode::KIKI ||
                Symbol == OpCode::BEND)
            {
                Partial->DynamicPolyphony = false;
                Partial->Polyphony = MidiPolyphony;
                Partial->Frequency = EvalFrequency::LIVE;
            }
            else if (IsLaneJoinSymbol(Symbol))
            {
                Partial->DynamicPolyphony = false;
                Partial->Polyphony = 1;
            }
            else
            {
                // Inherit from inputs.
                Partial->DynamicPolyphony = true;
                Partial->Polyphony = 1;
            }

            if (Symbol == OpCode::SIN || Symbol == OpCode::SQR || Symbol == OpCode::TRI || Symbol == OpCode::SAW || Symbol == OpCode::NOI ||
                Symbol == OpCode::PHASE || Symbol == OpCode::PLS || Symbol == OpCode::FLP || Symbol == OpCode::RNG || Symbol == OpCode::ADSR ||
                Symbol == OpCode::ISQN || Symbol == OpCode::RSQN || Symbol == OpCode::TAPE_LOOP)
            {
                Partial->Frequency = EvalFrequency::LIVE;
            }

            for (int PortIndex = 0; PortIndex < static_cast<int>(InputCount); ++PortIndex)
            {
                std::vector<PortHandle>& PortInputs = Partial->Inputs.emplace_back();
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

            for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
            {
                PortHandle OutputHandle = MakePortHandle(Tile, PortIndex);
                Partial->Outputs.push_back(OutputHandle);
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

    {
        // Remove persistence information for recently erased tiles.
        for (TileHandle Tile : ErasedTiles)
        {
            TileLanes.erase(Tile);
        }
        ErasedTiles.clear();
    }

    // Solve tile eval frequency via propagation.
    TileIsConstant.clear();
    {
        // We need to run multiple times when there are graph cycles to fully propagate the required
        // polyphony.  It is unclear if there is any situation where more than one retry would be needed
        // to fully solve the graph correctly.
        const uint32_t IterationCount = GraphHasCycles ? 2 : 1;
        for (uint32_t Iteration = 0; Iteration < IterationCount; ++Iteration)
        {
            for (TilePartialSharedPtr& Partial : FlatGraph)
            {
                assert(Partial->Type == PartialType::TILE);
                assert(Partial->Tile != 0);

                if (Partial->Frequency < EvalFrequency::LIVE)
                {
                    for (std::vector<PortHandle>& ConnectedOutputs : Partial->Inputs)
                    {
                        for (PortHandle ConnectedPort : ConnectedOutputs)
                        {
                            TileHandle ConnectedTile = PortHandleTilePart(ConnectedPort);
                            TilePartialSharedPtr& ConnectedPartial = PartialByTile.at(ConnectedTile);
                            Partial->Frequency = std::max(Partial->Frequency, ConnectedPartial->Frequency);
                        }
                    }
                }
            }
        }
        for (TilePartialSharedPtr& Partial : FlatGraph)
        {
            TileLanes.insert_or_assign(Partial->Tile, Partial->Polyphony);
            if (Partial->Frequency == EvalFrequency::ONCE)
            {
                TileIsConstant[Partial->Tile] = true;
                Partial->DynamicPolyphony = false;
                Partial->Polyphony = 1;
            }
            else
            {
                TileIsConstant[Partial->Tile] = false;
            }
        }
    }

    // Solve tile polyphony via propagation.
    TilePolyphony.clear();
    {
        // We need to run multiple times when there are graph cycles to fully propagate the required
        // polyphony.  It is unclear if there is any situation where more than one retry would be needed
        // to fully solve the graph correctly.
        const uint32_t IterationCount = GraphHasCycles ? 2 : 1;
        // It is possible to create valid graphs with cycles where it is not possible to determine
        // the lane width because none of the tiles have constants or midi inputs.  The correct thing
        // to do in such a situation is to default to a lane width of one.
        for (uint32_t Iteration = 0; Iteration < IterationCount; ++Iteration)
        {
            for (TilePartialSharedPtr& Partial : FlatGraph)
            {
                assert(Partial->Type == PartialType::TILE);
                assert(Partial->Tile != 0);

                if (Partial->DynamicPolyphony)
                {
                    for (std::vector<PortHandle>& ConnectedOutputs : Partial->Inputs)
                    {
                        for (PortHandle ConnectedPort : ConnectedOutputs)
                        {
                            TileHandle ConnectedTile = PortHandleTilePart(ConnectedPort);
                            TilePartialSharedPtr& ConnectedPartial = PartialByTile.at(ConnectedTile);
                            Partial->Polyphony = std::max(Partial->Polyphony, ConnectedPartial->Polyphony);
                        }
                    }
                }
            }
        }
        for (TilePartialSharedPtr& Partial : FlatGraph)
        {
            TileLanes.insert_or_assign(Partial->Tile, Partial->Polyphony);
            TilePolyphony[Partial->Tile] = Partial->Polyphony;
        }
    }

    {
        std::unordered_map<PortHandle, TilePartialSharedPtr> LaneFixupPartials;
        uint32_t NextVirtualPortIndex = 0;

        std::vector<TilePartialSharedPtr> NextFlatGraph;
        NextFlatGraph.reserve(FlatGraph.size());

        for (TilePartialSharedPtr& Partial : FlatGraph)
        {
            const OpCode Symbol = GetTileSymbol(Partial->Tile);
            uint32_t InPolyphony = Partial->Polyphony;
            if (IsLaneJoinSymbol(Symbol))
            {
                assert(Partial->Polyphony == 1);
                assert(Partial->Inputs.size() == 1);
                std::vector<PortHandle>& Input0 = Partial->Inputs[0];
                for (PortHandle& ConnectedPort : Input0)
                {
                    TileHandle ConnectedTile = PortHandleTilePart(ConnectedPort);
                    TilePartialSharedPtr ConnectedPartial = PartialByTile.at(ConnectedTile);
                    if (ConnectedPartial->Polyphony > 1)
                    {
                        assert(ConnectedPartial->Polyphony == MidiPolyphony);
                        InPolyphony = MidiPolyphony;
                        break;
                    }
                }
                if (InPolyphony == 1)
                {
                    // Turn this partial into a monophonic ADD.
                    Partial->Type = PartialType::ADD_TILE;
                    Partial->Tile = 0;
                    Partial->Polyphony = 1;
                }
            }

            // Create virtual partials for lane merging and splitting.
            for (std::vector<PortHandle>& ConnectedOutputs : Partial->Inputs)
            {
                for (PortHandle& ConnectedPort : ConnectedOutputs)
                {
                    TileHandle ConnectedTile = PortHandleTilePart(ConnectedPort);
                    TilePartialSharedPtr ConnectedPartial = PartialByTile.at(ConnectedTile);
                    if (InPolyphony != ConnectedPartial->Polyphony)
                    {
                        // This is a graph edge where a polyphonic tile is connected to a monophonic
                        // tile or vice versa.  To make this work, we will add a partial to insert a
                        // combiner to either merge or spread values across lanes.  The actual register
                        // allocation will happen later.

                        const PartialType Adapter = (InPolyphony == 1) ? PartialType::LANE_MERGE : PartialType::LANE_SCATTER;
                        const uint32_t OutPolyphony = (InPolyphony == 1) ? 1 : MidiPolyphony;

                        TilePartialSharedPtr& FixupPartial = LaneFixupPartials[ConnectedPort];
                        if (FixupPartial == nullptr)
                        {
                            // There is no lane merge for this port yet, so we need to set it up here.
                            FixupPartial = std::make_shared<TilePartial>();
                            NextFlatGraph.push_back(FixupPartial);
                            FixupPartial->Type = Adapter;
                            FixupPartial->Tile = 0;
                            FixupPartial->DynamicPolyphony = false;
                            FixupPartial->Polyphony = OutPolyphony;
                            FixupPartial->Inputs = { { ConnectedPort }, };
                            FixupPartial->Outputs = { MakePortHandle(0, NextVirtualPortIndex++) };
                        }
                        assert(FixupPartial->Type == Adapter);
                        assert(FixupPartial->Tile == 0);
                        assert(FixupPartial->DynamicPolyphony == false);
                        assert(FixupPartial->Polyphony == OutPolyphony);
                        assert(FixupPartial->Inputs.size() == 1);
                        assert(FixupPartial->Inputs[0].size() == 1);
                        assert(FixupPartial->Outputs.size() == 1);
                        ConnectedPort = FixupPartial->Outputs[0];
                    }
                }
            }

            NextFlatGraph.push_back(Partial);
        }
        std::swap(FlatGraph, NextFlatGraph);
    }

    // Output registers can't be allocated in tandem with thunk generation, as graphs can have cycles.
    // Likewise, we need to allocate registers for everything we want to persist between patch generations,
    // not just the registers needed for the current version of a patch.
    std::map<PortHandle, std::ptrdiff_t> RegisterMap;
#ifndef NDEBUG
    std::map<PortHandle, uint32_t> RegisterWidths;
#endif
    {
        auto AllocateRegister = [&](PortHandle Port, uint32_t Lanes, double InitialValue = 0.0) -> std::ptrdiff_t
        {
            std::ptrdiff_t Offset = Program->RegisterFile.size();
            Program->RegisterFile.insert(Program->RegisterFile.end(), Lanes, InitialValue);
            RegisterMap[Port] = Offset;
#ifndef NDEBUG
            RegisterWidths[Port] = Lanes;
#endif
            return Offset;
        };
        auto AllocatePersistentRegister = [&](PortHandle Port, uint32_t Lanes, double InitialValue = 0.0) -> std::ptrdiff_t
        {
            std::ptrdiff_t Offset = AllocateRegister(Port, Lanes, InitialValue);
            Program->PersistentRegisters[Port] = { Offset, Lanes };
            return Offset;
        };

        BreadCrumbs.clear();
        auto AllocateTileRegisters = [&](EvalFrequency Frequency, TileHandle Tile, uint32_t Lanes) -> void
        {
            if (!BreadCrumbs.insert(Tile).second)
            {
                return;
            }

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
                    if (Frequency == EvalFrequency::ONCE)
                    {
                        AllocateRegister(OutputPort, Lanes);
                    }
                    else
                    {
                        AllocatePersistentRegister(OutputPort, Lanes);
                    }
                }

                for (int ClosureIndex = 0; ClosureIndex < static_cast<int>(ClosureCount); ++ClosureIndex)
                {
                    // Closure registers represent a thunk's internal state, and as such they must be
                    // persistent across patch revisions.
                    PortHandle ClosurePort = MakeClosureHandle(Tile, ClosureIndex);
                    if (Frequency == EvalFrequency::ONCE)
                    {
                        AllocateRegister(ClosurePort, Lanes);
                    }
                    else
                    {
                        AllocatePersistentRegister(ClosurePort, Lanes);
                    }
                }
            }
        };

        // First we walk through the graph and allocate registers in order of thunk execution.
        for (TilePartialSharedPtr Partial : FlatGraph)
        {
            if (Partial->Type == PartialType::TILE)
            {
                AllocateTileRegisters(Partial->Frequency, Partial->Tile, Partial->Polyphony);
            }
            else
            {
                for (PortHandle OutputPort : Partial->Outputs)
                {
                    if (PortHandleTilePart(OutputPort) == 0 || Partial->Frequency == EvalFrequency::ONCE)
                    {
                        AllocateRegister(OutputPort, Partial->Polyphony);
                    }
                    else
                    {
                        AllocatePersistentRegister(OutputPort, Partial->Polyphony);
                    }
                }
            }
        }

        // Then we walk through the persistent tiles, and allocate anything that is missing.
        for (auto& TileAndLanes : TileLanes)
        {
            const TileHandle Tile = TileAndLanes.first;
            const uint32_t Lanes = TileAndLanes.second;
            AllocateTileRegisters(EvalFrequency::UNKNOWN, Tile, Lanes);
        }
    }

    // Make sure we have all our tapes.
    {
        std::ptrdiff_t TapeCount = 0;
        for (TileHandle Tile : TapeTiles)
        {
            PortHandle Port = MakePortHandle(Tile, 0);
            TilePartialSharedPtr Partial = PartialByTile.at(Tile);

            std::ptrdiff_t BaseOffset = TapeCount;
            uint32_t LaneCount = Partial->Polyphony;

            [[maybe_unused]] const auto [_, WasInserted] = Program->PersistentTapes.insert({ Port, { BaseOffset, LaneCount } });
            assert(WasInserted == true);

            TapeCount += LaneCount;
        }
        Program->TapeFile.resize(TapeCount);
        for (MagicTapeUniquePtr& Tape : Program->TapeFile)
        {
            Tape = std::make_unique<BlankTape>();
        }
    }

    // Emit thunks, and adjust default values.
    {
        std::vector<double>* RegisterFile = &(Program->RegisterFile);
        std::vector<MagicTapeUniquePtr>* TapeFile = &(Program->TapeFile);

        // This is the part of the patch that only has to be evaluated once.
        std::vector<InstructionThunkSharedPtr> ConstantProgram;

        for (TilePartialSharedPtr& Partial : FlatGraph)
        {
            std::vector<std::vector<std::ptrdiff_t>> Inputs;
            Inputs.reserve(Partial->Inputs.size());

            for (std::vector<PortHandle>& ConnectedOutputs : Partial->Inputs)
            {
                std::vector<std::ptrdiff_t>& InputRegisters = Inputs.emplace_back();
                InputRegisters.reserve(ConnectedOutputs.size());

                for (PortHandle ConnectedOutput : ConnectedOutputs)
                {
                    InputRegisters.push_back(RegisterMap.at(ConnectedOutput));
                }
            }

            std::vector<std::ptrdiff_t> Outputs;
            Outputs.reserve(Partial->Outputs.size());
            for (PortHandle Output : Partial->Outputs)
            {
                Outputs.push_back(RegisterMap.at(Output));
            }

#ifndef NDEBUG
            std::vector<uint32_t> OutputWidths;
            OutputWidths.reserve(Partial->Outputs.size());
            for (PortHandle Output : Partial->Outputs)
            {
                OutputWidths.push_back(RegisterWidths.at(Output));
            }
            auto AppendThunk = [&](const EvalFrequency Frequency, InstructionThunkSharedPtr Thunk) -> void
            {
                assert(Thunk != nullptr);
                if (OutputWidths.size() > 0)
                {
                    uint32_t ExpectedPolyphony = Thunk->LaneJoiner() ? 1 : Thunk->Registers.Polyphony;
                    uint32_t MaxWidth = 0;
                    uint32_t MinWidth = uint32_t(-1);
                    for (const uint32_t OutputWidth : OutputWidths)
                    {
                        MaxWidth = std::max(MaxWidth, OutputWidth);
                        MinWidth = std::min(MinWidth, OutputWidth);
                    }
                    for (const uint32_t OutputWidth : OutputWidths)
                    {
                        if (OutputWidth != MaxWidth)
                        {
                            std::print(
                                "thunk \"{}\" somehow has a mix of monophonic and polyphonic outputs!\n",
                                Thunk->DebugName);
                        }
                    }
                    if (MaxWidth != ExpectedPolyphony)
                    {
                        std::print(
                            "thunk \"{}\" expects output width {}, but its outputs are {}!\n",
                            Thunk->DebugName, ExpectedPolyphony, MaxWidth);
                    }
                    assert(MinWidth == MaxWidth);
                    assert(MaxWidth == ExpectedPolyphony);
                }
                if (Frequency == EvalFrequency::ONCE)
                {
                    ConstantProgram.push_back(Thunk);
                }
                else
                {
                    Program->Program.push_back(Thunk);
                }
            };
#else
            auto AppendThunk = [&](const EvalFrequency Frequency, InstructionThunkSharedPtr Thunk) -> void
            {
                if (Frequency == EvalFrequency::ONCE)
                {
                    ConstantProgram.push_back(Thunk);
                }
                else
                {
                    Program->Program.push_back(Thunk);
                }
            };
#endif

            if (Partial->Type == PartialType::TILE || Partial->Type == PartialType::ADD_TILE)
            {
                const bool IsVirtual = Partial->Tile == 0;
                if (IsVirtual)
                {
                    assert(Partial->Type == PartialType::ADD_TILE);
                }
                const OpCode Symbol = IsVirtual ? OpCode::ADD : GetTileSymbol(Partial->Tile);

                if (Symbol == OpCode::CONST || Symbol == OpCode::IN || Symbol == OpCode::LANE_COUNT)
                {
                    // No thunks are created for these symbols.
                    continue;
                }
                else if (IsOutputSymbol(Symbol))
                {
                    std::ptrdiff_t OutputRegister = 0;

                    if (Inputs[0].size() == 1)
                    {
                        OutputRegister = Inputs[0][0];
                    }
                    else
                    {
                        OutputRegister = RegisterMap.at(MakePortHandle(Partial->Tile, 0));
                        Outputs = { OutputRegister };
                        if (Inputs[0].size() > 1)
                        {
                            std::vector<std::ptrdiff_t> Closures;
                            static const BasicCreateAndConnectFn AddCreateAndConnect = SymbolInfoMap.BasicCreateAndConnect.at((int)OpCode::ADD);
                            InstructionThunkSharedPtr Thunk = AddCreateAndConnect(RegisterFile, Inputs, Outputs, Closures);
                            Thunk->Registers.Polyphony = 1;
                            AppendThunk(EvalFrequency::LIVE, Thunk);
                        }
                    }

                    // Collect the patch output registers.
                    if (Symbol == OpCode::OUT)
                    {
                        Program->Outputs.push_back(OutputRegister);
                    }
                    else if (Symbol == OpCode::AUX)
                    {
                        Program->AuxOutputs[Partial->Tile] = OutputRegister;
                    }

                    // This tile is also the current active probe.
                    if (Program->ProbeConnected && Partial->Tile == ActiveProbeTile)
                    {
                        Program->ProbeInput = OutputRegister;
                    }
                }
                else
                {
                    const size_t ClosureCount = GetClosureCount(Symbol);
                    std::vector<std::ptrdiff_t> Closures;
                    Closures.reserve(ClosureCount);
                    for (int ClosureIndex = 0; ClosureIndex < static_cast<int>(ClosureCount); ++ClosureIndex)
                    {
                        PortHandle ClosurePort = MakeClosureHandle(Partial->Tile, ClosureIndex);
                        Closures.push_back(RegisterMap.at(ClosurePort));
                    }

                    InstructionThunkSharedPtr Thunk = nullptr;
                    {
                        auto Found = SymbolInfoMap.BasicCreateAndConnect.find((int)Symbol);
                        if (Found != SymbolInfoMap.BasicCreateAndConnect.end())
                        {
                            Thunk = Found->second(RegisterFile, Inputs, Outputs, Closures);
                            Thunk->Registers.Polyphony = Partial->Polyphony;
                            AppendThunk(Partial->Frequency, Thunk);
                        }
                    }

                    if (Thunk == nullptr)
                    {
                        auto Found = SymbolInfoMap.WidgetCreateAndConnect.find((int)Symbol);
                        if (Found != SymbolInfoMap.WidgetCreateAndConnect.end())
                        {
                            Thunk = Found->second(RegisterFile, Inputs, Outputs, Closures, SpecialInputs[Partial->Tile]);
                            Thunk->Registers.Polyphony = 1;
                            assert(Partial->Frequency == EvalFrequency::LIVE);
                            AppendThunk(EvalFrequency::LIVE, Thunk);
                        }
                    }

                    if (Thunk == nullptr)
                    {
                        auto Found = SymbolInfoMap.MidiCreateAndConnect.find((int)Symbol);
                        if (Found != SymbolInfoMap.MidiCreateAndConnect.end())
                        {
                            Thunk = Found->second(RegisterFile, Program.get(), Inputs, Outputs, Closures);
                            Thunk->Registers.Polyphony = MidiPolyphony;
                            assert(Partial->Frequency == EvalFrequency::LIVE);
                            AppendThunk(EvalFrequency::LIVE, Thunk);
                        }
                    }

                    if (Thunk == nullptr)
                    {
                        auto Found = SymbolInfoMap.TapeCreateAndConnect.find((int)Symbol);
                        if (Found != SymbolInfoMap.TapeCreateAndConnect.end())
                        {
                            PortHandle Port = MakePortHandle(Partial->Tile, 0);
                            RegisterAllocation& TapeAllocation = Program->PersistentTapes.at(Port);
                            std::ptrdiff_t TapeIndex = TapeAllocation.BaseOffset;
                            Thunk = Found->second(RegisterFile, TapeFile, TapeIndex, Inputs, Outputs, Closures);
                            Thunk->Registers.Polyphony = Partial->Polyphony;
                            assert(Partial->Frequency == EvalFrequency::LIVE);
                            AppendThunk(EvalFrequency::LIVE, Thunk);
                        }
                    }

                    assert(Thunk != nullptr);
#ifndef NDEBUG
                    {
                        const std::string TileName = GetTileLabel(Partial->Tile);
                        Thunk->SetDebugName(TileName);
                    }
#endif
                    if (Symbol == OpCode::ADSR && Partial->Polyphony > 1)
                    {
                        Thunk->Retriggerable = true;
                        uint32_t ThunkIndex = Program->Program.size() - 1;
                        assert(Program->Program[ThunkIndex] == Thunk);
                        if (Partial->Frequency != EvalFrequency::ONCE)
                        {
                            // TODO: figure out some means of determining if the trigger is directly or indirectly
                            // connected to a gate tile inntead of using the ADSR's polyphony as a proxy for this.
                            Program->Retriggerables.push_back(ThunkIndex);
                        }
                    }
                    else if (Symbol == OpCode::ADD_LANES)
                    {
                        assert(Partial->Polyphony);
                        Thunk->Registers.Polyphony = MidiPolyphony;
                    }
                }
            }
            else if (Partial->Type == PartialType::LANE_SCATTER)
            {
                std::vector<std::ptrdiff_t> Closures;
                std::shared_ptr<LaneScatterThunk> Thunk = std::make_shared<LaneScatterThunk>();
                Thunk->SetDebugName("Lane Fixup (Scatter)");
                Thunk->Registers.Polyphony = MidiPolyphony;
                Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
                Thunk->Reset();
                AppendThunk(Partial->Frequency, Thunk);
            }
            else
            {
                assert(Partial->Type == PartialType::LANE_MERGE);
                std::vector<std::ptrdiff_t> Closures;
                std::shared_ptr<LaneMergeThunk> Thunk = std::make_shared<LaneMergeThunk>();
                Thunk->SetDebugName("Lane Fixup (Merge)");
                Thunk->Registers.Polyphony = MidiPolyphony;
                Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
                Thunk->Reset();
                //assert(Partial->Frequency == EvalFrequency::LIVE);
                AppendThunk(Partial->Frequency, Thunk);
            }
        }

        for (InstructionThunkSharedPtr& Thunk : ConstantProgram)
        {
            Thunk->Crank(0.0);
        }
    }

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

#ifndef NDEBUG
    {
        std::print("\n\n##############################################################################\n");
        std::print("Patch compiled:\n");
        uint32_t ThunkIndex = 0;
        for (InstructionThunkSharedPtr& Thunk : Program->Program)
        {
            assert(Thunk != nullptr);
            assert(Thunk->Registers.Polyphony > 0);
            std::print("{} {}:\n", ThunkIndex++, Thunk->DebugName);
        }
    }
#endif

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
        PortsByRegisterOffset[static_cast<uint32_t>(Allocation.BaseOffset)] = Port;
    }
    for (std::ptrdiff_t Register = 0; Register < (std::ptrdiff_t)RegisterFile.size(); ++Register)
    {
        const double Value = RegisterFile.at(Register);
        auto Found = PortsByRegisterOffset.find(static_cast<uint32_t>(Register));
        if (Found != PortsByRegisterOffset.end())
        {
            PortHandle Port = Found->second;
            TileHandle Tile = PortHandleTilePart(Port);
            int32_t Index = (int32_t)PortHandlePortIndexPart(Port);
            fmt::print("    {:>4}: {:^8.4} ({}:{})\n", Register, Value, Tile, Index);
        }
        else
        {
            fmt::print("    {:>4}: {:^8.4}\n", Register, Value);
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
    for (uint32_t ThunkIndex : Retriggerables)
    {
        InstructionThunkSharedPtr& Thunk = Program.at(ThunkIndex);
        assert(Thunk != nullptr);
        assert(Thunk->Retriggerable);
    }
#endif

    constexpr bool EnableDebugLogging = false;

    if (EnableDebugLogging)
    {
        fmt::print("\n\n==============================================================================\n");
        fmt::print("Old register file:\n");
        Old.PrintRegisters();
        fmt::print("\nRunning migration:\n");
    }

    for (auto const& [Port, NewAllocation] : PersistentRegisters)
    {
        auto Found = Old.PersistentRegisters.find(Port);
        if (Found != Old.PersistentRegisters.end())
        {
            if (EnableDebugLogging)
            {
                fmt::print(" + MATCH: {}:{}", PortHandleTilePart(Port), (int32_t)PortHandlePortIndexPart(Port));
            }

            const RegisterAllocation& OldAllocation = Found->second;
            if (OldAllocation.LaneCount == NewAllocation.LaneCount)
            {
                if (EnableDebugLogging)
                {
                    fmt::print(" (copy, no resize)\n");
                }

                for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                {
                    const double MigratedValue = Old.RegisterFile.at(OldAllocation.BaseOffset + Lane);
                    if (EnableDebugLogging)
                    {
                        const double StompedValue = RegisterFile.at(NewAllocation.BaseOffset + Lane);
                        const uint32_t WriteOffset = static_cast<uint32_t>(NewAllocation.BaseOffset) + Lane;
                        fmt::print("    > Register[{}] = {:.4} -> {:.4}\n", WriteOffset, StompedValue, MigratedValue);
                    }
                    RegisterFile.at(NewAllocation.BaseOffset + Lane) = MigratedValue;
                }
            }
            else if (OldAllocation.LaneCount == 1)
            {
                if (EnableDebugLogging)
                {
                    fmt::print(" (mono -> poly resize)\n");
                }
                for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                {
                    const double MigratedValue = Old.RegisterFile.at(OldAllocation.BaseOffset);
                    if (EnableDebugLogging)
                    {
                        const double StompedValue = RegisterFile.at(NewAllocation.BaseOffset + Lane);
                        const uint32_t WriteOffset = static_cast<uint32_t>(NewAllocation.BaseOffset) + Lane;
                        fmt::print("    | Register[{}] = {:.4} -> {:.4}\n", WriteOffset, StompedValue, MigratedValue);
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
                    fmt::print(" (poly->mono RESET)\n");
                }
            }

            if (EnableDebugLogging)
            {
                fmt::print("\n");
            }
        }
        else
        {
            // The register is totally new, and already has the correct starting value.  No additional
            // handling is required.
            if (EnableDebugLogging)
            {
                fmt::print(" - no match: {}:{}\n", PortHandleTilePart(Port), (int32_t)PortHandlePortIndexPart(Port));
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
        fmt::print("\nMigration complete!\n");
        fmt::print("\nNew register file:\n");
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

                    for (uint32_t ThunkIndex : Retriggerables)
                    {
                        InstructionThunkSharedPtr& Thunk = Program.at(ThunkIndex);
                        assert(Thunk != nullptr);
                        assert(Thunk->Retriggerable);
                        Thunk->Retrigger(AssignedLane);
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
            OutLeft = static_cast<float>(RegisterFile.at(Outputs[0]));
            OutRight = static_cast<float>(RegisterFile.at(Outputs[0]));
        }
        else if (Outputs.size() > 1)
        {
            OutLeft = static_cast<float>(RegisterFile.at(Outputs[0]));
            OutRight = static_cast<float>(RegisterFile.at(Outputs[1]));
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
