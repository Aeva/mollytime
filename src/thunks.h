
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

#include <cassert>
#include <cstdint>
#include <tuple>
#include <array>
#include <vector>
#include <map>
#include <atomic>
#include <mutex>
#include <string>
#include <memory>
#include <cmath>
#include <functional>
#ifndef NDEBUG
#include <algorithm>
#endif

#include "perf.h"


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
    KIKI,
    BEND,
    LANE_COUNT,
    LEAD_LANE,
    ADD_LANES,
    MIDI_HZ,
    LOUD_FUDGE,
    BOOP,
    TWEAK,
    TAPE_LOOP,
    MOON,
    Count
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


struct BlankTape : public MagicTape
{
    BlankTape()
    {
    }

    void Reset(double InSeconds)
    {
        Seconds = std::max(0.0, InSeconds);
        size_t SampleCount = size_t(Seconds * double(SampleRate));
        Samples.clear();
        Samples.resize(SampleCount, 0.0);
    }

    virtual size_t FindSample(double Position) override
    {
        if (Seconds > 0.0)
        {
            double Alpha = std::fmod(Position / Seconds, 1.0);
            if (Alpha < 0.0)
            {
                Alpha += 1.0;
            }
            Alpha = std::min(std::max(Alpha, 0.0), 1.0);
            size_t Index = size_t(double(Samples.size() - 1) * Alpha);
            return std::min(std::max(Index, 0zu), Samples.size());
        }
        else
        {
            return 0;
        }
    }

    virtual double ReadAndAdvance(uint64_t& Index) override
    {
        if (Samples.size())
        {
            Index %= Samples.size();
            return Samples[Index++];
        }
        else
        {
            return 0.0;
        }
    }

    virtual void WriteAndAdvance(uint64_t& Index, double NewSample) override
    {
        if (Samples.size() > 0)
        {
            Index %= Samples.size();
            Samples[Index++] = NewSample;
        }
    }

    virtual ~BlankTape()
    {
    };

    // TODO: pull sampling rate from the audio subsystem on Reset
    uint32_t SampleRate = 48000;
    double Seconds = 0.0;
    std::vector<double> Samples;
};


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


enum class InputCombiner
{
    ADD,
    MUL,
    MIN,
    MAX,
    DIRECT,
};


struct InputInfo
{
    std::string_view Name;
    double DefaultValue;
    InputCombiner Combiner;
};


template<int InputCount, int OutputCount, int ClosureCount_>
struct InstructionInfo
{
    OpCode Symbol;
    std::string_view Name;
    std::array<InputInfo, InputCount> InputPorts;
    std::array<std::string_view, OutputCount> OutputNames;
    int ClosureCount = ClosureCount_;
};


struct InstructionRegisters
{
    uint32_t Polyphony = 1;

    inline void Connect(
        std::vector<std::vector<std::ptrdiff_t>>& InInputs,
        std::vector<std::ptrdiff_t>& InOutputs,
        std::vector<std::ptrdiff_t>& InClosures,
        std::vector<double>* RegisterFile)
    {
        Input.reserve(InInputs.size());
        for (std::vector<std::ptrdiff_t>& InputOffsets: InInputs)
        {
            std::vector<double*>& InputRegisters = Input.emplace_back();
            InputRegisters.reserve(InputOffsets.size());
            for (std::ptrdiff_t Offset : InputOffsets)
            {
                InputRegisters.push_back(&(RegisterFile->at(Offset)));
            }
        }

        Output.reserve(InOutputs.size());
        for (std::ptrdiff_t Offset : InOutputs)
        {
            Output.push_back(&(RegisterFile->at(Offset)));
        }

        Closure.reserve(InClosures.size());
        for (std::ptrdiff_t Offset : InClosures)
        {
            Closure.push_back(&(RegisterFile->at(Offset)));
        }
    }

    inline const std::vector<double*>& InputVector(uint32_t InputIndex)
    {
        return Input[InputIndex];
    }

    inline bool InputConnected(uint32_t InputIndex)
    {
        return Input[InputIndex].size() > 0;
    }

    inline double CombineInput(uint32_t InputIndex, double Default = 0.0, CombinerFn Combiner = CombinerAdd)
    {
        std::vector<double*>& InputRegisters = Input[InputIndex];
        double Result = InputRegisters.size() == 0 ? Default : *InputRegisters[0];
        for (int Index = 1; Index < static_cast<int>(InputRegisters.size()); ++Index)
        {
            double* NextValue = InputRegisters[Index];
            Result = Combiner(Result, *NextValue);
        }
        return Result;
    }

    // TODO: name is misleading, this is basically only useful for AddLanesThunk
    inline double CombinePolyphonicInput(uint32_t InputIndex, double Default = 0.0, CombinerFn Combiner = CombinerAdd)
    {
        std::vector<double*>& InputRegisters = Input[InputIndex];
        const uint32_t InputCount = uint32_t(InputRegisters.size());
        if (InputCount == 0)
        {
            return Default;
        }
        else
        {
            double* Cursor = InputRegisters[0];
            double Accumulator = Cursor[0];
            for (uint32_t Lane = 1; Lane < Polyphony; ++Lane)
            {
                Accumulator = Combiner(Accumulator, Cursor[Lane]);
            }
            for (uint32_t Index = 1; Index < InputCount; ++Index)
            {
                Cursor = InputRegisters[Index];
                for (uint32_t Lane = 1; Lane < Polyphony; ++Lane)
                {
                    Accumulator = Combiner(Accumulator, Cursor[Lane]);
                }
            }
            return Accumulator;
        }
    }

    inline double CombineLaneInput(uint32_t InputIndex, uint32_t Lane, double Default = 0.0, CombinerFn Combiner = CombinerAdd)
    {
        std::vector<double*>& InputRegisters = Input[InputIndex];
        const uint32_t InputCount = uint32_t(InputRegisters.size());
        double Result = (InputCount == 0) ? Default : InputRegisters[0][Lane];
        for (uint32_t Index = 1; Index < InputCount; ++Index)
        {
            double* NextValue = InputRegisters[Index];
            Result = Combiner(Result, NextValue[Lane]);
        }
        return Result;
    }

    inline double* InputPtr(uint32_t InputIndex)
    {
        assert(Input[InputIndex].size() == 1);
        return Input[InputIndex][0];
    }

    inline double* OutputPtr(uint32_t OutputIndex)
    {
        return Output[OutputIndex];
    }

    inline double& OutputRef(uint32_t OutputIndex)
    {
        return *Output[OutputIndex];
    }

    inline double& ClosureRef(uint32_t ClosureIndex)
    {
        return *Closure[ClosureIndex];
    }

    inline void ZeroOut()
    {
        for (double* Register : Output)
        {
            *Register = 0.0;
        }
        for (double* Register : Closure)
        {
            *Register = 0.0;
        }
    }

private:
    std::vector<std::vector<double*>> Input;
    std::vector<double*> Output;
    std::vector<double*> Closure;

    // Temporary debug holepunch:
    friend struct Patch;
};


struct InstructionThunk
{
#ifndef NDEBUG
    std::string DebugName;
    void SetDebugName(const std::string& InDebugName)
    {
        DebugName = InDebugName;
        std::replace(DebugName.begin(), DebugName.end(), '\n', ' ');
    }
#else
    void SetDebugName(const std::string& InDebugName)
    {
    }
#endif
    InstructionRegisters Registers;
    bool Retriggerable = false;

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


using BasicCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        std::vector<double>* RegisterFile,
        std::vector<std::vector<std::ptrdiff_t>>& Inputs,
        std::vector<std::ptrdiff_t>& Outputs,
        std::vector<std::ptrdiff_t>& Closures)>;

using WidgetCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        std::vector<double>* RegisterFile,
        std::vector<std::vector<std::ptrdiff_t>>& Inputs,
        std::vector<std::ptrdiff_t>& Outputs,
        std::vector<std::ptrdiff_t>& Closures,
        AtomicRunningStateSharedPtr& SpecialInput)>;

using MidiCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        std::vector<double>* RegisterFile,
        struct Scratch* Program,
        std::vector<std::vector<std::ptrdiff_t>>& Inputs,
        std::vector<std::ptrdiff_t>& Outputs,
        std::vector<std::ptrdiff_t>& Closures)>;

using TapeCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        std::vector<double>* RegisterFile,
        std::vector<MagicTapeUniquePtr>* TapeFile,
        std::ptrdiff_t TapeIndex,
        std::vector<std::vector<std::ptrdiff_t>>& Inputs,
        std::vector<std::ptrdiff_t>& Outputs,
        std::vector<std::ptrdiff_t>& Closures)>;


struct SymbolInfo
{
    std::vector<std::string> DefaultNames;
    std::vector<std::vector<std::string>> InputNames;
    std::vector<std::vector<std::string>> OutputNames;
    std::vector<int> Closures;

    std::map<int, BasicCreateAndConnectFn> BasicCreateAndConnect;
    std::map<int, WidgetCreateAndConnectFn> WidgetCreateAndConnect;
    std::map<int, MidiCreateAndConnectFn> MidiCreateAndConnect;
    std::map<int, TapeCreateAndConnectFn> TapeCreateAndConnect;

    SymbolInfo();

private:
    void Set(OpCode Symbol, std::string Name,
             std::vector<std::string> Inputs, std::vector<std::string> Outputs, int HiddenOutputs = 0)
    {
        DefaultNames[(int)Symbol] = Name;
        InputNames[(int)Symbol] = Inputs;
        OutputNames[(int)Symbol] = Outputs;
        Closures[(int)Symbol] = HiddenOutputs;
    }

    template<typename ThunkT>
    void SetCommon()
    {
        const int ThunkIndex = (int)ThunkT::Info.Symbol;
        DefaultNames[ThunkIndex] = ThunkT::Info.Name;
        InputNames[ThunkIndex].reserve(ThunkT::Info.InputPorts.size());
        for (const InputInfo& InputPort : ThunkT::Info.InputPorts)
        {
            std::string InputName = std::string(InputPort.Name);
            InputNames[ThunkIndex].push_back(InputName);
        }
        OutputNames[ThunkIndex] = std::vector<std::string>(ThunkT::Info.OutputNames.begin(), ThunkT::Info.OutputNames.end());
        Closures[ThunkIndex] = ThunkT::Info.ClosureCount;
    }

    template<typename ThunkT>
    void SetBasic()
    {
        SetCommon<ThunkT>();
        BasicCreateAndConnect[(int)ThunkT::Info.Symbol] = [](
            std::vector<double>* RegisterFile, auto& Inputs, auto& Outputs, auto& Closures)
        {
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
            Thunk->Reset();
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }

    template<typename ThunkT>
    void SetMidi()
    {
        SetCommon<ThunkT>();
        MidiCreateAndConnect[(int)ThunkT::Info.Symbol] = [](
            std::vector<double>* RegisterFile, struct Scratch* Program, auto& Inputs, auto& Outputs, auto& Closures)
        {
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
            Thunk->Reset();
            Thunk->Program = Program;
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }

    template<typename ThunkT>
    void SetWidget()
    {
        SetCommon<ThunkT>();
        WidgetCreateAndConnect[(int)ThunkT::Info.Symbol] = [](
            std::vector<double>* RegisterFile, auto& Inputs, auto& Outputs, auto& Closures, auto& SpecialInput)
        {
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
            Thunk->Reset();
            Thunk->Input = SpecialInput;
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }

    template<typename ThunkT>
    void SetTape()
    {
        SetCommon<ThunkT>();
        TapeCreateAndConnect[(int)ThunkT::Info.Symbol] = [](
            std::vector<double>* RegisterFile, std::vector<MagicTapeUniquePtr>* TapeFile, std::ptrdiff_t TapeIndex, auto& Inputs, auto& Outputs, auto& Closures)
        {
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
            Thunk->Reset();
            Thunk->TapeFile = TapeFile;
            Thunk->TapeIndex = TapeIndex;
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }
};
