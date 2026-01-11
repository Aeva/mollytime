
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

#include <random>
#include <numbers>
#include <utility>

#include "thunks.h"
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


struct SinThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = \
    {
        OpCode::SIN, "sin",
        {{
            {"hz", 440.0, InputCombiner::ADD},
        }},
        {"amp"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SinThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Hz = Registers.CombineLaneInput(Lane, 0, 440.0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.ClosureRef(Lane, 0);

            Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
            Amplitude = std::sin(Phase * Tau);
        });
    }

    virtual ~SinThunk() {};
};


struct SqrThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = \
    {
        OpCode::SQR, "sqr",
        {{
            {"hz", 440.0, InputCombiner::ADD},
        }},
        {"amp"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SqrThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Hz = Registers.CombineLaneInput(Lane, 0, 440.0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.ClosureRef(Lane, 0);

            Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
            if (Phase < 0.0)
            {
                Phase += 1.0;
            }
            Amplitude = Phase < 0.5 ? 1.0 : -1.0;
        });
    }

    virtual ~SqrThunk() {};
};


struct TriThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = \
    {
        OpCode::TRI, "tri",
        {{
            {"hz", 440.0, InputCombiner::ADD},
        }},
        {"amp"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("TriThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Hz = Registers.CombineLaneInput(Lane, 0, 440.0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.ClosureRef(Lane, 0);

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
        });
    }

    virtual ~TriThunk() {};
};


struct SawThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = \
    {
        OpCode::SAW, "saw",
        {{
            {"hz", 440.0, InputCombiner::ADD},
        }},
        {"amp"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SawThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Hz = Registers.CombineLaneInput(Lane, 0, 440.0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.ClosureRef(Lane, 0);

            Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
            if (Phase < 0.0)
            {
                Phase += 1.0;
            }

            Amplitude = Phase * 2.0 - 1.0;
        });
    }

    virtual ~SawThunk() {};
};


struct NoiThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 3> Info = \
    {
        OpCode::NOI, "noise",
        {{
            {"hz", 440.0, InputCombiner::ADD},
        }},
        {"amp"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("NoiThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Hz = Registers.CombineLaneInput(Lane, 0, 440.0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.ClosureRef(Lane, 0);
            double& HighAmp = Registers.ClosureRef(Lane, 1);
            double& LowAmp = Registers.ClosureRef(Lane, 2);

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
        });
    }

    virtual ~NoiThunk() {};
};


struct PhaseThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::PHASE, "phase",
        {{
            {"hz", 440.0, InputCombiner::ADD},
        }},
        {"phase"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PhaseThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Hz = Registers.CombineLaneInput(Lane, 0, 440.0);
            double& Phase = Registers.OutputRef(Lane, 0);

            Phase = std::fmod(Phase + Hz * SampleInterval, 1.0);
            if (Phase < 0.0)
            {
                Phase += 1.0;
            }
        });
    }

    virtual ~PhaseThunk() {};
};


struct SinTrainThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 0> Info = \
    {
        OpCode::SIN_TRAIN, "sin\ntrain",
        {{
            {"phase", 0.0, InputCombiner::ADD},
        }},
        {"amp", "phase"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SinTrainThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double InPhase = Registers.CombineLaneInput(Lane, 0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.OutputRef(Lane, 1);

            Phase = std::fmod(InPhase, 1.0);
            Amplitude = std::sin(Phase * Tau);
        });
    }

    virtual ~SinTrainThunk() {};
};


struct SqrTrainThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 0> Info = \
    {
        OpCode::SQR_TRAIN, "sqr\ntrain",
        {{
            {"phase", 0.0, InputCombiner::ADD},
        }},
        {"amp", "phase"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SqrTrainThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double InPhase = Registers.CombineLaneInput(Lane, 0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.OutputRef(Lane, 1);

            Phase = std::fmod(InPhase, 1.0);
            if (Phase < 0.0)
            {
                Phase += 1.0;
            }
            Amplitude = Phase < 0.5 ? 1.0 : -1.0;
        });
    }

    virtual ~SqrTrainThunk() {};
};


struct TriTrainThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 0> Info = \
    {
        OpCode::TRI_TRAIN, "tri\ntrain",
        {{
            {"phase", 0.0, InputCombiner::ADD},
        }},
        {"amp", "phase"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("TriTrainThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double InPhase = Registers.CombineLaneInput(Lane, 0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.OutputRef(Lane, 1);

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
        });
    }

    virtual ~TriTrainThunk() {};
};


struct SawTrainThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 0> Info = \
    {
        OpCode::SAW_TRAIN, "saw\ntrain",
        {{
            {"phase", 0.0, InputCombiner::ADD},
        }},
        {"amp", "phase"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SawTrainThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double InPhase = Registers.CombineLaneInput(Lane, 0);
            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& Phase = Registers.OutputRef(Lane, 1);

            Phase = std::fmod(InPhase, 1.0);
            if (Phase < 0.0)
            {
                Phase += 1.0;
            }

            Amplitude = Phase * 2.0 - 1.0;
        });
    }

    virtual ~SawTrainThunk() {};
};


struct PhaseWidthModulationThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = \
    {
        OpCode::PWM, "pwm",
        {{
            {"phase", 0.0, InputCombiner::ADD},
            {"bal", 0.0, InputCombiner::ADD},
        }},
        {"phase"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PhaseWidthModulationThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Phase = Registers.CombineLaneInput(Lane, 0);
            double Balance = Registers.CombineLaneInput(Lane, 1);
            double& OutPhase = Registers.OutputRef(Lane, 0);

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
        });
    }

    virtual ~PhaseWidthModulationThunk() {};
};


struct AddThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::ADD, "add",
        {{
            {"+", 0.0, InputCombiner::ADD},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("AddThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = Registers.CombineLaneInput(Lane, 0, 0.0, CombinerAdd);
        });
    }

    virtual ~AddThunk() {};
};


struct MulThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::MUL, "mul",
        {{
            {"*", 0.0, InputCombiner::MUL},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MulThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = Registers.CombineLaneInput(Lane, 0, 0.0, CombinerMul);
        });
    }

    virtual ~MulThunk() {};
};


struct RcpThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::RCP, "rcp",
        {{
            {"#", 0.0, InputCombiner::MUL},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("RcpThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Divisor = Registers.CombineLaneInput(Lane, 0, 0.0, CombinerMul);
            double& Output = Registers.OutputRef(Lane, 0);

            if (Divisor != 0.0)
            {
                Output = 1.0 / Divisor;
            }
        });
    }

    virtual ~RcpThunk() {};
};


struct PowThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = \
    {
        OpCode::POW, "pow",
        {{
            {"n", 0.0, InputCombiner::ADD},
            {"^", 2.0, InputCombiner::ADD},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PowThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Base = Registers.CombineLaneInput(Lane, 0);
            double Exponent = Registers.CombineLaneInput(Lane, 1, 2.0);
            double& Output = Registers.OutputRef(Lane, 0);

            double Result = std::pow(Base, Exponent);
            if (std::isfinite(Result))
            {
                Output = Result;
            }
        });
    }

    virtual ~PowThunk() {};
};


struct SignPreservingPowThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = \
    {
        OpCode::SPOW, "spow",
        {{
            {"n", 0.0, InputCombiner::ADD},
            {"^", 2.0, InputCombiner::ADD},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SignPreservingPowThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Base = Registers.CombineLaneInput(Lane, 0);
            double Exponent = Registers.CombineLaneInput(Lane, 1, 2.0);
            double& Output = Registers.OutputRef(Lane, 0);

            double Sign = Base >= 0.0 ? 1.0 : -1.0;
            double Result = std::pow(std::abs(Base), Exponent);
            if (std::isfinite(Result))
            {
                Output = Result * Sign;
            }
        });
    }

    virtual ~SignPreservingPowThunk() {};
};


struct MinThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::MIN, "min",
        {{
            {"min", 0.0, InputCombiner::MIN},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MinThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = Registers.CombineLaneInput(Lane, 0, 0.0, CombinerMin);
        });
    }

    virtual ~MinThunk() {};
};


struct MaxThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::MAX, "max",
        {{
            {"max", 0.0, InputCombiner::MAX},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MaxThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = Registers.CombineLaneInput(Lane, 0, 0.0, CombinerMax);
        });
    }

    virtual ~MaxThunk() {};
};


struct ClampThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = \
    {
        OpCode::CLAMP, "clamp",
        {{
            {"#", 0.0, InputCombiner::ADD},
            {"high", 1.0, InputCombiner::MAX},
            {"low", -1.0, InputCombiner::MIN},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("ClampThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Sample = Registers.CombineLaneInput(Lane, 0);
            double High = Registers.CombineLaneInput(Lane, 1, 1.0, CombinerMax);
            double Low = Registers.CombineLaneInput(Lane, 2, -1.0, CombinerMin);
            double& Output = Registers.OutputRef(Lane, 0);
            Output = std::max(std::min(Sample, High), Low);
        });
    }

    virtual ~ClampThunk() {};
};


struct FloorThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::FLOOR, "floor",
        {{
            {"#", 0.0, InputCombiner::ADD},
        }},
        {"floor"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("FloorThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = std::floor(Registers.CombineLaneInput(Lane, 0));
        });
    }

    virtual ~FloorThunk() {};
};


struct CeilThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::CEIL, "ceil",
        {{
            {"#", 0.0, InputCombiner::ADD},
        }},
        {"ceil"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("CeilThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = std::ceil(Registers.CombineLaneInput(Lane, 0));
        });
    }

    virtual ~CeilThunk() {};
};


struct RoundThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::ROUND, "round",
        {{
            {"#", 0.0, InputCombiner::ADD},
        }},
        {"rounded"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("RoundThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = std::round(Registers.CombineLaneInput(Lane, 0));
        });
    }

    virtual ~RoundThunk() {};
};


struct SignThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::SIGN, "sign",
        {{
            {"#", 0.0, InputCombiner::ADD},
        }},
        {"sign"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("SignThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Number = Registers.CombineLaneInput(Lane, 0);
            double& Sign = Registers.OutputRef(Lane, 0);
            Sign = (Number < 0.0) ? -1.0 : 1.0;
        });
    }

    virtual ~SignThunk() {};
};


struct AbsThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::ABS, "abs",
        {{
            {"#", 0.0, InputCombiner::ADD},
        }},
        {"abs"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("AbsThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = std::abs(Registers.CombineLaneInput(Lane, 0));
        });
    }

    virtual ~AbsThunk() {};
};


struct FoldThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = \
    {
        OpCode::FLD, "fold",
        {{
            {"v", 0.0, InputCombiner::ADD},
            {"p", 1.0, InputCombiner::ADD},
            {"n", 0.0, InputCombiner::ADD},
        }},
        {"w"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("FoldThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Sample = Registers.CombineLaneInput(Lane, 0);
            double& Output = Registers.OutputRef(Lane, 0);

            double Threshold;
            if (Sample < 0.0 && Registers.InputConnected(2))
            {
                // Use the negative threshold input.
                Threshold = Registers.CombineLaneInput(Lane, 2);
            }
            else
            {
                // Use the positive threshold input or default to 1.
                Threshold = Registers.CombineLaneInput(Lane, 1, 1.0);
            }

            double Sign = Sample < 0.0 ? -1.0 : 1.0;
            Threshold = std::min(std::max(std::abs(Threshold), 0.0), 1.0);
            Sample = std::abs(Sample);
            if (Sample > Threshold)
            {
                Sample = Threshold - (Sample - Threshold);
            }
            Output = Sample * Sign;
        });
    }

    virtual ~FoldThunk() {};
};


struct InvertThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::INV, "invert",
        {{
            {"#", 0.0, InputCombiner::ADD},
        }},
        {"#"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("InvertThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Value = Registers.CombineLaneInput(Lane, 0);
            double& Output = Registers.OutputRef(Lane, 0);
            double Sign = Value < 0.0 ? -1.0 : 1.0;
            Output = (1.0 - std::abs(Value)) * Sign;
        });
    }

    virtual ~InvertThunk() {};
};


struct ToUnipolarThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::STU, "bipolar\nto\nunipolar",
        {{
            {"bi", 0.0, InputCombiner::ADD},
        }},
        {"uni"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("ToUnipolarThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = Registers.CombineLaneInput(Lane, 0) * 0.5 + 0.5;
        });
    }

    virtual ~ToUnipolarThunk() {};
};


struct ToBipolarThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::UTS, "unipolar\nto\nbipolar",
        {{
            {"uni", 0.0, InputCombiner::ADD},
        }},
        {"bi"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("ToBipolarThunk");
        CrankLanes([&](uint32_t Lane)
        {
            Registers.OutputRef(Lane, 0) = Registers.CombineLaneInput(Lane, 0) * 2.0 - 1.0;
        });
    }

    virtual ~ToBipolarThunk() {};
};


struct MixThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = \
    {
        OpCode::MIX, "mix",
        {{
            {"low", 0.0, InputCombiner::ADD},
            {"high", 0.0, InputCombiner::ADD},
            {"balance", 0.5, InputCombiner::ADD},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MixThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Left = Registers.CombineLaneInput(Lane, 0);
            double Right = Registers.CombineLaneInput(Lane, 1);
            double Alpha = Registers.CombineLaneInput(Lane, 2, 0.5);
            double& Output = Registers.OutputRef(Lane, 0);

            Output = (1.0 - Alpha) * Left + Alpha * Right;
        });
    }

    virtual ~MixThunk() {};
};


struct StereoBalanceThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 2, 0> Info = \
    {
        OpCode::BAL, "stereo\nbalance",
        {{
            {"sample", 0.0, InputCombiner::ADD},
            {"balance", 0.0, InputCombiner::ADD},
        }},
        {"left", "right"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("StereoBalanceThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Sample = Registers.CombineLaneInput(Lane, 0);
            double Balance = Registers.CombineLaneInput(Lane, 1);
            double& Left = Registers.OutputRef(Lane, 0);
            double& Right = Registers.OutputRef(Lane, 1);

            double Alpha = std::min(std::max(Balance, -1.0), 1.0) * 0.5 + 0.5;
            double InvA = 1.0 - Alpha;
            Left = Sample * InvA;
            Right = Sample * Alpha;
        });
    }

    virtual ~StereoBalanceThunk() {};
};


struct PulseThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = \
    {
        OpCode::PLS, "pulse",
        {{
            {"clock", 0.0, InputCombiner::ADD},
        }},
        {"pulse"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PulseThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Clock = Registers.CombineLaneInput(Lane, 0);
            double& Output = Registers.OutputRef(Lane, 0);
            double& Latch = Registers.ClosureRef(Lane, 0);

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
        });
    }

    virtual ~PulseThunk() {};
};


struct FlipFlopThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 2, 1> Info = \
    {
        OpCode::FLP, "flip\nflop",
        {{
            {"clock", 0.0, InputCombiner::ADD},
        }},
        {"even", "odd"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("FlipFlopThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Clock = Registers.CombineLaneInput(Lane, 0);
            double& EvenOutput = Registers.OutputRef(Lane, 0);
            double& OddOutput = Registers.OutputRef(Lane, 1);
            double& Latch = Registers.ClosureRef(Lane, 0);

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
        });
    }

    virtual ~FlipFlopThunk() {};
};


struct RandomThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 1> Info = \
    {
        OpCode::RNG, "rng",
        {{
            {"clock", 0.0, InputCombiner::ADD},
        }},
        {"#"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("RandomThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Clock = Registers.CombineLaneInput(Lane, 0);
            double& Output = Registers.OutputRef(Lane, 0);
            double& Latch = Registers.ClosureRef(Lane, 0);

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
        });
    }

    virtual ~RandomThunk() {};
};


struct GradualThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 1> Info = \
    {
        OpCode::GRAD, "grad",
        {{
            {"#", 0.0, InputCombiner::ADD},
            {"rate", 0.0, InputCombiner::ADD},
        }},
        {"#"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("GradualThunk");
        CrankLanes([&](uint32_t Lane)
        {
            if (Registers.InputConnected(0))
            {
                double Value = Registers.CombineLaneInput(Lane, 0);
                double Rate = Registers.CombineLaneInput(Lane, 1);
                double& Pos = Registers.OutputRef(Lane, 0);
                double& Initialized = Registers.ClosureRef(Lane, 0);

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
        });
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

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("TopologyPreservingTransformStateVariableFilterThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Sample = Registers.CombineLaneInput(Lane, 0);
            double Cutoff = Registers.CombineLaneInput(Lane, 1, 1000.0);
            double Resonance = Registers.CombineLaneInput(Lane, 2);
            double& Output = Registers.OutputRef(Lane, 0);
            double& LastCutoff = Registers.ClosureRef(Lane, 0);
            double& LastResonance = Registers.ClosureRef(Lane, 1);
            double& Gain = Registers.ClosureRef(Lane, 2);
            double& FeedbackDamping = Registers.ClosureRef(Lane, 3);
            // TODO: ShelfGain can be factored out for most specializations of this class
            double& ShelfGain = Registers.ClosureRef(Lane, 4);
            double& z1_A = Registers.ClosureRef(Lane, 5); // state variables (z^-1)
            double& z2_A = Registers.ClosureRef(Lane, 6);

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
        });
    }

    virtual void Reset() override
    {
        CrankLanes([&](uint32_t Lane)
        {
            Registers.ZeroOut(Lane);
            // Gain and Feedback coefficients init to 1.
            // See https://github.com/michaeldonovan/VAStateVariableFilter/blob/0e1384c62520ffcb3f321bb6ceb940472f5e152f/VAStateVariableFilter.cpp#L20
            Registers.ClosureRef(Lane, 2) = 1.0;
            Registers.ClosureRef(Lane, 3) = 1.0;
        });
    }

    virtual ~TopologyPreservingTransformStateVariableFilterThunk() {};
};


struct LowpassThunk : public TopologyPreservingTransformStateVariableFilterThunk<FilterType::Lowpass>
{
    static constexpr InstructionInfo<3, 1, 7> Info = \
    {
        OpCode::TPTSVF_LOWPASS, "low\npass",
        {{
            {"sample", 0.0, InputCombiner::ADD},
            {"cutoff", 1000.0, InputCombiner::ADD},
            {"res", 0.0, InputCombiner::ADD},
        }},
        {"lowpass"}
    };
};


struct BandpassThunk : public TopologyPreservingTransformStateVariableFilterThunk<FilterType::Bandpass>
{
    static constexpr InstructionInfo<3, 1, 7> Info = \
    {
        OpCode::TPTSVF_BANDPASS, "band\npass",
        {{
            {"sample", 0.0, InputCombiner::ADD},
            {"cutoff", 1000.0, InputCombiner::ADD},
            {"res", 0.0, InputCombiner::ADD},
        }},
        {"bandpass"}
    };
};


struct HighpassThunk : public TopologyPreservingTransformStateVariableFilterThunk<FilterType::Highpass>
{
    static constexpr InstructionInfo<3, 1, 7> Info = \
    {
        OpCode::TPTSVF_HIGHPASS, "high\npass",
        {{
            {"sample", 0.0, InputCombiner::ADD},
            {"cutoff", 1000.0, InputCombiner::ADD},
            {"res", 0.0, InputCombiner::ADD},
        }},
        {"highpass"}
    };
};


struct NotchThunk : public TopologyPreservingTransformStateVariableFilterThunk<FilterType::Notch>
{
    static constexpr InstructionInfo<3, 1, 7> Info = \
    {
        OpCode::TPTSVF_NOTCH, "notch",
        {{
            {"sample", 0.0, InputCombiner::ADD},
            {"cutoff", 1000.0, InputCombiner::ADD},
            {"res", 0.0, InputCombiner::ADD},
        }},
        {"notch"}
    };
};


struct AdsrThunk : public InstructionThunk
{
    static constexpr InstructionInfo<5, 1, 2> Info = \
    {
        OpCode::ADSR, "adsr",
        {{
            {"trigger", 0.0, InputCombiner::ADD},
            {"a", 0.1, InputCombiner::ADD},
            {"d", 0.1, InputCombiner::ADD},
            {"s", 1.0, InputCombiner::ADD},
            {"r", 1.0, InputCombiner::ADD},
        }},
        {"#"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("AdsrThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Trigger = Registers.CombineLaneInput(Lane, 0);
            const double Attack = std::max(Registers.CombineLaneInput(Lane, 1, 0.1), 0.0);
            const double Decay = std::max(Registers.CombineLaneInput(Lane, 2, 0.1), 0.0);
            const double Sustain = std::min(std::max(Registers.CombineLaneInput(Lane, 3, 1.0), 0.0), 1.0);
            const double Release = std::max(Registers.CombineLaneInput(Lane, 4, 1.0), 0.0);

            double& Amplitude = Registers.OutputRef(Lane, 0);
            double& LastTrigger = Registers.ClosureRef(Lane, 0);
            double& Mode = Registers.ClosureRef(Lane, 1);

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
        });
    }

    virtual void Retrigger(uint32_t Lane) override
    {
        // If the adsr is currently held it will retrigger this frame.
        double& LastTrigger = Registers.ClosureRef(Lane, 0);
        LastTrigger = 0.0;
    }

    virtual ~AdsrThunk() {};
};


struct QuantizeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 1, 0> Info = \
    {
        OpCode::QNTZ, "quantize",
        {{
            {"note", 0.0, InputCombiner::ADD},
            {"root", 60.0, InputCombiner::ADD}, // defaults to Middle C
            {"scale", 0.0, InputCombiner::DIRECT},
        }},
        {"note"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("QuantizeThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Note = Registers.CombineLaneInput(Lane, 0);
            const double Root = Registers.CombineLaneInput(Lane, 1, 60.0); // defaults to Middle C
            const std::vector<double*>& Intervals = Registers.InputVector(2);
            double& OutNote = Registers.OutputRef(Lane, 0);

            if (Registers.InputConnected(0) && Registers.InputConnected(2))
            {
                double Stride = 0.0;
                std::vector<double> Scale;
                Scale.reserve(Intervals.size() + 1);
                Scale.push_back(0.0);
                for (double* Register : Intervals)
                {
                    double Interval = std::max(Register[Lane], 0.0);
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
        });
    }

    virtual ~QuantizeThunk() {};
};


struct InputSequenceThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 2, 3> Info = \
    {
        OpCode::ISQN, "input\nseq",
        {{
            {"clock", 0.0, InputCombiner::ADD},
            {"input", 0.0, InputCombiner::DIRECT},
            {"restart", 0.0, InputCombiner::ADD},
        }},
        {"#", "complete"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("InputSequenceThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Clock = Registers.CombineLaneInput(Lane, 0);
            const std::vector<double*>& Sequence = Registers.InputVector(1);
            double Restart = Registers.CombineLaneInput(Lane, 2);
            double& OutValue = Registers.OutputRef(Lane, 0);
            double& OutComplete = Registers.OutputRef(Lane, 1);
            double& LastClock = Registers.ClosureRef(Lane, 0);
            double& LastRestart = Registers.ClosureRef(Lane, 1);
            double& Cursor = Registers.ClosureRef(Lane, 2);

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
                OutValue = Sequence[Index][Lane];

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
        });
    }

    virtual ~InputSequenceThunk() {};
};


struct RandomSequenceThunk : public InstructionThunk
{
    static constexpr InstructionInfo<3, 2, 4> Info = \
    {
        OpCode::RSQN, "seed\nseq",
        {{
            {"clock", 0.0, InputCombiner::ADD},
            {"period", 4.0, InputCombiner::ADD},
            {"seed", 0.0, InputCombiner::ADD},
        }},
        {"#", "complete"}
    };

    std::vector<std::vector<double>> Cache;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("RandomSequenceThunk");

        if (Cache.size() != Registers.Polyphony)
        {
            Cache.resize(Registers.Polyphony);
        }

        CrankLanes([&](uint32_t Lane)
        {
            double Clock = Registers.CombineLaneInput(Lane, 0);

            double& OutValue = Registers.OutputRef(Lane, 0);
            double& OutComplete = Registers.OutputRef(Lane, 1);
            double& LastClock = Registers.ClosureRef(Lane, 0);
            double& LastPeriod = Registers.ClosureRef(Lane, 1);
            double& LastSeed = Registers.ClosureRef(Lane, 2);
            double& Cursor = Registers.ClosureRef(Lane, 3);

            std::vector<double>& LaneCache = Cache[Lane];

            if (LastClock <= 0.0 && Clock >= 1.0)
            {
                int Period = std::max(int(Registers.CombineLaneInput(Lane, 1, 4.0)), 1);
                double Seed = Registers.CombineLaneInput(Lane, 2);

                const bool Reset = Period != int(LastPeriod) || Seed != LastSeed;
                if (Reset || int(LaneCache.size()) != Period)
                {
                    LastSeed = Seed;
                    LastPeriod = double(Period);
                    if (Reset)
                    {
                        Cursor = 0.0;
                    }
                    LaneCache.resize(Period);
                    std::mt19937 Generator;
                    Generator.seed(Seed);
                    double Low = Generator.min();
                    double Scale = 1.0 / (Generator.max() - Low);
                    for (double& Sample : LaneCache)
                    {
                        Sample = (Generator() - Low) * Scale;
                    }
                }

                int Index = int(Cursor);
                OutValue = LaneCache[Index];

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
        });
    }

    virtual ~RandomSequenceThunk() {};
};


static inline bool ChannelMatch(int Mask, int Channel)
{
    if (Mask < 0 && -Mask != Channel)
    {
        return true;
    }
    else if (Mask >= 0 && Mask == Channel)
    {
        return true;
    }
    return false;
}


struct GateThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::GATE, "gate",
        {{
            {"channel", 0.0, InputCombiner::DIRECT},
        }},
        {"gate"}
    };

    Scratch* Program;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("GateThunk");

        double* Gate = Registers.OutputPtr(0);
        for (uint32_t Lane = 0; Lane < Registers.Polyphony; ++Lane)
        {
            MidiNoteState& State = Program->MidiLanes[Lane];
            if (State.Channel == -1.0 || !Registers.InputConnected(0))
            {
                Gate[Lane] = State.Gate;
            }
            else if (Registers.InputConnected(0))
            {
                for (const double* ChannelMask : Registers.InputVector(0))
                {
                    if (ChannelMatch(int(ChannelMask[Lane]), int(State.Channel)))
                    {
                        Gate[Lane] = State.Gate;
                        break;
                    }
                }
            }
        }
    }

    virtual ~GateThunk() {};
};


struct NoteThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::NOTE, "note",
        {{
            {"channel", 0.0, InputCombiner::DIRECT},
        }},
        {"note"}
    };

    Scratch* Program;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("NoteThunk");

        double* Note = Registers.OutputPtr(0);
        for (uint32_t Lane = 0; Lane < Registers.Polyphony; ++Lane)
        {
            MidiNoteState& State = Program->MidiLanes[Lane];
            if (State.Channel == -1.0 || !Registers.InputConnected(0))
            {
                Note[Lane] = State.Note;
            }
            else if (Registers.InputConnected(0))
            {
                for (const double* ChannelMask : Registers.InputVector(0))
                {
                    if (ChannelMatch(int(ChannelMask[Lane]), int(State.Channel)))
                    {
                        Note[Lane] = State.Note;
                        break;
                    }
                }
            }
        }
    }

    virtual void Reset() override
    {
        CrankLanes([&](uint32_t Lane)
        {
            // Default last-played note until a new one is received.  This will be overwritten if the patch is migrated.
            Registers.ZeroOut(Lane);
            Registers.OutputRef(Lane, 0) = 50.0;
        });
    }

    virtual ~NoteThunk() {};
};


struct VelocityThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::VELO, "velocity",
        {{
            {"channel", 0.0, InputCombiner::DIRECT},
        }},
        {"velocity"}
    };

    Scratch* Program;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("VelocityThunk");

        double* Velocity = Registers.OutputPtr(0);
        for (uint32_t Lane = 0; Lane < Registers.Polyphony; ++Lane)
        {
            MidiNoteState& State = Program->MidiLanes[Lane];
            if (State.Channel == -1.0 || !Registers.InputConnected(0))
            {
                Velocity[Lane] = State.Velocity;
            }
            else if (Registers.InputConnected(0))
            {
                for (const double* ChannelMask : Registers.InputVector(0))
                {
                    if (ChannelMatch(int(ChannelMask[Lane]), int(State.Channel)))
                    {
                        Velocity[Lane] = State.Velocity;
                        break;
                    }
                }
            }
        }
    }

    virtual ~VelocityThunk() {};
};


struct PressureThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::PRES, "pressure",
        {{
            {"channel", 0.0, InputCombiner::DIRECT},
        }},
        {"pressure"}
    };

    Scratch* Program;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PressureThunk");

        double* Pressure = Registers.OutputPtr(0);
        for (uint32_t Lane = 0; Lane < Registers.Polyphony; ++Lane)
        {
            MidiNoteState& State = Program->MidiLanes[Lane];
            if (State.Channel == -1.0 || !Registers.InputConnected(0))
            {
                Pressure[Lane] = State.Pressure;
            }
            else if (Registers.InputConnected(0))
            {
                for (const double* ChannelMask : Registers.InputVector(0))
                {
                    if (ChannelMatch(int(ChannelMask[Lane]), int(State.Channel)))
                    {
                        Pressure[Lane] = State.Pressure;
                        break;
                    }
                }
            }
        }
    }

    virtual ~PressureThunk() {};
};


struct ControlChangeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<2, 1, 0> Info = \
    {
        OpCode::CTRL, "control\nchange",
        {{
            {"control", 0.0, InputCombiner::ADD},
            {"channel", 0.0, InputCombiner::DIRECT},
        }},
        {"value"}
    };

    Scratch* Program;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("ControlChangeThunk");

        for (uint32_t Lane = 0; Lane < Registers.Polyphony; ++Lane)
        {
            double Control = Registers.CombineLaneInput(Lane, 0);
            double* Value = Registers.OutputPtr(0);
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
                    if (ChannelMatch(int(ChannelMask[Lane]), int(State.Channel)))
                    {
                        Channel = State.Channel;
                        break;
                    }
                }
            }
            if (Channel >= 0.0 && Channel < 16.0 && Control >= 0.0 && Control < 128.0)
            {
                Value[Lane] = Program->ChannelControls[uint8_t(Channel)][uint8_t(Control)];
            }
        }
    }

    virtual ~ControlChangeThunk() {};
};


struct KikiThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::KIKI, "kiki",
        {{
            {"channel", 0.0, InputCombiner::DIRECT},
        }},
        {"kiki"}
    };

    Scratch* Program;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("KikiThunk");

        double* Kiki = Registers.OutputPtr(0);
        for (uint32_t Lane = 0; Lane < Registers.Polyphony; ++Lane)
        {
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
                    if (ChannelMatch(int(ChannelMask[Lane]), int(State.Channel)))
                    {
                        Channel = State.Channel;
                        break;
                    }
                }
            }
            if (Channel >= 0.0 && Channel < 16)
            {
                uint8_t ProgramNumber = Program->ChannelPrograms[uint8_t(Channel)];
                Kiki[Lane] = KikiTable[ProgramNumber];
            }
        }
    }

    virtual void Reset() override
    {
        CrankLanes([&](uint32_t Lane)
        {
            // Default last-played note until a new one is received.  This will be overwritten if the patch is migrated.
            Registers.ZeroOut(Lane);
            Registers.OutputRef(Lane, 0) = KikiTable[0];
        });
    }

    virtual ~KikiThunk() {};
};


struct PitchBendThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::BEND, "bend",
        {{
            {"channel", 0.0, InputCombiner::DIRECT},
        }},
        {"bend"}
    };

    Scratch* Program;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("PitchBendThunk");

        double* PitchBend = Registers.OutputPtr(0);
        for (uint32_t Lane = 0; Lane < Registers.Polyphony; ++Lane)
        {
            MidiNoteState& State = Program->MidiLanes[Lane];
            if (State.Channel == -1.0 || !Registers.InputConnected(0))
            {
                PitchBend[Lane] = Program->ChannelPitchBend[uint8_t(State.Channel)];
            }
            else if (Registers.InputConnected(0))
            {
                for (const double* ChannelMask : Registers.InputVector(0))
                {
                    if (ChannelMatch(int(ChannelMask[Lane]), int(State.Channel)))
                    {
                        PitchBend[Lane] = Program->ChannelPitchBend[uint8_t(State.Channel)];
                        break;
                    }
                }
            }
        }
    }

    virtual ~PitchBendThunk() {};
};


struct LeadLaneThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::LEAD_LANE, "lead\nlane",
        {{
            {"lane\nvalue", 0.0, InputCombiner::DIRECT},
        }},
        {"lead\nlane\nvalue"}
    };

    Scratch* Program;

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual bool LaneJoiner() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("LeadLaneThunk");

        uint32_t ReadLane = uint32_t(Program->MostRecentLane);
        if (ReadLane < Registers.Polyphony)
        {
            double Value = Registers.CombineLaneInput(ReadLane, 0);
            Registers.OutputRef(0) = Value;
        }
    }

    virtual void Reset() override
    {
        const uint32_t Lane = 0;
        Registers.ZeroOut(Lane);
    }

    virtual ~LeadLaneThunk() {};
};


struct AddLanesThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::ADD_LANES, "add\nlanes",
        {{
            {"+", 0.0, InputCombiner::ADD},
        }},
        {"="}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual bool LaneJoiner() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("AddLanesThunk");
        Registers.OutputRef(0) = Registers.CombinePolyphonicInput(0, 0.0, CombinerAdd);
    }

    virtual void Reset() override
    {
        const uint32_t Lane = 0;
        Registers.ZeroOut(Lane);
    }

    virtual ~AddLanesThunk() {};
};


struct MidiToHzThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::MIDI_HZ, "midi\nto hz",
        {{
            {"note", 69.0, InputCombiner::ADD}, // 440 hz
        }},
        {"hz"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("MidiToHzThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Note = Registers.CombineLaneInput(Lane, 0);
            double& Output = Registers.OutputRef(Lane, 0);
            Output = MidiNoteToHz(Note);
        });
    }

    virtual ~MidiToHzThunk() {};
};


struct LoudnessFudgeThunk : public InstructionThunk
{
    static constexpr InstructionInfo<1, 1, 0> Info = \
    {
        OpCode::LOUD_FUDGE, "loud\nfudge",
        {{
            {"hz", 440.0, InputCombiner::ADD},
        }},
        {"amp"}
    };

    virtual bool Polyphonic() override
    {
        return true;
    }

    virtual void Crank(double SampleInterval) override
    {
        THUNK_TRACEABLE_NAMED_SCOPE("LoudnessFudgeThunk");
        CrankLanes([&](uint32_t Lane)
        {
            double Hz = Registers.CombineLaneInput(Lane, 0);
            double& Output = Registers.OutputRef(Lane, 0);
            Output = PerceptualAmplitudeCorrectionByHz(Hz);
        });
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
        const uint32_t Lane = 0;
        Registers.OutputRef(Lane, 0) = Input->Get();
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
        const uint32_t Lane = 0;
        Registers.OutputRef(Lane, 0) = Input->Get();
    }

    virtual ~TweakThunk() {};
};


struct TapeLoopThunk : public InstructionThunk
{
    static constexpr InstructionInfo<4, 1, 4> Info = \
    {
        OpCode::TAPE_LOOP, "tape\nloop",
        {{
            {"sample", 0.0, InputCombiner::ADD},
            {"read\nstart", 0.0, InputCombiner::ADD},
            {"length", 0.0, InputCombiner::ADD},
            {"reset", 0.0, InputCombiner::ADD},
        }},
        {"sample"}
    };

    std::vector<MagicTapeUniquePtr>* TapeFile;
    std::ptrdiff_t TapeIndex;

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

        BlankTape* Tape;
        {
            MagicTapeUniquePtr& Found = TapeFile->at(TapeIndex);
            Tape = (BlankTape*)Found.get();
        }

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


SymbolInfo::SymbolInfo()
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

SymbolInfo SymbolInfoMap;
