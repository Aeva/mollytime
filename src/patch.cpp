
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
#include <string_view>
#include <functional>
#include <limits>
#include <numbers>
#include <utility>
#include <cmath>
#include <bit>

#include "errors.h"
#include "patch.h"
#include "moon.h"
#include "audio_backend.h"

constexpr double Tau = std::numbers::pi * 2.0;

const double ImprobableMagnitude = 123456789.0;


static std::random_device RandomDevice;
static std::mt19937 RandomGenerator{ RandomDevice() };
const double RngScale = 1.0 / double(RandomGenerator.max());

double Roll()
{
    // Returns between 0.0 and 1.0, inclusive.
    return double(RandomGenerator()) * RngScale;
}


// NOTE: std::pow not constexpr until C++26, and Clang 2c doesn't have it yet
/* constexpr */ double MidiNoteToHz(double Note)
{
    double Hz = std::pow(2.0, ((Note - 69.0) / 12.0)) * 440.0;
    return Hz;
}


// NOTE: std::log2 not constexpr until C++26, and Clang 2c doesn't have it yet
/* constexpr */ double HzToMidiNote(double Hz)
{
    if (Hz <= 0.0)
    {
        return 0.0;
    }
    double Note = std::log2(Hz / 440.0) * 12.0 + 69.0;
    return Note;
}


// NOTE: std::log10 not constexpr until C++26, and Clang 2c doesn't have it yet
/* constexpr */ double AmplitudeToDecibels(double Amplitude)
{
    // https://stackoverflow.com/questions/2445756/how-can-i-calculate-audio-db-level/9812267#9812267
    double dB = 20.0 * std::log10(Amplitude);
    return dB;
}


// NOTE: std::pow not constexpr until C++26, and Clang 2c doesn't have it yet
/* constexpr */ double DecibelsToAmplitude(double dB)
{
    double Amplitude = std::pow(10.0, dB / 20.0);
    return Amplitude;
}


// NOTE: Not constexpr until required C++26 features land.  See above notes
/* constexpr */ double PerceptualAmplitudeCorrectionByMidiNoteInner(double Note)
{
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

// NOTE: Not constexpr until required C++26 features land.  See above notes
/* constexpr */ double PerceptualAmplitudeCorrectionByMidiNote(double Note)
{
    // TODO: Make this constexpr once the required C++26 features land
    static const double Scale = 1.0 / PerceptualAmplitudeCorrectionByMidiNoteInner(HzToMidiNote(50.0));

    return PerceptualAmplitudeCorrectionByMidiNoteInner(Note) * Scale;
}

// NOTE: Not constexpr until required C++26 features land.  See above notes
/* constexpr */ double PerceptualAmplitudeCorrectionByHz(double Hz)
{
    if (Hz <= 0.0)
    {
        return 0.0;
    }
    const double Note = HzToMidiNote(Hz);
    return std::min(PerceptualAmplitudeCorrectionByMidiNote(Note), 1.0);
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


struct SinThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = { OpCode::SIN, "sin", {"hz"}, {"amp"} };
    InstructionRegisters<1, 1, 1> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SinThunk");
        std::vector<RunningStateSharedPtr>& InFrequencyHz = Registers.Input[0];
        RunningStateSharedPtr& OutAmplitude = Registers.Output[0];
        RunningStateSharedPtr& ActivePhase = Registers.Closure[0];

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
    static constexpr InstructionInfo<1, 1, 1> Info = { OpCode::SQR, "sqr", {"hz"}, {"amp"} };
    InstructionRegisters<1, 1, 1> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SqrThunk");
        std::vector<RunningStateSharedPtr>& InFrequencyHz = Registers.Input[0];
        RunningStateSharedPtr& OutAmplitude = Registers.Output[0];
        RunningStateSharedPtr& ActivePhase = Registers.Closure[0];

        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
        double Phase = ActivePhase->Get();
        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        ActivePhase->Set(Phase);
        if (Phase < 0.0)
        {
            Phase += 1.0;
        }
        double Sign = Phase < 0.5 ? 1.0 : -1.0;
        OutAmplitude->Set(Sign);
    }

    virtual ~SqrThunk() {};
};


struct TriThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = { OpCode::TRI, "tri", {"hz"}, {"amp"} };
    InstructionRegisters<1, 1, 1> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("TriThunk");
        std::vector<RunningStateSharedPtr>& InFrequencyHz = Registers.Input[0];
        RunningStateSharedPtr& OutAmplitude = Registers.Output[0];
        RunningStateSharedPtr& ActivePhase = Registers.Closure[0];

        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
        double Phase = ActivePhase->Get();
        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        ActivePhase->Set(Phase);
        if (Phase < 0.0)
        {
            Phase += 1.0;
        }
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


struct SawThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = { OpCode::SAW, "saw", {"hz"}, {"amp"} };
    InstructionRegisters<1, 1, 1> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SawThunk");
        std::vector<RunningStateSharedPtr>& InFrequencyHz = Registers.Input[0];
        RunningStateSharedPtr& OutAmplitude = Registers.Output[0];
        RunningStateSharedPtr& ActivePhase = Registers.Closure[0];

        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
        double Phase = ActivePhase->Get();
        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        ActivePhase->Set(Phase);
        if (Phase < 0.0)
        {
            Phase += 1.0;
        }
        /*
        double Sign = Phase < 0.5 ? 1.0 : -1.0;
        double IntegerPart = 0.0;
        double Alpha = std::modf(Phase * 4.0, &IntegerPart);
        if (int(IntegerPart) % 2 == 1)
        {
            Alpha = 1.0 - Alpha;
        }
        OutAmplitude->Set(Alpha * Sign);
        */
        OutAmplitude->Set(Phase * 2.0 - 1.0);
    }

    virtual ~SawThunk() {};
};


struct NoiThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 3> Info = { OpCode::NOI, "noise", {"hz"}, {"amp"} };
    InstructionRegisters<1, 1, 3> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("NoiThunk");
        std::vector<RunningStateSharedPtr>& InFrequencyHz = Registers.Input[0];
        RunningStateSharedPtr& OutAmplitude = Registers.Output[0];
        RunningStateSharedPtr& ActivePhase = Registers.Closure[0];
        RunningStateSharedPtr& HighAmp = Registers.Closure[1];
        RunningStateSharedPtr& LowAmp = Registers.Closure[2];

        double Hz = Combine(CombinerAdd, InFrequencyHz, 440.0);
        double Phase = ActivePhase->Get();
        int Before = int(Phase * 4.0);
        Phase += Hz * SampleInterval;
        int After = int(Phase * 4.0);
        if ((Hz >= 0 && Before < After) || (Before > After))
        {
            // TODO: the backwards case is not quite right?
            After %= 4;
            if (After == 1)
            {
                LowAmp->Set(Roll() * 2.0 - 1.0);
            }
            else if (After == 3)
            {
                HighAmp->Set(Roll() * 2.0 - 1.0);
            }
        }
        Phase = std::fmod(Phase, 1.0);
        ActivePhase->Set(Phase);
        double Alpha = std::sin(Phase * Tau) * .5 + .5;
        OutAmplitude->Set(LowAmp->Get() * (1.0 - Alpha) + HighAmp->Get() * Alpha);
    }

    virtual ~NoiThunk() {};
};


struct AddThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::ADD, "add", {"+"}, {"="} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("AddThunk");
        Registers.Output[0]->Set(Combine(CombinerAdd, Registers.Input[0], 0.0));
    }

    virtual ~AddThunk() {};
};


struct MulThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MUL, "mul", {"*"}, {"="} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MulThunk");
        Registers.Output[0]->Set(Combine(CombinerMul, Registers.Input[0], 0.0));
    }

    virtual ~MulThunk() {};
};


struct RcpThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::RCP, "rcp", {"#"}, {"="} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("RcpThunk");
        double Divisor = Combine(CombinerMul, Registers.Input[0], 0.0);
        if (Divisor != 0.0)
        {
            Registers.Output[0]->Set(1.0 / Divisor);
        }
    }

    virtual ~RcpThunk() {};
};


struct MinThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MIN, "min", {"min"}, {"="} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MinThunk");
        Registers.Output[0]->Set(Combine(CombinerMin, Registers.Input[0], 0.0));
    }

    virtual ~MinThunk() {};
};


struct MaxThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MAX, "max", {"max"}, {"="} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MaxThunk");
        Registers.Output[0]->Set(Combine(CombinerMax, Registers.Input[0], 0.0));
    }

    virtual ~MaxThunk() {};
};


struct FloorThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::FLOOR, "floor", {"#"}, {"floor"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("FloorThunk");
        Registers.Output[0]->Set(std::floor(Combine(CombinerAdd, Registers.Input[0], 0.0)));
    }

    virtual ~FloorThunk() {};
};


struct CeilThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::CEIL, "ceil", {"#"}, {"ceil"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("CeilThunk");
        Registers.Output[0]->Set(std::ceil(Combine(CombinerAdd, Registers.Input[0], 0.0)));
    }

    virtual ~CeilThunk() {};
};


struct RoundThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::ROUND, "round", {"#"}, {"rounded"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("RoundThunk");
        Registers.Output[0]->Set(std::round(Combine(CombinerAdd, Registers.Input[0], 0.0)));
    }

    virtual ~RoundThunk() {};
};


struct SignThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::SIGN, "sign", {"#"}, {"sign"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SignThunk");
        double Sign = Combine(CombinerAdd, Registers.Input[0], 0.0) < 0.0 ? -1.0 : 1.0;
        Registers.Output[0]->Set(Sign);
    }

    virtual ~SignThunk() {};
};


struct AbsThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::ABS, "abs", {"#"}, {"abs"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("AbsThunk");
        Registers.Output[0]->Set(std::abs(Combine(CombinerAdd, Registers.Input[0], 0.0)));
    }

    virtual ~AbsThunk() {};
};


struct FoldThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = { OpCode::FLD, "fold", {"v", "p", "n"}, {"w"} };
    InstructionRegisters<3, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("FoldThunk");
        std::vector<RunningStateSharedPtr>& InValue = Registers.Input[0];
        std::vector<RunningStateSharedPtr>& InPositive = Registers.Input[1];
        std::vector<RunningStateSharedPtr>& InNegative = Registers.Input[2];
        RunningStateSharedPtr& Output = Registers.Output[0];

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
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::INV, "invert", {"#"}, {"#"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("InvertThunk");
        double Value = Combine(CombinerAdd, Registers.Input[0], 0.0);
        double Sign = Value < 0.0 ? -1.0 : 1.0;
        Registers.Output[0]->Set((1.0 - std::abs(Value)) * Sign);
    }

    virtual ~InvertThunk() {};
};


struct ToUnipolarThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::STU, "bipolar\nto\nunipolar", {"bi"}, {"uni"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("ToUnipolarThunk");
        Registers.Output[0]->Set(Combine(CombinerAdd, Registers.Input[0], 0.0) * 0.5 + 0.5);
    }

    virtual ~ToUnipolarThunk() {};
};


struct ToBipolarThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::UTS, "unipolar\nto\nbipolar", {"uni"}, {"bi"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("ToBipolarThunk");
        Registers.Output[0]->Set(Combine(CombinerAdd, Registers.Input[0], 0.0) * 2.0 - 1.0);
    }

    virtual ~ToBipolarThunk() {};
};


struct MixThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = { OpCode::MIX, "mix", {"L", "R", "balance"}, {"="} };
    InstructionRegisters<3, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MixThunk");
        std::vector<RunningStateSharedPtr>& Left = Registers.Input[0];
        std::vector<RunningStateSharedPtr>& Right = Registers.Input[1];
        std::vector<RunningStateSharedPtr>& Balance = Registers.Input[2];
        RunningStateSharedPtr& Output = Registers.Output[0];

        double X = Combine(CombinerAdd, Left, 0.0);
        double Y = Combine(CombinerAdd, Right, 0.0);
        double Alpha = Combine(CombinerAdd, Balance, 0.5);
        Output->Set((1.0 - Alpha) * X + Alpha * Y);
    }

    virtual ~MixThunk() {};
};


struct StereoBalanceThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 2, 0> Info = { OpCode::BAL, "stereo\nbalance", {"sample", "balance"}, {"left", "right"} };
    InstructionRegisters<2, 2, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("StereoBalanceThunk");
        std::vector<RunningStateSharedPtr>& Sample = Registers.Input[0];
        std::vector<RunningStateSharedPtr>& Balance = Registers.Input[1];
        RunningStateSharedPtr& Left = Registers.Output[0];
        RunningStateSharedPtr& Right = Registers.Output[1];

        double Value = Combine(CombinerAdd, Sample, 0.0);
        double Alpha = std::min(std::max(Combine(CombinerAdd, Balance, 0.0), -1.0), 1.0) * 0.5 + 0.5;
        double InvA = 1.0 - Alpha;
        Left->Set(Value * InvA);
        Right->Set(Value * Alpha);
    }

    virtual ~StereoBalanceThunk() {};
};


struct PulseThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = { OpCode::PLS, "pulse", {"clock"}, {"pulse"} };
    InstructionRegisters<1, 1, 1> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("PulseThunk");
        std::vector<RunningStateSharedPtr>& Inputs = Registers.Input[0];
        RunningStateSharedPtr& Output = Registers.Output[0];
        RunningStateSharedPtr& Latch = Registers.Closure[0];

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
    static constexpr InstructionInfo<1, 2, 1> Info = { OpCode::FLP, "flip\nflop", {"clock"}, {"even", "odd"} };
    InstructionRegisters<1, 2, 1> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("FlipFlopThunk");
        std::vector<RunningStateSharedPtr>& Inputs = Registers.Input[0];
        RunningStateSharedPtr& EvenOutput = Registers.Output[0];
        RunningStateSharedPtr& OddOutput = Registers.Output[1];
        RunningStateSharedPtr& LastInput = Registers.Closure[0];

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
    static constexpr InstructionInfo<1, 1, 1> Info = { OpCode::RNG, "rng", {"clock"}, {"#"} };
    InstructionRegisters<1, 1, 1> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("RandomThunk");
        std::vector<RunningStateSharedPtr>& Inputs = Registers.Input[0];
        RunningStateSharedPtr& Output = Registers.Output[0];
        RunningStateSharedPtr& LastInput = Registers.Closure[0];

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
    static constexpr InstructionInfo<2, 1, 0> Info = { OpCode::GRAD, "grad", {"#", "rate"}, {"#"} };
    InstructionRegisters<2, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("GradualThunk");
        std::vector<RunningStateSharedPtr>& ValueInputs = Registers.Input[0];
        std::vector<RunningStateSharedPtr>& RateInputs = Registers.Input[1];
        RunningStateSharedPtr& Output = Registers.Output[0];

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


enum class FilterType
{
    Lowpass,
    Bandpass,
    Highpass,
    UnitGainBandpass,
    BandShelving,
    Notch,
    Allpass,
    Peak
};


template <FilterType Mode>
struct TopologyPreservingTransformStateVariableFilterThunk : public InstructionThunk
{
    InstructionRegisters<3, 1, 7> Registers;

    // Adapted from https://github.com/michaeldonovan/VAStateVariableFilter/
    // which in turn was adapted from https://github.com/JordanTHarris/VAStateVariableFilter/
    // Additional useful information: https://mastodon.gamedev.place/@rygorous/115082511872070814

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("TopologyPreservingTransformStateVariableFilterThunk");

        std::vector<RunningStateSharedPtr>& Sample = Registers.Input[0];
        std::vector<RunningStateSharedPtr>& Cutoff = Registers.Input[1];
        std::vector<RunningStateSharedPtr>& Resonance = Registers.Input[2];
        RunningStateSharedPtr& Output = Registers.Output[0];

        RunningStateSharedPtr& LastCutoff = Registers.Closure[0];
        RunningStateSharedPtr& LastResonance = Registers.Closure[1];

        RunningStateSharedPtr& Gain = Registers.Closure[2];
        RunningStateSharedPtr& FeedbackDamping = Registers.Closure[3];
        // TODO: ShelfGain can be factored out for most specializations of this class
        RunningStateSharedPtr& ShelfGain = Registers.Closure[4];
        RunningStateSharedPtr& StateVar_z1_A = Registers.Closure[5]; // state variables (z^-1)
        RunningStateSharedPtr& StateVar_z2_A = Registers.Closure[6];

        double Input = Combine(CombinerAdd, Sample, 0.0);
        double Cut = Combine(CombinerAdd, Cutoff, 1000.0);
        double Res = Combine(CombinerAdd, Resonance, 0.0);
        double LastCut = LastCutoff->Get();
        double LastRes = LastResonance->Get();

        // TODO: Is this section actually worth the two extra RunningState vars and the branch?
        if (Cut != LastCut || Res != LastRes)
        {
            LastCutoff->Set(Cut);
            LastResonance->Set(Res);

            // prewarp the cutoff (for bilinear-transform filters)
            double wd = Cut * Tau;
            double T = SampleInterval;
            double wa = (2.0 / T) * std::tan(wd * T / 2.0);

            // To prevent shooting off into infinity, 2 ** 53 is chosen as the maximum value of Q.
            // This is the highest double precision value where integers can be exactly represented,
            // which serves no other purpose than to be an improbably high value.
            double Q = std::min(1.0 / (2.0 * (1.0 - std::min(std::max(Res, 0.0), 1.0))), std::pow(2.0, 53.0));

            // Calculate g (gain element of integrator)
            Gain->Set(wa * T / 2.0);

            // Calculate Zavalishin's R from Q (referred to as damping parameter)
            FeedbackDamping->Set(1.0 / (2.0 * Q));

            // Gain for BandShelving filter
            //KCoeff = shelfGain;
        }

        double gCoeff = Gain->Get();
        double RCoeff = FeedbackDamping->Get();
        double KCoeff = ShelfGain->Get();
        double z1_A = StateVar_z1_A->Get();
        double z2_A = StateVar_z2_A->Get();

        double HP = (Input - (2.0 * RCoeff + gCoeff) * z1_A - z2_A) /
            (1.0 + (2.0 * RCoeff * gCoeff) + gCoeff * gCoeff);

        double BP = HP * gCoeff + z1_A;

        double LP = BP * gCoeff + z2_A;

        double UBP = 2.0 * RCoeff * BP;

        double BShelf = Input + UBP * KCoeff;

        double Notch = Input - UBP;

        double AP = Input - (4.0 * RCoeff * BP);

        double Peak = LP - HP;

        StateVar_z1_A->Set(gCoeff * HP + BP);
        StateVar_z2_A->Set(gCoeff * BP + LP);

        if constexpr (Mode == FilterType::Lowpass)
        {
            Output->Set(LP);
        }
        else if constexpr (Mode == FilterType::Bandpass)
        {
            Output->Set(BP);
        }
        else if constexpr (Mode == FilterType::Highpass)
        {
            Output->Set(HP);
        }
        else if constexpr (Mode == FilterType::UnitGainBandpass)
        {
            Output->Set(UBP);
        }
        else if constexpr (Mode == FilterType::BandShelving)
        {
            Output->Set(BShelf);
        }
        else if constexpr (Mode == FilterType::Notch)
        {
            Output->Set(Notch);
        }
        else if constexpr (Mode == FilterType::Allpass)
        {
            Output->Set(AP);
        }
        else if constexpr (Mode == FilterType::Peak)
        {
            Output->Set(Peak);
        }
    }

    virtual ~TopologyPreservingTransformStateVariableFilterThunk() {};
};


struct LowpassThunk : public TopologyPreservingTransformStateVariableFilterThunk<FilterType::Lowpass>
{
    static constexpr InstructionInfo<3, 1, 7> Info = { OpCode::TPTSVF_LOWPASS, "low\npass", {"sample", "cutoff", "res"}, {"lowpass"} };
};


struct BandpassThunk : public TopologyPreservingTransformStateVariableFilterThunk<FilterType::Bandpass>
{
    static constexpr InstructionInfo<3, 1, 7> Info = { OpCode::TPTSVF_BANDPASS, "band\npass", {"sample", "cutoff", "res"}, {"bandpass"} };
};


struct HighpassThunk : public TopologyPreservingTransformStateVariableFilterThunk<FilterType::Highpass>
{
    static constexpr InstructionInfo<3, 1, 7> Info = { OpCode::TPTSVF_HIGHPASS, "high\npass", {"sample", "cutoff", "res"}, {"highpass"} };
};


struct NotchThunk : public TopologyPreservingTransformStateVariableFilterThunk<FilterType::Notch>
{
    static constexpr InstructionInfo<3, 1, 7> Info = { OpCode::TPTSVF_NOTCH, "notch", {"sample", "cutoff", "res"}, {"notch"} };
};


struct AdsrThunk : public InstructionThunk
{
    static constexpr InstructionInfo<5, 1, 2> Info = { OpCode::ADSR, "adsr", {"trigger", "a", "d", "s", "r"}, {"#"} };
    InstructionRegisters<5, 1, 2> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("AdsrThunk");
        std::vector<RunningStateSharedPtr>& Trigger = Registers.Input[0];
        std::vector<RunningStateSharedPtr>& AttackTime = Registers.Input[1];
        std::vector<RunningStateSharedPtr>& DecayTime = Registers.Input[2];
        std::vector<RunningStateSharedPtr>& SustainAmount = Registers.Input[3];
        std::vector<RunningStateSharedPtr>& ReleaseTime = Registers.Input[4];
        RunningStateSharedPtr& OutAmplitude = Registers.Output[0];
        RunningStateSharedPtr& LastTrigger = Registers.Closure[0];
        RunningStateSharedPtr& Mode = Registers.Closure[1];

        double Trig = Combine(CombinerAdd, Trigger, 0.0);
        double Previous = LastTrigger->Get();
        LastTrigger->Set(Trig);

        double Attack = std::max(Combine(CombinerAdd, AttackTime, 0.1), 0.0);
        double Decay = std::max(Combine(CombinerAdd, DecayTime, 0.1), 0.0);
        double Sustain = std::min(std::max(Combine(CombinerAdd, SustainAmount, 1.0), 0.0), 1.0);
        double Release = std::max(Combine(CombinerAdd, ReleaseTime, 1.0), 0.0);

        double Amplitude = OutAmplitude->Get();

        if (Trig >= 1.0 && Previous <= 0.0)
        {
            if (Attack > 0.0)
            {
                // Begin attack.
                Mode->Set(2.0);
            }
            else
            {
                // Immediatly decay / sustain.
                Mode->Set(1.0);
                Amplitude = 1.0;
            }
        }
        else if (Trig <= 0.0 && Previous >= 1.0)
        {
            // Begin release.
            Mode->Set(0.0);
        }

        if (Mode->Get() == 2.0)
        {
            if (Attack > 0.0)
            {
                Amplitude = std::min(1.0, Amplitude + (SampleInterval / Attack));
                OutAmplitude->Set(Amplitude);
                if (Amplitude == 1.0)
                {
                    Mode->Set(1.0);
                }
            }
            else
            {
                Mode->Set(1.0);
            }
        }
        else
        {
            if (Amplitude > Sustain && (Mode->Get() == 1.0 || Decay < Attack))
            {
                Amplitude = std::max(Sustain, Amplitude - (SampleInterval / Decay) * (1.0 - Sustain));
            }
            else if (Amplitude > 0.0 && Mode->Get() == 0.0)
            {
                if (Release > 0.0)
                {
                    Amplitude = std::max(0.0, Amplitude - (SampleInterval / Release) * Sustain);
                }
                else
                {
                    Amplitude = 0.0;
                }
            }
            OutAmplitude->Set(Amplitude);
        }
    }

    virtual ~AdsrThunk() {};
};


struct GateThunk : public InstructionThunk
{
    Scratch* Program;
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("GateThunk");
        int Channel = int(Combine(CombinerAdd, Inputs, 0.0));
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            Output->Set(State.Gate->Get());
        }
    }

    virtual ~GateThunk() {};
};


struct NoteThunk : public InstructionThunk
{
    Scratch* Program;
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("NoteThunk");
        int Channel = int(Combine(CombinerAdd, Inputs, 0.0));
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            Output->Set(State.Note->Get());
        }
    }

    virtual ~NoteThunk() {};
};


struct VelocityThunk : public InstructionThunk
{
    Scratch* Program;
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("VelocityThunk");
        int Channel = int(Combine(CombinerAdd, Inputs, 0.0));
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            Output->Set(State.Velocity->Get());
        }
    }

    virtual ~VelocityThunk() {};
};


struct PressureThunk : public InstructionThunk
{
    Scratch* Program;
    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("PressureThunk");
        int Channel = int(Combine(CombinerAdd, Inputs, 0.0));
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            Output->Set(State.Pressure->Get());
        }
    }

    virtual ~PressureThunk() {};
};


struct ControlChangeThunk : public InstructionThunk
{
    Scratch* Program;
    std::vector<RunningStateSharedPtr> Control;
    std::vector<RunningStateSharedPtr> Channel;
    RunningStateSharedPtr Output;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("ControlChangeThunk");

        int EventChannel = int(Combine(CombinerAdd, Channel, 0.0));
        double EventParam = Combine(CombinerAdd, Control, 0.0);
        if (EventChannel >= 0 && EventChannel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[EventChannel];
            if (State.CtrlParam->Get() == EventParam)
            {
                Output->Set(State.CtrlValue->Get());
            }
        }
    }

    virtual ~ControlChangeThunk() {};
};


struct MidiToHzThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MIDI_HZ, "midi\nto hz", {"note"}, {"hz"} };
    InstructionRegisters<1, 1, 0> Registers;

    std::vector<RunningStateSharedPtr> Inputs;
    RunningStateSharedPtr Output = nullptr;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MidiToHzThunk");
        double Note = Combine(CombinerAdd, Registers.Input[0], 0.0);
        Registers.Output[0]->Set(MidiNoteToHz(Note));
    }

    virtual ~MidiToHzThunk() {};
};


struct LoudnessFudgeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::LOUD_FUDGE, "loud\nfudge", {"hz"}, {"amp"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("LoudnessFudgeThunk");
        double Hz = Combine(CombinerAdd, Registers.Input[0], 0.0);
        Registers.Output[0]->Set(PerceptualAmplitudeCorrectionByHz(Hz));
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
        uint64_t ReadIndex = std::bit_cast<uint64_t, double>(ReadHead->Get());
        uint64_t WriteIndex = std::bit_cast<uint64_t, double>(WriteHead->Get());

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
            ReadHead->Set(std::bit_cast<double, uint64_t>(ReadIndex));

            double Sample = Combine(CombinerAdd, InSample, 0.0);
            Tape->WriteAndAdvance(WriteIndex, Sample);
            WriteHead->Set(std::bit_cast<double, uint64_t>(WriteIndex));
        }
    }

    virtual ~TapeLoopThunk() {};
};


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
        Set<SinThunk>();
        Set<SqrThunk>();
        Set<TriThunk>();
        Set<SawThunk>();
        Set<NoiThunk>();
        Set<AddThunk>();
        Set<MulThunk>();
        Set<RcpThunk>();
        Set<MinThunk>();
        Set<MaxThunk>();
        Set<FloorThunk>();
        Set<CeilThunk>();
        Set<RoundThunk>();
        Set<SignThunk>();
        Set<AbsThunk>();
        Set<FoldThunk>();
        Set<InvertThunk>();
        Set<ToUnipolarThunk>();
        Set<ToBipolarThunk>();
        Set<MixThunk>();
        Set<StereoBalanceThunk>();
        Set<PulseThunk>();
        Set<FlipFlopThunk>();
        Set<RandomThunk>();
        Set<GradualThunk>();
        Set<LowpassThunk>();
        Set<BandpassThunk>();
        Set<HighpassThunk>();
        Set<NotchThunk>();
        Set<AdsrThunk>();
        Set(OpCode::GATE, "gate", {"channel"}, {"gate"});
        Set(OpCode::NOTE, "note", {"channel"}, {"note"});
        Set(OpCode::VELO, "velocity", {"channel"}, {"velocity"});
        Set(OpCode::PRES, "pressure", {"channel"}, {"pressure"});
        Set(OpCode::CTRL, "control\nchange", {"control", "channel"}, {"value"});
        Set<MidiToHzThunk>();
        Set<LoudnessFudgeThunk>();
        Set(OpCode::BOOP, "boop", {}, {"gate"});
        Set(OpCode::TAPE_LOOP, "tape\nloop", {"sample", "read\nstart", "length", "reset"}, {"sample"}, 4);
        Set<MoonThunk>();
    }

    void Set(OpCode Symbol, std::string Name,
             std::vector<std::string> Inputs, std::vector<std::string> Outputs, int HiddenOutputs = 0)
    {
        DefaultNames[(int)Symbol] = Name;
        InputNames[(int)Symbol] = Inputs;
        OutputNames[(int)Symbol] = Outputs;
        Closures[(int)Symbol] = HiddenOutputs;
    }

    template<typename ThunkT>
    void Set()
    {
        DefaultNames[(int)ThunkT::Info.Symbol] = ThunkT::Info.Name;
        InputNames[(int)ThunkT::Info.Symbol] = std::vector<std::string>(ThunkT::Info.InputNames.begin(), ThunkT::Info.InputNames.end());
        OutputNames[(int)ThunkT::Info.Symbol] = std::vector<std::string>(ThunkT::Info.OutputNames.begin(), ThunkT::Info.OutputNames.end());
        Closures[(int)ThunkT::Info.Symbol] = ThunkT::Info.ClosureCount;
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
    else if (Symbol == OpCode::TPTSVF_LOWPASS || Symbol == OpCode::TPTSVF_BANDPASS || Symbol == OpCode::TPTSVF_HIGHPASS
        || Symbol == OpCode::TPTSVF_NOTCH)
    {
        // Gain and Feedback coefficients init to 1.
        // See https://github.com/michaeldonovan/VAStateVariableFilter/blob/0e1384c62520ffcb3f321bb6ceb940472f5e152f/VAStateVariableFilter.cpp#L20
        ActiveOutputs[MakeClosureHandle(AllocatedHandle, 2)] = std::make_shared<RunningState>(1.0);
        ActiveOutputs[MakeClosureHandle(AllocatedHandle, 3)] = std::make_shared<RunningState>(1.0);
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


template<typename ThunkT>
static std::shared_ptr<InstructionThunk> CreateAndConnectThunk(
    std::vector<std::vector<RunningStateSharedPtr>>& Inputs,
    std::vector<RunningStateSharedPtr>& Outputs,
    std::vector<RunningStateSharedPtr>& Closures)
{
    auto Thunk = std::make_shared<ThunkT>();
    Thunk->Registers.Connect(Inputs, Outputs, Closures);
    return std::static_pointer_cast<InstructionThunk>(Thunk);
}


ScratchSharedPtr Patch::Compile()
{
    TRACEABLE_SCOPE;

    std::set<TileHandle> BreadCrumbs;
    ScratchSharedPtr Program = std::make_shared<Scratch>();
    Program->MidiChannels = MidiChannels;
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

        const size_t InputCount = SymbolInfoMap.InputNames[(int)Symbol].size();
        const size_t OutputCount = SymbolInfoMap.OutputNames[(int)Symbol].size();
        const size_t ClosureCount = SymbolInfoMap.Closures[(int)Symbol];

        // Recurse first to populate everything sequentally.
        for (int PortIndex = 0; PortIndex < static_cast<int>(InputCount); ++PortIndex)
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
                return ActiveOutputs.at(*ConnectedOutputs.begin());
            }
            else
            {
                std::vector<RunningStateSharedPtr> Input0;
                for (PortHandle ConnectedOutput : ConnectedOutputs)
                {
                    Input0.push_back(ActiveOutputs.at(ConnectedOutput));
                }

                std::vector<std::vector<RunningStateSharedPtr>> Inputs = { Input0 };
                std::vector<RunningStateSharedPtr> Outputs = { std::make_shared<RunningState>(0.0) };
                std::vector<RunningStateSharedPtr> Closures;
                Program->Program.push_back(CreateAndConnectThunk<AddThunk>(Inputs, Outputs, Closures));
                return Outputs[0];
            }
        }
        else
        {
            std::vector<std::vector<RunningStateSharedPtr>> Inputs;
            for (int PortIndex = 0; PortIndex < static_cast<int>(InputCount); ++PortIndex)
            {
                std::vector<RunningStateSharedPtr>& PortInputs = Inputs.emplace_back();
                PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
                for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
                {
                    PortInputs.push_back(ActiveOutputs.at(ConnectedOutput));
                }
            }

            std::vector<RunningStateSharedPtr> Outputs;
            for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
            {
                PortHandle OutputHandle = MakePortHandle(Tile, PortIndex);
                Outputs.push_back(ActiveOutputs.at(OutputHandle));
            }

            std::vector<RunningStateSharedPtr> Closures;
            for (int ClosureIndex = 0; ClosureIndex < static_cast<int>(ClosureCount); ++ClosureIndex)
            {
                Closures.push_back(ActiveOutputs.at(MakeClosureHandle(Tile, ClosureIndex)));
            }

            if (Symbol == OpCode::SIN)
            {
                Program->Program.push_back(CreateAndConnectThunk<SinThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::SQR)
            {
                Program->Program.push_back(CreateAndConnectThunk<SqrThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::TRI)
            {
                Program->Program.push_back(CreateAndConnectThunk<TriThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::SAW)
            {
                Program->Program.push_back(CreateAndConnectThunk<SawThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::NOI)
            {
                Program->Program.push_back(CreateAndConnectThunk<NoiThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::ADD)
            {
                Program->Program.push_back(CreateAndConnectThunk<AddThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::MUL)
            {
                Program->Program.push_back(CreateAndConnectThunk<MulThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::RCP)
            {
                Program->Program.push_back(CreateAndConnectThunk<RcpThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::MIN)
            {
                Program->Program.push_back(CreateAndConnectThunk<MinThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::MAX)
            {
                Program->Program.push_back(CreateAndConnectThunk<MaxThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::FLOOR)
            {
                Program->Program.push_back(CreateAndConnectThunk<FloorThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::CEIL)
            {
                Program->Program.push_back(CreateAndConnectThunk<CeilThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::ROUND)
            {
                Program->Program.push_back(CreateAndConnectThunk<RoundThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::SIGN)
            {
                Program->Program.push_back(CreateAndConnectThunk<SignThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::ABS)
            {
                Program->Program.push_back(CreateAndConnectThunk<AbsThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::FLD)
            {
                Program->Program.push_back(CreateAndConnectThunk<FoldThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::INV)
            {
                Program->Program.push_back(CreateAndConnectThunk<InvertThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::STU)
            {
                Program->Program.push_back(CreateAndConnectThunk<ToUnipolarThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::UTS)
            {
                Program->Program.push_back(CreateAndConnectThunk<ToBipolarThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::MIX)
            {
                Program->Program.push_back(CreateAndConnectThunk<MixThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::BAL)
            {
                Program->Program.push_back(CreateAndConnectThunk<StereoBalanceThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::PLS)
            {
                Program->Program.push_back(CreateAndConnectThunk<PulseThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::FLP)
            {
                Program->Program.push_back(CreateAndConnectThunk<FlipFlopThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::RNG)
            {
                Program->Program.push_back(CreateAndConnectThunk<RandomThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::GRAD)
            {
                Program->Program.push_back(CreateAndConnectThunk<GradualThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::TPTSVF_LOWPASS)
            {
                Program->Program.push_back(CreateAndConnectThunk<LowpassThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::TPTSVF_BANDPASS)
            {
                Program->Program.push_back(CreateAndConnectThunk<BandpassThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::TPTSVF_HIGHPASS)
            {
                Program->Program.push_back(CreateAndConnectThunk<HighpassThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::TPTSVF_NOTCH)
            {
                Program->Program.push_back(CreateAndConnectThunk<NotchThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::ADSR)
            {
                Program->Program.push_back(CreateAndConnectThunk<AdsrThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::GATE)
            {
                auto Thunk = std::make_shared<GateThunk>();
                Thunk->Program = Program.get();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::NOTE)
            {
                auto Thunk = std::make_shared<NoteThunk>();
                Thunk->Program = Program.get();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::VELO)
            {
                auto Thunk = std::make_shared<VelocityThunk>();
                Thunk->Program = Program.get();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::PRES)
            {
                auto Thunk = std::make_shared<PressureThunk>();
                Thunk->Program = Program.get();
                Thunk->Inputs = Inputs[0];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::CTRL)
            {
                auto Thunk = std::make_shared<ControlChangeThunk>();
                Thunk->Program = Program.get();
                Thunk->Control = Inputs[0];
                Thunk->Channel = Inputs[1];
                Thunk->Output = Outputs[0];
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::MIDI_HZ)
            {
                Program->Program.push_back(CreateAndConnectThunk<MidiToHzThunk>(Inputs, Outputs, Closures));
            }
            else if (Symbol == OpCode::LOUD_FUDGE)
            {
                Program->Program.push_back(CreateAndConnectThunk<LoudnessFudgeThunk>(Inputs, Outputs, Closures));
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
                Thunk->ReadHead = Closures[0];
                Thunk->WriteHead = Closures[1];
                Thunk->LastReset = Closures[2];
                Thunk->LastOffset = Closures[3];
                Thunk->Tape = std::static_pointer_cast<BlankTape>(TapeCollection.at(Tile));
                Program->Program.push_back(std::static_pointer_cast<InstructionThunk>(Thunk));
            }
            else if (Symbol == OpCode::MOON)
            {
                Program->Program.push_back(CreateAndConnectThunk<MoonThunk>(Inputs, Outputs, Closures));
            }
            return nullptr;
        }
    };

    std::vector<TileHandle> Scopes;
    Program->Outputs.clear();

    // The graphs for output tiles are staged here so that they can be named and
    // assigned to physical audio outputs deterministically.  The output with the
    // lowest tile ID is assigned to the left channel (or the mono output if there
    // is only one), the next lowest is the right channel, and the rest are
    // ignored (and labeled accordingly).
    std::map<TileHandle, RunningStateSharedPtr> AcceptedOutputs;

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
                AcceptedOutputs[Tile] = Output;
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
    for (const auto& [Tile, Output] : AcceptedOutputs)
    {
        Program->Outputs.push_back(Output);
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

    OutputTileNames.clear();
    if (AcceptedOutputs.size() >= 2)
    {
        int OutputIndex = 0;
        for (const auto& [Tile, Output] : AcceptedOutputs)
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

    return Program;
}


void Patch::Recompile()
{
    TRACEABLE_SCOPE;
    if (!Frozen)
    {
        ScratchSharedPtr CurrentProgram = Compile();
        Audio::GetStream()->ProgramChange(CurrentProgram);
    }
}


void Scratch::Crank(double SampleInterval, float& OutLeft, float& OutRight)
{
    TRACEABLE_SCOPE;
    {
        TRACEABLE_NAMED_SCOPE("MIDI PHASE");

        MidiMessage Message;
        if (PopMidiMessage(Message))
        {
            MidiChannelState& State = MidiChannels[Message.Channel];
            if (Message.Type == MidiMessageType::Note)
            {
                double& Note = Message.Param1;
                double& Velocity = Message.Param2;
                if (Velocity > 0.0)
                {
                    State.Note->Set(Note);
                    State.Gate->Set(1.0);
                    State.Velocity->Set(Velocity);
                }
                else if (Note == State.Note->Get())
                {
                    State.Gate->Set(0.0);
                    State.Velocity->Set(0.0);
                    State.Pressure->Set(0.0);
                }
            }
            else if (Message.Type == MidiMessageType::PolyPress)
            {
                double& Note = Message.Param1;
                double& Pressure = Message.Param2;
                if (Note == State.Note->Get())
                {
                    State.Pressure->Set(Pressure);
                }
            }
            else if (Message.Type == MidiMessageType::ControlChange)
            {
                State.CtrlParam->Set(Message.Param1);
                State.CtrlValue->Set(Message.Param2);
            }
        }
    }
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
