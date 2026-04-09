#include "sdl.h"
#include "colors.h"

#include <SDL3_ttf/SDL_ttf.h>

#include <fmt/format.h>

#include <cassert>
#include <stdexcept>

void Font::Init()
{
    if (!TTF_Init())
    {
        throw std::runtime_error(fmt::format("Failed to initialize SDL_ttf. SDL error: {}", SDL_GetError()));
    }
}

Font::Font(const std::string_view& FilePath, float InSize)
{
    SDL_IOStream* IO = SDL_IOFromFile(FilePath.data(), "rb");
    if (IO == nullptr)
    {
        throw std::runtime_error(fmt::format("Failed to open font file '{}'. SDL error: {}", FilePath, SDL_GetError()));
    }

    SDLFont = TTF_OpenFontIO(IO, true, InSize);
    if(SDLFont == nullptr)
    {
        throw std::runtime_error(fmt::format("Failed to load font file '{}'. SDL error: {}", FilePath, SDL_GetError()));
    }
}

Font::~Font()
{
    TTF_CloseFont(SDLFont);
    SDLFont = nullptr;
}

int Font::GetAscent() const
{
    assert(SDLFont != nullptr);
    return TTF_GetFontAscent(SDLFont);
}

int Font::GetDescent() const
{
    assert(SDLFont != nullptr);
    return TTF_GetFontDescent(SDLFont);
}

std::tuple<int, int> Font::EstimateGlyphHeight(char Glyph) const
{
    assert(SDLFont != nullptr);

    int MinY, MaxY;
    if (!TTF_GetGlyphMetrics(SDLFont, Glyph, nullptr, nullptr, &MinY, &MaxY, nullptr))
    {
        throw std::runtime_error(fmt::format("Failed to get glyph metrics. SDL error: {}", SDL_GetError()));
    }

    return { MinY, MaxY };
}

Draw::Texture Font::Render(const std::string_view& Text, const ColorPoint& Color) const
{
    assert(SDLFont != nullptr);

    const auto [R, G, B] = Color.To8BitRGB();
    const SDL_Color SDLColor { R, G, B, SDL_ALPHA_OPAQUE };

    SDL_Surface* Surface = TTF_RenderText_Blended(SDLFont, Text.data(), Text.size(), SDLColor);
    if(Surface == nullptr)
    {
        throw std::runtime_error(fmt::format("Failed to render font. SDL error: {}", SDL_GetError()));
    }

    return Draw::Texture(Surface);
}