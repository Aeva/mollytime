
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
    OUT,
    SIN,
    SQR,
    TRI,
    ADD,
    MUL,
    RCP,
    MIN,
    MAX,
    FLOOR,
    CEIL,
    STU,
    UTS,
    MIX,
    PLS,
    FLP,
    RNG,
    GRAD,
    ADSR,
    GATE,
    NOTE,
    VELO,
    PRES,
    MIDI_HZ,
    LOUD_FUDGE,
    BOOP,

#if 0
    // I figure blank tape takes two args, a sample length, and a sample frequency
    // ideally it would reject dynamic inputs, but I don't have any way to determine
    // that.  Alternatively it could take the number of samples, and the sampling
    // rate.  Sampling rate defaults to whatever pipewire wants if unset.
    // Output is a "handle", which probably is just a dictionary key.
    BLANK_TAPE,

    // These guys take a tape handle, and a speed multiplier in addition to the obvious
    // inputs and outputs.  READ outputs a sample.  WRITE outputs nothing.
    TAPE_WRITE,
    TAPE_READ,

    // like TAPE_WRITE and TAPE_READ but they take a clock signal instead of a speed multiplier.
    STEP_WRITE,
    STEP_READ,

    // This means there needs to be a map object to hold the shared pointers for active tapes,
    // and this needs to also be mirrored on the scratch object.
    // Tape handle inputs should only support one connection at a time like the scope instruction.
    // Additionally, tape handle outputs and outputs should reject connectinos with other nodes,
    // which effectively means this introduces a concept of a "type".

    // Tape tiles should also only eval their inputs once at compile time, and their shared
    // pointers should be reassigned like the grad node does when stuff is reconnected to avoid
    // disrupting running patches.  These inputs would be omitted from the compiled patch unless
    // they're referenced by a live branch of the patch.

    // It would be nice to forbid oscillators and other dynamic inputs from being connected to
    // tape tile inputs, but that will require additional metadata.  It might make sense to have
    // this also be port type data, where some type attributes are determined by the tile, and
    // some are propagated via connections.

    // This means that there then needs to be a way to eval subpatches.
#endif
    Count
};


PortHandle MakePortHandle(TileHandle TileId, uint32_t PortNumber);
TileHandle PortHandleTilePart(PortHandle Handle);
uint32_t PortHandlePortIndexPart(PortHandle Handle);


std::string GetDefaultName(OpCode Symbol);


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


struct Scratch : public MidiHandler
{
    std::vector<std::shared_ptr<InstructionThunk>> Program;
    std::vector<RunningStateSharedPtr> Outputs;
    RunningStateSharedPtr ProbeInput = nullptr;
    ProbeRunningStateSharedPtr OutputProbe;
    ProbeRunningStateSharedPtr ScopeProbe;

    RunningStateSharedPtr MidiGate;
    RunningStateSharedPtr MidiNote;
    RunningStateSharedPtr MidiVelocity;
    RunningStateSharedPtr MidiPressure;

    double Eval(double SampleInterval);

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
    RunningStateSharedPtr MidiNote = std::make_shared<RunningState>(255.0);
    RunningStateSharedPtr MidiVelocity = std::make_shared<RunningState>(0.0);
    RunningStateSharedPtr MidiPressure = std::make_shared<RunningState>(0.0);

    TileHandle LastAssignedTileHandle;
    std::unordered_map<PortHandle, RunningStateSharedPtr> ActiveOutputs;
    std::unordered_map<TileHandle, AtomicRunningStateSharedPtr> SpecialInputs;
    ProbeRunningStateSharedPtr OutputProbe = std::make_shared<ProbeRunningState>();
    ProbeRunningStateSharedPtr ScopeProbe = std::make_shared<ProbeRunningState>();

    void Recompile();
    ScratchSharedPtr Compile();
};
