#include "sdl.h"

#include <SDL3/SDL_mouse.h>

namespace Mouse
{
    std::tuple<float, float> GetPosition()
    {
        float x, y;
        SDL_GetMouseState(&x, &y);

        return { x, y };
    }
}
