
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
#include "kiki.inl"

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


struct SinThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = { OpCode::SIN, "sin", {"hz"}, {"amp"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SinThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SqrThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("TriThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SawThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("NoiThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PhaseThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SinTrainThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SqrTrainThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("TriTrainThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SawTrainThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PhaseWidthModulationThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("AddThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerAdd);
    }

    virtual ~AddThunk() {};
};


struct MulThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MUL, "mul", {"*"}, {"="} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MulThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerMul);
    }

    virtual ~MulThunk() {};
};


struct RcpThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::RCP, "rcp", {"#"}, {"="} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("RcpThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PowThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SignPreservingPowThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MinThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerMin);
    }

    virtual ~MinThunk() {};
};


struct MaxThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MAX, "max", {"max"}, {"="} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MaxThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerMax);
    }

    virtual ~MaxThunk() {};
};


struct ClampThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = { OpCode::CLAMP, "clamp", {"#", "high", "low"}, {"="} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("ClampThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("FloorThunk");
        Registers.OutputRef(0) = std::floor(Registers.CombineInput(0));
    }

    virtual ~FloorThunk() {};
};


struct CeilThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::CEIL, "ceil", {"#"}, {"ceil"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("CeilThunk");
        Registers.OutputRef(0) = std::ceil(Registers.CombineInput(0));
    }

    virtual ~CeilThunk() {};
};


struct RoundThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::ROUND, "round", {"#"}, {"rounded"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("RoundThunk");
        Registers.OutputRef(0) = std::round(Registers.CombineInput(0));
    }

    virtual ~RoundThunk() {};
};


struct SignThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::SIGN, "sign", {"#"}, {"sign"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SignThunk");
        double Number = Registers.CombineInput(0);
        double& Sign = Registers.OutputRef(0);
        Sign = (Number < 0.0) ? -1.0 : 1.0;
    }

    virtual ~SignThunk() {};
};


struct AbsThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::ABS, "abs", {"#"}, {"abs"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("AbsThunk");
        Registers.OutputRef(0) = std::abs(Registers.CombineInput(0));
    }

    virtual ~AbsThunk() {};
};


struct FoldThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = { OpCode::FLD, "fold", {"v", "p", "n"}, {"w"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("FoldThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("InvertThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("ToUnipolarThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0) * 0.5 + 0.5;
    }

    virtual ~ToUnipolarThunk() {};
};


struct ToBipolarThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::UTS, "unipolar\nto\nbipolar", {"uni"}, {"bi"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("ToBipolarThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0) * 2.0 - 1.0;
    }

    virtual ~ToBipolarThunk() {};
};


struct MixThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = { OpCode::MIX, "mix", {"L", "R", "balance"}, {"="} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MixThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("StereoBalanceThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PulseThunk");

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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("FlipFlopThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("RandomThunk");
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("GradualThunk");
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
    // Adapted from https://github.com/michaeldonovan/VAStateVariableFilter/
    // which in turn was adapted from https://github.com/JordanTHarris/VAStateVariableFilter/
    // Additional useful information: https://mastodon.gamedev.place/@rygorous/115082511872070814

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("TopologyPreservingTransformStateVariableFilterThunk");

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

    virtual void Reset() override
    {
        Registers.ZeroOut();
        // Gain and Feedback coefficients init to 1.
        // See https://github.com/michaeldonovan/VAStateVariableFilter/blob/0e1384c62520ffcb3f321bb6ceb940472f5e152f/VAStateVariableFilter.cpp#L20
        Registers.ClosureRef(2) = 1.0;
        Registers.ClosureRef(3) = 1.0;
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("AdsrThunk");

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

    virtual void Retrigger() override
    {
        // If the adsr is currently held it will retrigger this frame.
        double& LastTrigger = Registers.ClosureRef(0);
        LastTrigger = 0.0;
    }

    virtual ~AdsrThunk() {};
};


struct QuantizeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = { OpCode::QNTZ, "quantize", {"note", "root", "scale"}, {"note"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("QuantizeThunk");

        double Note = Registers.CombineInput(0);
        const double Root = Registers.CombineInput(1, 60.0); // defaults to Middle C
        const std::vector<double*>& Intervals = Registers.InputVector(2);
        double& OutNote = Registers.OutputRef(0);

        if (Registers.InputConnected(0) && Registers.InputConnected(2))
        {
            double Stride = 0.0;
            std::vector<double> Scale;
            Scale.reserve(Intervals.size() + 1);
            Scale.push_back(0.0);
            for (double* Register : Intervals)
            {
                double Interval = std::max(*Register, 0.0);
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

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("InputSequenceThunk");
        double Clock = Registers.CombineInput(0);
        const std::vector<double*>& Sequence = Registers.InputVector(1);
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
            OutValue = *Sequence[Index];

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

    std::vector<double> Cache;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("RandomSequenceThunk");
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
    Scratch* Program;
    uint32_t Lane;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("GateThunk");

        double& Gate = Registers.OutputRef(0);
        MidiNoteState& State = Program->MidiLanes[Lane];
        if (State.Channel == -1.0 || !Registers.InputConnected(0))
        {
            Gate = State.Gate;
        }
        else if (Registers.InputConnected(0))
        {
            for (const double* ChannelMask : Registers.InputVector(0))
            {
                if (int(*ChannelMask) == int(State.Channel))
                {
                    Gate = State.Gate;
                    break;
                }
            }
        }
    }

    virtual ~GateThunk() {};
};


struct NoteThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::NOTE, "note", {"channel"}, {"note"} };
    Scratch* Program;
    uint32_t Lane;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("NoteThunk");

        double& Note = Registers.OutputRef(0);
        MidiNoteState& State = Program->MidiLanes[Lane];
        if (State.Channel == -1.0 || !Registers.InputConnected(0))
        {
            Note = State.Note;
        }
        else if (Registers.InputConnected(0))
        {
            for (const double* ChannelMask : Registers.InputVector(0))
            {
                if (int(*ChannelMask) == int(State.Channel))
                {
                    Note = State.Note;
                    break;
                }
            }
        }
    }

    virtual void Reset() override
    {
        // Default last-played note until a new one is received.  This will be overwritten if the patch is migrated.
        Registers.ZeroOut();
        Registers.OutputRef(0) = 50.0;
    }

    virtual ~NoteThunk() {};
};


struct VelocityThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::VELO, "velocity", {"channel"}, {"velocity"} };
    Scratch* Program;
    uint32_t Lane;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("VelocityThunk");

        double& Velocity = Registers.OutputRef(0);
        MidiNoteState& State = Program->MidiLanes[Lane];
        if (State.Channel == -1.0 || !Registers.InputConnected(0))
        {
            Velocity = State.Velocity;
        }
        else if (Registers.InputConnected(0))
        {
            for (const double* ChannelMask : Registers.InputVector(0))
            {
                if (int(*ChannelMask) == int(State.Channel))
                {
                    Velocity = State.Velocity;
                    break;
                }
            }
        }
    }

    virtual ~VelocityThunk() {};
};


struct PressureThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::PRES, "pressure", {"channel"}, {"pressure"} };
    Scratch* Program;
    uint32_t Lane;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PressureThunk");

        double& Pressure = Registers.OutputRef(0);
        MidiNoteState& State = Program->MidiLanes[Lane];
        if (State.Channel == -1.0 || !Registers.InputConnected(0))
        {
            Pressure = State.Pressure;
        }
        else if (Registers.InputConnected(0))
        {
            for (const double* ChannelMask : Registers.InputVector(0))
            {
                if (int(*ChannelMask) == int(State.Channel))
                {
                    Pressure = State.Pressure;
                    break;
                }
            }
        }
    }

    virtual ~PressureThunk() {};
};


struct ControlChangeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = { OpCode::CTRL, "control\nchange", {"control", "channel"}, {"value"} };
    Scratch* Program;
    uint32_t Lane;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("ControlChangeThunk");

        double Control = Registers.CombineInput(0);
        double& Value = Registers.OutputRef(0);
        MidiNoteState& State = Program->MidiLanes[Lane];
        double Channel = -1.0;
        if (!Registers.InputConnected(1))
        {
            Channel = State.Channel;
        }
        else if (Registers.InputConnected(1))
        {
            for (const double* ChannelMask : Registers.InputVector(1))
            {
                if (int(*ChannelMask) == int(State.Channel))
                {
                    Channel = State.Channel;
                    break;
                }
            }
        }
        if (Channel >= 0.0 && Channel < 16.0 && Control >= 0.0 && Control < 128.0)
        {
            Value = Program->ChannelControls[uint8_t(Channel)][uint8_t(Control)];
        }
    }

    virtual ~ControlChangeThunk() {};
};


struct KikiThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::KIKI, "kiki", {"channel"}, {"kiki"} };
    Scratch* Program;
    uint32_t Lane;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("KikiThunk");

        double& Kiki = Registers.OutputRef(0);
        MidiNoteState& State = Program->MidiLanes[Lane];
        double Channel = -1.0;
        if (!Registers.InputConnected(0))
        {
            Channel = State.Channel;
        }
        else if (Registers.InputConnected(0))
        {
            for (const double* ChannelMask : Registers.InputVector(0))
            {
                if (int(*ChannelMask) == int(State.Channel))
                {
                    Channel = State.Channel;
                    break;
                }
            }
        }
        if (Channel >= 0.0 && Channel < 16)
        {
            uint8_t ProgramNumber = Program->ChannelPrograms[uint8_t(Channel)];
            Kiki = KikiTable[ProgramNumber];
        }
    }

    virtual void Reset() override
    {
        // Default last-played note until a new one is received.  This will be overwritten if the patch is migrated.
        Registers.ZeroOut();
        Registers.OutputRef(0) = KikiTable[0];
    }

    virtual ~KikiThunk() {};
};


struct PitchBendThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::BEND, "bend", {"channel"}, {"bend"} };
    Scratch* Program;
    uint32_t Lane;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PitchBendThunk");

        double& PitchBend = Registers.OutputRef(0);
        MidiNoteState& State = Program->MidiLanes[Lane];
        double Channel = -1.0;
        if (!Registers.InputConnected(0))
        {
            Channel = State.Channel;
        }
        else if (Registers.InputConnected(0))
        {
            for (const double* ChannelMask : Registers.InputVector(0))
            {
                if (int(*ChannelMask) == int(State.Channel))
                {
                    Channel = State.Channel;
                    break;
                }
            }
        }
        if (Channel >= 0.0 && Channel < 16)
        {
            PitchBend = Program->ChannelPitchBend[uint8_t(Channel)];
        }
    }

    virtual ~PitchBendThunk() {};
};


struct LeadLaneThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::LEAD_LANE, "lead\nlane", {"lane\nvalue"}, {"lead\nlane\nvalue"} };
    Scratch* Program;
    uint32_t Lane; // not used, required by SetMidi

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("LeadLaneThunk");

        uint32_t ReadLane = uint32_t(Program->MostRecentLane);
        uint32_t LaneCount = Program->MidiLanes.size();
        if (ReadLane < LaneCount)
        {
            double Value = Registers.CombineStridedInput(0, ReadLane, LaneCount);
            Registers.OutputRef(0) = Value;
        }
    }

    virtual ~LeadLaneThunk() {};
};


struct AddLanesThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::ADD_LANES, "add\nlanes", {"+"}, {"="} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("AddLanesThunk");
        Registers.OutputRef(0) = Registers.CombineInput(0, 0.0, CombinerAdd);
    }

    virtual ~AddLanesThunk() {};
};


struct MidiToHzThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::MIDI_HZ, "midi\nto hz", {"note"}, {"hz"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MidiToHzThunk");
        double Note = Registers.CombineInput(0);
        double& Output = Registers.OutputRef(0);
        Output = MidiNoteToHz(Note);
    }

    virtual ~MidiToHzThunk() {};
};


struct LoudnessFudgeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = { OpCode::LOUD_FUDGE, "loud\nfudge", {"hz"}, {"amp"} };

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("LoudnessFudgeThunk");
        double Hz = Registers.CombineInput(0);
        double& Output = Registers.OutputRef(0);
        Output = PerceptualAmplitudeCorrectionByHz(Hz);
    }

    virtual ~LoudnessFudgeThunk() {};
};


struct BoopThunk : public InstructionThunk
{
    static constexpr InstructionInfo<0, 1, 0> Info = { OpCode::BOOP, "boop", {}, {"gate"} };
    AtomicRunningStateSharedPtr Input;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("BoopThunk");
        Registers.OutputRef(0) = Input->Get();
    }

    virtual ~BoopThunk() {};
};


struct TweakThunk : public InstructionThunk
{
    static constexpr InstructionInfo<0, 1, 0> Info = { OpCode::TWEAK, "tweak", {}, {"value"} };
    AtomicRunningStateSharedPtr Input;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("TweakThunk");
        Registers.OutputRef(0) = Input->Get();
    }

    virtual ~TweakThunk() {};
};


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


struct TapeLoopThunk : public InstructionThunk
{
    static constexpr InstructionInfo<4, 1, 4> Info = { OpCode::TAPE_LOOP, "tape\nloop", {"sample", "read\nstart", "length", "reset"}, {"sample"} };
    Scratch* Program;
    PortHandle Port;
    uint32_t Lane;

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("TapeLoopThunk");
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

        BlankTape* Tape = (BlankTape*)Program->FindTape(Port, Lane);

        if (!Tape)
        {
            return;
        }

        auto ResetOffset = [&]()
        {
            if (Tape->Samples.size() > 0)
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
        ScratchUniquePtr& Program,
        std::vector<std::vector<std::ptrdiff_t>>& Inputs,
        std::vector<std::ptrdiff_t>& Outputs,
        std::vector<std::ptrdiff_t>& Closures)>;

using WidgetCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        ScratchUniquePtr& Program,
        std::vector<std::vector<std::ptrdiff_t>>& Inputs,
        std::vector<std::ptrdiff_t>& Outputs,
        std::vector<std::ptrdiff_t>& Closures,
        AtomicRunningStateSharedPtr& SpecialInput)>;

using MidiCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        ScratchUniquePtr& Program,
        std::vector<std::vector<std::ptrdiff_t>>& Inputs,
        std::vector<std::ptrdiff_t>& Outputs,
        std::vector<std::ptrdiff_t>& Closures,
        uint32_t Lane)>;

using TapeCreateAndConnectFn = std::function<
    std::shared_ptr<InstructionThunk>(
        ScratchUniquePtr& Program,
        std::vector<std::vector<std::ptrdiff_t>>& Inputs,
        std::vector<std::ptrdiff_t>& Outputs,
        std::vector<std::ptrdiff_t>& Closures,
        PortHandle Port,
        uint32_t Lane)>;


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
        SetMidi<KikiThunk>();
        SetMidi<PitchBendThunk>();

        Set(OpCode::LANE_COUNT, "lane\ncount", {}, {"#"});
        SetMidi<LeadLaneThunk>();
        SetBasic<AddLanesThunk>();

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
        BasicCreateAndConnect[(int)ThunkT::Info.Symbol] = [](ScratchUniquePtr& Program, auto& Inputs, auto& Outputs, auto& Closures)
        {
            std::vector<double>* RegisterFile = &(Program->RegisterFile);
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->DebugSymbol = ThunkT::Info.Symbol;
            Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
            Thunk->Reset();
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }

    template<typename ThunkT>
    void SetMidi()
    {
        SetCommon<ThunkT>();
        MidiCreateAndConnect[(int)ThunkT::Info.Symbol] = [](ScratchUniquePtr& Program, auto& Inputs, auto& Outputs, auto& Closures, uint32_t Lane)
        {
            std::vector<double>* RegisterFile = &(Program->RegisterFile);
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->DebugSymbol = ThunkT::Info.Symbol;
            Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
            Thunk->Reset();
            Thunk->Program = Program.get();
            Thunk->Lane = Lane;
            return std::static_pointer_cast<InstructionThunk>(Thunk);
        };
    }

    template<typename ThunkT>
    void SetWidget()
    {
        SetCommon<ThunkT>();
        WidgetCreateAndConnect[(int)ThunkT::Info.Symbol] = [](ScratchUniquePtr& Program, auto& Inputs, auto& Outputs, auto& Closures, auto& SpecialInput)
        {
            std::vector<double>* RegisterFile = &(Program->RegisterFile);
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->DebugSymbol = ThunkT::Info.Symbol;
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
            ScratchUniquePtr& Program, auto& Inputs, auto& Outputs, auto& Closures, PortHandle Port, uint32_t Lane)
        {
            std::vector<double>* RegisterFile = &(Program->RegisterFile);
            auto Thunk = std::make_shared<ThunkT>();
            Thunk->DebugSymbol = ThunkT::Info.Symbol;
            Thunk->Registers.Connect(Inputs, Outputs, Closures, RegisterFile);
            Thunk->Reset();
            Thunk->Program = Program.get();
            Thunk->Port = Port;
            Thunk->Lane = Lane;
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


static bool IsOutputSymbol(const OpCode Symbol)
{
    return (Symbol == OpCode::OUT || Symbol == OpCode::AUX || Symbol == OpCode::SCOPE);
};


static bool IsLaneJoinSymbol(const OpCode Symbol)
{
    return (Symbol == OpCode::LEAD_LANE || Symbol == OpCode::ADD_LANES);
};


static uint32_t DefaultPolyphony = 4;
void SetDefaultPolyphony(int Polyphony)
{
    DefaultPolyphony = uint32_t(std::max(1, Polyphony));
}


Patch::Patch()
    : LastAssignedTileHandle(0)
{
    static uint64_t NextPatchIdentity = 0;
    Identity = ++NextPatchIdentity;
    MidiPolyphony = DefaultPolyphony;
    // This forces the playing patch to clear, which is useful for the editor, but
    // probably not something we want in a future stand-alone runtime.
    Recompile();
}


void Patch::SetPolyphony(int NewPolyphony)
{
    MidiPolyphony = NewPolyphony;
    Recompile();
}


int Patch::GetPolyphony()
{
    return MidiPolyphony;
}


TileHandle Patch::MakeTile(OpCode Symbol)
{
    TRACEABLE_SCOPE;
    TileHandle AllocatedHandle;
    {
        do
        {
            AllocatedHandle = ++LastAssignedTileHandle;
        }
        while (AllocatedHandle == 0 || AllocatedHandle == uint32_t(-1));
    }
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
    }

    // TODO: Move this into Patch::Compile somehow?
    if (Symbol == OpCode::BOOP || Symbol == OpCode::TWEAK)
    {
        SpecialInputs[AllocatedHandle] = std::make_shared<AtomicRunningState>(0.0);
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

    // TODO: Move this into Patch::Compile somehow?
    OpCode Symbol = GetTileSymbol(Tile);
    if (Symbol == OpCode::BOOP || Symbol == OpCode::TWEAK)
    {
        SpecialInputs.erase(Tile);
    }

    TileSymbols.erase(Tile);
    TileConstants.erase(Tile);
    TileNames.erase(Tile);
    ErasedTiles.push_back(Tile);
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


int Patch::GetTilePolyphony(TileHandle Tile)
{
    auto Found = TilePolyphony.find(Tile);
    if (Found != TilePolyphony.end())
    {
        return Found->second;
    }
    else
    {
        return 0;
    }
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


ScratchUniquePtr Patch::Compile()
{
    TRACEABLE_SCOPE;

    std::set<TileHandle> BreadCrumbs;
    std::map<TileHandle, std::vector<PortHandle>> InputSequences;

    struct TilePartial
    {
        TileHandle Tile;
        uint32_t Polyphony = 0; // Zero indicates to inherit from inputs.

        // This is NOT redundant to Patch::ByInput because its elements are ordered,
        // and that ordering is determined at compile time (e.g. by OpCode::GO).
        std::vector<std::vector<PortHandle>> Inputs;
    };
    std::vector<TilePartial> FlatGraph;
    std::unordered_map<TileHandle, TilePartial*> PartialByTile;
    FlatGraph.reserve(TileSymbols.size());

    assert(MidiPolyphony > 0);
    MidiPolyphony = std::max(MidiPolyphony, 1u);

    ScratchUniquePtr Program = std::make_unique<Scratch>();
    Program->Identity = Identity;
    Program->OutputProbe = OutputProbe;
    Program->ScopeProbe = ScopeProbe;
    Program->Polyphony = MidiPolyphony;
    Program->MidiLanes.resize(MidiPolyphony);
    Program->Retriggerables.resize(MidiPolyphony);

    auto VisitTile = [&](TileHandle Tile) -> TilePartial&
    {
        TilePartial& Partial = FlatGraph.emplace_back();
        Partial.Tile = Tile;
        PartialByTile[Tile] = &Partial;
        return Partial;
    };

    std::function<void(TileHandle)> Step = [&](const TileHandle Tile) -> void
    {
        if (!BreadCrumbs.insert(Tile).second)
        {
            return;
        }

        const OpCode Symbol = GetTileSymbol(Tile);
        if (Symbol == OpCode::CONST || Symbol == OpCode::IN || Symbol == OpCode::LANE_COUNT || Symbol == OpCode::BOOP || Symbol == OpCode::TWEAK)
        {
            TilePartial& Partial = VisitTile(Tile);
            Partial.Polyphony = 1;
            return;
        }

        const size_t InputCount = SymbolInfoMap.InputNames[(int)Symbol].size();

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
        else if (IsOutputSymbol(Symbol))
        {
            TilePartial& Partial = VisitTile(Tile);
            Partial.Polyphony = 1;
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
                Symbol == OpCode::PRES || Symbol == OpCode::CTRL || Symbol == OpCode::KIKI ||
                Symbol == OpCode::BEND)
            {
                Partial.Polyphony = MidiPolyphony;
            }
            else if (IsLaneJoinSymbol(Symbol))
            {
                Partial.Polyphony = 1;
            }
            else
            {
                // Inherit from inputs.
                Partial.Polyphony = 0;
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
        }
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
    // Tape tiles aren't actually outputs, but they're solved before anything else
    // to ensure consistent ordering and latency in the event that they're used to
    // create feedback loops.
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

    Program->ProbeConnected = false;
    if (ActiveProbeTile != TileHandle(-1))
    {
        auto Found = TileSymbols.find(ActiveProbeTile);
        if (Found != TileSymbols.end())
        {
            Program->ProbeConnected = true;
            const OpCode Symbol = Found->second;
            if (Symbol == OpCode::SCOPE)
            {
                Step(ActiveProbeTile);
            }
        }
    }
    if (!Program->ProbeConnected)
    {
        // The scope tile does appear to be disconnected, so force the probe values to zero
        // since they most likely will not be updated when the patch runs.
        OutputProbe->Set(0.0);
        ScopeProbe->Set(0.0);
        Program->ProbeInput = -1;
    }

    // From this point on, `FlatGraph` contains entries for everything that will contribute to the compiled patch program.


    // Remove persistence information for recently erased tiles.
    {
        for (TileHandle Tile : ErasedTiles)
        {
            TileLanes.erase(Tile);
        }
        ErasedTiles.clear();
    }

    // Solve tile polyphony via propagation.
    TilePolyphony.clear();
    for (TilePartial& Partial : FlatGraph)
    {
        if (Partial.Polyphony < 1)
        {
            Partial.Polyphony = 1;
            for (std::vector<PortHandle>& ConnectedOutputs : Partial.Inputs)
            {
                for (PortHandle ConnectedPort : ConnectedOutputs)
                {
                    TileHandle ConnectedTile = PortHandleTilePart(ConnectedPort);
                    TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                    Partial.Polyphony = std::max(Partial.Polyphony, ConnectedPartial->Polyphony);
                }
            }
        }
        TileLanes.insert_or_assign(Partial.Tile, Partial.Polyphony);
        TilePolyphony[Partial.Tile] = Partial.Polyphony;
    }

    std::unordered_map<PortHandle, PortHandle> LaneMergePorts;
    {
        uint32_t NextVirtualPortIndex = 0;
        for (TilePartial& Partial : FlatGraph)
        {
            const OpCode Symbol = GetTileSymbol(Partial.Tile);
            if (IsLaneJoinSymbol(Symbol))
            {
                // No virtual ports needed.
            }
            else if (Partial.Polyphony == 1)
            {
                for (std::vector<PortHandle>& ConnectedOutputs : Partial.Inputs)
                {
                    for (PortHandle ConnectedPort : ConnectedOutputs)
                    {
                        TileHandle ConnectedTile = PortHandleTilePart(ConnectedPort);
                        TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                        if (ConnectedPartial->Polyphony > 1)
                        {
                            PortHandle VirtualPort = MakePortHandle(0, NextVirtualPortIndex);
                            if (LaneMergePorts.insert({ConnectedPort, VirtualPort}).second)
                            {
                                ++NextVirtualPortIndex;
                            }
                        }
                    }
                }
            }
        }
    }

    std::map<PortHandle, std::ptrdiff_t> RegisterMap;
    auto AllocateRegister = [&](PortHandle Port, uint32_t Lanes, double InitialValue = 0.0) -> std::ptrdiff_t
    {
        std::ptrdiff_t Offset = Program->RegisterFile.size();
        Program->RegisterFile.insert(Program->RegisterFile.end(), Lanes, InitialValue);
        RegisterMap[Port] = Offset;
        return Offset;
    };
    auto AllocatePersistentRegister = [&](PortHandle Port, uint32_t Lanes, double InitialValue = 0.0) -> std::ptrdiff_t
    {
        std::ptrdiff_t Offset = AllocateRegister(Port, Lanes, InitialValue);
        Program->PersistentRegisters[Port] = { Offset, Lanes };
        return Offset;
    };

    // Output registers can't be allocated in tandem with thunk generation, as graphs can have cycles.
    // Likewise, we need to allocate registers for everything we want to persist between patch generations,
    // not just the registers needed for the current version of a patch.
    for (auto& TileAndLanes : TileLanes)
    {
        const TileHandle Tile = TileAndLanes.first;
        const uint32_t Lanes = TileAndLanes.second;
        const OpCode Symbol = GetTileSymbol(Tile);

        if (Symbol == OpCode::CONST)
        {
            // A temporary register is fine here, because this should never be overwritten.
            const double ConstantValue = GetConstant(Tile);
            AllocateRegister(MakePortHandle(Tile, 0), Lanes, ConstantValue);
        }
        else if (Symbol == OpCode::LANE_COUNT)
        {
            // A temporary register is fine here, because this should never be overwritten.
            AllocateRegister(MakePortHandle(Tile, 0), Lanes, double(MidiPolyphony));
        }
        else if (Symbol == OpCode::IN)
        {
            // If the audio backend is not guaranteed to write to this input every frame, a persistent register
            // might be better, depending on whether or not resseting to zero is more or less ideal than holding
            // the last known value.
            Program->Inputs[Tile] = AllocateRegister(MakePortHandle(Tile, 0), Lanes);
        }
        else if (IsOutputSymbol(Symbol))
        {
            const size_t InputCount = SymbolInfoMap.InputNames[(int)Symbol].size();
            const size_t ConnectedInputCount = (InputCount > 0) ? ByInput.at(MakePortHandle(Tile, 0)).size() : 0;
            if (ConnectedInputCount != 1)
            {
                // If we have zero connected inputs, then we make a register so we have something to output.
                // If we have exactly one, we'll reuse the output of the connection.
                // If we have more than one, we'll need to emit a thunk to add them, and that thunk will need
                // a register to output to.  This all happens elsewhere, we just need to allocate or not allocate
                // for now.
                // A temporary register is fine here because this will be written every frame in any
                // situation where it is possible to observe its effect, output tiles have no persistent
                // state, and cannot be used to construct graph cycles.
                AllocateRegister(MakePortHandle(Tile, 0), Lanes);
            }
        }
        else
        {
            const size_t OutputCount = SymbolInfoMap.OutputNames[(int)Symbol].size();
            const size_t ClosureCount = SymbolInfoMap.Closures[(int)Symbol];

            for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
            {
                // Persistent registers are used here, because there may be graph cycles, in which
                // case we need to be able to read values from the previous frame, which may have come
                // from a different patch.  Additionally, thunks are free to assume their outputs are
                // persistent, which can be used to save on allocating extra closure registers.
                PortHandle OutputPort = MakePortHandle(Tile, PortIndex);
                AllocatePersistentRegister(OutputPort, Lanes);

                auto Found = LaneMergePorts.find(OutputPort);
                if (Found != LaneMergePorts.end())
                {
                    // A monophonic tile requires a temporary register for the merged output.
                    PortHandle VirtualPort = Found->second;
                    AllocateRegister(VirtualPort, 1);
                }
            }

            for (int ClosureIndex = 0; ClosureIndex < static_cast<int>(ClosureCount); ++ClosureIndex)
            {
                // Closure registers represent a thunk's internal state, and as such they must be
                // persistent across patch revisions.
                PortHandle ClosurePort = MakeClosureHandle(Tile, ClosureIndex);
                AllocatePersistentRegister(ClosurePort, Lanes);
            }
        }
    }

    // Make sure we have all our tapes.
    for (TileHandle Tile : TapeTiles)
    {
        TilePartial* Partial = PartialByTile.at(Tile);
        PortHandle Port = MakePortHandle(Tile, 0);

        auto Result = Program->Tapes.try_emplace(Port, (size_t)Partial->Polyphony);
        std::vector<MagicTapeUniquePtr>& Bank = Result.first->second;
        if (Result.second)
        {
            for (MagicTapeUniquePtr& Tape : Bank)
            {
                Tape = std::make_unique<BlankTape>();
            }
        }
    }

    // Emit thunks, and adjust default values.
    for (TilePartial& Partial : FlatGraph)
    {
        const OpCode Symbol = GetTileSymbol(Partial.Tile);

        std::vector<std::vector<std::ptrdiff_t>> Inputs;
        Inputs.reserve(Partial.Inputs.size());

        std::vector<std::vector<uint32_t>> InputWidths;
        InputWidths.reserve(Partial.Inputs.size());

        for (std::vector<PortHandle>& ConnectedOutputs : Partial.Inputs)
        {
            std::vector<std::ptrdiff_t>& InputRegisters = Inputs.emplace_back();
            InputRegisters.reserve(ConnectedOutputs.size());

            std::vector<uint32_t>& Widths = InputWidths.emplace_back();
            Widths.reserve(ConnectedOutputs.size());

            if (IsLaneJoinSymbol(Symbol))
            {
                if (Symbol == OpCode::LEAD_LANE)
                {
                    for (PortHandle ConnectedOutput : ConnectedOutputs)
                    {
                        TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                        TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                        std::ptrdiff_t BaseAddress = RegisterMap.at(ConnectedOutput);
                        // LeadLaneThunk will read the inputs with a stride.
                        uint32_t Width = ConnectedPartial->Polyphony;
                        if (Width == 1)
                        {
                            // Monophonic inputs need to be copied to fill the full lane width.
                            for (uint32_t Offset = 0; Offset < MidiPolyphony; ++Offset)
                            {
                                InputRegisters.push_back(BaseAddress);
                                Widths.push_back(1);
                            }
                        }
                        else
                        {
                            // Polyphonic inputs have each register connected as different input.
                            assert(Width == MidiPolyphony);
                            for (uint32_t Offset = 0; Offset < Width; ++Offset)
                            {
                                InputRegisters.push_back(BaseAddress + Offset);
                                Widths.push_back(1);
                            }
                        }
                    }
                }
                else if (Symbol == OpCode::ADD_LANES)
                {
                    for (PortHandle ConnectedOutput : ConnectedOutputs)
                    {
                        TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                        TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                        std::ptrdiff_t BaseAddress = RegisterMap.at(ConnectedOutput);
                        uint32_t Width = ConnectedPartial->Polyphony;
                        for (uint32_t Offset = 0; Offset < Width; ++Offset)
                        {
                            InputRegisters.push_back(BaseAddress + Offset);
                            Widths.push_back(1);
                        }
                    }
                }
            }
            else if (Partial.Polyphony == 1)
            {
                for (PortHandle ConnectedOutput : ConnectedOutputs)
                {
                    auto Found = LaneMergePorts.find(ConnectedOutput);
                    if (Found != LaneMergePorts.end())
                    {
                        ConnectedOutput = Found->second;
                    }
                    InputRegisters.push_back(RegisterMap.at(ConnectedOutput));
                    Widths.push_back(1);
                }
            }
            else
            {
                for (PortHandle ConnectedOutput : ConnectedOutputs)
                {
                    InputRegisters.push_back(RegisterMap.at(ConnectedOutput));
                    TileHandle ConnectedTile = PortHandleTilePart(ConnectedOutput);
                    TilePartial* ConnectedPartial = PartialByTile.at(ConnectedTile);
                    Widths.push_back(ConnectedPartial->Polyphony);
                }
            }
        }

        std::vector<std::ptrdiff_t> Outputs;
        std::vector<std::ptrdiff_t> Closures;

        if (Symbol == OpCode::CONST || Symbol == OpCode::IN || Symbol == OpCode::LANE_COUNT)
        {
            // No thunks are created for these symbols.
            continue;
        }
        else if (IsOutputSymbol(Symbol))
        {
            std::ptrdiff_t OutputRegister;

            if (Inputs[0].size() == 1)
            {
                OutputRegister = Inputs[0][0];
            }
            else
            {
                OutputRegister = RegisterMap.at(MakePortHandle(Partial.Tile, 0));
                Outputs = { OutputRegister };
                if (Inputs[0].size() > 1)
                {
                    static const BasicCreateAndConnectFn AddCreateAndConnect = SymbolInfoMap.BasicCreateAndConnect.at((int)OpCode::ADD);
                    Program->Program.push_back(AddCreateAndConnect(Program, Inputs, Outputs, Closures));
                }
            }

            // Collect the patch output registers.
            if (Symbol == OpCode::OUT)
            {
                Program->Outputs.push_back(OutputRegister);
            }
            else if (Symbol == OpCode::AUX)
            {
                Program->AuxOutputs[Partial.Tile] = OutputRegister;
            }

            // This tile is also the current active probe.
            if (Program->ProbeConnected && Partial.Tile == ActiveProbeTile)
            {
                Program->ProbeInput = OutputRegister;
            }
        }
        else
        {
            const size_t OutputCount = SymbolInfoMap.OutputNames[(int)Symbol].size();
            const size_t ClosureCount = SymbolInfoMap.Closures[(int)Symbol];

            Outputs.reserve(OutputCount);
            for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
            {
                PortHandle OutputPort = MakePortHandle(Partial.Tile, PortIndex);
                Outputs.push_back(RegisterMap.at(OutputPort));
            }

            Closures.reserve(ClosureCount);
            for (int ClosureIndex = 0; ClosureIndex < static_cast<int>(ClosureCount); ++ClosureIndex)
            {
                PortHandle ClosurePort = MakeClosureHandle(Partial.Tile, ClosureIndex);
                Closures.push_back(RegisterMap.at(ClosurePort));
            }

            for (uint32_t Lane = 0; Lane < Partial.Polyphony; ++Lane)
            {
                if (Lane > 0)
                {
                    for (uint32_t InputIndex = 0; InputIndex < Inputs.size(); ++InputIndex)
                    {
                        std::vector<std::ptrdiff_t>& InputRegisters = Inputs[InputIndex];
                        for (uint32_t Connection = 0; Connection < InputRegisters.size(); ++Connection)
                        {
                            // TODO: assert that this matches if the width is not 1
                            if (InputWidths[InputIndex][Connection] == Partial.Polyphony)
                            {
                                ++(InputRegisters[Connection]);
                            }
                        }
                    }
                    for (std::ptrdiff_t& OutputRegister : Outputs)
                    {
                        ++OutputRegister;
                    }
                    for (std::ptrdiff_t& ClosureRegister : Closures)
                    {
                        ++ClosureRegister;
                    }
                }

                InstructionThunkSharedPtr Thunk = nullptr;
                {
                    auto Found = SymbolInfoMap.BasicCreateAndConnect.find((int)Symbol);
                    if (Found != SymbolInfoMap.BasicCreateAndConnect.end())
                    {
                        Thunk = Found->second(Program, Inputs, Outputs, Closures);
                        Program->Program.push_back(Thunk);
                    }
                }

                if (Thunk == nullptr)
                {
                    auto Found = SymbolInfoMap.WidgetCreateAndConnect.find((int)Symbol);
                    if (Found != SymbolInfoMap.WidgetCreateAndConnect.end())
                    {
                        Thunk = Found->second(Program, Inputs, Outputs, Closures, SpecialInputs[Partial.Tile]);
                        Program->Program.push_back(Thunk);
                    }
                }

                if (Thunk == nullptr)
                {
                    auto Found = SymbolInfoMap.MidiCreateAndConnect.find((int)Symbol);
                    if (Found != SymbolInfoMap.MidiCreateAndConnect.end())
                    {
                        Thunk = Found->second(Program, Inputs, Outputs, Closures, Lane);
                        Program->Program.push_back(Thunk);
                    }
                }

                if (Thunk == nullptr)
                {
                    auto Found = SymbolInfoMap.TapeCreateAndConnect.find((int)Symbol);
                    if (Found != SymbolInfoMap.TapeCreateAndConnect.end())
                    {
                        Thunk = Found->second(Program, Inputs, Outputs, Closures, MakePortHandle(Partial.Tile, 0), Lane);
                        Program->Program.push_back(Thunk);
                    }
                }

                assert(Thunk != nullptr);
                if (Partial.Polyphony > 1 && Symbol == OpCode::ADSR)
                {
                    Thunk->Retriggerable = true;
                    uint32_t ThunkIndex = Program->Program.size() - 1;
                    assert(Program->Program[ThunkIndex] == Thunk);
                    // TODO: figure out some means of determining if the trigger is directly or indirectly
                    // connected to a gate tile inntead of using the ADSR's polyphony as a proxy for this.
                    Program->Retriggerables[Lane].push_back(ThunkIndex);
                }

                if (Symbol == OpCode::LEAD_LANE)
                {
                    break;
                }
            }

            if (Partial.Polyphony > 1)
            {
                for (int PortIndex = 0; PortIndex < static_cast<int>(OutputCount); ++PortIndex)
                {
                    PortHandle OutputPort = MakePortHandle(Partial.Tile, PortIndex);

                    auto Found = LaneMergePorts.find(OutputPort);
                    if (Found != LaneMergePorts.end())
                    {
                        PortHandle VirtualPort = Found->second;

                        std::ptrdiff_t PolyphonicBaseAddress = RegisterMap.at(OutputPort);
                        std::ptrdiff_t ResultAddress = RegisterMap.at(VirtualPort);

                        std::vector<std::ptrdiff_t> MergeLanes;
                        MergeLanes.reserve(Partial.Polyphony);
                        for (std::ptrdiff_t Lane = 0; Lane < (std::ptrdiff_t)Partial.Polyphony; ++Lane)
                        {
                            MergeLanes.push_back(PolyphonicBaseAddress + Lane);
                        }
                        std::vector<std::vector<std::ptrdiff_t>> JoinInputs = { MergeLanes };
                        std::vector<std::ptrdiff_t> JoinOutputs = { ResultAddress };
                        std::vector<std::ptrdiff_t> JoinClosures;

                        static const BasicCreateAndConnectFn AddCreateAndConnect = SymbolInfoMap.BasicCreateAndConnect.at((int)OpCode::ADD);
                        Program->Program.push_back(AddCreateAndConnect(Program, JoinInputs, JoinOutputs, JoinClosures));
                    }
                }
            }
        }
    }

    for (InstructionThunkSharedPtr& Thunk : Program->Program)
    {
        assert(Thunk != nullptr);
    }

#if 0
    std::print("\n\n==============================================================================\n");
    int ThunkIndex = 0;
    for (InstructionThunkSharedPtr& Thunk : Program->Program)
    {
        OpCode Symbol = Thunk->DebugSymbol;
        std::print("THUNK {}:\n{}\n", ThunkIndex++, GetDefaultName(Symbol));
        {
            int InputIndex = 0;
            for (std::vector<std::ptrdiff_t>& InputConnections : Thunk->Registers.Input)
            {
                std::print("\tINPUT {}:\n", InputIndex++);
                for (std::ptrdiff_t& Register : InputConnections)
                {
                    std::print("\t - {}\n", Register);
                }
            }
        }
        {
            int OutputIndex = 0;
            for (std::ptrdiff_t& Register : Thunk->Registers.Output)
            {
                std::print("\tOUTPUT {}: {}\n", OutputIndex++, Register);
            }
        }
        {
            int ClosureIndex = 0;
            for (std::ptrdiff_t& Register : Thunk->Registers.Closure)
            {
                std::print("\nCLOSURE {}: {}\n", ClosureIndex++, Register);
            }
        }
    }
#endif

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
        ScratchUniquePtr NewProgram = Compile();
        Audio::GetStream()->ProgramChange(std::move(NewProgram));
    }
}


void Scratch::PrintRegisters() const
{
    std::unordered_map<uint32_t, PortHandle> PortsByRegisterOffset;
    for (auto const& [Port, Allocation] : PersistentRegisters)
    {
        PortsByRegisterOffset[Allocation.BaseOffset] = Port;
    }
    for (std::ptrdiff_t Register = 0; Register < (std::ptrdiff_t)RegisterFile.size(); ++Register)
    {
        const double Value = RegisterFile.at(Register);
        auto Found = PortsByRegisterOffset.find(Register);
        if (Found != PortsByRegisterOffset.end())
        {
            PortHandle Port = Found->second;
            TileHandle Tile = PortHandleTilePart(Port);
            int32_t Index = (int32_t)PortHandlePortIndexPart(Port);
            std::print("    {:>4}: {:^8.4} ({}:{})\n", Register, Value, Tile, Index);
        }
        else
        {
            std::print("    {:>4}: {:^8.4}\n", Register, Value);
        }
    }
}


void Scratch::Migrate(Scratch& Old)
{
    TRACEABLE_SCOPE;

#if 0
    MidiChannels = Old.MidiChannels;
    MostRecentChannel = Old.MostRecentChannel;
#endif
    if (Polyphony == Old.Polyphony)
    {
        assert(Old.MidiLanes.size() == Old.Polyphony);
        MidiLanes = Old.MidiLanes;
    }
    // TODO: probably should always do this regardless of matching identity.
    // The midi scheduler's running state should probably be a persistent mixin.
    ChannelPrograms = Old.ChannelPrograms;
    ChannelPitchBend = Old.ChannelPitchBend;
    ChannelControls = Old.ChannelControls;
    assert(MidiLanes.size() == Polyphony);

    for (std::vector<uint32_t>& ThunkIndices : Retriggerables)
    {
        for (uint32_t ThunkIndex : ThunkIndices)
        {
            InstructionThunkSharedPtr& Thunk = Program.at(ThunkIndex);
            assert(Thunk != nullptr);
            assert(Thunk->Retriggerable);
        }
    }

    constexpr bool EnableDebugLogging = false;

    if (EnableDebugLogging)
    {
        std::print("\n\n==============================================================================\n");
        std::print("Old register file:\n");
        Old.PrintRegisters();
        std::print("\nRunning migration:\n");
    }

    for (auto const& [Port, NewAllocation] : PersistentRegisters)
    {
        {
            auto Found = Old.PersistentRegisters.find(Port);
            if (Found != Old.PersistentRegisters.end())
            {
                if (EnableDebugLogging)
                {
                    std::print(" + MATCH: {}:{}", PortHandleTilePart(Port), (int32_t)PortHandlePortIndexPart(Port));
                }

                const RegisterAllocation& OldAllocation = Found->second;
                if (OldAllocation.LaneCount == NewAllocation.LaneCount)
                {
                    if (EnableDebugLogging)
                    {
                        std::print(" (copy, no resize)\n");
                    }

                    for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                    {
                        const double MigratedValue = Old.RegisterFile.at(OldAllocation.BaseOffset + Lane);
                        if (EnableDebugLogging)
                        {
                            const double StompedValue = RegisterFile.at(NewAllocation.BaseOffset + Lane);
                            const uint32_t WriteOffset = NewAllocation.BaseOffset + Lane;
                            std::print("    > Register[{}] = {:.4} -> {:.4}\n", WriteOffset, StompedValue, MigratedValue);
                        }
                        RegisterFile.at(NewAllocation.BaseOffset + Lane) = MigratedValue;
                    }
                }
                else if (OldAllocation.LaneCount == 1)
                {
                    if (EnableDebugLogging)
                    {
                        std::print(" (mono -> poly resize)\n");
                    }
                    for (uint32_t Lane = 0; Lane < NewAllocation.LaneCount; ++Lane)
                    {
                        const double MigratedValue = Old.RegisterFile.at(OldAllocation.BaseOffset);
                        if (EnableDebugLogging)
                        {
                            const double StompedValue = RegisterFile.at(NewAllocation.BaseOffset + Lane);
                            const uint32_t WriteOffset = NewAllocation.BaseOffset + Lane;
                            std::print("    | Register[{}] = {:.4} -> {:.4}\n", WriteOffset, StompedValue, MigratedValue);
                        }
                        RegisterFile.at(NewAllocation.BaseOffset + Lane) = MigratedValue;
                    }
                }
                else
                {
                    // If the register is migrating from polyphonic to monophonic, the correct default value for
                    // that port is expected to already be in the new register file.  Usually this value will be
                    // zero, but some thunks will set different default values for their ports.
                    if (EnableDebugLogging)
                    {
                        std::print(" (poly->mono RESET)\n");
                    }
                }

                if (EnableDebugLogging)
                {
                    std::print("\n");
                }
            }
            else
            {
                // The register is totally new, and already has the correct starting value.  No additional
                // handling is required.
                if (EnableDebugLogging)
                {
                    std::print(" - no match: {}:{}\n", PortHandleTilePart(Port), (int32_t)PortHandlePortIndexPart(Port));
                }
            }
        }
        {
            auto Found = Old.Tapes.find(Port);
            if (Found != Old.Tapes.end())
            {
                Tapes[Port] = std::move(Found->second);
            }
        }
    }

    if (EnableDebugLogging)
    {
        std::print("\nMigration complete!\n");
        std::print("\nNew register file:\n");
        PrintRegisters();
    }
}


void Scratch::Crank(double SampleInterval, float& OutLeft, float& OutRight)
{
    TRACEABLE_SCOPE;
    assert(MidiLanes.size() == Polyphony);
    {
        TRACEABLE_NAMED_SCOPE("MIDI PHASE");

        MidiMessage Message;
        if (PopMidiMessage(Message))
        {
            int32_t AssignedLane = -1;
            if (Message.Type == MidiMessageType::Reset)
            {
                for (MidiNoteState& State : MidiLanes)
                {
                    State = MidiNoteState();
                }
            }
            else if (Message.Type == MidiMessageType::ControlChange)
            {
                uint8_t Control = uint8_t(Message.Param1);
                ChannelControls[Message.Channel][Control] = Message.Param2;
                if (Control == 123 && Message.Param2 == 0.0)
                {
                    // All notes off.  See: http://midi.teragonaudio.com/tech/midispec/ntnoff.htm
                    for (MidiNoteState& State : MidiLanes)
                    {
                        State = MidiNoteState();
                        State.Gate = 0.0;
                        State.Pressure = 0.0;
                    }
                }
            }
            else if (Message.Type == MidiMessageType::ProgramChange)
            {
                ChannelPrograms[Message.Channel] = uint8_t(Message.Param1);
                for (MidiNoteState& State : MidiLanes)
                {
                    if (State.Channel == Message.Channel)
                    {
                        State.Gate = 0.0;
                        State.Velocity = 0.0;
                        State.Pressure = 0.0;
                    }
                }
            }
            else if (Message.Type == MidiMessageType::ChannelPressure)
            {
                for (MidiNoteState& State : MidiLanes)
                {
                    if (State.Channel == Message.Channel)
                    {
                        State.Pressure = Message.Param1;
                    }
                }
            }
            else if (Message.Type == MidiMessageType::PitchBend)
            {
                ChannelPitchBend[Message.Channel] = Message.Param1;
            }
            else if (Message.Type == MidiMessageType::Note || Message.Type == MidiMessageType::PolyPress)
            {
                bool LaneReset = false;
                const double Note = Message.Param1;
                const int Channel = int(Message.Channel);
                if (MostRecentLane == -1)
                {
                    AssignedLane = 0;
                    LaneReset = true;
                }
                else
                {
                    // If we have a lane that matches the note and mask, use that.
                    for (uint32_t Lane = 0; Lane < Polyphony; ++Lane)
                    {
                        const double LaneNote = MidiLanes.at(Lane).Note;
                        const int LaneChannel = int(MidiLanes.at(Lane).Channel);
                        if (Note == LaneNote && Channel == LaneChannel)
                        {
                            LaneReset = MidiLanes.at(Lane).Gate == 0.0;
                            AssignedLane = Lane;
                            break;
                        }
                    }
                    if (AssignedLane == -1)
                    {
                        // Otherwise select the oldest lane, prioritizing inactive lanes over active lanes.
                        int32_t OldestLane = -1;
                        int64_t OldestAge = -1;
                        int32_t OldestInactiveLane = -1;
                        int64_t OldestInactiveAge = -1;
                        for (uint32_t Lane = 0; Lane < Polyphony; ++Lane)
                        {
                            double Gate = MidiLanes.at(Lane).Gate;
                            int64_t Age = MidiLanes.at(Lane).Age;
                            if (Gate == 0.0 && Age > OldestInactiveAge)
                            {
                                OldestInactiveLane = Lane;
                                OldestInactiveAge = Age;
                            }
                            if (Age > OldestAge)
                            {
                                OldestLane = Lane;
                                OldestAge = Age;
                            }
                        }
                        if (OldestInactiveLane > 0)
                        {
                            LaneReset = true;
                            AssignedLane = OldestInactiveLane;
                        }
                        else if (OldestLane > 0)
                        {
                            LaneReset = true;
                            AssignedLane = OldestLane;
                        }
                    }
                    if (AssignedLane == -1)
                    {
                        // In the event that we somehow selected nothing, just pick "the next one".
                        LaneReset = true;
                        AssignedLane = (MostRecentLane + 1) % Polyphony;
                    }
                }
                if (LaneReset)
                {
                    MostRecentLane = AssignedLane;
                    for (MidiNoteState& State : MidiLanes)
                    {
                        ++State.Age;
                    }

                    MidiNoteState& State = MidiLanes.at(AssignedLane);
                    State.Note = Note;
                    State.Gate = 0.0;
                    State.Velocity = 0.0;
                    State.Pressure = 0.0;
                    State.Channel = double(Channel);
                    State.Age = 0;

                    for (uint32_t ThunkIndex : Retriggerables[AssignedLane])
                    {
                        InstructionThunkSharedPtr& Thunk = Program.at(ThunkIndex);
                        assert(Thunk != nullptr);
                        assert(Thunk->Retriggerable);
                        Thunk->Retrigger();
                    }
                }
                if (Message.Type == MidiMessageType::Note)
                {
                    MidiNoteState& State = MidiLanes.at(AssignedLane);
                    const double Velocity = Message.Param2;
                    if (Velocity > 0.0)
                    {
                        State.Gate = 1.0;
                        State.Velocity = Velocity;
                    }
                    else
                    {
                        // Leave the velocity alone so the adsr can ring out instead.
                        State.Gate = 0.0;
                        State.Pressure = 0.0;
                    }
                }
                else if (Message.Type == MidiMessageType::PolyPress)
                {
                    MidiNoteState& State = MidiLanes.at(AssignedLane);
                    const double Pressure = Message.Param2;
                    State.Pressure = Pressure;
                }
            }
        }
    }
    {
        TRACEABLE_NAMED_SCOPE("CRANK PHASE");
        for (InstructionThunkSharedPtr& Thunk : Program)
        {
            assert(Thunk != nullptr);
            Thunk->Crank(SampleInterval);
        }
    }
    {
        if (Outputs.size() == 1)
        {
            OutLeft = RegisterFile.at(Outputs[0]);
            OutRight = RegisterFile.at(Outputs[0]);
        }
        else if (Outputs.size() > 1)
        {
            OutLeft = RegisterFile.at(Outputs[0]);
            OutRight = RegisterFile.at(Outputs[1]);
        }
    }
    if (ProbeConnected)
    {
        TRACEABLE_NAMED_SCOPE("UPDATE PROBES");
        ScopeProbe->Set(RegisterFile.at(ProbeInput));
        if (Outputs.size() > 0)
        {
            // TODO : per-output probes
            OutputProbe->Set(RegisterFile.at(Outputs[0]));
        }
    }
    else if (Outputs.size() > 0)
    {
        TRACEABLE_NAMED_SCOPE("UPDATE PROBES");
        // TODO : per-output probes
        ScopeProbe->Set(RegisterFile.at(Outputs[0]));
        OutputProbe->Set(RegisterFile.at(Outputs[0]));
    }
}


MagicTape* Scratch::FindTape(PortHandle Port, uint32_t Lane)
{
    auto Found = Tapes.find(Port);
    if (Found != Tapes.end())
    {
        std::vector<MagicTapeUniquePtr>& Bank = Found->second;
        if (Lane < Bank.size())
        {
            return Bank[Lane].get();
        }
    }
    return nullptr;
}
