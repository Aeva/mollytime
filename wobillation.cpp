
#include <math.h>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include <vector>
#include <print>
#include <mutex>
#include <cstring>


constinit double Tau = M_PI * 2.0;

bool PipeWireInitialized = false;
struct PipeWireStream* PipeWireSession = nullptr;


enum class OpCode : std::uint8_t
{
    Var,
    Sin,
    Mul,
    Add
};


struct SynthInstruction
{
    OpCode Name;
    std::uint16_t Param1;
    std::uint16_t Param2;

    SynthInstruction(OpCode InName)
        : Name(InName)
        , Param1(0)
        , Param2(0)
    {
    }

    SynthInstruction(OpCode InName, std::uint16_t InParam1)
        : Name(InName)
        , Param1(InParam1)
        , Param2(0)
    {
    }

    SynthInstruction(OpCode InName, std::uint16_t InParam1, std::uint16_t InParam2)
        : Name(InName)
        , Param1(InParam1)
        , Param2(InParam2)
    {
    }
};


struct SynthProgram
{
    int VariableCount = 0;
    std::vector<SynthInstruction> Program;
    std::vector<double> Phases;
    std::vector<double> Intermediaries;

    void Commit(std::vector<double>& Variables);

    std::uint16_t PushVar(double Value);
    std::uint16_t PushSin(std::uint16_t Param1);
    std::uint16_t PushMul(std::uint16_t Param1, std::uint16_t Param2);
    std::uint16_t PushAdd(std::uint16_t Param1, std::uint16_t Param2);

    double Eval(const double SampleInterval);
};


void SynthProgram::Commit(std::vector<double>& Variables)
{
    Variables.clear();
    Variables.resize(VariableCount, 0.0);
    for (int Index = 0; Index < VariableCount; ++Index)
    {
        Variables[Index] = Intermediaries[Index];
    }

    int ProgramCounter = 0;
    for (const SynthInstruction& Instruction : Program)
    {
        if (Instruction.Name == OpCode::Sin)
        {
            if (!(Instruction.Param1 < Intermediaries.size()))
            {
                std::print("{}: Oscillator Hz param is out of bounds!!!\n", ProgramCounter);
            }
            if (!(Instruction.Param2 < Phases.size()))
            {
                std::print("{}: Oscillator phase index is out of bounds!!!\n", ProgramCounter);
            }
        }
        else if (Instruction.Name == OpCode::Mul)
        {
            if (!(Instruction.Param1 < Intermediaries.size()))
            {
                std::print("{}: Mul LHS param is out of bounds!!!\n", ProgramCounter);
            }
            if (!(Instruction.Param2 < Intermediaries.size()))
            {
                std::print("{}: Mul RHS param is out of bounds!!!\n", ProgramCounter);
            }
        }
        else if (Instruction.Name == OpCode::Add)
        {
            if (!(Instruction.Param1 < Intermediaries.size()))
            {
                std::print("{}: Add LHS param is out of bounds!!!\n", ProgramCounter);
            }
            if (!(Instruction.Param2 < Intermediaries.size()))
            {
                std::print("{}: Add RHS param is out of bounds!!!\n", ProgramCounter);
            }
        }
        ++ProgramCounter;
    }
}


std::uint16_t SynthProgram::PushVar(double Value)
{
    ++VariableCount;
    std::uint16_t Handle = (std::uint16_t)Program.size();
    Program.emplace_back(OpCode::Var);
    Intermediaries.push_back(Value);
    return Handle;
}


std::uint16_t SynthProgram::PushSin(std::uint16_t Param1)
{
    std::uint16_t Handle = (std::uint16_t)Program.size();
    Program.emplace_back(OpCode::Sin, Param1, (std::uint16_t)Phases.size());
    Phases.push_back(0.0);
    Intermediaries.push_back(0.0);
    return Handle;
}


std::uint16_t SynthProgram::PushMul(std::uint16_t Param1, std::uint16_t Param2)
{
    std::uint16_t Handle = (std::uint16_t)Program.size();
    Program.emplace_back(OpCode::Mul, Param1, Param2);
    Intermediaries.push_back(0.0);
    return Handle;
}


std::uint16_t SynthProgram::PushAdd(std::uint16_t Param1, std::uint16_t Param2)
{
    std::uint16_t Handle = (std::uint16_t)Program.size();
    Program.emplace_back(OpCode::Add, Param1, Param2);
    Intermediaries.push_back(0.0);
    return Handle;
}


double SynthProgram::Eval(const double SampleInterval)
{
    for (int ProgramCounter = VariableCount; ProgramCounter < Program.size(); ++ProgramCounter)
    {
        const SynthInstruction& Instruction = Program[ProgramCounter];
        if (Instruction.Name == OpCode::Sin)
        {
            const double Hz = Intermediaries[Instruction.Param1];
            double Phase = Phases[Instruction.Param2];

            Phase += Tau * Hz * SampleInterval;
            if (Phase > Tau)
            {
                Phase -= Tau;
            }

            Phases[Instruction.Param2] = Phase;
            Intermediaries[ProgramCounter] = sin(Phase);
        }
        else if (Instruction.Name == OpCode::Mul)
        {
            const double LHS = Intermediaries[Instruction.Param1];
            const double RHS = Intermediaries[Instruction.Param2];
            Intermediaries[ProgramCounter] = LHS * RHS;
        }
        else if (Instruction.Name == OpCode::Add)
        {
            const double LHS = Intermediaries[Instruction.Param1];
            const double RHS = Intermediaries[Instruction.Param2];
            Intermediaries[ProgramCounter] = LHS + RHS;
        }
    }

    return Intermediaries.back();
}


struct ThreadShared
{
    std::mutex Mutex;
    std::vector<double> Variables;
    SynthProgram* PendingProgram = nullptr;
};


struct StreamRealTimeThread
{
    void SetupPorts(ThreadShared* InBufferState, pw_stream* Stream, int SampleRate);

    static void OnProcess(void *UserData);

private:
    ThreadShared* BufferState = nullptr;
    pw_stream* Stream = nullptr;
    double SampleInterval = 0.0;

    SynthProgram* Program = nullptr;

    void OnProcessInner();
};


void StreamRealTimeThread::SetupPorts(ThreadShared* InBufferState, pw_stream* InStream, int SampleRate)
{
    BufferState = InBufferState;
    Stream = InStream;
    SampleInterval = 1.0 / double(SampleRate);
}


void StreamRealTimeThread::OnProcess(void *UserData)
{
    StreamRealTimeThread* RealTimeThread = (StreamRealTimeThread*)UserData;
    RealTimeThread->OnProcessInner();
}


void StreamRealTimeThread::OnProcessInner()
{
    pw_buffer* StreamBuffer = pw_stream_dequeue_buffer(Stream);
    if (!StreamBuffer)
    {
        return;
    }

    struct spa_data& StreamMetaData = StreamBuffer->buffer->datas[0];

    int Stride = sizeof(float); // multiply by channel count if you convert this to stereo
    int FrameCount = StreamMetaData.maxsize / Stride;

    if (StreamBuffer->requested)
    {
        FrameCount = SPA_MIN(StreamBuffer->requested, FrameCount);
    }

    float* WritePtr = (float*)StreamMetaData.data;

    {
        std::lock_guard<std::mutex> Lock(BufferState->Mutex);
        if (BufferState->PendingProgram)
        {
            if (Program)
            {
                delete Program;
            }
            Program = BufferState->PendingProgram;
            Program->Commit(BufferState->Variables);
            BufferState->PendingProgram = nullptr;
        }
        else
        {
            size_t Bytes = sizeof(double) * BufferState->Variables.size();
            memcpy(Program->Intermediaries.data(), BufferState->Variables.data(), Bytes);
        }
    }

    if (Program)
    {
        for (int Frame = 0; Frame < FrameCount; ++Frame)
        {
            WritePtr[Frame] = float(Program->Eval(SampleInterval));
        }
    }
    else
    {
        for (int Frame = 0; Frame < FrameCount; ++Frame)
        {
            WritePtr[Frame] = 0.0f;
        }
    }

    StreamMetaData.chunk->offset = 0;
    StreamMetaData.chunk->stride = Stride;
    StreamMetaData.chunk->size = FrameCount * Stride;

    pw_stream_queue_buffer(Stream, StreamBuffer);
}


struct PipeWireStream
{
    struct pw_thread_loop* Loop = nullptr;
    struct pw_stream* Stream = nullptr;

    PipeWireStream(int SampleRate);

    void Run();

    void ProgramChange(SynthProgram* PendingProgram);

    void Reset();

    ~PipeWireStream();

private:
    StreamRealTimeThread RealTimeThread;
    ThreadShared BufferState;
};


PipeWireStream::PipeWireStream(int SampleRate)
{
    {
        BufferState.PendingProgram = new SynthProgram();
        SynthProgram& Program = *BufferState.PendingProgram;
        std::uint16_t FrequencyHz = Program.PushVar(440.0);
        std::uint16_t Volume = Program.PushVar(0.0);
        std::uint16_t Oscillator = Program.PushSin(FrequencyHz);
        std::uint16_t Output = Program.PushMul(Oscillator, Volume);
    }

    std::vector<const spa_pod*> Params;

    uint8_t BuilderBuffer[1024];
    spa_pod_builder PodBuilder = SPA_POD_BUILDER_INIT(BuilderBuffer, sizeof(BuilderBuffer));

    Loop = pw_thread_loop_new("convolver", nullptr);
    pw_thread_loop_lock(Loop);

    static const pw_stream_events StreamEvents =
    {
        .version = PW_VERSION_STREAM_EVENTS,
        .process = StreamRealTimeThread::OnProcess,
    };

    Stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(Loop),
        "wobillation",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Playback",
            PW_KEY_MEDIA_ROLE, "Music",
            nullptr),
        &StreamEvents,
        &RealTimeThread);

    RealTimeThread.SetupPorts(&BufferState, Stream, SampleRate);

    {
        spa_audio_info_raw StreamFormat =
        {
            .format = SPA_AUDIO_FORMAT_DSP_F32,
            .rate = (uint32_t)SampleRate,
            .channels = 1
        };
        Params.push_back(spa_format_audio_raw_build(&PodBuilder, SPA_PARAM_EnumFormat, &StreamFormat));
    }

    pw_thread_loop_unlock(Loop);

    {
        int Flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS;
        int StatusCode = pw_stream_connect(
            Stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, (enum pw_stream_flags)Flags, Params.data(), Params.size());

        if (StatusCode < 0)
        {
            std::print("Can't connect pipewire stream :(\n");
            Reset();
        }
    }
}


void PipeWireStream::ProgramChange(SynthProgram* PendingProgram)
{
    std::lock_guard<std::mutex> Lock(BufferState.Mutex);
    if (BufferState.PendingProgram)
    {
        delete BufferState.PendingProgram;
    }
    BufferState.PendingProgram = PendingProgram;
}


void PipeWireStream::Run()
{
    if (Loop && Stream)
    {
        pw_thread_loop_lock(Loop);
        pw_thread_loop_start(Loop);
        pw_thread_loop_unlock(Loop);
    }
}


void PipeWireStream::Reset()
{
    if (Loop)
    {
        pw_thread_loop_lock(Loop);
    }
    if (Stream)
    {
        pw_stream_destroy(Stream);
        Stream = nullptr;
    }
    if (Loop)
    {
        pw_thread_loop_unlock(Loop);
        pw_thread_loop_stop(Loop);
        pw_thread_loop_destroy(Loop);
        Loop = nullptr;
    }
}


PipeWireStream::~PipeWireStream()
{
    Reset();
}


extern "C"
void init()
{
    if (!PipeWireInitialized)
    {
        PipeWireInitialized = true;
        int argc = 0;
        char*** argv = nullptr;
        pw_init(&argc, argv);
    }

    if (PipeWireSession == nullptr)
    {
        PipeWireSession = new PipeWireStream(44100);
        PipeWireSession->Run();
    }
}


static SynthProgram* IncompleteProgram = nullptr;


extern "C"
void clear()
{
    if (PipeWireSession != nullptr)
    {
        if (IncompleteProgram != nullptr)
        {
            delete IncompleteProgram;
        }
        IncompleteProgram = new SynthProgram();
    }
    else
    {
        std::print("invalid use of clear\n");
    }
}


extern "C"
int push_var(double InitValue)
{
    if (IncompleteProgram != nullptr)
    {
        //std::print("push_var({})\n", InitValue);
        return IncompleteProgram->PushVar(InitValue);
    }
    std::print("invalid use of push_var\n");
    return -1;
}


extern "C"
int push_sin(std::uint16_t Frequency)
{
    if (IncompleteProgram != nullptr)
    {
        //std::print("push_sin({})\n", Frequency);
        return IncompleteProgram->PushSin(Frequency);
    }
    std::print("invalid use of push_sin\n");
    return -1;
}


extern "C"
int push_mul(std::uint16_t LHS, std::uint16_t RHS)
{
    if (IncompleteProgram != nullptr)
    {
        //std::print("push_mul({}, {})\n", LHS, RHS);
        return IncompleteProgram->PushMul(LHS, RHS);
    }
    std::print("invalid use of push_mul\n");
    return -1;
}


extern "C"
int push_add(std::uint16_t LHS, std::uint16_t RHS)
{
    if (IncompleteProgram != nullptr)
    {
        //std::print("push_add({}, {})\n", LHS, RHS);
        return IncompleteProgram->PushAdd(LHS, RHS);
    }
    std::print("invalid use of push_add\n");
    return -1;
}


extern "C"
void commit_program()
{
    if (PipeWireSession != nullptr && IncompleteProgram != nullptr)
    {
        PipeWireSession->ProgramChange(IncompleteProgram);
        IncompleteProgram = nullptr;
    }
    else
    {
        std::print("invalid use of commit_program\n");
    }
}


extern "C"
void halt()
{
    if (PipeWireSession != nullptr)
    {
        delete PipeWireSession;
        PipeWireSession = nullptr;
    }
    if (PipeWireInitialized)
    {
        PipeWireInitialized = false;
        pw_deinit();
    }
}
