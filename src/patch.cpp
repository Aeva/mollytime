
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
#include <random>
#include <format>
#include <print>
#include <functional>
#include <limits>
#include <utility>
#include <cmath>

#include "errors.h"
#include "patch.h"
#include "pipewire.h"


constinit double Pi = M_PI;
constinit double Tau = M_PI * 2.0;
constinit double Leftovers = M_PI / 2.0;


static std::random_device RandomDevice;
static std::mt19937 RandomGenerator{ RandomDevice() };
const double RngScale = 1.0 / double(RandomGenerator.max());

double Roll()
{
    // Returns between 0.0 and 1.0, inclusive.
    return double(RandomGenerator()) * RngScale;
}


constexpr double MidiNoteToHz(double Note)
{
    double Hz = std::pow(2.0, ((Note - 69.0) / 12.0)) * 440.0;
    return Hz;
}


constexpr double HzToMidiNote(double Hz)
{
    double Note = std::log2(Hz / 440.0) * 12.0 + 69.0;
    return Note;
}


constexpr double AmplitudeToDecibels(double Amplitude)
{
    // https://stackoverflow.com/questions/2445756/how-can-i-calculate-audio-db-level/9812267#9812267
    double dB = 20.0 * std::log10(Amplitude);
    return dB;
}


constexpr double DecibelsToAmplitude(double dB)
{
    double Amplitude = std::pow(10.0, dB / 20.0);
    return Amplitude;
}


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
    std::vector<std::string> DefaultNames;
    std::vector<std::vector<std::string>> InputNames;
    std::vector<std::vector<std::string>> OutputNames;
    std::vector<int> Closures;

    SymbolInfo()
    {
        DefaultNames.resize((int)OpCode::Count);
        InputNames.resize((int)OpCode::Count);
        OutputNames.resize((int)OpCode::Count);
        Closures.resize((int)OpCode::Count);

        Set(OpCode::CONST, "const", {}, {"#"});
        Set(OpCode::OUT, "out", {"out"}, {});
        Set(OpCode::SIN, "sin", {"hz"}, {"amp"}, 1);
        Set(OpCode::SQR, "sqr", {"hz"}, {"amp"}, 1);
        Set(OpCode::TRI, "tri", {"hz"}, {"amp"}, 1);
        Set(OpCode::ADD, "add", {"+"}, {"="});
        Set(OpCode::MUL, "mul", {"*"}, {"="});
        Set(OpCode::RCP, "rcp", {"*"}, {"="});
        Set(OpCode::MIN, "min", {"min"}, {"="});
        Set(OpCode::MAX, "max", {"max"}, {"="});
        Set(OpCode::MIX, "mix", {"L", "R", "balance"}, {"="});
        Set(OpCode::FLP, "flip\nflop", {"clock"}, {"even", "odd"}, 1);
        Set(OpCode::RNG, "rng", {"clock"}, {"#"}, 1);
        Set(OpCode::ADSR, "adsr", {"trigger", "a", "d", "s", "r"}, {"#"}, 3);
        Set(OpCode::GATE, "gate", {}, {"gate"});
        Set(OpCode::NOTE, "note", {}, {"note"});
        Set(OpCode::VELO, "velocity", {}, {"velocity"});
        Set(OpCode::PRES, "pressure", {}, {"pressure"});
        Set(OpCode::MIDI_HZ, "midi\nto hz", {"note"}, {"hz"});
        Set(OpCode::LOUD_FUDGE, "loud\nfudge", {"hz"}, {"amp"});
    }

    void Set(OpCode Symbol, std::string Name,
             std::vector<std::string> Inputs, std::vector<std::string> Outputs, int HiddenOutputs = 0)
    {
        DefaultNames[(int)Symbol] = Name;
        InputNames[(int)Symbol] = Inputs;
        OutputNames[(int)Symbol] = Outputs;
        Closures[(int)Symbol] = HiddenOutputs;
    }
};

const SymbolInfo SymbolInfoMap;


std::string GetDefaultName(OpCode Symbol)
{
    return SymbolInfoMap.DefaultNames[(int)Symbol];
}


int GetClosureCount(OpCode Symbol)
{
    return SymbolInfoMap.Closures[(int)Symbol];
}


double Combine(auto& Combiner, std::vector<RunningStateSharedPtr>& Inputs, double Default=0.0)
{
    double Result = Inputs.size() == 0 ? Default : Inputs[0]->Get();
    for (int Index = 1; Index < Inputs.size(); ++Index)
    {
        Result = Combiner(Result, Inputs[Index]->Get());
    }
    return Result;
}

static const auto CombinerAdd = [](double LHS, double RHS) -> double { return LHS +RHS; };
static const auto CombinerMul = [](double LHS, double RHS) -> double { return LHS *RHS; };
static const auto CombinerMin = [](double LHS, double RHS) -> double { return std::min(LHS, RHS); };
static const auto CombinerMax = [](double LHS, double RHS) -> double { return std::max(LHS, RHS); };


struct SinThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> InFrequencyHz;
    RunningStateSharedPtr OutAmplitude = nullptr;
    RunningStateSharedPtr ActivePhase = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
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
    RunningStateSharedPtr OutAmplitude = nullptr;
    RunningStateSharedPtr ActivePhase = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
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
    RunningStateSharedPtr OutAmplitude = nullptr;
    RunningStateSharedPtr ActivePhase = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
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
        if (int(IntegerPart) % 2 == 1)
        {
            Alpha = 1.0 - Alpha;
        }
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
        Output->Set(Combine(CombinerAdd, Inputs, 0.0));
    }

    virtual ~AddThunk() {};
};


struct MulThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        Output->Set(Combine(CombinerMul, Inputs, 0.0));
    }

    virtual ~MulThunk() {};
};


struct RcpThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Input = Combine(CombinerMul, Inputs, 0.0);
        Output->Set(1.0 / Input);
    }

    virtual ~RcpThunk() {};
};


struct MinThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        Output->Set(Combine(CombinerMin, Inputs, 0.0));
    }

    virtual ~MinThunk() {};
};


struct MaxThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        Output->Set(Combine(CombinerMax, Inputs, 0.0));
    }

    virtual ~MaxThunk() {};
};


struct MixThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Left;
    std::vector<RunningStateSharedPtr> Right;
    std::vector<RunningStateSharedPtr> Balance;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double X = Combine(CombinerAdd, Left, 0.0);
        double Y = Combine(CombinerAdd, Right, 0.0);
        double Alpha = Combine(CombinerAdd, Balance, 0.5);
        Output->Set((1.0 - Alpha) * X + Alpha * Y);
    }

    virtual ~MixThunk() {};
};


struct FlipFlopThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr EvenOutput = nullptr;
    RunningStateSharedPtr OddOutput = nullptr;
    RunningStateSharedPtr LastInput = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double LastEven = EvenOutput->Get();
        double LastOdd = OddOutput->Get();
        if (LastEven == LastOdd)
        {
            EvenOutput->Set(1.0);
            OddOutput->Set(0.0);
        }

        if (Inputs.size() > 0)
        {
            double Clock = Combine(CombinerAdd, Inputs, 0.0);

            double Previous = LastInput->Get();
            LastInput->Set(Clock);
            if (Previous <= 0.0 && Clock >= 1.0)
            {
                if (LastEven > 0.0)
                {
                    EvenOutput->Set(0.0);
                    OddOutput->Set(1.0);
                }
                else
                {
                    EvenOutput->Set(1.0);
                    OddOutput->Set(0.0);
                }
            }
        }
    }

    virtual ~FlipFlopThunk() {};
};


struct RandomThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;
    RunningStateSharedPtr LastInput = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        if (Inputs.size() > 0)
        {
            double Clock = Combine(CombinerAdd, Inputs, 0.0);

            double Previous = LastInput->Get();
            LastInput->Set(Clock);
            if (Previous <= 0.0 && Clock >= 1.0)
            {
                Output->Set(Roll());
            }
        }
    }

    virtual ~RandomThunk() {};
};


struct AdsrThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Trigger;
    std::vector<RunningStateSharedPtr> AttackTime;
    std::vector<RunningStateSharedPtr> DecayTime;
    std::vector<RunningStateSharedPtr> SustainAmount;
    std::vector<RunningStateSharedPtr> ReleaseTime;
    RunningStateSharedPtr OutAmplitude = nullptr;
    RunningStateSharedPtr LastTrigger = nullptr;
    RunningStateSharedPtr ElapsedTime = nullptr;
    RunningStateSharedPtr Mode = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Trig = Combine(CombinerAdd, Trigger, 0.0);
        double Previous = LastTrigger->Get();
        LastTrigger->Set(Trig);

        double Attack = Combine(CombinerAdd, AttackTime, 0.1);
        double Decay = Combine(CombinerAdd, DecayTime, 0.1);
        double Sustain = Combine(CombinerAdd, SustainAmount, 1.0);
        double Release = Combine(CombinerAdd, ReleaseTime, 1.0);

        if (Trig >= 1.0 && Previous <= 0.0)
        {
            // Begin attack.
            OutAmplitude->Set(0.0);
            ElapsedTime->Set(0.0);
            Mode->Set(1.0); // rising
        }
        else if (Trig <= 0.0 && Previous >= 1.0)
        {
            // Begin release.
            OutAmplitude->Set(Sustain);
            ElapsedTime->Set(0.0);
            Mode->Set(-1.0); // falling
        }
        else if (Mode->Get() == 1.0)
        {
            // Attack, decay, or sustain
            double Elapsed = std::max(0.0, std::min(Attack + Decay, ElapsedTime->Get() + SampleInterval));
            ElapsedTime->Set(Elapsed);

            double Peak = Decay > 0.0 ? 1.0 : Sustain;
            if (Elapsed < Attack)
            {
                double Alpha = std::max(0.0, std::min(1.0, Elapsed / Attack));
                OutAmplitude->Set(Peak * Alpha);
            }
            else
            {
                double Alpha = std::max(0.0, std::min(1.0, (Elapsed - Attack) / Decay));
                OutAmplitude->Set((1.0 - Alpha) * Peak + Alpha * Sustain);
            }

        }
        else if (Mode->Get() == -1.0)
        {
            // Release
            double Elapsed = std::max(0.0, std::min(Release, ElapsedTime->Get() + SampleInterval));
            ElapsedTime->Set(Elapsed);
            if (Elapsed < Release)
            {
                double Alpha = 1.0 - std::max(0.0, std::min(1.0, Elapsed / Release));
                OutAmplitude->Set(Sustain * Alpha);
            }
            else
            {
                Mode->Set(0.0);
                OutAmplitude->Set(0.0);
            }
        }
    }

    virtual ~AdsrThunk() {};
};


struct GateThunk : public InstructionThunk
{
    RunningStateSharedPtr MidiGate;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        Output->Set(MidiGate->Get());
    }

    virtual ~GateThunk() {};
};


struct NoteThunk : public InstructionThunk
{
    RunningStateSharedPtr MidiNote;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        Output->Set(MidiNote->Get());
    }

    virtual ~NoteThunk() {};
};


struct VelocityThunk : public InstructionThunk
{
    RunningStateSharedPtr MidiVelocity;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        Output->Set(MidiVelocity->Get());
    }

    virtual ~VelocityThunk() {};
};


struct PressureThunk : public InstructionThunk
{
    RunningStateSharedPtr MidiPressure;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        Output->Set(MidiPressure->Get());
    }

    virtual ~PressureThunk() {};
};


struct MidiToHzThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        double Note = Combine(CombinerAdd, Inputs, 0.0);
        Output->Set(MidiNoteToHz(Note));
    }

    virtual ~MidiToHzThunk() {};
};


struct LoudnessFudgeThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        // https://merveilles.town/@cancel/114848900879804284
        const double Peak = AmplitudeToDecibels(1.0);
        const double LowEdge = HzToMidiNote(2000.0) - 6.0;
        const double HighEdge = LowEdge + 6.0;
        double Hz = Combine(CombinerAdd, Inputs, 0.0);
        double Note = HzToMidiNote(Hz);
        double dB = Peak;
        if (Note >= LowEdge && Note <= HighEdge)
        {
            dB -= 3.0;
        }
        else
        {
            double NearestEdge = (Note < LowEdge) ? LowEdge : HighEdge;
            double Offset = std::abs(Note - NearestEdge) / 12.0;
            dB += Offset * 4.5;
        }
        Output->Set(DecibelsToAmplitude(dB));
    }

    virtual ~LoudnessFudgeThunk() {};
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
    int Closures = GetClosureCount(Symbol);
    for (int ClosureIndex = 0; ClosureIndex < Closures; ++ClosureIndex)
    {
        PortHandle Closure = MakeClosureHandle(AllocatedHandle, ClosureIndex);
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
        Disconnect(std::get<0>(Wire), std::get<1>(Wire));
    }
    for (PortHandle Port : GetTileOutputPorts(Tile))
    {
        ActiveOutputs.erase(Port);
    }

    OpCode Symbol = GetTileSymbol(Tile);
    int Closures = GetClosureCount(Symbol);
    for (int ClosureIndex = 0; ClosureIndex < Closures; ++ClosureIndex)
    {
        PortHandle Closure = MakeClosureHandle(Tile, ClosureIndex);
        ActiveOutputs.erase(Closure);
    }

    TileSymbols.erase(Tile);
    TileConstants.erase(Tile);
    TileNames.erase(Tile);
    Recompile();
}


std::vector<TileHandle> Patch::GetAllTileHandles()
{
    std::vector<TileHandle> Handles;
    for (const auto& Entry : TileSymbols)
    {
        Handles.push_back(Entry.first);
    }
    return Handles;
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
    Program->MidiGate = MidiGate;
    Program->MidiNote = MidiNote;
    Program->MidiVelocity = MidiVelocity;
    Program->MidiPressure = MidiPressure;

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
            std::vector<std::vector<RunningStateSharedPtr>> Inputs;
            for (int PortIndex = 0; PortIndex < InputCount; ++PortIndex)
            {
                std::vector<RunningStateSharedPtr>& PortInputs = Inputs.emplace_back();
                PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
                for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
                {
                    PortInputs.push_back(ActiveOutputs.at(ConnectedOutput));
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
                Thunk->InFrequencyHz = Inputs[0];
                Thunk->OutAmplitude = Outputs[0];
                Thunk->ActivePhase = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                return nullptr;
            }
            else if (Symbol == OpCode::SQR)
            {
                auto Thunk = std::make_shared<SqrThunk>();
                Thunk->InFrequencyHz = Inputs[0];
                Thunk->OutAmplitude = Outputs[0];
                Thunk->ActivePhase = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                return nullptr;
            }
            else if (Symbol == OpCode::TRI)
            {
                auto Thunk = std::make_shared<TriThunk>();
                Thunk->InFrequencyHz = Inputs[0];
                Thunk->OutAmplitude = Outputs[0];
                Thunk->ActivePhase = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
                return nullptr;
            }
            else if (Symbol == OpCode::ADD)
            {
                auto Thunk = std::make_shared<AddThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::MUL)
            {
                auto Thunk = std::make_shared<MulThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::RCP)
            {
                auto Thunk = std::make_shared<RcpThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::MIN)
            {
                auto Thunk = std::make_shared<MinThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::MAX)
            {
                auto Thunk = std::make_shared<MaxThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::MIX)
            {
                auto Thunk = std::make_shared<MixThunk>();
                Thunk->Left = Inputs[0];
                Thunk->Right = Inputs[1];
                Thunk->Balance = Inputs[2];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::FLP)
            {
                auto Thunk = std::make_shared<FlipFlopThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->EvenOutput = Outputs[0];
                Thunk->OddOutput = Outputs[1];
                Thunk->LastInput = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::RNG)
            {
                auto Thunk = std::make_shared<RandomThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Thunk->LastInput = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::ADSR)
            {
                auto Thunk = std::make_shared<AdsrThunk>();
                Thunk->Trigger = Inputs[0];
                Thunk->AttackTime = Inputs[1];
                Thunk->DecayTime = Inputs[2];
                Thunk->SustainAmount = Inputs[3];
                Thunk->ReleaseTime = Inputs[4];
                Thunk->OutAmplitude = Outputs[0];
                Thunk->LastTrigger = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Thunk->ElapsedTime = ActiveOutputs.at(MakeClosureHandle(Tile, 1));
                Thunk->Mode = ActiveOutputs.at(MakeClosureHandle(Tile, 2));
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::GATE)
            {
                auto Thunk = std::make_shared<GateThunk>();
                Thunk->MidiGate = MidiGate;
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::NOTE)
            {
                auto Thunk = std::make_shared<NoteThunk>();
                Thunk->MidiNote = MidiNote;
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::VELO)
            {
                auto Thunk = std::make_shared<VelocityThunk>();
                Thunk->MidiVelocity = MidiVelocity;
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::PRES)
            {
                auto Thunk = std::make_shared<PressureThunk>();
                Thunk->MidiPressure = MidiPressure;
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::MIDI_HZ)
            {
                auto Thunk = std::make_shared<MidiToHzThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::LOUD_FUDGE)
            {
                auto Thunk = std::make_shared<LoudnessFudgeThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            return nullptr;
        }

        std::unreachable();
    };

    Program->Outputs.clear();
    for (const auto& [Tile, Symbol] : TileSymbols)
    {
        if (Symbol == OpCode::OUT)
        {
            RunningStateSharedPtr Output = Step(Tile);
            if (Output != nullptr)
            {
                Program->Outputs.push_back(Output);
            }
        }
    }

    return Program;
}


void Patch::Recompile()
{
    ScratchSharedPtr CurrentProgram = Compile();
    AudioStream::Get()->ProgramChange(CurrentProgram);
}


double Scratch::Eval(double SampleInterval)
{
    Midi::ProcessEvents(this);

    for (std::shared_ptr<InstructionThunk>& Thunk : Program)
    {
        Thunk->Crank(SampleInterval);
    }
    double Out = 0.0;
    for (RunningStateSharedPtr Output : Outputs)
    {
        Out += Output->Get();
    }
    return Out;
}


void Scratch::NoteOn(uint8_t Note, uint8_t Velocity, uint8_t Channel)
{
    if (Velocity > 0)
    {
        MidiGate->Set(1.0);
        MidiNote->Set(double(Note));
        double V = double(Velocity) / 127.0;
        MidiVelocity->Set(V);
    }
    else if (double(Note) == MidiNote->Get())
    {
        MidiGate->Set(0.0);
        MidiVelocity->Set(0.0);
        MidiPressure->Set(0.0);
    }
}


void Scratch::NotePressure(uint8_t Note, uint8_t Pressure, uint8_t Channel)
{
    if (double(Note) == MidiNote->Get())
    {
        MidiPressure->Set(double(Pressure) / 127.0);
    }
}
