
#include <math.h>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include <vector>
#include <print>
#include <mutex>


constinit double Tau = M_PI * 2.0;

bool PipeWireInitialized = false;
struct PipeWireStream* PipeWireSession = nullptr;


struct ThreadShared
{
    std::mutex Mutex;
    double CarrierHz = 440.0;
    double ModulatorHz = 440.0;
    double Feedback = 0.0;
};


struct StreamRealTimeThread
{
    void SetupPorts(ThreadShared* InBufferState, pw_stream* Stream, int SampleRate);

    static void OnProcess(void *UserData);

private:
    ThreadShared* BufferState = nullptr;
    pw_stream* Stream = nullptr;
    double SampleInterval = 0.0;

    double CarrierHz = 440.0;
    double ModulatorHz = 440.0;
    double Feedback = 0.0;

    double CarrierPhase = 0.0;
    double ModulatorPhase = 0.0;
    double LastSample = 0.0;

    void OnProcessInner();
};


void StreamRealTimeThread::SetupPorts(ThreadShared* InBufferState, pw_stream* InStream, int SampleRate)
{
    BufferState = InBufferState;
    Stream = InStream;

    CarrierHz = BufferState->CarrierHz;
    ModulatorHz = BufferState->ModulatorHz;
    Feedback = BufferState->Feedback;

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

        CarrierHz = BufferState->CarrierHz;
        ModulatorHz = BufferState->ModulatorHz;
        Feedback = BufferState->Feedback;
    }

    double Modulation = 0.0;

    for (int Frame = 0; Frame < FrameCount; ++Frame)
    {
        Modulation = LastSample * Feedback;
        ModulatorPhase += Tau * (ModulatorHz + ModulatorHz * Modulation) * SampleInterval;
        if (ModulatorPhase > Tau)
        {
            ModulatorPhase -= Tau;
        }

        Modulation = sin(ModulatorPhase) * 0.5;
        CarrierPhase += Tau * (CarrierHz + CarrierHz * Modulation) * SampleInterval;
        if (CarrierPhase > Tau)
        {
            CarrierPhase -= Tau;
        }

        LastSample = sin(CarrierPhase);
        WritePtr[Frame] = float(LastSample * 0.5);
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

    PipeWireStream(int SampleRate, double CarrierHz, double ModulatorHz);

    void Tune(int Index, double Frequency);

    void SetFeedback(double Amount);

    void Run();

    void Reset();

    ~PipeWireStream();

private:
    StreamRealTimeThread RealTimeThread;
    ThreadShared BufferState;
};


PipeWireStream::PipeWireStream(int SampleRate, double CarrierHz, double ModulatorHz)
{
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

    Tune(0, CarrierHz);
    Tune(1, ModulatorHz);
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


void PipeWireStream::Tune(int Index, double Frequency)
{
    std::lock_guard<std::mutex> Lock(BufferState.Mutex);
    switch (Index)
    {
    case 0:
        BufferState.CarrierHz = Frequency;
        break;
    case 1:
        BufferState.ModulatorHz = Frequency;
        break;
    default:
        break;
    }
}


void PipeWireStream::SetFeedback(double Amount)
{
    std::lock_guard<std::mutex> Lock(BufferState.Mutex);
    BufferState.Feedback = Amount;
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
void init(double CarrierHz, double ModulatorHz)
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
        PipeWireSession = new PipeWireStream(44100, CarrierHz, ModulatorHz);
        PipeWireSession->Run();
    }
}


extern "C"
void tune(int Index, double Frequency)
{
    if (PipeWireSession != nullptr)
    {
        PipeWireSession->Tune(Index, Frequency);
    }
}


extern "C"
void set_feedback(double Amount)
{
    if (PipeWireSession != nullptr)
    {
        PipeWireSession->SetFeedback(Amount);
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
