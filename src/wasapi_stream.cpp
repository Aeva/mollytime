
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

#ifdef AUDIO_WASAPI

#include "wasapi_stream.h"

#include <cassert>
#include <span>


#define CheckHResult winrt::check_hresult

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
#pragma clang diagnostic pop


WasapiRealTimeThread::WasapiRealTimeThread(WasapiThreadShared* WasapiBufferState, int SampleRate)
{
    assert(WasapiBufferState != nullptr);
    assert(SampleRate != 0);

    BufferState = WasapiBufferState;
    ResetFramePressure();

    // Entering the "COM zone."
    CheckHResult(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    // Get the default audio device (hardware endpoint).
    ComPtr<IMMDeviceEnumerator> Enumerator;
    Enumerator.capture(CoCreateInstance, CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL);

    CheckHResult(Enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, Device.put()));

    // Activate and initialize an audio client (stream to hardware endpoint).
    CheckHResult(Device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, AudioClient.put_void()));

    REFERENCE_TIME DevicePeriod;
    CheckHResult(AudioClient->GetDevicePeriod(nullptr, &DevicePeriod));

    WAVEFORMATEX *WaveFormat;
    CheckHResult(AudioClient->GetMixFormat(&WaveFormat));
    assert(WaveFormat != nullptr);

    CheckHResult(AudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,           // Non-exclusive audio output.
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,  // Use a waitable Event to signal when more audio is needed.
        DevicePeriod,
        DevicePeriod,
        WaveFormat,
        nullptr
    ));

    CoTaskMemFree(WaveFormat);

    // Fire this event when more audio is needed.
    // Initialize it in the "signaled" state, so that our first loop iteration fills the audio buffer.
    EventHandle = CreateEvent(nullptr, FALSE, TRUE, nullptr);
    CheckHResult(AudioClient->SetEventHandle(EventHandle));

    // Initialize intermediate stereo buffers. (We'll need to interleave samples before sending to the WASAPI output buffer.)
    CheckHResult(AudioClient->GetBufferSize(&BufferSize));
    WasapiBufferState->OutputSamplesLeft.resize(BufferSize);
    WasapiBufferState->OutputSamplesRight.resize(BufferSize);

    // Set up a render client (output interface to the IAudioClient stream).
    RenderClient.capture(AudioClient, &IAudioClient::GetService);

    // Set up the audio clock.
    AudioClock.capture(AudioClient, &IAudioClient::GetService);

    // Calculate the sample interval from the reported clock frequency.
    uint64_t ClockFrequency;
    CheckHResult(AudioClock->GetFrequency(&ClockFrequency)); // bytes per second
    SampleInterval = double(sizeof(double)) / double(ClockFrequency);

    // Ready to go! Start streaming in separate thread.
    AudioClient->Start();
    new(&LoopThread) std::thread(&WasapiRealTimeThread::Loop, this);
}


WasapiRealTimeThread::~WasapiRealTimeThread()
{
    // Join the audio thread, ending the stream.
    IsRunning.store(false);
    LoopThread.join();

    // End the audio stream.
    AudioClient->Stop();

    // Exit the COM zone.
    CoUninitialize();
}


void WasapiRealTimeThread::BeginFrame(FramePointers& Frame)
{
    assert(BufferState != nullptr);

    WasapiThreadShared& WasapiBufferState = static_cast<WasapiThreadShared &>(*BufferState);
    assert(static_cast<ptrdiff_t>(WasapiBufferState.OutputSamplesLeft.size()) >= Frame.SampleCount);
    assert(static_cast<ptrdiff_t>(WasapiBufferState.OutputSamplesRight.size()) >= Frame.SampleCount);

    Frame.OutLeft = WasapiBufferState.OutputSamplesLeft.data();
    Frame.OutRight = WasapiBufferState.OutputSamplesRight.data();
}


void WasapiRealTimeThread::EndFrame(FramePointers& Frame)
{
    struct OutputFrame
    {
        float Left;
        float Right;
    };
    static_assert(sizeof(OutputFrame) == sizeof(float) * 2);

    TRACEABLE_SCOPE;

    assert(BufferState != nullptr);
    assert(RenderClient);

    // Now that we've collected stereo samples into our intermediate buffers, get the WASAPI output buffer.
    // Its size will be (SampleCount * sizeof(OutputFrame)).
    BYTE *Data;
    CheckHResult(RenderClient->GetBuffer(static_cast<UINT32>(Frame.SampleCount), &Data));
    OutputFrame *OutputFrames = reinterpret_cast<OutputFrame *>(Data);

    // Interleave samples.
    WasapiThreadShared& WasapiBufferState = static_cast<WasapiThreadShared &>(*BufferState);
    for (ptrdiff_t OutputFrameIndex = 0; OutputFrameIndex < Frame.SampleCount; OutputFrameIndex++)
    {
        OutputFrame& OutputFrame = OutputFrames[OutputFrameIndex];
        OutputFrame.Left = WasapiBufferState.OutputSamplesLeft[OutputFrameIndex];
        OutputFrame.Right = WasapiBufferState.OutputSamplesRight[OutputFrameIndex];
    }

    // Release the buffer, letting WASAPI know it's safe to send it over to the hardware endpoint.
    CheckHResult(RenderClient->ReleaseBuffer(Frame.SampleCount, 0));
}


void WasapiRealTimeThread::Loop()
{
    IsRunning.store(true);
    while(IsRunning.load())
    {
        // Wait until more audio is needed.
        WaitForSingleObject(EventHandle, 1000);

        // The output buffer might not be *completely* empty.
        // Any data not yet processed is called "padding", and should be subtracted from the write count.
        UINT32 BufferPadding;
        CheckHResult(AudioClient->GetCurrentPadding(&BufferPadding));

        FramePointers Frame;
        Frame.SampleCount = BufferSize - BufferPadding;
        AdvanceFrames(Frame);
    }
}


WasapiStream::WasapiStream(int SampleRate) :
    BufferState(),
    RealTimeThread(&BufferState, SampleRate)
{ }


float WasapiStream::GetTemporalPressure()
{
    TRACEABLE_SCOPE;
    return BufferState.TemporalPressure.load();
}


void WasapiStream::ProgramChange(ScratchUniquePtr&& NewProgram)
{
    TRACEABLE_SCOPE;
    TRACEABLE_LOCK_GUARD(BufferState.Mutex);
    BufferState.PendingProgram = std::move(NewProgram);
}

#endif
