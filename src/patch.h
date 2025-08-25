
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
#include <vector>
#include <string>
#include <memory>

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
    NOI,
    ADD,
    MUL,
    RCP,
    MIN,
    MAX,
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
    PLS,
    FLP,
    RNG,
    GRAD,
    TPTSVF_LOWPASS,
    TPTSVF_BANDPASS,
    TPTSVF_HIGHPASS,
    TPTSVF_NOTCH,
    TPTSVF_ALLPASS,
    ADSR,
    GATE,
    NOTE,
    VELO,
    PRES,
    MIDI_HZ,
    LOUD_FUDGE,
    BOOP,
    TAPE_LOOP,
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
        return { SampleMin, SampleMax };
    }
    void Set(double NewSample)
    {
        TRACEABLE_LOCK_GUARD(Crit);
        if (Reset)
        {
            Reset = false;
            SampleMin = NewSample;
            SampleMax = NewSample;
        }
        else
        {
            SampleMin = std::min(SampleMin, NewSample);
            SampleMax = std::max(SampleMax, NewSample);
        }
    }

private:
    double SampleMin;
    double SampleMax;
    double Reset = 0;
    DECLARE_TRACEABLE_MUTEX(Crit);
};

using ProbeRunningStateSharedPtr = std::shared_ptr<ProbeRunningState>;


struct InstructionThunk
{
    virtual void Crank(double SampleInterval) = 0;
    virtual ~InstructionThunk() {};
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

    RunningStateSharedPtr MidiGate;
    RunningStateSharedPtr MidiNote;
    RunningStateSharedPtr MidiVelocity;
    RunningStateSharedPtr MidiPressure;

    void Crank(double SampleInterval, float& OutLeft, float& OutRight);
    MagicTapeSharedPtr FindTape(double WireValue);

    virtual void NoteOn(uint8_t Note, uint8_t Velocity, uint8_t Channel) override;
    virtual void NotePressure(uint8_t Note, uint8_t Pressure, uint8_t Channel) override;
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

    void Connect(PortHandle OutputPort, PortHandle InputPort);
    void Disconnect(PortHandle OutputPort, PortHandle InputPort);
    void ToggleConnection(PortHandle OutputPort, PortHandle InputPort);

    bool CanConnect(TileHandle OutputTile, TileHandle InputTile);
    std::optional<WireHandle> GetImplicitWire(TileHandle OutputTile, TileHandle InputTile);

    std::tuple<double, double> ReadOutputProbe();
    std::tuple<double, double> ReadScopeProbe();
    void SetSpecialInput(TileHandle Tile, double Value);

private:
    void ReplaceConstantOutput(TileHandle Tile, double NewValue);

    // These should only ever be set or read by the audio thread:
    RunningStateSharedPtr MidiGate = std::make_shared<RunningState>(0.0);
    RunningStateSharedPtr MidiNote = std::make_shared<RunningState>(50.0);
    RunningStateSharedPtr MidiVelocity = std::make_shared<RunningState>(0.0);
    RunningStateSharedPtr MidiPressure = std::make_shared<RunningState>(0.0);

    TileHandle LastAssignedTileHandle;
    std::unordered_map<PortHandle, RunningStateSharedPtr> ActiveOutputs;
    std::unordered_map<TileHandle, AtomicRunningStateSharedPtr> SpecialInputs;
    std::unordered_map<TileHandle, MagicTapeSharedPtr> TapeCollection;
    ProbeRunningStateSharedPtr OutputProbe = std::make_shared<ProbeRunningState>();
    ProbeRunningStateSharedPtr ScopeProbe = std::make_shared<ProbeRunningState>();

    void Recompile();
    ScratchSharedPtr Compile();
};
