
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

#include <algorithm>
#include <cassert>
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
    const double CutCenter = 95.0; // HzToMidiNote(2000.0), approximately
    const double LowEdge = CutCenter - 6.0;
    const double HighEdge = CutCenter + 6.0;
    double dB = Peak;
    double NearestEdge = (Note < CutCenter) ? LowEdge : HighEdge;
    double EdgeDistance = std::abs(Note - NearestEdge);
    if (Note >= LowEdge && Note <= HighEdge)
    {
        double Offset = std::min(EdgeDistance, 1.0);
        dB -= 3.0 * Offset;
    }
    else
    {
        double Offset = EdgeDistance / 12.0;
        dB += 4.5 * Offset;
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
        double Hz = Registers.CombineInput(0, 440.0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.ClosureRef(0);

        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        Amplitude = std::sin(Phase * Tau);
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
        double Hz = Registers.CombineInput(0, 440.0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.ClosureRef(0);

        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        if (Phase < 0.0)
        {
            Phase += 1.0;
        }
        Amplitude = Phase < 0.5 ? 1.0 : -1.0;
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
        double Hz = Registers.CombineInput(0, 440.0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.ClosureRef(0);

        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
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
        Amplitude = Alpha * Sign;
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
        double Hz = Registers.CombineInput(0, 440.0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.ClosureRef(0);

        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        if (Phase < 0.0)
        {
            Phase += 1.0;
        }

        Amplitude = Phase * 2.0 - 1.0;
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
        double Hz = Registers.CombineInput(0, 440.0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.ClosureRef(0);
        double& HighAmp = Registers.ClosureRef(1);
        double& LowAmp = Registers.ClosureRef(2);

        int Before = int(Phase * 4.0);
        Phase += Hz * SampleInterval;
        int After = int(Phase * 4.0);
        if ((Hz >= 0 && Before < After) || (Before > After))
        {
            // TODO: the backwards case is not quite right?
            After %= 4;
            if (After == 1)
            {
                LowAmp = Roll() * 2.0 - 1.0;
            }
            else if (After == 3)
            {
                HighAmp = Roll() * 2.0 - 1.0;
            }
        }
        Phase = std::fmod(Phase, 1.0);
        double Alpha = std::sin(Phase * Tau) * .5 + .5;
        Amplitude = LowAmp * (1.0 - Alpha) + HighAmp * Alpha;
    }

    virtual ~NoiThunk() {};
};


struct PhaseThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::PHASE, "phase", {"hz"}, {"phase"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("PhaseThunk");
        double Hz = Registers.CombineInput(0, 440.0);
        double& Phase = Registers.OutputRef(0);

        Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
        if (Phase < 0.0)
        {
            Phase += 1.0;
        }
    }

    virtual ~PhaseThunk() {};
};


struct SinTrainThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 0> Info = { OpCode::SIN_TRAIN, "sin\ntrain", {"phase"}, {"amp", "phase"} };
    InstructionRegisters<1, 2, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SinTrainThunk");
        double InPhase = Registers.CombineInput(0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.OutputRef(1);

        Phase = std::fmod(InPhase, 1.0);
        Amplitude = std::sin(Phase * Tau);
    }

    virtual ~SinTrainThunk() {};
};


struct SqrTrainThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 0> Info = { OpCode::SQR_TRAIN, "sqr\ntrain", {"phase"}, {"amp", "phase"} };
    InstructionRegisters<1, 2, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SqrTrainThunk");
        double InPhase = Registers.CombineInput(0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.OutputRef(1);

        Phase = std::fmod(InPhase, 1.0);
        if (Phase < 0.0)
        {
            Phase += 1.0;
        }
        Amplitude = Phase < 0.5 ? 1.0 : -1.0;
    }

    virtual ~SqrTrainThunk() {};
};


struct TriTrainThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 0> Info = { OpCode::TRI_TRAIN, "tri\ntrain", {"phase"}, {"amp", "phase"} };
    InstructionRegisters<1, 2, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("TriTrainThunk");
        double InPhase = Registers.CombineInput(0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.OutputRef(1);

        Phase = std::fmod(InPhase, 1.0);
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
        Amplitude = Alpha * Sign;
    }

    virtual ~TriTrainThunk() {};
};


struct SawTrainThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 0> Info = { OpCode::SAW_TRAIN, "saw\ntrain", {"phase"}, {"amp", "phase"} };
    InstructionRegisters<1, 2, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SawTrainThunk");
        double InPhase = Registers.CombineInput(0);
        double& Amplitude = Registers.OutputRef(0);
        double& Phase = Registers.OutputRef(1);

        Phase = std::fmod(InPhase, 1.0);
        if (Phase < 0.0)
        {
            Phase += 1.0;
        }

        Amplitude = Phase * 2.0 - 1.0;
    }

    virtual ~SawTrainThunk() {};
};


struct PhaseWidthModulationThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = { OpCode::PWM, "pwm", {"phase", "bal"}, {"phase"} };
    InstructionRegisters<2, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("PhaseWidthModulationThunk");
        double Phase = Registers.CombineInput(0);
        double Balance = Registers.CombineInput(1);
        double& OutPhase = Registers.OutputRef(0);

        const double Pivot = (Balance * 0.5 + 0.5);
        Phase = std::fmod(Phase, 1.0);

        if (Phase <= Pivot && Pivot > 0.0)
        {
            const double Alpha = Phase / Pivot;
            Phase = Alpha * 0.5;
        }
        else if (Phase >= Pivot && Pivot < 1.0)
        {
            const double Alpha = (Phase - Pivot) / (1.0 - Pivot);
            Phase = 0.5 + Alpha * 0.5;
        }

        OutPhase = Phase;
    }

    virtual ~PhaseWidthModulationThunk() {};
};


struct AddThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::ADD, "add", {"+"}, {"="} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("AddThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerAdd);
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
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerMul);
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
        double Divisor = Registers.CombineInput(0, 0.0, CombinerMul);
        double& Output = Registers.OutputRef(0);

        if (Divisor != 0.0)
        {
            Output = 1.0 / Divisor;
        }
    }

    virtual ~RcpThunk() {};
};


struct PowThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = { OpCode::POW, "pow", {"n", "^"}, {"="} };
    InstructionRegisters<2, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("PowThunk");
        double Base = Registers.CombineInput(0);
        double Exponent = Registers.CombineInput(1, 2.0);
        double& Output = Registers.OutputRef(0);

        double Result = std::pow(Base, Exponent);
        if (std::isfinite(Result))
        {
            Output = Result;
        }
    }

    virtual ~PowThunk() {};
};


struct SignPreservingPowThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = { OpCode::SPOW, "spow", {"n", "^"}, {"="} };
    InstructionRegisters<2, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("SignPreservingPowThunk");
        double Base = Registers.CombineInput(0);
        double Exponent = Registers.CombineInput(1, 2.0);
        double& Output = Registers.OutputRef(0);

        double Sign = Base >= 0.0 ? 1.0 : -1.0;
        double Result = std::pow(std::abs(Base), Exponent);
        if (std::isfinite(Result))
        {
            Output = Result * Sign;
        }
    }

    virtual ~SignPreservingPowThunk() {};
};


struct MinThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MIN, "min", {"min"}, {"="} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MinThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerMin);
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
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerMax);
    }

    virtual ~MaxThunk() {};
};


struct ClampThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = { OpCode::CLAMP, "clamp", {"#", "high", "low"}, {"="} };
    InstructionRegisters<3, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("ClampThunk");
        double Sample = Registers.CombineInput(0);
        double High = Registers.CombineInput(1, 1.0, CombinerMax);
        double Low = Registers.CombineInput(2, -1.0, CombinerMin);
        double& Output = Registers.OutputRef(0);
        Output = std::max(std::min(Sample, High), Low);
    }

    virtual ~ClampThunk() {};
};


struct FloorThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::FLOOR, "floor", {"#"}, {"floor"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("FloorThunk");
        Registers.OutputRef(0) = std::floor(Registers.CombineInput(0));
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
        Registers.OutputRef(0) = std::ceil(Registers.CombineInput(0));
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
        Registers.OutputRef(0) = std::round(Registers.CombineInput(0));
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
        double Number = Registers.CombineInput(0);
        double& Sign = Registers.OutputRef(0);
        Sign = (Number < 0.0) ? -1.0 : 1.0;
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
        Registers.OutputRef(0) = std::abs(Registers.CombineInput(0));
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
        double Sample = Registers.CombineInput(0);
        double& Output = Registers.OutputRef(0);

        double Threshold;
        if (Sample < 0.0 && Registers.InputConnected(2))
        {
            // Use the negative threshold input.
            Threshold = Registers.CombineInput(2);
        }
        else
        {
            // Use the positive threshold input or default to 1.
            Threshold = Registers.CombineInput(1, 1.0);
        }

        double Sign = Sample < 0.0 ? -1.0 : 1.0;
        Threshold = std::min(std::max(std::abs(Threshold), 0.0), 1.0);
        Sample = std::abs(Sample);
        if (Sample > Threshold)
        {
            Sample = Threshold - (Sample - Threshold);
        }
        Output = Sample * Sign;
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
        double Value = Registers.CombineInput(0);
        double& Output = Registers.OutputRef(0);
        double Sign = Value < 0.0 ? -1.0 : 1.0;
        Output = (1.0 - std::abs(Value)) * Sign;
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
        Registers.OutputRef(0) = Registers.CombineInput(0) * 0.5 + 0.5;
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
        Registers.OutputRef(0) = Registers.CombineInput(0) * 2.0 - 1.0;
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
        double Left = Registers.CombineInput(0);
        double Right = Registers.CombineInput(1);
        double Alpha = Registers.CombineInput(2, 0.5);
        double& Output = Registers.OutputRef(0);

        Output = (1.0 - Alpha) * Left + Alpha * Right;
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
        double Sample = Registers.CombineInput(0);
        double Balance = Registers.CombineInput(1);
        double& Left = Registers.OutputRef(0);
        double& Right = Registers.OutputRef(1);

        double Alpha = std::min(std::max(Balance, -1.0), 1.0) * 0.5 + 0.5;
        double InvA = 1.0 - Alpha;
        Left = Sample * InvA;
        Right = Sample * Alpha;
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

        double Clock = Registers.CombineInput(0);
        double& Output = Registers.OutputRef(0);
        double& Latch = Registers.ClosureRef(0);

        if (Registers.InputConnected(0))
        {
            if (Latch == 0.0 && Clock >= 1.0)
            {
                Latch = 1.0;
                Output = 1.0;
            }
            else if (Latch == 1.0 && Clock <= 0.0)
            {
                Latch = 0.0;
                Output = 0.0;
            }
            else
            {
                Output = 0.0;
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
        double Clock = Registers.CombineInput(0);
        double& EvenOutput = Registers.OutputRef(0);
        double& OddOutput = Registers.OutputRef(1);
        double& Latch = Registers.ClosureRef(0);

        const double LastEven = EvenOutput;
        const double LastOdd = OddOutput;
        if (LastEven == LastOdd)
        {
            EvenOutput = 1.0;
            OddOutput = 0.0;
        }

        if (Registers.InputConnected(0))
        {
            if (Latch <= 0.0 && Clock >= 1.0)
            {
                Latch = 1.0;
                if (LastEven > 0.0)
                {
                    EvenOutput = 0.0;
                    OddOutput = 1.0;
                }
                else
                {
                    EvenOutput = 1.0;
                    OddOutput = 0.0;
                }
            }
            else if (Clock <= 0.0)
            {
                Latch = 0.0;
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
        double Clock = Registers.CombineInput(0);
        double& Output = Registers.OutputRef(0);
        double& Latch = Registers.ClosureRef(0);

        if (Registers.InputConnected(0))
        {
            if (Latch <= 0.0 && Clock >= 1.0)
            {
                Output = Roll();
            }
            Latch = Clock;
        }
        else
        {
            Output = Roll();
        }
    }

    virtual ~RandomThunk() {};
};


struct GradualThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 1> Info = { OpCode::GRAD, "grad", {"#", "rate"}, {"#"} };
    InstructionRegisters<2, 1, 1> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("GradualThunk");
        if (Registers.InputConnected(0))
        {
            double Value = Registers.CombineInput(0);
            double Rate = Registers.CombineInput(1);
            double& Pos = Registers.OutputRef(0);
            double& Initialized = Registers.ClosureRef(0);

            Rate *= SampleInterval;

            if (Initialized == 0.0)
            {
                Initialized = 1.0;
                Pos = Value;
            }
            else
            {
                double Delta = Value - Pos;
                double Sign = (Delta < 0.0) ? -1.0 : 1.0;
                Delta = std::min(std::abs(Delta), std::abs(Rate)) * Sign;
                Pos += Delta;
            }
        }
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

        double Sample = Registers.CombineInput(0);
        double Cutoff = Registers.CombineInput(1, 1000.0);
        double Resonance = Registers.CombineInput(2);
        double& Output = Registers.OutputRef(0);
        double& LastCutoff = Registers.ClosureRef(0);
        double& LastResonance = Registers.ClosureRef(1);
        double& Gain = Registers.ClosureRef(2);
        double& FeedbackDamping = Registers.ClosureRef(3);
        // TODO: ShelfGain can be factored out for most specializations of this class
        double& ShelfGain = Registers.ClosureRef(4);
        double& z1_A = Registers.ClosureRef(5); // state variables (z^-1)
        double& z2_A = Registers.ClosureRef(6);

        // TODO: Is this section actually worth the two extra RunningState vars and the branch?
        if (Cutoff != LastCutoff || Resonance != LastResonance)
        {
            LastCutoff = Cutoff;
            LastResonance = Resonance;

            // prewarp the cutoff (for bilinear-transform filters)
            double wd = Cutoff * Tau;
            double T = SampleInterval;
            double wa = (2.0 / T) * std::tan(wd * T / 2.0);

            // To prevent shooting off into infinity, 2 ** 53 is chosen as the maximum value of Q.
            // This is the highest double precision value where integers can be exactly represented,
            // which serves no other purpose than to be an improbably high value.
            double Q = std::min(1.0 / (2.0 * (1.0 - std::min(std::max(Resonance, 0.0), 1.0))), std::pow(2.0, 53.0));

            // Calculate g (gain element of integrator)
            Gain = wa * T / 2.0;

            // Calculate Zavalishin's R from Q (referred to as damping parameter)
            FeedbackDamping = 1.0 / (2.0 * Q);

            // Gain for BandShelving filter
            //ShelfGain = ShelfGain; ????????
        }

        double HP = (Sample - (2.0 * FeedbackDamping + Gain) * z1_A - z2_A) /
            (1.0 + (2.0 * FeedbackDamping * Gain) + Gain * Gain);

        double BP = HP * Gain + z1_A;

        double LP = BP * Gain + z2_A;

        double UBP = 2.0 * FeedbackDamping * BP;

        double BShelf = Sample + UBP * ShelfGain;

        double Notch = Sample - UBP;

        double AP = Sample - (4.0 * FeedbackDamping * BP);

        double Peak = LP - HP;

        z1_A = Gain * HP + BP;
        z2_A = Gain * BP + LP;

        if constexpr (Mode == FilterType::Lowpass)
        {
            Output = LP;
        }
        else if constexpr (Mode == FilterType::Bandpass)
        {
            Output = BP;
        }
        else if constexpr (Mode == FilterType::Highpass)
        {
            Output = HP;
        }
        else if constexpr (Mode == FilterType::UnitGainBandpass)
        {
            Output = UBP;
        }
        else if constexpr (Mode == FilterType::BandShelving)
        {
            Output = BShelf;
        }
        else if constexpr (Mode == FilterType::Notch)
        {
            Output = Notch;
        }
        else if constexpr (Mode == FilterType::Allpass)
        {
            Output = AP;
        }
        else if constexpr (Mode == FilterType::Peak)
        {
            Output = Peak;
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

        double Trigger = Registers.CombineInput(0);
        const double Attack = std::max(Registers.CombineInput(1, 0.1), 0.0);
        const double Decay = std::max(Registers.CombineInput(2, 0.1), 0.0);
        const double Sustain = std::min(std::max(Registers.CombineInput(3, 1.0), 0.0), 1.0);
        const double Release = std::max(Registers.CombineInput(4, 1.0), 0.0);

        double& Amplitude = Registers.OutputRef(0);
        double& LastTrigger = Registers.ClosureRef(0);
        double& Mode = Registers.ClosureRef(1);

        // Use simple rates of change for attack, decay, and release.  These
        // input parameters are the number of seconds it takes to transit one
        // unit of amplitude.  Effectively `abs(Rise / Run)`, where Rise is
        // amplitude, and Run is seconds.
        const double AttackRate = 1.0 / Attack;
        const double DecayRate = 1.0 / Decay;
        const double ReleaseRate = 1.0 / Release;

        auto BeginAttack = [&]()
        {
            Mode = 3.0;
        };

        auto BeginDecayToSustain = [&]()
        {
            Mode = 2.0;
        };

        auto BeginDecayToRelease = [&]()
        {
            Mode = 1.0;
        };

        auto BeginRelease = [&]()
        {
            Mode = 0.0;
        };

        if (Trigger >= 1.0 && LastTrigger <= 0.0)
        {
            BeginAttack();
        }
        else if (Trigger <= 0.0 && LastTrigger >= 1.0)
        {
            // Note this is comparing divisors, so larger Rate values are faster:
            if (Amplitude > Sustain && DecayRate > ReleaseRate)
            {
                // If Amplitude is above the Sustain threshold, and the decay rate is faster
                // than the release rate, use the decay rate until the Amplitude is no longer
                // above the Sustain threshold.
                BeginDecayToRelease();
            }
            else
            {
                // Begin release.  Amplitude is assumed to be below the sustain threshold, or
                // it doesn't matter because the Release's rate is faster than decay's.
                BeginRelease();
            }
        }

        if (Mode == 3.0 && Attack == 0.0)
        {
            // If Attack is zero, then Amplitude rises to one immediately.
            Amplitude = 1.0;
        }
        else if ((Mode == 1.0 || Mode == 2.0) && (Decay == 0.0 || Sustain == 1.0))
        {
            // If Decay is zero, then Amplitude drops to Sustain immediately.
            // If Sustain is one, then Decay is not applied.
            Amplitude = std::min(Amplitude, Sustain);
        }
        else if (Mode == 0.0 && Release == 0.0)
        {
            // If Release is zero, then Amplitude drops to zero immediately.
            // the release transition occurs.
            Amplitude = 0.0;
        }
        else
        {
            double Rate;
            switch (int(Mode))
            {
            case 3:
                Rate = AttackRate;
                break;
            case 2:
            case 1:
                Rate = DecayRate;
                break;
            case 0:
            default:
                Rate = ReleaseRate;
            }

            // Apply the rate of change appropriate for the current phase.
            const double Direction = (Mode == 3.0) ? 1.0 : -1.0;
            Amplitude = std::min(std::max(Rate * SampleInterval * Direction + Amplitude, 0.0), 1.0);
        }

        if (Mode == 3.0 && Amplitude == 1.0)
        {
            BeginDecayToSustain();
        }
        else if (Mode == 2.0 && Amplitude < Sustain)
        {
            Amplitude = Sustain;
        }
        else if (Mode == 1.0 && Amplitude <= Sustain)
        {
            BeginRelease();
        }

        LastTrigger = Trigger;
    }

    virtual ~AdsrThunk() {};
};


struct QuantizeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = { OpCode::QNTZ, "quantize", {"note", "root", "scale"}, {"note"} };
    InstructionRegisters<3, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("QuantizeThunk");

        double Note = Registers.CombineInput(0);
        const double Root = Registers.CombineInput(1, 60.0); // defaults to Middle C
        std::vector<double> Intervals = Registers.InputVector(2);
        double& OutNote = Registers.OutputRef(0);

        if (Registers.InputConnected(0) && Registers.InputConnected(2))
        {
            double Stride = 0.0;
            std::vector<double> Scale;
            Scale.reserve(Intervals.size() + 1);
            Scale.push_back(0.0);
            for (double Interval : Intervals)
            {
                Interval = std::max(Interval, 0.0);
                if (Interval > 0.0)
                {
                    Stride += Interval;
                    Scale.push_back(Stride);
                }
            }

            if (Scale.size() == 0)
            {
                OutNote = Note;
                return;
            }

            double Shift = 0.0;
            Note -= Root;
            while (Note < 0.0)
            {
                --Shift;
                Note += Stride;
            }
            while (Note > Stride)
            {
                ++Shift;
                Note -= Stride;
            }

#if 1
            {
                double Alpha = Note / Stride;
                int IndexLow = 0;
                int IndexHigh = 0;
                double AlphaLow = 0.0;
                double AlphaHigh = 1.0;
                for (int Index = 1; Index < int(Scale.size()); ++Index)
                {
                    IndexHigh = int(Index);
                    AlphaHigh = double(Index) / double(Scale.size() - 1);
                    if (AlphaLow <= Alpha && Alpha <= AlphaHigh)
                    {
                        break;
                    }
                    else
                    {
                        IndexLow = IndexHigh;
                        AlphaLow = AlphaHigh;
                    }
                }
                Alpha = (Alpha - AlphaLow) / (AlphaHigh - AlphaLow);
                Note = (1.0 - Alpha) * Scale[IndexLow] + Alpha * Scale[IndexLow + 1];
            }
#endif

            double Low = 0.0;
            double High = 0.0;
            for (int Index = 0; Index < int(Scale.size()) - 1; ++Index)
            {
                Low = Scale[Index];
                High = Scale[Index + 1];
                if (Low <= Note && Note <= High)
                {
                    break;
                }
            }

            Note = (std::abs(Note - Low) <= std::abs(Note - High)) ? Low : High;
            Note += Shift * Stride + Root;
        }

        OutNote = Note;
    }

    virtual ~QuantizeThunk() {};
};


struct InputSequenceThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 2, 3> Info = { OpCode::ISQN, "input\nseq", {"clock", "input", "restart"}, {"#", "complete"} };
    InstructionRegisters<3, 2, 3> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("InputSequenceThunk");
        double Clock = Registers.CombineInput(0);
        std::vector<double> Sequence = Registers.InputVector(1);
        double Restart = Registers.CombineInput(2);
        double& OutValue = Registers.OutputRef(0);
        double& OutComplete = Registers.OutputRef(1);
        double& LastClock = Registers.ClosureRef(0);
        double& LastRestart = Registers.ClosureRef(1);
        double& Cursor = Registers.ClosureRef(2);

        if (LastRestart <= 0.0 && Restart >= 1.0)
        {
            Cursor = 0.0;
        }
        LastRestart = Restart;

        if (LastClock <= 0.0 && Clock >= 1.0)
        {
            // Modulating the index happens at the start of this thunk, because the patch may
            // have been modified between calls, which could result in the sequence changing
            // length.
            const int Period = Sequence.size();
            int Index = int(Cursor) % Period;
            OutValue = Sequence[Index];

            // We trigger the "complete" pulse on the beginning of the last sample in the sequence.
            // Patches that use this signal to switch between sequences will want to add an extra
            // step to each sequence using this signal.  Generally this extra note will never be
            // heard if this pulse triggers a flip flop to switch to another sequence, because even
            // if you pause the clock on this sequence, it'll usually pulse again when you switch
            // back to this sequence.  In other words, the way of constructing a patch that chains
            // sequences that was most obvious to me always skips the last note in each sequence.
            // So an 8-4-4 repeating sequence chain would have lengths of 9, 5, and 5.
            OutComplete = double(Index == (Period - 1));

            Index = (Index + 1);
            Cursor = double(Index);
        }
        LastClock = Clock;
    }

    virtual ~InputSequenceThunk() {};
};


struct RandomSequenceThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 2, 4> Info = { OpCode::RSQN, "seed\nseq", {"clock", "period", "seed"}, {"#", "complete"} };
    InstructionRegisters<3, 2, 4> Registers;

    std::vector<double> Cache;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("RandomSequenceThunk");
        double Clock = Registers.CombineInput(0);

        double& OutValue = Registers.OutputRef(0);
        double& OutComplete = Registers.OutputRef(1);
        double& LastClock = Registers.ClosureRef(0);
        double& LastPeriod = Registers.ClosureRef(1);
        double& LastSeed = Registers.ClosureRef(2);
        double& Cursor = Registers.ClosureRef(3);

        if (LastClock <= 0.0 && Clock >= 1.0)
        {
            int Period = std::max(int(Registers.CombineInput(1, 4.0)), 1);
            double Seed = Registers.CombineInput(2);

            const bool Reset = Period != int(LastPeriod) || Seed != LastSeed;
            if (Reset || int(Cache.size()) != Period)
            {
                LastSeed = Seed;
                LastPeriod = double(Period);
                if (Reset)
                {
                    Cursor = 0.0;
                }
                Cache.resize(Period);
                std::mt19937 Generator;
                Generator.seed(Seed);
                double Low = Generator.min();
                double Scale = 1.0 / (Generator.max() - Low);
                for (double& Sample : Cache)
                {
                    Sample = (Generator() - Low) * Scale;
                }
            }

            int Index = int(Cursor);
            OutValue = Cache[Index];

            // We trigger the "complete" pulse on the beginning of the last sample in the sequence.
            // Patches that use this signal to switch between sequences will want to add an extra
            // step to each sequence using this signal.  Generally this extra note will never be
            // heard if this pulse triggers a flip flop to switch to another sequence, because even
            // if you pause the clock on this sequence, it'll usually pulse again when you switch
            // back to this sequence.  In other words, the way of constructing a patch that chains
            // sequences that was most obvious to me always skips the last note in each sequence.
            // So an 8-4-4 repeating sequence chain would have lengths of 9, 5, and 5.
            OutComplete = double(Index == (Period - 1));

            Index = (Index + 1) % Period;
            Cursor = double(Index);
        }
        else
        {
            OutComplete = 0.0;
        }

        LastClock = Clock;
    }

    virtual ~RandomSequenceThunk() {};
};


struct GateThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::GATE, "gate", {"channel"}, {"gate"} };
    InstructionRegisters<1, 1, 0> Registers;
    Scratch* Program;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("GateThunk");
        int Channel = int(Registers.CombineInput(0, double(Program->MostRecentChannel)));
        double& Gate = Registers.OutputRef(0);
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            Gate = State.Gate->Get();
        }
    }

    virtual ~GateThunk() {};
};


struct NoteThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::NOTE, "note", {"channel"}, {"note"} };
    InstructionRegisters<1, 1, 0> Registers;
    Scratch* Program;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("NoteThunk");
        int Channel = int(Registers.CombineInput(0, double(Program->MostRecentChannel)));
        double& Note = Registers.OutputRef(0);
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            Note = State.Note->Get();
        }
    }

    virtual ~NoteThunk() {};
};


struct VelocityThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::VELO, "velocity", {"channel"}, {"velocity"} };
    InstructionRegisters<1, 1, 0> Registers;
    Scratch* Program;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("VelocityThunk");
        int Channel = int(Registers.CombineInput(0, double(Program->MostRecentChannel)));
        double& Velocity = Registers.OutputRef(0);
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            Velocity = State.Velocity->Get();
        }
    }

    virtual ~VelocityThunk() {};
};


struct PressureThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::PRES, "pressure", {"channel"}, {"pressure"} };
    InstructionRegisters<1, 1, 0> Registers;
    Scratch* Program;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("PressureThunk");
        int Channel = int(Registers.CombineInput(0, double(Program->MostRecentChannel)));
        double& Pressure = Registers.OutputRef(0);
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            Pressure = State.Pressure->Get();
        }
    }

    virtual ~PressureThunk() {};
};


struct ControlChangeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = { OpCode::CTRL, "control\nchange", {"control", "channel"}, {"value"} };
    InstructionRegisters<2, 1, 0> Registers;
    Scratch* Program;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("ControlChangeThunk");
        int Control = int(Registers.CombineInput(0));
        int Channel = int(Registers.CombineInput(1, double(Program->MostRecentChannel)));
        double& Output = Registers.OutputRef(0);
        if (Channel >= 0 && Channel <= 15)
        {
            MidiChannelState& State = Program->MidiChannels[Channel];
            if (int(State.CtrlParam->Get()) == Control)
            {
                Output = State.CtrlValue->Get();
            }
        }
    }

    virtual ~ControlChangeThunk() {};
};


struct MidiToHzThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MIDI_HZ, "midi\nto hz", {"note"}, {"hz"} };
    InstructionRegisters<1, 1, 0> Registers;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("MidiToHzThunk");
        double Note = Registers.CombineInput(0);
        double& Output = Registers.OutputRef(0);
        Output = MidiNoteToHz(Note);
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
        double Hz = Registers.CombineInput(0);
        double& Output = Registers.OutputRef(0);
        Output = PerceptualAmplitudeCorrectionByHz(Hz);
    }

    virtual ~LoudnessFudgeThunk() {};
};


struct BoopThunk : public InstructionThunk
{
    static constexpr InstructionInfo<0, 1, 0> Info = { OpCode::BOOP, "boop", {}, {"gate"} };
    InstructionRegisters<0, 1, 0> Registers;
    AtomicRunningStateSharedPtr Input;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("BoopThunk");
        Registers.OutputRef(0) = Input->Get();
    }

    virtual ~BoopThunk() {};
};


struct TweakThunk : public InstructionThunk
{
    static constexpr InstructionInfo<0, 1, 0> Info = { OpCode::TWEAK, "tweak", {}, {"value"} };
    InstructionRegisters<0, 1, 0> Registers;
    AtomicRunningStateSharedPtr Input;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("TweakThunk");
        Registers.OutputRef(0) = Input->Get();
    }

    virtual ~TweakThunk() {};
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
    static constexpr InstructionInfo<4, 1, 4> Info = { OpCode::TAPE_LOOP, "tape\nloop", {"sample", "read\nstart", "length", "reset"}, {"sample"} };
    InstructionRegisters<4, 1, 4> Registers;
    BlankTapeSharedPtr Tape;

    virtual void Crank(double SampleInterval) override
    {
        TRACEABLE_NAMED_SCOPE("TapeLoopThunk");
        double Sample = Registers.CombineInput(0);
        double Offset = Registers.CombineInput(1);
        double Seconds = Registers.CombineInput(2);
        double Reset = Registers.CombineInput(3);
        double& Output = Registers.OutputRef(0);
        double& ReadHead = Registers.ClosureRef(0);
        double& WriteHead = Registers.ClosureRef(1);
        double& LastReset = Registers.ClosureRef(2);
        double& LastOffset = Registers.ClosureRef(3);

        uint64_t ReadIndex = std::bit_cast<uint64_t, double>(ReadHead);
        uint64_t WriteIndex = std::bit_cast<uint64_t, double>(WriteHead);

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
            LastOffset = Offset;
        };

        if (Seconds != Tape->Seconds)
        {
            Tape->Reset(Seconds);
            WriteIndex = 0;
            ResetOffset();
        }

        if (Tape->Samples.size() > 0)
        {
            if (LastReset <= 0.0 && Reset >= 1.0)
            {
                Tape->Reset(Seconds);
                WriteIndex = 0;
                ResetOffset();
            }
            else if (Offset != LastOffset)
            {
                ResetOffset();
            }
            LastReset = Reset;

            Output = Tape->ReadAndAdvance(ReadIndex);
            ReadHead = std::bit_cast<double, uint64_t>(ReadIndex);

            Tape->WriteAndAdvance(WriteIndex, Sample);
            WriteHead = std::bit_cast<double, uint64_t>(WriteIndex);
        }
    }

    virtual ~TapeLoopThunk() {};
};


using BasicCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        std::vector<std::vector<RunningStateSharedPtr>>& Inputs,
        std::vector<RunningStateSharedPtr>& Outputs,
        std::vector<RunningStateSharedPtr>& Closures)>;

using WidgetCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        std::vector<std::vector<RunningStateSharedPtr>>& Inputs,
        std::vector<RunningStateSharedPtr>& Outputs,
        std::vector<RunningStateSharedPtr>& Closures,
        AtomicRunningStateSharedPtr& SpecialInput)>;

using MidiCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        std::vector<std::vector<RunningStateSharedPtr>>& Inputs,
        std::vector<RunningStateSharedPtr>& Outputs,
        std::vector<RunningStateSharedPtr>& Closures,
        Scratch* Program)>;

using TapeCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        std::vector<std::vector<RunningStateSharedPtr>>& Inputs,
        std::vector<RunningStateSharedPtr>& Outputs,
        std::vector<RunningStateSharedPtr>& Closures,
        MagicTapeSharedPtr& Tape)>;


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
        Set(OpCode::GO, "go", {"before", "after"}, {"linked"});

        SetBasic<SinThunk>();
        SetBasic<SqrThunk>();
        SetBasic<TriThunk>();
        SetBasic<SawThunk>();
        SetBasic<NoiThunk>();
        SetBasic<PhaseThunk>();
        SetBasic<SinTrainThunk>();
        SetBasic<SqrTrainThunk>();
        SetBasic<TriTrainThunk>();
        SetBasic<SawTrainThunk>();
        SetBasic<PhaseWidthModulationThunk>();
        SetBasic<AddThunk>();
        SetBasic<MulThunk>();
        SetBasic<RcpThunk>();
        SetBasic<PowThunk>();
        SetBasic<SignPreservingPowThunk>();
        SetBasic<MinThunk>();
        SetBasic<MaxThunk>();
        SetBasic<ClampThunk>();
        SetBasic<FloorThunk>();
        SetBasic<CeilThunk>();
        SetBasic<RoundThunk>();
        SetBasic<SignThunk>();
        SetBasic<AbsThunk>();
        SetBasic<FoldThunk>();
        SetBasic<InvertThunk>();
        SetBasic<ToUnipolarThunk>();
        SetBasic<ToBipolarThunk>();
        SetBasic<MixThunk>();
        SetBasic<StereoBalanceThunk>();
        SetBasic<PulseThunk>();
        SetBasic<FlipFlopThunk>();
        SetBasic<RandomThunk>();
        SetBasic<GradualThunk>();
        SetBasic<LowpassThunk>();
        SetBasic<BandpassThunk>();
        SetBasic<HighpassThunk>();
        SetBasic<NotchThunk>();
        SetBasic<AdsrThunk>();
        SetBasic<QuantizeThunk>();
        SetBasic<InputSequenceThunk>();
        SetBasic<RandomSequenceThunk>();
        SetBasic<MidiToHzThunk>();
        SetBasic<LoudnessFudgeThunk>();
        SetBasic<MoonThunk>();

        SetWidget<BoopThunk>();
        SetWidget<TweakThunk>();

        SetMidi<GateThunk>();
        SetMidi<NoteThunk>();
        SetMidi<VelocityThunk>();
        SetMidi<PressureThunk>();
        SetMidi<ControlChangeThunk>();

        SetTape<TapeLoopThunk>();
    }

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
        DefaultNames[(int)ThunkT::Info.Symbol] = ThunkT::Info.Name;
        InputNames[(int)ThunkT::Info.Symbol] = std::vector<std::string>(ThunkT::Info.InputNames.begin(), ThunkT::Info.InputNames.end());
        OutputNames[(int)ThunkT::Info.Symbol] = std::vector<std::string>(ThunkT::Info.OutputNames.begin(), ThunkT::Info.OutputNames.end());
        Closures[(int)ThunkT::Info.Symbol] = ThunkT::Info.ClosureCount;
    }

    template<typename ThunkT>
    void SetBasic()
    {
        SetCommon<ThunkT>();
        BasicCreateAndConnect[(int)ThunkT::Info.Symbol] = [](auto& Inputs, auto& Outputs, auto& Closures)
        {
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->Registers.Connect(Inputs, Outputs, Closures);
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }

    template<typename ThunkT>
    void SetMidi()
    {
        SetCommon<ThunkT>();
        MidiCreateAndConnect[(int)ThunkT::Info.Symbol] = [](auto& Inputs, auto& Outputs, auto& Closures, Scratch* Program)
        {
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->Registers.Connect(Inputs, Outputs, Closures);
            Thunk->Program = Program;
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }

    template<typename ThunkT>
    void SetWidget()
    {
        SetCommon<ThunkT>();
        WidgetCreateAndConnect[(int)ThunkT::Info.Symbol] = [](auto& Inputs, auto& Outputs, auto& Closures, auto& SpecialInput)
        {
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->Registers.Connect(Inputs, Outputs, Closures);
            Thunk->Input = SpecialInput;
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }

    template<typename ThunkT>
    void SetTape()
    {
        SetCommon<ThunkT>();
        TapeCreateAndConnect[(int)ThunkT::Info.Symbol] = [](auto& Inputs, auto& Outputs, auto& Closures, auto& Tape)
        {
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->Registers.Connect(Inputs, Outputs, Closures);
            Thunk->Tape = std::static_pointer_cast<BlankTape>(Tape);
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
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
    static uint64_t NextPatchIdentity = 0;
    Identity = ++NextPatchIdentity;
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
    if (Symbol == OpCode::TPTSVF_LOWPASS || Symbol == OpCode::TPTSVF_BANDPASS || Symbol == OpCode::TPTSVF_HIGHPASS
        || Symbol == OpCode::TPTSVF_NOTCH)
    {
        // Gain and Feedback coefficients init to 1.
        // See https://github.com/michaeldonovan/VAStateVariableFilter/blob/0e1384c62520ffcb3f321bb6ceb940472f5e152f/VAStateVariableFilter.cpp#L20
        ActiveOutputs[MakeClosureHandle(AllocatedHandle, 2)] = std::make_shared<RunningState>(1.0);
        ActiveOutputs[MakeClosureHandle(AllocatedHandle, 3)] = std::make_shared<RunningState>(1.0);
    }
    else if (Symbol == OpCode::BOOP || Symbol == OpCode::TWEAK)
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

    if (Symbol == OpCode::BOOP || Symbol == OpCode::TWEAK)
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


void Patch::SetActiveProbe(TileHandle Tile)
{
    if (ActiveProbeTile != Tile)
    {
        ActiveProbeTile = Tile;
        Recompile();
    }
}


void Patch::ClearActiveProbe()
{
    TileHandle Tile = -1;
    Patch::SetActiveProbe(Tile);
}


void Patch::SetSpecialInput(TileHandle Tile, double Value)
{
    TRACEABLE_SCOPE;
    SpecialInputs[Tile]->Set(Value);
}


void Patch::AddSpecialInput(TileHandle Tile, double Value)
{
    TRACEABLE_SCOPE;
    SpecialInputs[Tile]->Add(Value);
}


void Patch::AddRangeSpecialInput(TileHandle Tile, double Value, double LimitLow, double LimitHigh)
{
    TRACEABLE_SCOPE;
    SpecialInputs[Tile]->Add(Value, LimitLow, LimitHigh);
}


double Patch::GetSpecialInput(TileHandle Tile)
{
    TRACEABLE_SCOPE;
    return SpecialInputs[Tile]->Get();
}


ScratchSharedPtr Patch::Compile()
{
    TRACEABLE_SCOPE;

    std::set<TileHandle> BreadCrumbs;
    std::map<TileHandle, std::vector<PortHandle>> InputSequences;

    struct TilePartial
    {
        TileHandle Tile;
        uint32_t Polyphony = 1;
        bool PatchOutput = false;

        // This is NOT redundant to Patch::ByInput because its elements are ordered,
        // and that ordering is determined at compile time (e.g. by OpCode::GO).
        std::vector<std::vector<PortHandle>> Inputs;

        // TODO: These may be inferred implicitly, and probably don't need to be recorded:
        std::vector<PortHandle> Outputs;
        std::vector<PortHandle> Closures;
    };
    std::vector<TilePartial> FlatGraph;
    FlatGraph.reserve(TileSymbols.size());

    ScratchSharedPtr Program = std::make_shared<Scratch>();
    Program->Identity = Identity;
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

    auto VisitTile = [&](TileHandle Tile) -> TilePartial&
    {
        TilePartial& Partial = FlatGraph.emplace_back();
        Partial.Tile = Tile;
        return Partial;
    };

    std::function<void(TileHandle)> Step = [&](const TileHandle Tile) -> void
    {
        if (!BreadCrumbs.insert(Tile).second)
        {
            return;
        }

        const OpCode Symbol = GetTileSymbol(Tile);
        if (Symbol == OpCode::CONST)
        {
            // Constant tiles return early because they terminate recursion,
            // and because they have no thunk.
            return;
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

        if (Symbol == OpCode::GO)
        {
            // Go tiles only build input lists for other tiles, and do not emit any thunks.
            auto Result = InputSequences.try_emplace(Tile);
            if (Result.second)
            {
                std::vector<PortHandle>& Sequence = (Result.first->second);
                for (int PortIndex = 0; PortIndex < static_cast<int>(InputCount); ++PortIndex)
                {
                    PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
                    for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
                    {
                        TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                        OpCode ConnectedSymbol = GetTileSymbol(ConnectedTile);
                        if (ConnectedSymbol == OpCode::GO)
                        {
                            std::vector<PortHandle>& Append = InputSequences.at(ConnectedTile);
                            Sequence.insert(Sequence.end(), Append.cbegin(), Append.cend());
                        }
                        else
                        {
                            Sequence.push_back(ConnectedOutput);
                        }
                    }
                }
            }
        }
        else if (Symbol == OpCode::OUT || Symbol == OpCode::AUX || Symbol == OpCode::SCOPE)
        {
            TilePartial& Partial = VisitTile(Tile);
            Partial.PatchOutput = true;
            std::vector<PortHandle>& Input0 = Partial.Inputs.emplace_back();

            const PortHandle InputHandle = MakePortHandle(Tile, 0);
            for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
            {
                Input0.push_back(ConnectedOutput);
            }
        }
        else
        {
            TilePartial& Partial = VisitTile(Tile);

            if (Symbol == OpCode::GATE || Symbol == OpCode::NOTE || Symbol == OpCode::VELO ||
                Symbol == OpCode::PRES || Symbol == OpCode::CTRL)
            {
                Partial.Polyphony = MidiPolyphony;
            }

            for (int PortIndex = 0; PortIndex < static_cast<int>(InputCount); ++PortIndex)
            {
                std::vector<PortHandle>& PortInputs = Partial.Inputs.emplace_back();
                PortHandle InputHandle = MakePortHandle(Tile, PortIndex);
                for (PortHandle ConnectedOutput : ByInput.at(InputHandle))
                {
                    TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                    OpCode ConnectedSymbol = GetTileSymbol(ConnectedTile);
                    if (ConnectedSymbol == OpCode::GO)
                    {
                        for (PortHandle ForwardedOutput : InputSequences.at(ConnectedTile))
                        {
                            PortInputs.push_back(ForwardedOutput);
                        }
                    }
                    else
                    {
                        PortInputs.push_back(ConnectedOutput);
                    }
                }
            }

            for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
            {
                Partial.Outputs.push_back(MakePortHandle(Tile, PortIndex));
            }

            for (int ClosureIndex = 0; ClosureIndex < static_cast<int>(ClosureCount); ++ClosureIndex)
            {
                Partial.Closures.push_back(MakeClosureHandle(Tile, ClosureIndex));
            }
        }

        // TODO should this be considered unreachable?
    };

    Program->Outputs.clear();

    // The handles for all the different types of output tiles are staged here so
    // they can be sorted and stepped deterministically.  The outputs are evaluated
    // in ascending order of TileID magnitude.  If there are multiple OpCode::OUT
    // tiles, they will renamed to indicate which semantic output they represent.
    // OpCode::AUX tiles are always renamed in accordance of their tile handles.
    // Everything else retains its default name.
    // OpCode::SCOPE is the only exception: it will only ever be included in the
    // graph when the operator sets it as the active probe tile.
    std::vector<TileHandle> TapeTiles;
    std::vector<TileHandle> OutputTiles;
    std::vector<TileHandle> AuxTiles;
    {
        for (const auto& [Tile, Symbol] : TileSymbols)
        {
            switch (Symbol)
            {
                case OpCode::OUT:
                    OutputTiles.push_back(Tile);
                    break;
                case OpCode::AUX:
                    AuxTiles.push_back(Tile);
                    break;
                case OpCode::TAPE_LOOP:
                    TapeTiles.push_back(Tile);
                    break;
                default:
                    break;
            }
        }
        auto SortAndStep = [Step](std::vector<TileHandle>& OutputSet) -> void
        {
            // You are filled with determinism.
            std::sort(OutputSet.begin(), OutputSet.end());
            for (TileHandle Tile : OutputSet)
            {
                Step(Tile);
            }
        };
        SortAndStep(TapeTiles);
        SortAndStep(OutputTiles);
        SortAndStep(AuxTiles);
    }

    bool ProbeConnected = false;
    if (ActiveProbeTile != TileHandle(-1))
    {
        auto Found = TileSymbols.find(ActiveProbeTile);
        if (Found != TileSymbols.end())
        {
            ProbeConnected = true;
            const OpCode Symbol = Found->second;
            if (Symbol == OpCode::SCOPE)
            {
                Step(ActiveProbeTile);
            }
        }
    }
    if (!ProbeConnected)
    {
        // The scope tile does appear to be disconnected, so force the probe values to zero
        // since they most likely will not be updated when the patch runs.
        OutputProbe->Set(0.0);
        ScopeProbe->Set(0.0);
        Program->ProbeInput = nullptr;
    }

    // From this point on, `FlatGraph` contains entries for everything that will contribute to the compiled patch program.

    for (TilePartial& Partial : FlatGraph)
    {
        const OpCode Symbol = GetTileSymbol(Partial.Tile);

        std::vector<std::vector<RunningStateSharedPtr>> Inputs;
        Inputs.reserve(Partial.Inputs.size());
        for (std::vector<PortHandle>& InputPorts : Partial.Inputs)
        {
            std::vector<RunningStateSharedPtr>& InputRegisters = Inputs.emplace_back();
            InputRegisters.reserve(InputPorts.size());
            for (PortHandle InputPort : InputPorts)
            {
                InputRegisters.push_back(ActiveOutputs.at(InputPort));
            }
        }

        std::vector<RunningStateSharedPtr> Outputs;
        Outputs.reserve(Partial.Outputs.size());
        for (PortHandle OutputPort : Partial.Outputs)
        {
            Outputs.push_back(ActiveOutputs.at(OutputPort));
        }

        std::vector<RunningStateSharedPtr> Closures;
        Closures.reserve(Partial.Closures.size());
        for (PortHandle ClosurePort : Partial.Closures)
        {
            Closures.push_back(ActiveOutputs.at(ClosurePort));
        }

        if (Partial.PatchOutput)
        {
            Outputs = { std::make_shared<RunningState>(0.0) };
            if (Inputs[0].size() == 1)
            {
                Outputs[0] = Inputs[0][0];
            }
            else if (Inputs[0].size() > 1)
            {
                static const BasicCreateAndConnectFn AddCreateAndConnect = SymbolInfoMap.BasicCreateAndConnect.at((int)OpCode::ADD);
                Program->Program.push_back(AddCreateAndConnect(Inputs, Outputs, Closures));
            }

            // Collect the patch output registers.
            if (Symbol == OpCode::OUT)
            {
                Program->Outputs.push_back(Outputs[0]);
            }
            else if (Symbol == OpCode::AUX)
            {
                Program->AuxOutputs[Partial.Tile] = Outputs[0];
            }

            // This tile is also the current active probe.
            if (Partial.Tile == ActiveProbeTile)
            {
                Program->ProbeInput = Outputs[0];
            }
        }
        else
        {
            {
                auto Found = SymbolInfoMap.BasicCreateAndConnect.find((int)Symbol);
                if (Found != SymbolInfoMap.BasicCreateAndConnect.end())
                {
                    Program->Program.push_back(Found->second(Inputs, Outputs, Closures));
                    continue;
                }
            }
            {
                auto Found = SymbolInfoMap.WidgetCreateAndConnect.find((int)Symbol);
                if (Found != SymbolInfoMap.WidgetCreateAndConnect.end())
                {
                    Program->Program.push_back(Found->second(Inputs, Outputs, Closures, SpecialInputs[Partial.Tile]));
                    continue;
                }
            }
            {
                auto Found = SymbolInfoMap.MidiCreateAndConnect.find((int)Symbol);
                if (Found != SymbolInfoMap.MidiCreateAndConnect.end())
                {
                    Program->Program.push_back(Found->second(Inputs, Outputs, Closures, Program.get()));
                    continue;
                }
            }
            {
                auto Found = SymbolInfoMap.TapeCreateAndConnect.find((int)Symbol);
                if (Found != SymbolInfoMap.TapeCreateAndConnect.end())
                {
                    Program->Program.push_back(Found->second(Inputs, Outputs, Closures, TapeCollection.at(Partial.Tile)));
                    continue;
                }
            }
        }
    }

    OutputTileNames.clear();
    if (OutputTiles.size() >= 2)
    {
        int OutputIndex = 0;
        for (const TileHandle& Tile : OutputTiles)
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


void Scratch::Migrate(const Scratch& Old)
{
    TRACEABLE_SCOPE;

    MidiChannels = Old.MidiChannels;
    MostRecentChannel = Old.MostRecentChannel;

    for (auto const& [Handle, NewAllocation] : RegisterMap)
    {
        auto Found = Old.RegisterMap.find(Handle);
        if (Found != Old.RegisterMap.end())
        {
            const RegisterAllocation& OldAllocation = Found->second;
            if (OldAllocation.LaneCount == NewAllocation.LaneCount)
            {
                for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                {
                    RegisterFile[NewAllocation.BaseOffset + Lane] = RegisterFile[OldAllocation.BaseOffset + Lane];
                }
            }
            else if (OldAllocation.LaneCount == 1)
            {
                for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                {
                    RegisterFile[NewAllocation.BaseOffset + Lane] = RegisterFile[OldAllocation.BaseOffset];
                }
            }
        }
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
                MostRecentChannel = Message.Channel;
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
                MostRecentChannel = Message.Channel;
                double& Note = Message.Param1;
                double& Pressure = Message.Param2;
                if (Note == State.Note->Get())
                {
                    State.Pressure->Set(Pressure);
                }
            }
            else if (Message.Type == MidiMessageType::ControlChange)
            {
                MostRecentChannel = Message.Channel;
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
