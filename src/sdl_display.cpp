#include "sdl.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_version.h>
#include <SDL3/SDL_revision.h>

#include <fmt/format.h>

#include <cassert>
#include <span>
#include <stdexcept>
#include <string>

namespace Display
{
    static SDL_Window* Window;

    static int GetDisplayCount()
    {
        int DisplayCount;
        SDL_DisplayID* DisplayIdPtr = SDL_GetDisplays(&DisplayCount);
        if (DisplayIdPtr == nullptr)
        {
            throw std::runtime_error(fmt::format("Failed to get displays. SDL error: {}", SDL_GetError()));
        }
        SDL_free(DisplayIdPtr);
        return std::max(0, DisplayCount);
    }

    static std::vector<SDL_DisplayID> GetDisplayIds()
    {
        int DisplayCount;
        SDL_DisplayID* DisplayIdPtr = SDL_GetDisplays(&DisplayCount);
        if (DisplayIdPtr == nullptr)
        {
            throw std::runtime_error(fmt::format("Failed to get displays. SDL error: {}", SDL_GetError()));
        }
        if ( DisplayCount < 1)
        {
            throw std::runtime_error("No displays found.");
        }
        std::vector<SDL_DisplayID> DisplayIDs(DisplayCount);
        for (int DisplayIndex = 0; DisplayIndex < DisplayCount; ++DisplayIndex)
        {
            DisplayIDs[DisplayIndex] = DisplayIdPtr[DisplayIndex];
        }
        SDL_free(DisplayIdPtr);
        return DisplayIDs;
    }

    static std::vector<SDL_DisplayMode*> GetDisplayModes(SDL_DisplayID DisplayId)
    {
        int DisplayModeCount;
        SDL_DisplayMode** DisplayModesPtr = SDL_GetFullscreenDisplayModes(DisplayId, &DisplayModeCount);
        if (DisplayModesPtr == nullptr)
        {
            throw std::runtime_error(fmt::format("Failed to get display mode. SDL error: {}", SDL_GetError()));
        }
        if (DisplayModeCount < 1)
        {
            throw std::runtime_error("No display modes found.");
        }
        std::vector<SDL_DisplayMode*> DisplayModes(DisplayModeCount);
        for (int DisplayModeIndex = 0; DisplayModeIndex < DisplayModeCount; ++DisplayModeIndex)
        {
            assert(DisplayModesPtr[DisplayModeIndex] != nullptr);
            DisplayModes[DisplayModeIndex] = DisplayModesPtr[DisplayModeIndex];
        }
        SDL_free(DisplayModesPtr);
        return DisplayModes;
    }

    void Init(int ForceFullscreen)
    {
        SDL_SetHint(SDL_HINT_APP_ID, "mollytime");
        SDL_SetHint(SDL_HINT_APP_NAME, "mollytime");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_PEN_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_PEN_TOUCH_EVENTS, "0");
        SDL_SetEventEnabled(SDL_EVENT_FINGER_DOWN, true);
        SDL_SetEventEnabled(SDL_EVENT_FINGER_UP, true);
        SDL_SetEventEnabled(SDL_EVENT_FINGER_MOTION, true);
        SDL_SetEventEnabled(SDL_EVENT_PEN_DOWN, true);
        SDL_SetEventEnabled(SDL_EVENT_PEN_UP, true);
        SDL_SetEventEnabled(SDL_EVENT_PEN_MOTION, true);
        const char* WindowTitle = "mollytime";

        if (!SDL_Init(SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_HAPTIC | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS | SDL_INIT_SENSOR))
        {
            throw std::runtime_error(fmt::format("Failed to initialize SDL. SDL error: {}", SDL_GetError()));
        }

        std::string_view LinkedRevision = SDL_GetRevision();
        std::string_view CompiledRevision = SDL_REVISION;
        if (LinkedRevision != CompiledRevision)
        {
            fmt::print("Compiled SDL version: {}\n", CompiledRevision);
            fmt::print("Linked SDL version: {}\n", LinkedRevision);
        }
        else
        {
            fmt::print("SDL version: {}\n", CompiledRevision);
        }

        if (Window != nullptr)
        {
            SDL_DestroyWindow(Window);
        }

        int WindowFlags = SDL_WINDOW_RESIZABLE;
        const bool StartFullscreened = ForceFullscreen == 1 || (ForceFullscreen != -1 && GetDisplayCount() < 2);
        if (StartFullscreened)
        {
            WindowFlags |= SDL_WINDOW_FULLSCREEN;
        }

        // Start in windowed mode.
        int InitialWidth = 640;
        int InitialHeight = 480;
        {
            SDL_DisplayID PrimaryDisplayID = SDL_GetPrimaryDisplay();
            SDL_Rect PrimaryDisplayRect;
            if (SDL_GetDisplayBounds(PrimaryDisplayID, &PrimaryDisplayRect))
            {
                int Smallest = std::min(PrimaryDisplayRect.w, PrimaryDisplayRect.h);
                int Margin = int(float(Smallest) * .15f);
                InitialWidth = std::max(InitialWidth, PrimaryDisplayRect.w - Margin);
                InitialHeight = std::max(InitialHeight, PrimaryDisplayRect.h - Margin);
            }
        }
        Window = SDL_CreateWindow(WindowTitle, InitialWidth, InitialHeight, WindowFlags);

        if (Window == nullptr)
        {
            throw std::runtime_error(fmt::format("Failed to create window. SDL error: {}", SDL_GetError()));
        }
        else
        {
            if (!SDL_SetWindowMinimumSize(Window, 320, 240))
            {
                throw std::runtime_error(fmt::format("Failed to set minimum window size to 320x240. SDL error: {}", SDL_GetError()));
            }
        }
    }

    int GetCurrentDisplayIndex()
    {
        const std::vector<SDL_DisplayID> DisplayIds = GetDisplayIds();
        const SDL_DisplayID CurrentDisplayId = SDL_GetDisplayForWindow(Window);
        int DisplayIndex = 0;
        for (const SDL_DisplayID DisplayID : DisplayIds)
        {
            if (DisplayID == CurrentDisplayId)
            {
                return DisplayIndex;
            }
            else
            {
                ++DisplayIndex;
            }
        }
        return 0;
    }

    std::vector<Size> GetDesktopSizes()
    {
        const std::vector<SDL_DisplayID> DisplayIds = GetDisplayIds();

        std::vector<Size> Sizes;
        for (const SDL_DisplayID& ID : DisplayIds)
        {
            const SDL_DisplayMode* DisplayMode = SDL_GetDesktopDisplayMode(ID);
            if (DisplayMode == nullptr)
            {
                throw std::runtime_error(fmt::format("Failed to get display mode. SDL error: {}", SDL_GetError()));
            }

            Sizes.emplace_back(DisplayMode->w, DisplayMode->h);
        }

        return Sizes;
    }

    std::vector<Size> ListModes(int DisplayIndex)
    {
        const std::vector<SDL_DisplayID> DisplayIds = GetDisplayIds();
        if (DisplayIndex < 0 || DisplayIndex >= std::ssize(DisplayIds))
        {
            // std::span doesn't have bounds-checked `.at()` until C++26...
            throw std::out_of_range(fmt::format("Display index out of range. Expected >= 0, < {}.", DisplayIds.size()));
        }

        const SDL_DisplayID DisplayId = DisplayIds[DisplayIndex];
        const std::vector<SDL_DisplayMode*> DisplayModes = GetDisplayModes(DisplayId);

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
            throw std::runtime_error(fmt::format("Failed to set window title. SDL error: {}", SDL_GetError()));
        }
    }

    void SetIcon(const Draw::Texture& Texture)
    {
        SDL_Texture* SDLTexture = Texture.GetTexture();
        SDL_Renderer* Renderer = SDL_GetRendererFromTexture(SDLTexture);
        SDL_Rect Rect = { 0, 0, (int)Texture.GetWidth(), (int)Texture.GetHeight() };
        SDL_Surface* Surface = SDL_RenderReadPixels(Renderer, &Rect);
        SDL_SetWindowIcon(Window, Surface);
        SDL_DestroySurface(Surface);
    }

    static bool GetFullscreenState()
    {
        assert(Window != nullptr);
        SDL_WindowFlags WindowFlags = SDL_GetWindowFlags(Window);
        return ((WindowFlags & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN);
    }

    void ToggleFullscreen()
    {
        assert(Window != nullptr);
        const bool IsCurrentlyFullscreen = GetFullscreenState();
        if (IsCurrentlyFullscreen)
        {
            if (!SDL_SetWindowFullscreen(Window, false))
            {
                throw std::runtime_error(fmt::format("Unable to leave fullscreen. SDL error: {}", SDL_GetError()));
            }
        }
        else
        {
            if (!SDL_SetWindowFullscreen(Window, true))
            {
                throw std::runtime_error(fmt::format("Failed to enter fullscreen. SDL error: {}", SDL_GetError()));
            }
        }
    }

    float GetResolutionScale()
    {
        assert(Window != nullptr);
        float Scale = SDL_GetWindowDisplayScale(Window);
        if (Scale <= 0.0f)
        {
            throw std::runtime_error(fmt::format("Unable to determine resolution scale. SDL error: {}", SDL_GetError()));
        }
        return Scale;
    }

    SDL_Window* GetWindow()
    {
        assert(Window != nullptr);
        return Window;
    }

    static int LoadStatus = 0;
    static std::string LoadPath = "";

    static void LoadDialogCallback(void* UserData, const char* const* FileList, int Filter)
    {
        if (FileList == nullptr || *FileList == nullptr)
        {
            // An error happened, or the operator cancelled the request.
            LoadStatus = -1;
            LoadPath = "";
        }
        else
        {
            LoadStatus = 1;
            LoadPath = (const char*)FileList[0];
        }
    }

    static int SaveStatus = 0;
    static std::string SavePath = "";

    static void SaveDialogCallback(void* UserData, const char* const* FileList, int Filter)
    {
        if (FileList == nullptr || *FileList == nullptr)
        {
            // An error happened, or the operator cancelled the request.
            SaveStatus = -1;
            SavePath = "";
        }
        else
        {
            SaveStatus = 1;
            SavePath = (const char*)FileList[0];
        }
    }

    void ShowLoadDialog(const std::string_view& PatchDir)
    {
        LoadStatus = 0;
        LoadPath = "";

        const SDL_DialogFileFilter Filters[] = {
            { "mollytime files", "beep"},
            { "all files", "*"}
        };

        SDL_ShowOpenFileDialog(LoadDialogCallback, nullptr, Window, Filters, 2, PatchDir.data(), false);
    }

    void ShowSaveDialog(const std::string_view& PatchDir)
    {
        SaveStatus = 0;
        SavePath = "";

        const SDL_DialogFileFilter Filters[] = {
            { "mollytime files", "beep"},
            { "all files", "*"}
        };

        SDL_ShowSaveFileDialog(SaveDialogCallback, nullptr, Window, Filters, 2, PatchDir.data());
    }

    std::tuple<int, std::string_view> GetLoadDialogResult()
    {
        return { LoadStatus, LoadPath };
    }

    std::tuple<int, std::string_view> GetSaveDialogResult()
    {
        return { SaveStatus, SavePath };
    }
}
