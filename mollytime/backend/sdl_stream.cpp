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

#include "sdl_stream.h"

#include <fmt/format.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_audio.h>


SDLRealTimeThread::SDLRealTimeThread(SDLThreadShared* SDLBufferState, int SampleRate)
{
    assert(SDLBufferState != nullptr);
    assert(SampleRate >= 0);

    // Up-the-chain init that we have to do here for some reason.
    BufferState = SDLBufferState;
    ResetFramePressure();
    SampleInterval = 1.0 / double(SampleRate);

    // SDL Audio must be initialized. If it's already initialized, this will harmlessly no-op.
    if(!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        throw std::runtime_error(fmt::format("Failed to initialize SDL Audio subsystem: {}", SDL_GetError()));
    }

    // Create an SDL audio stream with our given options. (Data format & stereo output are currently hardcoded.)
    SDL_AudioSpec AudioSpec;
    AudioSpec.channels = 2;
    AudioSpec.format = SDL_AUDIO_F32;
    AudioSpec.freq = SampleRate;

    SDLStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &AudioSpec,
        &SDLRealTimeThread::OnAudioRequested,
        this
    );
    if(SDLStream == nullptr)
    {
        throw std::runtime_error(fmt::format("Failed to open SDL audio stream: {}", SDL_GetError()));
    }

    // This one-and-done API begins with a paused stream. We have to manually resume it.
    if(!SDL_ResumeAudioStreamDevice(SDLStream))
    {
        throw std::runtime_error(fmt::format("Failed to resume SDL audio stream: {}", SDL_GetError()));
    }

    // SDL doesn't guarantee a fixed-size output buffer, nor does it provide any kind of high-water estimate.
    // In my testing (Windows), it wants samples 3840 per request, at least with our current format (48000 Hz stereo).
    // We'll dynamically resize these intermediary buffers if needed, but that risks a runtime hitch.
    // So, start with a size of 5000, so that we hopefully never need to.
    SDLBufferState->OutputSamplesLeft.resize(5000);
    SDLBufferState->OutputSamplesRight.resize(5000);
}


SDLRealTimeThread::~SDLRealTimeThread()
{
    SDL_DestroyAudioStream(SDLStream);
    SDLStream = nullptr;
}


void SDLRealTimeThread::OnAudioRequested(void* UserData, SDL_AudioStream*, int AdditionalAmount, int)
{
    assert(UserData != nullptr);
    
    SDLRealTimeThread& Thread = *static_cast<SDLRealTimeThread*>(UserData);

    // SDL will emit this callback whenever the output buffer needs filling, *or* is finalized for playback.
    // We're only interested if audio is needed, represented by the "additional amount" of samples required.
    if (AdditionalAmount > 0)
    {
        FramePointers Frame;
        Frame.SampleCount = AdditionalAmount;
        Thread.AdvanceFrames(Frame);
    }
}


void SDLRealTimeThread::BeginFrame(FramePointers& Frame)
{
    assert(BufferState != nullptr);

    SDLThreadShared& SDLBufferState = static_cast<SDLThreadShared &>(*BufferState);

    // SDL doesn't guarantee a fixed-size output buffer. We might need to resize our intermediary buffers.
    // They'll then maintain the previous high-water mark. A resize should happen very infrequently, if ever.
    if (static_cast<ptrdiff_t>(SDLBufferState.OutputSamplesLeft.size()) < Frame.SampleCount)
    {
        SDLBufferState.OutputSamplesLeft.resize(Frame.SampleCount);
    }
    if (static_cast<ptrdiff_t>(SDLBufferState.OutputSamplesRight.size()) < Frame.SampleCount)
    {
        SDLBufferState.OutputSamplesRight.resize(Frame.SampleCount);
    }

    // Assign intermediary buffers to receive generated samples.
    Frame.OutLeft = SDLBufferState.OutputSamplesLeft.data();
    Frame.OutRight = SDLBufferState.OutputSamplesRight.data();
}


void SDLRealTimeThread::EndFrame(FramePointers& Frame)
{
    TRACEABLE_SCOPE;

    assert(BufferState != nullptr);
    assert(SDLStream != nullptr);

    SDLThreadShared& SDLBufferState = static_cast<SDLThreadShared &>(*BufferState);

    // SDL requires interleaved channels, but can do the interleaving for us. This will, necessarily, perform copies.
    // If this isn't fast enough, `SDL_PutAudioStreamDataNoCopy()` can read directly from an already-interleaved buffer,
    // but that means we'd have 1. do our own faster-than-SDL interleave, or 2. generate samples pre-interleaved.
    const float* SamplesLeft = SDLBufferState.OutputSamplesLeft.data();
    const float* SamplesRight = SDLBufferState.OutputSamplesRight.data();
    const void* Channels[] = { SamplesLeft, SamplesRight };

    if(!SDL_PutAudioStreamPlanarData(SDLStream, Channels, 2, Frame.SampleCount))
    {
        throw std::runtime_error(fmt::format("Failed to put SDL Audio stream data: {}", SDL_GetError()));
    }
}


SDLStream::SDLStream(int SampleRate) :
    BufferState(),
    RealTimeThread(&BufferState, SampleRate)
{ }


float SDLStream::GetTemporalPressure()
{
    TRACEABLE_SCOPE;
    return BufferState.TemporalPressure.load();
}


void SDLStream::ProgramChange(ScratchUniquePtr&& NewProgram)
{
    TRACEABLE_SCOPE;
    TRACEABLE_LOCK_GUARD(BufferState.Mutex);
    BufferState.PendingProgram = std::move(NewProgram);
}