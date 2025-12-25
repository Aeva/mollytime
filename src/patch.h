
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
#include <map>
#include <unordered_map>
#include <set>
#include <mutex>
#include <atomic>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <cmath>

#include "perf.h"
#include "alsa_midi.h"


// TileId
using TileHandle = uint32_t;


// TileId is the upper DWORD, Port Index lower DWORD
using PortHandle = uint64_t;


using WireHandle = std::tuple<PortHandle, PortHandle>;


enum class OpCode : uint32_t
{
    GO = 0,
    CONST,
    SCOPE,
    IN,
    OUT,
    AUX,
    SIN,
    SQR,
    TRI,
    SAW,
    NOI,
    PHASE,
    SIN_TRAIN,
    SQR_TRAIN,
    TRI_TRAIN,
    SAW_TRAIN,
    PWM,
    ADD,
    MUL,
    RCP,
    POW,
    SPOW,
    MIN,
    MAX,
    CLAMP,
    FLOOR,
    CEIL,
    ROUND,
    SIGN,
    ABS,
    FLD,
    INV,
    STU,
    UTS,
    MIX,
    BAL,
    PLS,
    FLP,
    RNG,
    GRAD,
    TPTSVF_LOWPASS,
    TPTSVF_BANDPASS,
    TPTSVF_HIGHPASS,
    TPTSVF_NOTCH,
    ADSR,
    QNTZ,
    ISQN,
    RSQN,
    GATE,
    NOTE,
    VELO,
    PRES,
    CTRL,
    MIDI_HZ,
    LOUD_FUDGE,
    BOOP,
    TWEAK,
    TAPE_LOOP,
    MOON,
    Count
};


PortHandle MakePortHandle(TileHandle TileId, uint32_t PortNumber);
TileHandle PortHandleTilePart(PortHandle Handle);
uint32_t PortHandlePortIndexPart(PortHandle Handle);

double EncodeSampleHandle(uint32_t SampleHandle);
uint32_t DecodeSampleHandle(double WireValue);


std::string GetDefaultName(OpCode Symbol);


struct RegisterAllocation
{
    std::ptrdiff_t BaseOffset;
    uint32_t LaneCount;
};


struct AtomicRunningState
{
    AtomicRunningState(double InSample)
        : Sample(InSample)
    {
    }
    double Get()
    {
        return Sample.load();
    }
    void Set(double NewSample)
    {
        Sample.store(NewSample);
    }
    void Add(double Increment)
    {
        Sample.fetch_add(Increment);
    }
    void Add(double Increment, double LimitLow, double LimitHigh)
    {
        double Value = Sample.load();
        Sample.store(std::min(std::max(Value + Increment, LimitLow), LimitHigh));
    }

private:
    std::atomic<double> Sample;
};

using AtomicRunningStateSharedPtr = std::shared_ptr<AtomicRunningState>;


struct MagicTape
{
    MagicTape()
    {
    }
    virtual size_t FindSample(double Position) = 0;
    virtual double ReadAndAdvance(uint64_t& Index) = 0;
    virtual void WriteAndAdvance(uint64_t& Index, double NewSample) = 0;
    virtual ~MagicTape()
    {
    }
};

using MagicTapeUniquePtr = std::unique_ptr<MagicTape>;


struct ProbeRunningState
{
    ProbeRunningState()
        : SampleMin(0.0)
        , SampleMax(0.0)
        , Reset(true)
    {
    }
    std::tuple<double, double> Get()
    {
        TRACEABLE_LOCK_GUARD(Crit);
        Reset = true;
        if (HandedNaN)
        {
            return { 1.0, -1.0 };
        }
        else
        {
            return { SampleMin, SampleMax };
        }
    }
    void Set(double NewSample)
    {
        TRACEABLE_LOCK_GUARD(Crit);
        if (Reset)
        {
            Reset = false;
            HandedNaN = false;
            SampleMin = NewSample;
            SampleMax = NewSample;
        }
        else
        {
            if (std::isnan(NewSample))
            {
                HandedNaN = true;
            }

            SampleMin = std::min(SampleMin, NewSample);
            SampleMax = std::max(SampleMax, NewSample);
        }
    }

private:
    double SampleMin;
    double SampleMax;
    double Reset = 0;
    bool HandedNaN = false;
    DECLARE_TRACEABLE_MUTEX(Crit);
};

using ProbeRunningStateSharedPtr = std::shared_ptr<ProbeRunningState>;


inline double CombinerAdd(double LHS, double RHS)
{
    return LHS + RHS;
}


inline double CombinerMul(double LHS, double RHS)
{
    return LHS * RHS;
}


inline double CombinerMin(double LHS, double RHS)
{
    return std::min(LHS, RHS);
}


inline double CombinerMax(double LHS, double RHS)
{
    return std::max(LHS, RHS);
}


using CombinerFn = decltype((CombinerAdd));


template<int InputCount, int OutputCount, int ClosureCount_>
struct InstructionInfo
{
    OpCode Symbol;
    std::string_view Name;
    std::array<std::string_view, InputCount> InputNames;
    std::array<std::string_view, OutputCount> OutputNames;
    int ClosureCount = ClosureCount_;
};


struct InstructionRegisters
{
    inline void Connect(
        std::vector<std::vector<std::ptrdiff_t>>& InInputs,
        std::vector<std::ptrdiff_t>& InOutputs,
        std::vector<std::ptrdiff_t>& InClosures,
        std::vector<double>* InRegisterFile)
    {
        Input = InInputs;
        Output = InOutputs;
        Closure = InClosures;
        RegisterFile = InRegisterFile;
    }

    inline std::vector<double> InputVector(uint32_t InputIndex)
    {
        std::vector<std::ptrdiff_t>& Target = Input[InputIndex];
        std::vector<double> Out;
        Out.reserve(Target.size());
        for (std::ptrdiff_t& Offset : Target)
        {
            Out.push_back(RegisterValue(Offset));
        }
        return Out;
    }

    inline bool InputConnected(uint32_t InputIndex)
    {
        return Input[InputIndex].size() > 0;
    }

    inline double CombineInput(uint32_t InputIndex, double Default = 0.0, CombinerFn Combiner = CombinerAdd)
    {
        std::vector<std::ptrdiff_t>& InputRegisters = Input[InputIndex];
        double Result = InputRegisters.size() == 0 ? Default : RegisterValue(InputRegisters[0]);
        for (int Index = 1; Index < static_cast<int>(InputRegisters.size()); ++Index)
        {
            double NextValue = RegisterValue(InputRegisters[Index]);
            Result = Combiner(Result, NextValue);
        }
        return Result;
    }

    inline double& OutputRef(uint32_t OutputIndex)
    {
        return *RegisterPtr(Output[OutputIndex]);
    }

    inline double& ClosureRef(uint32_t ClosureIndex)
    {
        return *RegisterPtr(Closure[ClosureIndex]);
    }

    inline void ZeroOut()
    {
        for (std::ptrdiff_t Offset : Output)
        {
            RegisterFile->data()[Offset] = 0.0;
        }
        for (std::ptrdiff_t Offset : Closure)
        {
            RegisterFile->data()[Offset] = 0.0;
        }
    }

private:
    inline double* RegisterPtr(std::ptrdiff_t Offset)
    {
        return RegisterFile->data() + Offset;
    }

    inline double RegisterValue(std::ptrdiff_t Offset)
    {
        return *RegisterPtr(Offset);
    }

    std::vector<std::vector<std::ptrdiff_t>> Input;
    std::vector<std::ptrdiff_t> Output;
    std::vector<std::ptrdiff_t> Closure;
    std::vector<double>* RegisterFile;

    // Temporary debug holepunch:
    friend struct Patch;
};


struct InstructionThunk
{
    OpCode DebugSymbol;
    InstructionRegisters Registers;
    virtual void Crank(double SampleInterval) = 0;

    virtual void Reset()
    {
        Registers.ZeroOut();
    }

    virtual void Retrigger()
    {
    }

    virtual ~InstructionThunk()
    {
    }
};

using InstructionThunkSharedPtr = std::shared_ptr<InstructionThunk>;


#if 0
struct MidiChannelState
{
    double Gate = 0.0;
    double Note = 50.0;
    double Velocity = 0.0;
    double Pressure = 0.0;

    double CtrlParam = 0.0;
    double CtrlValue = 0.0;
};
#endif

struct MidiNoteState
{
    double Gate = 0.0;
    double Note = 50.0;
    double Velocity = 0.0;
    double Pressure = 0.0;
    double Channel = -1.0;

    std::vector<InstructionThunkSharedPtr> Retriggerables;
};


struct Scratch final : public MidiHandler
{
    uint64_t Identity;
    uint32_t Polyphony;

    std::vector<double> RegisterFile;
    std::map<PortHandle, RegisterAllocation> PersistentRegisters;
    std::unordered_map<PortHandle, std::vector<MagicTapeUniquePtr>> Tapes;

    std::vector<InstructionThunkSharedPtr> Program;
    std::vector<std::ptrdiff_t> Outputs;
    std::map<TileHandle, std::ptrdiff_t> Inputs;
    std::map<TileHandle, std::ptrdiff_t> AuxOutputs;

    std::ptrdiff_t ProbeInput;
    ProbeRunningStateSharedPtr OutputProbe;
    ProbeRunningStateSharedPtr ScopeProbe;

    std::vector<MidiNoteState> MidiLanes;
    int32_t NextMidiLane = -1;

#if 0
    std::array<MidiChannelState, 16> MidiChannels;
    int MostRecentChannel = 0;
#endif

    void Migrate(Scratch& Old);

    void Crank(double SampleInterval, float& OutLeft, float& OutRight);
    MagicTape* FindTape(PortHandle Port, uint32_t Lane);

private:
    void PrintRegisters() const;
};

using ScratchSharedPtr = std::shared_ptr<Scratch>;


struct Patch
{
    uint64_t Identity;
    uint32_t MidiPolyphony = 4;
    std::unordered_map<TileHandle, OpCode> TileSymbols;
    std::unordered_map<TileHandle, double> TileConstants;
    std::unordered_map<TileHandle, std::string> TileNames;

    std::set<WireHandle> Wires;
    std::unordered_map<PortHandle, std::set<PortHandle>> ByInput;
    std::unordered_map<PortHandle, std::set<PortHandle>> ByOutput;

    std::unordered_map<TileHandle, uint32_t> TileLanes; // Tiles that have live registers, and their current widths.
    std::vector<TileHandle> ErasedTiles; // Used to erase stale registers

    Patch();

    TileHandle MakeTile(OpCode Symbol);
    TileHandle MakeTile(double Constant);
    void EraseTile(TileHandle Tile);

    std::vector<TileHandle> GetAllTileHandles();

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

    void Freeze();
    void Unfreeze();
    bool GetFrozen();

    void Connect(PortHandle OutputPort, PortHandle InputPort);
    void Disconnect(PortHandle OutputPort, PortHandle InputPort);
    void ToggleConnection(PortHandle OutputPort, PortHandle InputPort);

    bool CanConnect(TileHandle OutputTile, TileHandle InputTile);
    std::optional<WireHandle> GetImplicitWire(TileHandle OutputTile, TileHandle InputTile);

    std::tuple<double, double> ReadOutputProbe();
    std::tuple<double, double> ReadScopeProbe();
    void SetActiveProbe(TileHandle Tile);
    void ClearActiveProbe();
    void SetSpecialInput(TileHandle Tile, double Value);
    void AddSpecialInput(TileHandle Tile, double Value);
    void AddRangeSpecialInput(TileHandle Tile, double Value, double LimitLow, double LimitHigh);
    double GetSpecialInput(TileHandle Tile);

private:
    void ReplaceConstantOutput(TileHandle Tile, double NewValue);

#if 0
    // These should only ever be set or read by the audio thread:
    std::array<MidiChannelState, 16> MidiChannels;
#endif

    // This is a cache of known output tiles for the purpose of labeling
    // audio channels.  This is updated every time the program is compiled.
    std::unordered_map<TileHandle, std::string> OutputTileNames;

    TileHandle LastAssignedTileHandle;
    std::unordered_map<TileHandle, AtomicRunningStateSharedPtr> SpecialInputs;
    ProbeRunningStateSharedPtr OutputProbe = std::make_shared<ProbeRunningState>();
    ProbeRunningStateSharedPtr ScopeProbe = std::make_shared<ProbeRunningState>();
    TileHandle ActiveProbeTile;

    void Recompile();
    ScratchSharedPtr Compile();

    bool Frozen = false;
};
