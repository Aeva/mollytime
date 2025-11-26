
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
    CONST = 0,
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
    RSQN,
    GATE,
    NOTE,
    VELO,
    PRES,
    CTRL,
    MIDI_HZ,
    LOUD_FUDGE,
    BOOP,
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


struct RunningState
{
    RunningState(double InSample)
        : Sample(InSample)
    {
    }
    double* DangerGet()
    {
        return &Sample;
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

private:
    std::atomic<double> Sample;
};

using AtomicRunningStateSharedPtr = std::shared_ptr<AtomicRunningState>;


struct MagicTape
{
    MagicTape(TileHandle Tile)
        : TapeHandle(EncodeSampleHandle(Tile))
    {
    }

    virtual size_t FindSample(double Position)
    {
        return 0;
    }

    virtual double ReadAndAdvance(uint64_t& Index)
    {
        return 0.0;
    }

    virtual void WriteAndAdvance(uint64_t& Index, double NewSample)
    {
    }

    virtual ~MagicTape()
    {
    }

    double TapeHandle;
};

using MagicTapeSharedPtr = std::shared_ptr<MagicTape>;


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


inline double Combine(auto& Combiner, std::vector<RunningStateSharedPtr>& Inputs, double Default=0.0)
{
    double Result = Inputs.size() == 0 ? Default : Inputs[0]->Get();
    for (int Index = 1; Index < static_cast<int>(Inputs.size()); ++Index)
    {
        Result = Combiner(Result, Inputs[Index]->Get());
    }
    return Result;
}


template<int InputCount, int OutputCount, int ClosureCount_>
struct InstructionInfo
{
    OpCode Symbol;
    std::string_view Name;
    std::array<std::string_view, InputCount> InputNames;
    std::array<std::string_view, OutputCount> OutputNames;
    int ClosureCount = ClosureCount_;
};


template<int InputCount, int OutputCount, int ClosureCount>
struct InstructionRegisters
{
    std::array<std::vector<RunningStateSharedPtr>, InputCount> Input;
    std::array<RunningStateSharedPtr, OutputCount> Output;
    std::array<RunningStateSharedPtr, ClosureCount> Closure;

    void Connect(
        std::vector<std::vector<RunningStateSharedPtr>>& AssignedInputs,
        std::vector<RunningStateSharedPtr>& AssignedOutputs,
        std::vector<RunningStateSharedPtr>& AssignedClosures)
    {
        for (int Index = 0; Index < InputCount; ++Index)
        {
            Input[Index] = AssignedInputs[Index];
        }
        for (int Index = 0; Index < OutputCount; ++Index)
        {
            Output[Index] = AssignedOutputs[Index];
        }
        for (int Index = 0; Index < ClosureCount; ++Index)
        {
            Closure[Index] = AssignedClosures[Index];
        }
    }
};


struct InstructionThunk
{
    virtual void Crank(double SampleInterval) = 0;
    virtual ~InstructionThunk() {};
};


struct MidiChannelState
{
    RunningStateSharedPtr Gate = std::make_shared<RunningState>(0.0);
    RunningStateSharedPtr Note = std::make_shared<RunningState>(50.0);
    RunningStateSharedPtr Velocity = std::make_shared<RunningState>(0.0);
    RunningStateSharedPtr Pressure = std::make_shared<RunningState>(0.0);

    RunningStateSharedPtr CtrlParam = std::make_shared<RunningState>(0.0);
    RunningStateSharedPtr CtrlValue = std::make_shared<RunningState>(0.0);
};


struct Scratch final : public MidiHandler
{
    std::vector<std::shared_ptr<InstructionThunk>> Program;
    std::vector<RunningStateSharedPtr> Outputs;
    std::map<TileHandle, RunningStateSharedPtr> Inputs;
    std::map<TileHandle, RunningStateSharedPtr> AuxOutputs;

    std::unordered_map<TileHandle, MagicTapeSharedPtr> Tapes;
    RunningStateSharedPtr ProbeInput = nullptr;
    ProbeRunningStateSharedPtr OutputProbe;
    ProbeRunningStateSharedPtr ScopeProbe;

    std::array<MidiChannelState, 16> MidiChannels;

    void Crank(double SampleInterval, float& OutLeft, float& OutRight);
    MagicTapeSharedPtr FindTape(double WireValue);
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

private:
    void ReplaceConstantOutput(TileHandle Tile, double NewValue);

    // These should only ever be set or read by the audio thread:
    std::array<MidiChannelState, 16> MidiChannels;

    // This is a cache of known output tiles for the purpose of labeling
    // audio channels.  This is updated every time the program is compiled.
    std::unordered_map<TileHandle, std::string> OutputTileNames;

    TileHandle LastAssignedTileHandle;
    std::unordered_map<PortHandle, RunningStateSharedPtr> ActiveOutputs;
    std::unordered_map<TileHandle, AtomicRunningStateSharedPtr> SpecialInputs;
    std::unordered_map<TileHandle, MagicTapeSharedPtr> TapeCollection;
    ProbeRunningStateSharedPtr OutputProbe = std::make_shared<ProbeRunningState>();
    ProbeRunningStateSharedPtr ScopeProbe = std::make_shared<ProbeRunningState>();
    TileHandle ActiveProbeTile;

    void Recompile();
    ScratchSharedPtr Compile();

    bool Frozen = false;
};
