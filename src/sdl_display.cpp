#include "sdl.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>

#include <cassert>
#include <format>
#include <span>
#include <stdexcept>

namespace Display
{
    static SDL_Window* Window;

    static std::span<const SDL_DisplayID> GetDisplayIds()
    {
        int DisplayCount;
        const SDL_DisplayID* DisplayIdPtr = SDL_GetDisplays(&DisplayCount);
        if(DisplayIdPtr == nullptr)
        {
            throw std::runtime_error(std::format("Failed to get displays. SDL error: {}", SDL_GetError()));
        }
        if(DisplayCount < 1)
        {
            throw std::runtime_error("No displays found.");
        }

        return std::span<const SDL_DisplayID>(DisplayIdPtr, DisplayCount);
    }

    static std::span<SDL_DisplayMode*> GetDisplayModes(SDL_DisplayID DisplayId)
    {
        int DisplayModeCount;
        SDL_DisplayMode** DisplayModesPtr = SDL_GetFullscreenDisplayModes(DisplayId, &DisplayModeCount);
        if(DisplayModesPtr == nullptr)
        {
            throw std::runtime_error(std::format("Failed to get display mode. SDL error: {}", SDL_GetError()));
        }
        if(DisplayModeCount < 1)
        {
            throw std::runtime_error("No display modes found.");
        }

        return std::span<SDL_DisplayMode*>(DisplayModesPtr, DisplayModeCount);
    }

    void Init()
    {
        SDL_SetHint(SDL_HINT_APP_ID, "mollytime");
        SDL_SetHint(SDL_HINT_APP_NAME, "mollytime");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_PEN_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_PEN_TOUCH_EVENTS, "0");

        if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_HAPTIC | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_SENSOR))
        {
            throw std::runtime_error(std::format("Failed to initialize SDL. SDL error: {}", SDL_GetError()));
        }

        if (Window != nullptr)
        {
            SDL_DestroyWindow(Window);
        }

        Window = SDL_CreateWindow("", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS);
        if (Window == nullptr)
        {
            throw std::runtime_error(std::format("Failed to create window. SDL error: {}", SDL_GetError()));
        }
    }

    std::vector<Size> GetDesktopSizes()
    {
        const std::span<const SDL_DisplayID> DisplayIds = GetDisplayIds();

        std::vector<Size> Sizes;
        for (const SDL_DisplayID& ID : DisplayIds)
        {
            const SDL_DisplayMode* DisplayMode = SDL_GetDesktopDisplayMode(ID);
            if (DisplayMode == nullptr)
            {
                throw std::runtime_error(std::format("Failed to get display mode. SDL error: {}", SDL_GetError()));
            }

            Sizes.emplace_back(DisplayMode->w, DisplayMode->h);
        }

        return Sizes;
    }

    std::vector<Size> ListModes(int DisplayIndex)
    {
        const std::span<const SDL_DisplayID> DisplayIds = GetDisplayIds();
        if (DisplayIndex < 0 || DisplayIndex >= std::ssize(DisplayIds))
        {
            // std::span doesn't have bounds-checked `.at()` until C++26...
            throw std::out_of_range(std::format("Display index out of range. Expected >= 0, < {}.", DisplayIds.size()));
        }

        const SDL_DisplayID DisplayId = DisplayIds[DisplayIndex];
        const std::span<SDL_DisplayMode*> DisplayModes = GetDisplayModes(DisplayId);

        std::vector<Size> Sizes;
        for(SDL_DisplayMode* Mode : DisplayModes)
        {
            assert(Mode != nullptr);
            Sizes.emplace_back(Mode->w, Mode->h);
        }

        return Sizes;
    }

    void SetCaption(const std::string_view& Title)
    {
        assert(Window != nullptr);

        if (!SDL_SetWindowTitle(Window, Title.data()))
        {
            throw std::runtime_error(std::format("Failed to set window title. SDL error: {}", SDL_GetError()));
        }
    }

    void SetIcon(const Draw::Texture& Texture)
    {
        // TODO: SDL_SetWindowIcon() expects a Surface (CPU texture), but all we have is a Texture (GPU).
    }

    Draw::Texture SetMode(int DisplayIndex, const Size& Size, WindowFlags Flags)
    {
        assert(Window != nullptr);

        const std::span<const SDL_DisplayID> DisplayIds = GetDisplayIds();
        if (DisplayIndex < 0 || DisplayIndex >= std::ssize(DisplayIds))
        {
            // std::span doesn't have bounds-checked `.at()` until C++26...
            throw std::out_of_range(std::format("Display index out of range. Expected >= 0, < {}.", DisplayIds.size()));
        }

        const auto& [Width, Height] = Size;
        const bool wants_fullscreen = static_cast<uint32_t>(Flags) & static_cast<uint32_t>(WindowFlags::Fullscreen);
        const bool wants_borderless = static_cast<uint32_t>(Flags) & static_cast<uint32_t>(WindowFlags::Borderless);
        
        const SDL_DisplayID DisplayId = DisplayIds[DisplayIndex];
        const bool is_exclusive_fullscreen = wants_fullscreen && !wants_borderless;

        // Set exclusive fullscreen mode.
        if (!SDL_SetWindowFullscreen(Window, is_exclusive_fullscreen))
        {
            throw std::runtime_error(std::format("Failed to set fullscreen state. SDL error: {}", SDL_GetError()));
        }

        // Display mode method changes if we're in exclusive fullscreen.
        if (is_exclusive_fullscreen)
        {
            // Look for an exclusive fullscreen display mode that exactly-matches the requested dimensions.
            const std::span<SDL_DisplayMode*> DisplayModes = GetDisplayModes(DisplayId);
            for (SDL_DisplayMode* Mode : DisplayModes)
            {
                assert(Mode != nullptr);

                if(Mode->w == Width && Mode->h == Height)
                {
                    if (!SDL_SetWindowFullscreenMode(Window, Mode))
                    {
                        throw std::runtime_error(std::format("Failed to set fullscreen mode. SDL error: {}", SDL_GetError()));
                    }

                    return Draw::Texture(Window, Width, Height);
                }
            }

            // No match. This is now the user's problem.
            throw std::invalid_argument(std::format(
                "Display index {} doesn't support the requested fullscreen size ({}x{}).",
                DisplayIndex, Width, Height
            ));
        }
        else
        {
            // Borderless windows are normal windows, and can be set to any resolution.
            if (!SDL_SetWindowBordered(Window, !wants_borderless))
            {
                throw std::runtime_error(std::format("Failed to set window border state. SDL error: {}", SDL_GetError()));
            }
            if (!SDL_SetWindowSize(Window, Width, Height))
            {
                throw std::runtime_error(std::format("Failed to set window size. SDL error: {}", SDL_GetError()));
            }

            // Thus, we need to re-center the window after setting resolution.
            const int CenterPosition = SDL_WINDOWPOS_CENTERED_DISPLAY(DisplayIndex);
            if (!SDL_SetWindowPosition(Window, CenterPosition, CenterPosition))
            {
                throw std::runtime_error(std::format("Failed to set window position. SDL error: {}", SDL_GetError()));
            }
            return Draw::Texture(Window, Width, Height);
        }
    }

    SDL_Window& GetWindow()
    {
        assert(Window != nullptr);
        return *Window;
    }
}
