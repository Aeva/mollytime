#ifdef ENABLE_JACK

#include "audio_backend.h"
#include "jack_stream.h"

#include <cassert>
#include <print>

#include <jack/jack.h>


// ---


JackRealTimeThread::JackRealTimeThread(jack_client_t* JackClient, JackThreadShared* JackBufferState, int SampleRate)
{
    assert(JackClient != nullptr);
    std::println("jack client is good");

    assert(JackBufferState != nullptr);
    std::println("buffer staté is good");

    BufferState = JackBufferState;
    SampleInterval = 1.0 / double(SampleRate);
    ResetFramePressure();

    JackBufferState->OutputPorts.clear();
    JackBufferState->OutputPorts.resize(2, nullptr);

    JackBufferState->OutputPorts[0] = jack_port_register(
        JackClient, "output_FL", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    assert(JackBufferState->OutputPorts[0] != nullptr);
    std::println("port 0 is good");

    JackBufferState->OutputPorts[1] = jack_port_register(
        JackClient, "output_FR", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    std::println("port 1 is good");

    for (jack_port_t* Port : JackBufferState->OutputPorts)
    {
        if (Port == nullptr)
        {
            throw std::runtime_error("Unable to create jack ports!\n");
        }
    }

    if (jack_activate(JackClient))
    {
        throw std::runtime_error("Unable to activate jack client!\n");
    }

    const char** Ports = jack_get_ports(JackClient, nullptr, nullptr, JackPortIsPhysical|JackPortIsInput);
    if (Ports == nullptr)
    {
        throw std::runtime_error("No physical playback ports!\n");
    }

    for (int PortIndex = 0; Ports[PortIndex] != nullptr && PortIndex < 2; ++PortIndex)
    {
        const char* ProgramOut = jack_port_name(JackBufferState->OutputPorts[PortIndex]);
        if (jack_connect(JackClient, ProgramOut, Ports[PortIndex]))
        {
            // unable to connect to physical port
        }
    }

    jack_free(Ports);
}


int JackRealTimeThread::OnProcess(uint32_t FrameCount, void *UserData)
{
    JackRealTimeThread* RealTimeThread = (JackRealTimeThread*)UserData;
    return RealTimeThread->OnProcessInner(FrameCount);
}


int JackRealTimeThread::OnProcessInner(uint32_t FrameCount)
{
    static_assert(std::is_same_v<float, jack_default_audio_sample_t>);
    TRACEABLE_SCOPE;
    FramePointers Frame;
    Frame.SampleCount = size_t(FrameCount);

    // This will invoke JackRealTimeThread::BeginFrame
    AdvanceFrames(Frame);
    return 0;
}


void JackRealTimeThread::BeginFrame(FramePointers& Frame)
{
    const jack_nframes_t FrameCount = jack_nframes_t(Frame.SampleCount);
    JackThreadShared* JackBufferState = (JackThreadShared*)BufferState;

    // All "input" jack ports are guaranteed to correspond to a program input.
    for (const auto& [Tile, JackPort] : JackBufferState->InputPorts)
    {
        double* WritePtr = Program->Inputs.at(Tile)->DangerGet();
        Frame.InPtrs.emplace_back((float*)jack_port_get_buffer(JackPort, FrameCount), WritePtr);
    }
    if (JackBufferState->OutputPorts.size() >= 2)
    {
        Frame.OutLeft = (float*)jack_port_get_buffer(JackBufferState->OutputPorts[0], FrameCount);
        Frame.OutRight = (float*)jack_port_get_buffer(JackBufferState->OutputPorts[1], FrameCount);
    }
    // All "aux" jack ports are always guaranteed to correspond to an "aux" program output.
    for (const auto& [Tile, JackPort] : JackBufferState->AuxOutPorts)
    {
        double* ReadPtr = Program->AuxOutputs.at(Tile)->DangerGet();
        Frame.AuxPtrs.emplace_back(ReadPtr, (float*)jack_port_get_buffer(JackPort, FrameCount));
    }
}


// ---


static jack_client_t* OpenJackClient(const char*& ClientName)
{
    jack_status_t JackStatus;
    jack_options_t JackOptions = JackNoStartServer;
    jack_client_t* JackClient = jack_client_open(ClientName, JackOptions, &JackStatus, nullptr);
    if (JackClient == nullptr)
    {
        throw std::runtime_error(std::format("jack_client_open() failed, jack status = {}\n", (int)JackStatus));
    }
    if (JackStatus & JackNameNotUnique)
    {
        ClientName = jack_get_client_name(JackClient);
    }

    return JackClient;
}


JackStream::JackStream(int SampleRate) :
    JackClient(OpenJackClient(ClientName)),
    RealTimeThread(JackClient, &BufferState, SampleRate)
{
    static_assert(std::is_same_v<jack_nframes_t, uint32_t>);
    jack_set_process_callback(JackClient, JackRealTimeThread::OnProcess, &RealTimeThread);
}


JackStream::~JackStream()
{
    if(JackClient != nullptr)
    {
        jack_client_close(JackClient);
        JackClient = nullptr;
    }
}


float JackStream::GetTemporalPressure()
{
    TRACEABLE_SCOPE;
    return BufferState.TemporalPressure.load();
}


void JackStream::ProgramChange(ScratchSharedPtr& NewProgram)
{
    TRACEABLE_SCOPE;
    TRACEABLE_LOCK_GUARD(BufferState.Mutex);
    BufferState.PendingProgram = NewProgram;

    {
        // Remove all jack input ports that no longer correspond to patch input ports.
        std::vector<TileHandle> Erased;
        for (const auto& [Tile, JackPort] : BufferState.InputPorts)
        {
            if (!NewProgram->Inputs.contains(Tile))
            {
                Erased.push_back(Tile);
                jack_port_unregister(JackClient, JackPort);
            }
        }
        for (TileHandle Tile : Erased)
        {
            BufferState.InputPorts.erase(Tile);
        }
    }
    {
        // Remove all jack aux ports that no longer correspond to patch aux ports.
        std::vector<TileHandle> Erased;
        for (const auto& [Tile, JackPort] : BufferState.AuxOutPorts)
        {
            if (!NewProgram->AuxOutputs.contains(Tile))
            {
                Erased.push_back(Tile);
                jack_port_unregister(JackClient, JackPort);
            }
        }
        for (TileHandle Tile : Erased)
        {
            BufferState.AuxOutPorts.erase(Tile);
        }
    }
    {
        // Create jack input ports for any new patch input ports.
        for (const auto& [Tile, InputRegister] : NewProgram->Inputs)
        {
            if (!BufferState.InputPorts.contains(Tile))
            {
                std::string Name = std::format("in {}", Tile);
                jack_port_t* JackPort = jack_port_register(
                    JackClient, Name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
                if (JackPort)
                {
                    BufferState.InputPorts[Tile] = JackPort;
                }
            }
        }
    }
    {
        // Create jack aux ports for any new patch aux ports.
        for (const auto& [Tile, OutputRegister] : NewProgram->AuxOutputs)
        {
            if (!BufferState.AuxOutPorts.contains(Tile))
            {
                std::string Name = std::format("aux {}", Tile);
                jack_port_t* JackPort = jack_port_register(
                    JackClient, Name.c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
                if (JackPort)
                {
                    BufferState.AuxOutPorts[Tile] = JackPort;
                }
            }
        }
    }
}

#endif