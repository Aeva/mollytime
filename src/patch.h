
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

#include "thunks.h"
#include "alsa_midi.h"


// TileId
using TileHandle = uint32_t;

// TileId is the upper DWORD, Port Index lower DWORD
using PortHandle = uint64_t;

using WireHandle = std::tuple<PortHandle, PortHandle>;


PortHandle MakePortHandle(TileHandle TileId, uint32_t PortNumber);
TileHandle PortHandleTilePart(PortHandle Handle);
uint32_t PortHandlePortIndexPart(PortHandle Handle);


AudioSample EncodeSampleHandle(uint32_t SampleHandle);
uint32_t DecodeSampleHandle(AudioSample WireValue);


std::string GetDefaultName(OpCode Symbol);

void SetDefaultPolyphony(int Polyphony);


struct RegisterAllocation
{
    std::ptrdiff_t BaseOffset;
    uint32_t LaneCount;
};


struct MidiNoteState
{
    AudioSample Gate = 0.0;
    AudioSample Note = 50.0;
    AudioSample Velocity = 0.0;
    AudioSample Pressure = 0.0;
    AudioSample Channel = -1.0;
    int64_t Age = 0;
};


struct Scratch final : public MidiHandler
{
    uint64_t Identity;
    uint32_t Polyphony;
    uint16_t ChannelMask;

    std::vector<AudioSample> RegisterFile;
    std::map<PortHandle, RegisterAllocation> PersistentRegisters;
    std::vector<MagicTapeUniquePtr> TapeFile;
    std::map<PortHandle, RegisterAllocation> PersistentTapes;

    std::vector<InstructionThunkSharedPtr> Program;
    std::vector<std::ptrdiff_t> Outputs;
    std::map<TileHandle, std::ptrdiff_t> Inputs;
    std::map<TileHandle, std::ptrdiff_t> AuxOutputs;

    bool ProbeConnected = false;
    std::ptrdiff_t ProbeInput;
    ProbeRunningStateSharedPtr OutputProbe;
    ProbeRunningStateSharedPtr ScopeProbe;

    std::vector<MidiNoteState> MidiLanes;
    std::vector<uint32_t> Retriggerables;
    std::array<uint8_t, 16> ChannelPrograms;
    std::array<AudioSample, 16> ChannelPitchBend;
    std::array<std::array<AudioSample, 128>, 16> ChannelControls;
    int32_t MostRecentLane = -1;

    void Migrate(Scratch& Old);

    void Crank(AudioSample SampleInterval, float& OutLeft, float& OutRight);

private:
    void PrintRegisters() const;
};

using ScratchUniquePtr = std::unique_ptr<Scratch>;


struct Patch
{
    std::unordered_map<TileHandle, OpCode> TileSymbols;
    std::unordered_map<TileHandle, AudioSample> TileConstants;
    std::unordered_map<TileHandle, std::string> TileNames;

    std::set<WireHandle> Wires;
    std::unordered_map<PortHandle, std::set<PortHandle>> ByInput;
    std::unordered_map<PortHandle, std::set<PortHandle>> ByOutput;

    std::unordered_map<TileHandle, uint32_t> TileLanes; // Tiles that have live registers, and their current widths.
    std::vector<TileHandle> ErasedTiles; // Used to erase stale registers

    // Used for queries from the UI.
    std::unordered_map<TileHandle, uint32_t> TilePolyphony;

    Patch();

    void SetPolyphony(int NewPolyphony);
    int GetPolyphony();

    TileHandle MakeTile(OpCode Symbol);
    TileHandle MakeTile(AudioSample Constant);
    void EraseTile(TileHandle Tile);

    std::vector<TileHandle> GetAllTileHandles();

    OpCode GetTileSymbol(TileHandle Tile);

    std::string GetTileName(TileHandle Tile);
    void SetTileName(TileHandle Tile, std::string NewName);

    AudioSample GetConstant(TileHandle Tile);
    void SetConstant(TileHandle Tile, AudioSample NewValue);

    std::string GetTileLabel(TileHandle Tile);

    std::vector<PortHandle> GetTileInputPorts(TileHandle Tile);
    std::vector<PortHandle> GetTileOutputPorts(TileHandle Tile);
    std::string GetTileInputName(PortHandle Port);
    std::string GetTileOutputName(PortHandle Port);
    int GetTilePolyphony(TileHandle Tile);

    void Freeze();
    void Unfreeze();
    bool GetFrozen();

    void Connect(PortHandle OutputPort, PortHandle InputPort);
    void Disconnect(PortHandle OutputPort, PortHandle InputPort);
    void ToggleConnection(PortHandle OutputPort, PortHandle InputPort);

    bool CanConnect(TileHandle OutputTile, TileHandle InputTile);
    std::optional<WireHandle> GetImplicitWire(TileHandle OutputTile, TileHandle InputTile);

    std::tuple<AudioSample, AudioSample> ReadOutputProbe();
    std::tuple<AudioSample, AudioSample> ReadScopeProbe();
    void SetActiveProbe(TileHandle Tile);
    void ClearActiveProbe();
    void SetSpecialInput(TileHandle Tile, AudioSample Value);
    void AddSpecialInput(TileHandle Tile, AudioSample Value);
    void AddRangeSpecialInput(TileHandle Tile, AudioSample Value, AudioSample LimitLow, AudioSample LimitHigh);
    AudioSample GetSpecialInput(TileHandle Tile);
    bool GetChannelMask(int Channel);
    void SetChannelMask(int Channel, bool Listen);


private:
    uint64_t Identity;
    uint32_t MidiPolyphony;
    uint16_t ChannelMask = 0xFFFF;

    void ReplaceConstantOutput(TileHandle Tile, AudioSample NewValue);

    // This is a cache of known output tiles for the purpose of labeling
    // audio channels.  This is updated every time the program is compiled.
    std::unordered_map<TileHandle, std::string> OutputTileNames;

    TileHandle LastAssignedTileHandle;
    std::unordered_map<TileHandle, AtomicRunningStateSharedPtr> SpecialInputs;
    ProbeRunningStateSharedPtr OutputProbe = std::make_shared<ProbeRunningState>();
    ProbeRunningStateSharedPtr ScopeProbe = std::make_shared<ProbeRunningState>();
    TileHandle ActiveProbeTile;

    void Recompile();
    ScratchUniquePtr Compile();

    bool Frozen = false;
};
