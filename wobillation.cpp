
#include <math.h>

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

#include <vector>
#include <print>
#include <atomic>


constinit double Tau = M_PI * 2.0;
bool PipeWireInitialized = false;
struct PipeWireStream* PipeWireSession = nullptr;


struct ThreadShared
{
    std::atomic<double> Frequency = 440.0;
};


struct StreamRealTimeThread
{
    void SetupPorts(ThreadShared* InBufferState, pw_stream* Stream, int SampleRate);

    static void OnProcess(void *UserData);

private:
    ThreadShared* BufferState = nullptr;
    pw_stream* Stream = nullptr;

    double SampleInterval = 0.0f;
    double Phase = 0.0f;

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
    double Frequency = BufferState->Frequency.load();

    for (int Frame = 0; Frame < FrameCount; ++Frame)
    {
        Phase += Tau * Frequency * SampleInterval;
        if (Phase > Tau)
        {
            Phase -= Tau;
        }

        WritePtr[Frame] = sin(Phase) * 0.5;
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

    PipeWireStream(int SampleRate, double Frequency);

    void Tune(double NewFrequency);

    void Run();

    void Reset();

    ~PipeWireStream();

private:
    StreamRealTimeThread RealTimeThread;
    ThreadShared BufferState;
};


PipeWireStream::PipeWireStream(int SampleRate, double Frequency)
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

    Tune(Frequency);
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


void PipeWireStream::Tune(double Frequency)
{
    BufferState.Frequency.store(Frequency);
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
void init(double Frequency)
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
        PipeWireSession = new PipeWireStream(44100, Frequency);
        PipeWireSession->Run();
    }
}


extern "C"
void tune(double Frequency)
{
    if (PipeWireSession != nullptr)
    {
        PipeWireSession->Tune(Frequency);
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
