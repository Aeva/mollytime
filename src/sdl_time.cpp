#include "sdl.h"

#include <SDL3/SDL_timer.h>

namespace Time
{
    Clock::Clock() :
        LastTick(SDL_GetTicksNS())
    { }
        
    void Clock::Tick(double Framerate)
    {
        if (Framerate <= 0.0) {
            return;
        }

        const uint64_t FramerateInterval = (1.0 / Framerate) * 1e+9;
        const uint64_t TimeSinceLastTick = SDL_GetTicksNS() - this->LastTick;
        const uint64_t Zero = 0;
        const uint64_t TimeToDelay = std::max(Zero, FramerateInterval - TimeSinceLastTick);

        SDL_DelayPrecise(TimeToDelay);
    }
}
