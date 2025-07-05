
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
#include <print>
#include <functional>
#include <limits>
#include <utility>
#include <cmath>

#include "patch.h"
#include "pipewire.h"


constinit double Pi = M_PI;
constinit double Tau = M_PI * 2.0;
constinit double Leftovers = M_PI / 2.0;


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
        Set(OpCode::SQR, OpCode::ADD, "sqr", {"hz"}, {"amp"});
        Set(OpCode::TRI, OpCode::ADD, "tri", {"hz"}, {"amp"});
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


struct SinThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> InFrequencyHz;
    RunningStateSharedPtr ActivePhase = nullptr;
    RunningStateSharedPtr OutAmplitude = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Hz = InFrequencyHz.size() == 0 ? 440.0 : 0.0;
        for (const RunningStateSharedPtr& Input : InFrequencyHz)
        {
            Hz += Input->Get();
        }
        double Phase = ActivePhase->Get();
        Phase += Tau * Hz * SampleInterval;
        if (Phase > Tau)
        {
            Phase -= Tau;
        }
        ActivePhase->Set(Phase);
        OutAmplitude->Set(std::sin(Phase));
    }

    virtual ~SinThunk() {};
};


struct SqrThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> InFrequencyHz;
    RunningStateSharedPtr ActivePhase = nullptr;
    RunningStateSharedPtr OutAmplitude = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Hz = InFrequencyHz.size() == 0 ? 440.0 : 0.0;
        for (const RunningStateSharedPtr& Input : InFrequencyHz)
        {
            Hz += Input->Get();
        }
        double Phase = ActivePhase->Get();
        Phase += Tau * Hz * SampleInterval;
        while (Phase > Tau)
        {
            Phase -= Tau;
        }
        ActivePhase->Set(Phase);
        double Sign = Phase <= Pi ? 1.0 : -1.0;
        OutAmplitude->Set(Sign);
    }

    virtual ~SqrThunk() {};
};


struct TriThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> InFrequencyHz;
    RunningStateSharedPtr ActivePhase = nullptr;
    RunningStateSharedPtr OutAmplitude = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Hz = InFrequencyHz.size() == 0 ? 440.0 : 0.0;
        for (const RunningStateSharedPtr& Input : InFrequencyHz)
        {
            Hz += Input->Get();
        }
        double Phase = ActivePhase->Get();
        Phase += Tau * Hz * SampleInterval;
        while (Phase > Tau)
        {
            Phase -= Tau;
        }
        ActivePhase->Set(Phase);
        double Sign = Phase <= Pi ? 1.0 : -1.0;
        double IntegerPart = 0.0;
        double Alpha = std::modf(Phase / Leftovers, &IntegerPart);
        OutAmplitude->Set(Alpha * Sign);
    }

    virtual ~TriThunk() {};
};


struct AddThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Result = Inputs[0]->Get();
        for (int Index = 1; Index < Inputs.size(); ++Index)
        {
            Result += Inputs[Index]->Get();
        }
        Output->Set(Result);
    }

    virtual ~AddThunk() {};
};


struct MulThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Result = Inputs[0]->Get();
        for (int Index = 1; Index < Inputs.size(); ++Index)
        {
            Result *= Inputs[Index]->Get();
        }
        Output->Set(Result);
    }

    virtual ~MulThunk() {};
};


struct MinThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Result = Inputs[0]->Get();
        for (int Index = 1; Index < Inputs.size(); ++Index)
        {
            Result = std::min(Result, Inputs[Index]->Get());
        }
        Output->Set(Result);
    }

    virtual ~MinThunk() {};
};


struct MaxThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Result = Inputs[0]->Get();
        for (int Index = 1; Index < Inputs.size(); ++Index)
        {
            Result = std::max(Result, Inputs[Index]->Get());
        }
        Output->Set(Result);
    }

    virtual ~MaxThunk() {};
};


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
        ActiveOutputs[Port] = std::make_shared<RunningState>(0.0);
    }
    if (Symbol == OpCode::SIN || Symbol == OpCode::SQR || Symbol == OpCode::TRI)
    {
        PortHandle Closure = MakeClosureHandle(AllocatedHandle, 0);
        ActiveOutputs[Closure] = std::make_shared<RunningState>(0.0);
    }
    return AllocatedHandle;
}


TileHandle Patch::MakeTile(double Constant)
{
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
    for (PortHandle Port : GetTileInputPorts(Tile))
    {
        ByInput.erase(Port);
    }
    for (PortHandle Port : GetTileOutputPorts(Tile))
    {
        ByOutput.erase(Port);
        ActiveOutputs.erase(Port);
    }

    OpCode Symbol = GetTileSymbol(Tile);
    if (Symbol == OpCode::SIN || Symbol == OpCode::SQR || Symbol == OpCode::TRI)
    {
        PortHandle Closure = MakeClosureHandle(Tile, 0);
        ActiveOutputs.erase(Closure);
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


double Patch::GetConstant(TileHandle Tile)
{
    return TileConstants.at(Tile);
}


void Patch::SetConstant(TileHandle Tile, double NewValue)
{
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
    // Patch should never mutate the shared pointers stored in Patch::ActiveOutputs,
    // as the active Scratch object will be continuously reading and mutating these
    // values.  By instead replacing the entries stored in Patch::ActiveOutputs, the
    // new constant values only take effect in subsequently compiled Scratch objects.
    PortHandle Port = MakePortHandle(Tile, 0);
    ActiveOutputs[Port] = std::make_shared<RunningState>(NewValue);
    Recompile();
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

    Recompile();
}


void Patch::Disconnect(PortHandle OutputPort, PortHandle InputPort)
{
    Wires.erase({OutputPort, InputPort});
    ByInput[InputPort].erase(OutputPort);
    ByOutput[OutputPort].erase(InputPort);

    Recompile();
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


ScratchSharedPtr Patch::Compile()
{
    std::set<TileHandle> BreadCrumbs;
    ScratchSharedPtr Program = std::make_shared<Scratch>();

    std::function<RunningStateSharedPtr(TileHandle)> Step = [&](const TileHandle Tile) -> RunningStateSharedPtr
    {
        if (!BreadCrumbs.insert(Tile).second)
        {
            return nullptr;
        }

        const OpCode Symbol = GetTileSymbol(Tile);
        if (Symbol == OpCode::CONST)
        {
            // Constant tiles return early because they terminate recursion,
            // and because they have no thunk.
            return nullptr;
        }

        const int InputCount = SymbolInfoMap.InputNames[(int)Symbol].size();
        const int OutputCount = SymbolInfoMap.OutputNames[(int)Symbol].size();

        // Recurse first to populate everything sequentally.
        for (int PortIndex = 0; PortIndex < InputCount; ++PortIndex)
        {
            PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
            for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
            {
                Step(PortHandleTilePart(ConnectedOutput));
            }
        }

        if (Symbol == OpCode::OUT)
        {
            // The output tile does not have any specific behavior, but may emit
            // an implicit add.

            const PortHandle InputHandle = MakePortHandle(Tile, 0);
            std::set<PortHandle> ConnectedOutputs = ByInput.at(InputHandle);

            if (ConnectedOutputs.size() == 0)
            {
                return nullptr;
            }
            else if (ConnectedOutputs.size() == 1)
            {
                for (PortHandle ConnectedOutput : ConnectedOutputs)
                {
                    return ActiveOutputs.at(ConnectedOutput);
                }
            }
            else
            {
                std::vector<RunningStateSharedPtr> Inputs;
                for (PortHandle ConnectedOutput : ConnectedOutputs)
                {
                    Inputs.push_back(ActiveOutputs.at(ConnectedOutput));
                }

                auto Thunk = std::make_shared<AddThunk>();
                Thunk->Inputs = Inputs;
                Thunk->Output = std::make_shared<RunningState>(0.0);
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                return Thunk->Output;
            }
        }
        else
        {
            std::vector<RunningStateSharedPtr> Inputs;
            for (int PortIndex = 0; PortIndex < InputCount; ++PortIndex)
            {
                PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
                for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
                {
                    Inputs.push_back(ActiveOutputs.at(ConnectedOutput));
                }
            }

            std::vector<RunningStateSharedPtr> Outputs;
            for (int PortIndex = 0; PortIndex < OutputCount; ++PortIndex)
            {
                PortHandle OutputHandle = MakePortHandle(Tile, PortIndex);
                Outputs.push_back(ActiveOutputs.at(OutputHandle));
            }

            if (Symbol == OpCode::SIN)
            {
                auto Thunk = std::make_shared<SinThunk>();
                Thunk->InFrequencyHz = Inputs;
                Thunk->ActivePhase = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Thunk->OutAmplitude = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                return nullptr;
            }
            else if (Symbol == OpCode::SQR)
            {
                auto Thunk = std::make_shared<SqrThunk>();
                Thunk->InFrequencyHz = Inputs;
                Thunk->ActivePhase = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Thunk->OutAmplitude = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                return nullptr;
            }
            else if (Symbol == OpCode::TRI)
            {
                auto Thunk = std::make_shared<TriThunk>();
                Thunk->InFrequencyHz = Inputs;
                Thunk->ActivePhase = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Thunk->OutAmplitude = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                return nullptr;
            }

            if (Inputs.size() > 0)
            {
                if (Symbol == OpCode::ADD)
                {
                    auto Thunk = std::make_shared<AddThunk>();
                    Thunk->Inputs = Inputs;
                    Thunk->Output = Outputs[0];
                    Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                }
                else if (Symbol == OpCode::MUL)
                {
                    auto Thunk = std::make_shared<MulThunk>();
                    Thunk->Inputs = Inputs;
                    Thunk->Output = Outputs[0];
                    Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                }
                else if (Symbol == OpCode::MIN)
                {
                    auto Thunk = std::make_shared<MinThunk>();
                    Thunk->Inputs = Inputs;
                    Thunk->Output = Outputs[0];
                    Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                }
                else if (Symbol == OpCode::MAX)
                {
                    auto Thunk = std::make_shared<MaxThunk>();
                    Thunk->Inputs = Inputs;
                    Thunk->Output = Outputs[0];
                    Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                }
            }

            return nullptr;
        }

        std::unreachable();
    };

    RunningStateSharedPtr FinalOutput = nullptr;
    for (const auto& [Tile, Symbol] : TileSymbols)
    {
        if (Symbol == OpCode::OUT)
        {
            FinalOutput = Step(Tile);
            if (FinalOutput != nullptr)
            {
                break;
            }
        }
    }

    if (FinalOutput == nullptr)
    {
        FinalOutput = std::make_shared<RunningState>(0.0);
    }

    Program->Output = FinalOutput;
    return Program;
}


void Patch::Recompile()
{
    ScratchSharedPtr CurrentProgram = Compile();
#if 0
    const int InstructionCount = CurrentProgram->Program.size();
    if (InstructionCount > 0)
    {
        std::print("Compiled instruction count: {}\n", InstructionCount);
        std::vector<double> Samples;
        Samples.resize(146);
        double Gain = 0.5;
        for (double& Sample : Samples)
        {
            Sample = CurrentProgram->Eval(1.0f / 48000.0f) * Gain;
        }
        for (int y = 0; y < 30; ++y)
        {
            double Alpha = (double(y) / 29.0f) * 2.0f - 1.0f;
            for (double& Sample : Samples)
            {
                if ((Alpha < 0) == (Sample < 0) && std::abs(Alpha) < std::abs(Sample))
                {
                    std::print("*");
                }
                else
                {
                    std::print(" ");
                }
            }
            std::print("\n");
        }
    }
#endif
    AudioStream::Get()->ProgramChange(CurrentProgram);
}


double Scratch::Eval(double SampleInterval)
{
    for (std::shared_ptr<InstructionThunk>& Thunk : Program)
    {
        Thunk->Crank(SampleInterval);
    }
    return Output->Get();
}
