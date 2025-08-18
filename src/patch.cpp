
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
#include <bit>

#include "errors.h"
#include "patch.h"
#include "audio_backend.h"


constinit double Pi = M_PI;
constinit double Tau = M_PI * 2.0;

const double ImprobableMagnitude = 123456789.0;


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
    // NOTE: std::pow not constexpr until C++26, and Clang 2c doesn't have it yet
    double Hz = std::pow(2.0, ((Note - 69.0) / 12.0)) * 440.0;
    return Hz;
}


constexpr double HzToMidiNote(double Hz)
{
    if (Hz <= 0.0)
    {
        return 0.0;
    }
    // NOTE: std::log2 not constexpr until C++26, and Clang 2c doesn't have it yet
    double Note = std::log2(Hz / 440.0) * 12.0 + 69.0;
    return Note;
}


constexpr double AmplitudeToDecibels(double Amplitude)
{
    // https://stackoverflow.com/questions/2445756/how-can-i-calculate-audio-db-level/9812267#9812267
    // NOTE: std::log10 not constexpr until C++26, and Clang 2c doesn't have it yet
    double dB = 20.0 * std::log10(Amplitude);
    return dB;
}


constexpr double DecibelsToAmplitude(double dB)
{
    // NOTE: std::pow not constexpr until C++26, and Clang 2c doesn't have it yet
    double Amplitude = std::pow(10.0, dB / 20.0);
    return Amplitude;
}


constexpr double PerceptualAmplitudeCorrectionByMidiNoteInner(double Note)
{
    // NOTE: Not constexpr until required C++26 features land.  See above notes

    // https://merveilles.town/@cancel/114848900879804284
    const double Peak = AmplitudeToDecibels(1.0);
    const double LowEdge = HzToMidiNote(2000.0) - 6.0;
    const double HighEdge = LowEdge + 6.0;
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
    return DecibelsToAmplitude(dB);
}


constexpr double PerceptualAmplitudeCorrectionByMidiNote(double Note)
{
    // TODO: Make this constinit once the required C++26 features land
    static const double Scale = 1.0 / PerceptualAmplitudeCorrectionByMidiNoteInner(HzToMidiNote(50.0));

    return PerceptualAmplitudeCorrectionByMidiNoteInner(Note) * Scale;
}


constexpr double PerceptualAmplitudeCorrectionByHz(double Hz)
{
    if (Hz <= 0.0)
    {
        return 0.0;
    }
    const double Note = HzToMidiNote(Hz);
    return PerceptualAmplitudeCorrectionByMidiNote(Note);
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


double EncodeSampleHandle(uint32_t SampleHandle)
{
#if 0
    const uint64_t NaN = (0xffful << 51);
    uint64_t Encoded = NaN | uint64_t(SampleHandle);
    return std::bit_cast<double, uint64_t>(Encoded);
#endif
    return std::bit_cast<double, uint64_t>(uint64_t(SampleHandle));
}


uint32_t DecodeSampleHandle(double WireValue)
{
#if 0
    // 0x7ffffffffffff is the safe area for encoding things, and that leaves
    // 19 bits for flags in the future.  We only need the bottom 32 bits right
    // now, however.
    const uint64_t HandlePart = std::numeric_limits<uint32_t>::max();
    uint64_t Encoded = std::bit_cast<double, uint64_t>(WireValue);
    uint32_t SampleHandle = uint32_t(Encoded & HandlePart);
    return SampleHandle;
#endif
    return uint32_t(std::bit_cast<uint64_t, double>(WireValue));
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
        Set(OpCode::SCOPE, "scope", {"out"}, {});
        Set(OpCode::IN, "in", {}, {"in"});
        Set(OpCode::OUT, "out", {"out"}, {});
        Set(OpCode::AUX, "aux", {"out"}, {});
        Set(OpCode::SIN, "sin", {"hz"}, {"amp"}, 1);
        Set(OpCode::SQR, "sqr", {"hz"}, {"amp"}, 1);
        Set(OpCode::TRI, "tri", {"hz"}, {"amp"}, 1);
        Set(OpCode::ADD, "add", {"+"}, {"="});
        Set(OpCode::MUL, "mul", {"*"}, {"="});
        Set(OpCode::RCP, "rcp", {"*"}, {"="});
        Set(OpCode::MIN, "min", {"min"}, {"="});
        Set(OpCode::MAX, "max", {"max"}, {"="});
        Set(OpCode::FLOOR, "floor", {"#"}, {"floor"});
        Set(OpCode::CEIL, "ceil", {"#"}, {"ceil"});
        Set(OpCode::ROUND, "round", {"#"}, {"rounded"});
        Set(OpCode::SIGN, "sign", {"#"}, {"sign"});
        Set(OpCode::ABS, "abs", {"#"}, {"abs"});
        Set(OpCode::FLD, "fold", {"v", "p", "n"}, {"w"});
        Set(OpCode::INV, "invert", {"#"}, {"#"});
        Set(OpCode::STU, "bipolar\nto\nunipolar", {"bi"}, {"uni"});
        Set(OpCode::UTS, "unipolar\nto\nbipolar", {"uni"}, {"bi"});
        Set(OpCode::MIX, "mix", {"L", "R", "balance"}, {"="});
        Set(OpCode::PLS, "pulse", {"clock"}, {"pulse"}, 1);
        Set(OpCode::FLP, "flip\nflop", {"clock"}, {"even", "odd"}, 1);
        Set(OpCode::RNG, "rng", {"clock"}, {"#"}, 1);
        Set(OpCode::GRAD, "grad", {"#", "rate"}, {"#"});
        Set(OpCode::ADSR, "adsr", {"trigger", "a", "d", "s", "r"}, {"#"}, 3);
        Set(OpCode::GATE, "gate", {}, {"gate"});
        Set(OpCode::NOTE, "note", {}, {"note"});
        Set(OpCode::VELO, "velocity", {}, {"velocity"});
        Set(OpCode::PRES, "pressure", {}, {"pressure"});
        Set(OpCode::MIDI_HZ, "midi\nto hz", {"note"}, {"hz"});
        Set(OpCode::LOUD_FUDGE, "loud\nfudge", {"hz"}, {"amp"});
        Set(OpCode::BOOP, "boop", {}, {"gate"});
        Set(OpCode::TAPE_LOOP, "tape\nloop", {"sample", "read\nstart", "length", "reset"}, {"sample"}, 4);
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
        TRACEABLE_NAMED_SCOPE("NAME");
        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
        double Phase = ActivePhase->Get();
        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        ActivePhase->Set(Phase);
        OutAmplitude->Set(std::sin(Phase * Tau));
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
        TRACEABLE_NAMED_SCOPE("SqrThunk");
        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
        double Phase = ActivePhase->Get();
        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        ActivePhase->Set(Phase);
        double Sign = Phase < 0.5 ? 1.0 : -1.0;
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
        TRACEABLE_NAMED_SCOPE("TriThunk");
        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
        double Phase = ActivePhase->Get();
        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        ActivePhase->Set(Phase);
        double Sign = Phase < 0.5 ? 1.0 : -1.0;
        double IntegerPart = 0.0;
        double Alpha = std::modf(Phase * 4.0, &IntegerPart);
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
        TRACEABLE_NAMED_SCOPE("AddThunk");
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
        TRACEABLE_NAMED_SCOPE("MulThunk");
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
        TRACEABLE_NAMED_SCOPE("RcpThunk");
        double Divisor = Combine(CombinerMul, Inputs, 0.0);
        if (Divisor != 0.0)
        {
            Output->Set(1.0 / Divisor);
        }
    }

    virtual ~RcpThunk() {};
};


struct MinThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MinThunk");
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
        TRACEABLE_NAMED_SCOPE("MaxThunk");
        Output->Set(Combine(CombinerMax, Inputs, 0.0));
    }

    virtual ~MaxThunk() {};
};


struct FloorThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("FloorThunk");
        Output->Set(std::floor(Combine(CombinerAdd, Inputs, 0.0)));
    }

    virtual ~FloorThunk() {};
};


struct CeilThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("CeilThunk");
        Output->Set(std::ceil(Combine(CombinerAdd, Inputs, 0.0)));
    }

    virtual ~CeilThunk() {};
};


struct RoundThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("RoundThunk");
        Output->Set(std::round(Combine(CombinerAdd, Inputs, 0.0)));
    }

    virtual ~RoundThunk() {};
};


struct SignThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SignThunk");
        double Sign = Combine(CombinerAdd, Inputs, 0.0) < 0.0 ? -1.0 : 1.0;
        Output->Set(Sign);
    }

    virtual ~SignThunk() {};
};


struct AbsThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("AbsThunk");
        Output->Set(std::abs(Combine(CombinerAdd, Inputs, 0.0)));
    }

    virtual ~AbsThunk() {};
};


struct FoldThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> InValue;
    std::vector<RunningStateSharedPtr> InPositive;
    std::vector<RunningStateSharedPtr> InNegative;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("FoldThunk");
        double Val = Combine(CombinerAdd, InValue, 0.0);
        double Threshold = 1.0;
        if (Val < 0.0 && InNegative.size() > 0)
        {
            Threshold = Combine(CombinerAdd, InNegative, 0.0);
        }
        else
        {
            Threshold = Combine(CombinerAdd, InPositive, 0.0);
        }
        double Sign = Val < 0.0 ? -1.0 : 1.0;
        Threshold = std::min(std::max(std::abs(Threshold), 0.0), 1.0);
        Val = std::abs(Val);
        if (Val > Threshold)
        {
            Val = Threshold - (Val - Threshold);
        }
        Output->Set(Val * Sign);
    }

    virtual ~FoldThunk() {};
};


struct InvertThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("InvertThunk");
        double Value = Combine(CombinerAdd, Inputs, 0.0);
        double Sign = Value < 0.0 ? -1.0 : 1.0;
        Output->Set((1.0 - std::abs(Value)) * Sign);
    }

    virtual ~InvertThunk() {};
};


struct ToUnipolarThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("ToUnipolarThunk");
        Output->Set(Combine(CombinerAdd, Inputs, 0.0) * 0.5 + 0.5);
    }

    virtual ~ToUnipolarThunk() {};
};


struct ToBipolarThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("ToBipolarThunk");
        Output->Set(Combine(CombinerAdd, Inputs, 0.0) * 2.0 - 1.0);
    }

    virtual ~ToBipolarThunk() {};
};


struct MixThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Left;
    std::vector<RunningStateSharedPtr> Right;
    std::vector<RunningStateSharedPtr> Balance;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MixThunk");
        double X = Combine(CombinerAdd, Left, 0.0);
        double Y = Combine(CombinerAdd, Right, 0.0);
        double Alpha = Combine(CombinerAdd, Balance, 0.5);
        Output->Set((1.0 - Alpha) * X + Alpha * Y);
    }

    virtual ~MixThunk() {};
};


struct PulseThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;
    RunningStateSharedPtr Latch = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("PulseThunk");
        if (Inputs.size() > 0)
        {
            double Clock = Combine(CombinerAdd, Inputs, 0.0);
            double State = Latch->Get();

            if (State == 0.0 && Clock >= 1.0)
            {
                Latch->Set(1.0);
                Output->Set(1.0);
            }
            else if (State == 1.0 && Clock <= 0.0)
            {
                Latch->Set(0.0);
                Output->Set(0.0);
            }
            else
            {
                Output->Set(0.0);
            }
        }
    }

    virtual ~PulseThunk() {};
};


struct FlipFlopThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr EvenOutput = nullptr;
    RunningStateSharedPtr OddOutput = nullptr;
    RunningStateSharedPtr LastInput = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("FlipFlopThunk");
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
        TRACEABLE_NAMED_SCOPE("RandomThunk");
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


struct GradualThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> ValueInputs;
    std::vector<RunningStateSharedPtr> RateInputs;
    RunningStateSharedPtr Output = nullptr;
    RunningStateSharedPtr Weight = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("GradualThunk");
        double Value = Combine(CombinerAdd, ValueInputs, 0.0);
        double Rate = Combine(CombinerAdd, RateInputs, 0.0) * SampleInterval;
        double Pos = Output->Get();
        if (Pos == ImprobableMagnitude)
        {
            Pos = Value;
        }
        else
        {
            double Delta = Value - Pos;
            double Sign = (Delta < 0.0) ? -1.0 : 1.0;
            Delta = std::min(std::abs(Delta), std::abs(Rate)) * Sign;
            Pos += Delta;
        }
        Output->Set(Pos);
    }

    virtual ~GradualThunk() {};
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
        TRACEABLE_NAMED_SCOPE("AdsrThunk");
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
        TRACEABLE_NAMED_SCOPE("GateThunk");
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
        TRACEABLE_NAMED_SCOPE("NoteThunk");
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
        TRACEABLE_NAMED_SCOPE("VelocityThunk");
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
        TRACEABLE_NAMED_SCOPE("PressureThunk");
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
        TRACEABLE_NAMED_SCOPE("MidiToHzThunk");
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
        TRACEABLE_NAMED_SCOPE("LoudnessFudgeThunk");
        double Hz = Combine(CombinerAdd, Inputs, 0.0);
        Output->Set(PerceptualAmplitudeCorrectionByHz(Hz));
    }

    virtual ~LoudnessFudgeThunk() {};
};


struct BoopThunk : public InstructionThunk
{
    AtomicRunningStateSharedPtr Input;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("BoopThunk");
        Output->Set(Input->Get());
    }

    virtual ~BoopThunk() {};
};


struct BlankTape : public MagicTape
{
    BlankTape(TileHandle Tile)
        : MagicTape(Tile)
    {
    }

    void Reset(double InSeconds)
    {
        Seconds = InSeconds;
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
            return std::min(std::max(Index, 0ul), Samples.size());
        }
        else
        {
            return 0;
        }
    }

    virtual double ReadAndAdvance(size_t& Index) override
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

    virtual void WriteAndAdvance(size_t& Index, double NewSample) override
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

using BlankTapeSharedPtr = std::shared_ptr<BlankTape>;


struct TapeLoopThunk : public InstructionThunk
{
    std::vector<RunningStateSharedPtr> InSample;
    std::vector<RunningStateSharedPtr> InOffset;
    std::vector<RunningStateSharedPtr> InLength;
    std::vector<RunningStateSharedPtr> InReset;
    RunningStateSharedPtr Output = nullptr;
    RunningStateSharedPtr ReadHead = nullptr;
    RunningStateSharedPtr WriteHead = nullptr;
    RunningStateSharedPtr LastReset = nullptr;
    RunningStateSharedPtr LastOffset = nullptr;
    BlankTapeSharedPtr Tape;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("TapeLoopThunk");

        double Offset = Combine(CombinerAdd, InOffset, 0.0);
        double Seconds = Combine(CombinerAdd, InLength, 0.0);
        size_t ReadIndex = std::bit_cast<size_t, double>(ReadHead->Get());
        size_t WriteIndex = std::bit_cast<size_t, double>(WriteHead->Get());

        auto ResetOffset = [&]()
        {
            if (Tape && Tape->Samples.size() > 0)
            {
                ReadIndex = Tape->FindSample(Offset);
                ReadIndex = (ReadIndex + WriteIndex) % Tape->Samples.size();
            }
            else
            {
                ReadIndex = 0;
            }
            LastOffset->Set(Offset);
        };

        if (Seconds != Tape->Seconds)
        {
            Tape->Reset(Seconds);
            WriteIndex = 0;
            ResetOffset();
        }

        if (Tape->Samples.size() > 0)
        {
            double Reset = Combine(CombinerAdd, InReset, 0.0);
            if (LastReset->Get() <= 0.0 && Reset >= 1.0)
            {
                Tape->Reset(Seconds);
                WriteIndex = 0;
                ResetOffset();
            }
            else if (Offset != LastOffset->Get())
            {
                ResetOffset();
            }
            LastReset->Set(Reset);

            Output->Set(Tape->ReadAndAdvance(ReadIndex));
            ReadHead->Set(std::bit_cast<double, size_t>(ReadIndex));

            double Sample = Combine(CombinerAdd, InSample, 0.0);
            Tape->WriteAndAdvance(WriteIndex, Sample);
            WriteHead->Set(std::bit_cast<double, size_t>(WriteIndex));
        }
    }

    virtual ~TapeLoopThunk() {};
};


Patch::Patch()
    : LastAssignedTileHandle(0)
{
    // This forces the playing patch to clear, which is useful for the editor, but
    // probably not something we want in a future stand-alone runtime.
    Recompile();
}


TileHandle Patch::MakeTile(OpCode Symbol)
{
    TRACEABLE_SCOPE;
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
    if (Symbol == OpCode::GRAD)
    {
        PortHandle Port = MakePortHandle(AllocatedHandle, 0);
        ActiveOutputs[Port]->Set(ImprobableMagnitude);
    }
    else if (Symbol == OpCode::BOOP)
    {
        SpecialInputs[AllocatedHandle] = std::make_shared<AtomicRunningState>(0.0);
    }
    else if (Symbol == OpCode::TAPE_LOOP)
    {
        TapeCollection[AllocatedHandle] = std::make_shared<BlankTape>(AllocatedHandle);
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

    if (Symbol == OpCode::BOOP)
    {
        SpecialInputs.erase(Tile);
    }
    else if (Symbol == OpCode::TAPE_LOOP)
    {
        TapeCollection.erase(Tile);
    }

    TileSymbols.erase(Tile);
    TileConstants.erase(Tile);
    TileNames.erase(Tile);
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
    else
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
    TRACEABLE_SCOPE;
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

    TileHandle ReceiverTile = PortHandleTilePart(InputPort);
    OpCode ReceiverSymbol = GetTileSymbol(ReceiverTile);
    if (ReceiverSymbol == OpCode::SCOPE)
    {
        // Disconnect all other connected scopes before applying the new connection.
        std::vector<WireHandle> ScopeConnections;
        for (const WireHandle& Wire : Wires)
        {
            if (GetTileSymbol(PortHandleTilePart(std::get<1>(Wire))) == OpCode::SCOPE)
            {
                ScopeConnections.push_back(Wire);
            }
        }
        for (const WireHandle& Wire : ScopeConnections)
        {
            Disconnect(std::get<0>(Wire), std::get<1>(Wire));
        }
    }
    else if (ReceiverSymbol == OpCode::GRAD && PortHandlePortIndexPart(InputPort) == 0)
    {
        PortHandle Port = MakePortHandle(ReceiverTile, 0);
        ActiveOutputs[Port] = std::make_shared<RunningState>(ImprobableMagnitude);
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


void Patch::SetSpecialInput(TileHandle Tile, double Value)
{
    TRACEABLE_SCOPE;
    SpecialInputs[Tile]->Set(Value);
}


ScratchSharedPtr Patch::Compile()
{
    TRACEABLE_SCOPE;

    std::set<TileHandle> BreadCrumbs;
    ScratchSharedPtr Program = std::make_shared<Scratch>();
    Program->MidiGate = MidiGate;
    Program->MidiNote = MidiNote;
    Program->MidiVelocity = MidiVelocity;
    Program->MidiPressure = MidiPressure;
    Program->OutputProbe = OutputProbe;
    Program->ScopeProbe = ScopeProbe;

    // Input tiles must be processed first and added to the "BreadCrumbs" set
    // to prevent the Step function below from attempting to process them.
    for (const auto& [Tile, Symbol] : TileSymbols)
    {
        if (Symbol == OpCode::IN)
        {
            BreadCrumbs.insert(Tile);
            Program->Inputs[Tile] = ActiveOutputs.at(MakePortHandle(Tile, 0));
        }
    }

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

        if (Symbol == OpCode::OUT || Symbol == OpCode::AUX || Symbol == OpCode::SCOPE)
        {
            // The output tile does not have any specific behavior, but may emit
            // an implicit add.

            const PortHandle InputHandle = MakePortHandle(Tile, 0);
            std::set<PortHandle> ConnectedOutputs = ByInput.at(InputHandle);

            if (ConnectedOutputs.size() == 0)
            {
                return std::make_shared<RunningState>(0.0);
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
            else if (Symbol == OpCode::FLOOR)
            {
                auto Thunk = std::make_shared<FloorThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::CEIL)
            {
                auto Thunk = std::make_shared<CeilThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::ROUND)
            {
                auto Thunk = std::make_shared<RoundThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::SIGN)
            {
                auto Thunk = std::make_shared<SignThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::ABS)
            {
                auto Thunk = std::make_shared<AbsThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::FLD)
            {
                auto Thunk = std::make_shared<FoldThunk>();
                Thunk->InValue = Inputs[0];
                Thunk->InPositive = Inputs[1];
                Thunk->InNegative = Inputs[2];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::INV)
            {
                auto Thunk = std::make_shared<InvertThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::STU)
            {
                auto Thunk = std::make_shared<ToUnipolarThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::UTS)
            {
                auto Thunk = std::make_shared<ToBipolarThunk>();
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
            else if (Symbol == OpCode::PLS)
            {
                auto Thunk = std::make_shared<PulseThunk>();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Thunk->Latch = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
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
            else if (Symbol == OpCode::GRAD)
            {
                auto Thunk = std::make_shared<GradualThunk>();
                Thunk->ValueInputs = Inputs[0];
                Thunk->RateInputs = Inputs[1];
                Thunk->Output = Outputs[0];
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
            else if (Symbol == OpCode::BOOP)
            {
                auto Thunk = std::make_shared<BoopThunk>();
                Thunk->Input = SpecialInputs[Tile];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::TAPE_LOOP)
            {
                auto Thunk = std::make_shared<TapeLoopThunk>();
                Thunk->InSample = Inputs[0];
                Thunk->InOffset = Inputs[1];
                Thunk->InLength = Inputs[2];
                Thunk->InReset = Inputs[3];
                Thunk->Output = Outputs[0];
                Thunk->ReadHead = ActiveOutputs.at(MakeClosureHandle(Tile, 0));
                Thunk->WriteHead = ActiveOutputs.at(MakeClosureHandle(Tile, 1));
                Thunk->LastReset = ActiveOutputs.at(MakeClosureHandle(Tile, 2));
                Thunk->LastOffset = ActiveOutputs.at(MakeClosureHandle(Tile, 3));
                Thunk->Tape = std::static_pointer_cast<BlankTape>(TapeCollection.at(Tile));
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            return nullptr;
        }

        std::unreachable();
    };

    std::vector<TileHandle> Scopes;
    Program->Outputs.clear();
    for (const auto& [Tile, Symbol] : TileSymbols)
    {
        if (Symbol == OpCode::TAPE_LOOP)
        {
            // TODO: In theory, we probably want to evaluate all of the dependencies for
            // pseudo outputs first, then evaluate the thunks for the pseudo outputs, then
            // the rest of the program graph.
            RunningStateSharedPtr Ignore = Step(Tile);
        }
    }
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
        else if (Symbol == OpCode::AUX)
        {
            RunningStateSharedPtr Output = Step(Tile);
            if (Output != nullptr)
            {
                Program->AuxOutputs[Tile] = Output;
            }
        }
        else if (Symbol == OpCode::SCOPE)
        {
            Scopes.push_back(Tile);
        }
    }
    for (const TileHandle& Tile : Scopes)
    {
        RunningStateSharedPtr Output = Step(Tile);
        if (Output != nullptr)
        {
            // Only one probe may be connected at a time.
            Program->ProbeInput = Output;
            break;
        }
    }

    return Program;
}


void Patch::Recompile()
{
    TRACEABLE_SCOPE;
    ScratchSharedPtr CurrentProgram = Compile();
    AudioStream::Get()->ProgramChange(CurrentProgram);
}


void Scratch::Crank(double SampleInterval, float& OutLeft, float& OutRight)
{
    TRACEABLE_SCOPE;
    {
        TRACEABLE_NAMED_SCOPE("CRANK PHASE");
        for (std::shared_ptr<InstructionThunk>& Thunk : Program)
        {
            Thunk->Crank(SampleInterval);
        }
    }
    {
        if (Outputs.size() == 1)
        {
            OutLeft = float(Outputs[0]->Get());
            OutRight = float(Outputs[0]->Get());
        }
        else if (Outputs.size() > 1)
        {
            OutLeft = float(Outputs[0]->Get());
            OutRight = float(Outputs[1]->Get());
        }
    }
    if (ProbeInput)
    {
        TRACEABLE_NAMED_SCOPE("UPDATE PROBES");
        ScopeProbe->Set(ProbeInput->Get());
        if (Outputs.size() > 0)
        {
            // TODO : per-output probes
            OutputProbe->Set(Outputs[0]->Get());
        }
    }
    else if (Outputs.size() > 0)
    {
        TRACEABLE_NAMED_SCOPE("UPDATE PROBES");
        // TODO : per-output probes
        ScopeProbe->Set(Outputs[0]->Get());
        OutputProbe->Set(Outputs[0]->Get());
    }
}


MagicTapeSharedPtr Scratch::FindTape(double WireValue)
{
    uint32_t SampleHandle = DecodeSampleHandle(WireValue);
    auto Found = Tapes.find(SampleHandle);
    if (Found != Tapes.end())
    {
        return Found->second;
    }
    return nullptr;
}


void Scratch::NoteOn(uint8_t Note, uint8_t Velocity, uint8_t Channel)
{
    TRACEABLE_SCOPE;
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
    TRACEABLE_SCOPE;
    if (double(Note) == MidiNote->Get())
    {
        MidiPressure->Set(double(Pressure) / 127.0);
    }
}
