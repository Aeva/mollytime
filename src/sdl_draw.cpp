#include "sdl.h"
#include "colors.h"

#include <SDL3/SDL_render.h>

#include <algorithm>
#include <cassert>
#include <format>
#include <stdexcept>
#include <print>

namespace Draw
{
    Texture::Texture(SDL_Window* Window, int InWidth, int InHeight)
        : SDLTexture(nullptr)
        , Width(InWidth)
        , Height(InHeight)
    {
        // SDL_GetWindowSize is claiming the window size is (1, 1) on Linux for some reason,
        // but we don't really need to call it since the window size is already known.
    }

    Texture::Texture(SDL_Surface* Surface)
    {
        assert(Surface != nullptr);

        SDLTexture = SDL_CreateTextureFromSurface(&GetRenderer(), Surface);
        if(SDLTexture == nullptr)
        {
            throw std::runtime_error(std::format("Failed to create texture from surface. SDL error: {}", SDL_GetError()));
        }

        float WidthF, HeightF;
        if (!SDL_GetTextureSize(SDLTexture, &WidthF, &HeightF))
        {
            throw std::runtime_error(std::format("Failed to get texture size. SDL error: {}", SDL_GetError()));
        }

        Width = static_cast<int>(WidthF);
        Height = static_cast<int>(HeightF);
    }

    Texture::Texture(int Width, int Height) :
        Width(Width),
        Height(Height)
    {
        assert(Width + Height >= 1);

        SDLTexture = SDL_CreateTexture(
            &Draw::GetRenderer(),
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET,
            Width,
            Height
        );

        if (SDLTexture == nullptr)
        {
            throw std::runtime_error(std::format("Failed to create texture. SDL error: {}", SDL_GetError()));
        }
    }

    Texture::Texture(const Size& Size) :
        Texture(std::get<0>(Size), std::get<1>(Size))
    { }
    
    Texture::~Texture()
    {
        // TODO: Debug why Textures are destroyed unexpectedly
        // SDL_DestroyTexture(SDLTexture);
        // SDLTexture = nullptr;
    }

    float Texture::GetWidth() const
    {
        return static_cast<float>(Width);
    }

    float Texture::GetHeight() const
    {
        return static_cast<float>(Height);
    }
    
    Rect Texture::GetRect() const
    {
        return { 0.0f, 0.0f, GetWidth(), GetHeight() };
    }

    Texture Texture::Copy() const
    {
        Texture NewTexture(Width, Height);
        NewTexture.Blit(*this, GetRect());

        return NewTexture;
    }
    
    void Texture::SetAlpha(int Alpha)
    {
        if (!SDL_SetTextureAlphaMod(SDLTexture, static_cast<Uint8>(Alpha)))
        {
            throw std::runtime_error(std::format("Failed to set texture alpha. SDL error: {}", SDL_GetError()));
        }
    }

    void Texture::Fill(ColorPoint& Color)
    {
        const auto [R, G, B] = Color.To8BitRGB();

        if (!SDL_SetRenderTarget(&Draw::GetRenderer(), SDLTexture))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_SetRenderDrawColor(&Draw::GetRenderer(), R, G, B, 255))
        {
            throw std::runtime_error(std::format("Failed to set render color. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_RenderClear(&Draw::GetRenderer()))
        {
            throw std::runtime_error(std::format("Failed to fill texture. SDL error: {}", SDL_GetError()));
        }
    }

    void Texture::Blit(const Texture& Source, const Rect& Region)
    {
        const SDL_FRect SourceRect
        {
            0.0f,
            0.0f,
            static_cast<float>(Source.Width),
            static_cast<float>(Source.Height)
        };

        const SDL_FRect DestRect
        {
            Region.X,
            Region.Y,
            Region.Width,
            Region.Height
        };

        if (!SDL_SetRenderTarget(&Draw::GetRenderer(), SDLTexture))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }
        
        if (!SDL_RenderTexture(&Draw::GetRenderer(), Source.SDLTexture, &SourceRect, &DestRect))
        {
            throw std::runtime_error(std::format("Failed to render texture. SDL error: {}", SDL_GetError()));
        }
    }

    void Texture::Blit(const Texture& Source, const Point& Offset)
    {
        const auto [X, Y] = Offset;
        Blit(Source, { X, Y, Width - X, Height - Y });
    }

    SDL_Texture& Texture::GetTexture()
    {
        return *SDLTexture;
    }
}

namespace Draw
{
    static SDL_Renderer* Renderer;

    void Init()
    {
        Renderer = SDL_CreateRenderer(&Display::GetWindow(), nullptr);
        if (Renderer == nullptr)
        {
            throw std::runtime_error(std::format("Failed to initialize renderer. SDL error: {}", SDL_GetError()));
        }
    }

    void Flip()
    {
        assert(Renderer != nullptr);

        if (!SDL_RenderPresent(Renderer))
        {
            throw std::runtime_error(std::format("Failed to present renderer. SDL error: {}", SDL_GetError()));
        }
    }
    
    void DrawLine(Texture& Texture, const ColorPoint& Color, const Point& Start, const Point& End, int Width, float Alpha)
    {
        assert(Renderer != nullptr);

        // TODO: Actually use Width
        const auto [R, G, B] = Color.To8BitRGB();
        const auto [X1, Y1] = Start;
        const auto [X2, Y2] = End;

        if (!SDL_SetRenderTarget(Renderer, &Texture.GetTexture()))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_SetRenderDrawColor(Renderer, R, G, B, 255))
        {
            throw std::runtime_error(std::format("Failed to set render color. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_RenderLine(Renderer, static_cast<float>(X1), static_cast<float>(Y1), static_cast<float>(X2), static_cast<float>(Y2)))
        {
            throw std::runtime_error(std::format("Failed to render line. SDL error: {}", SDL_GetError()));
        }
    }

    void DrawLineAntiAliased(Texture& Texture, const ColorPoint& Color, const Point& Start, const Point& End, int Width, float Alpha)
    {
        // TODO: Implement
        DrawLine(Texture, Color, Start, End, Width);
    }

    void DrawRect(Texture& Texture, const ColorPoint& Color, const Rect& Rect, int BorderWidth, float Alpha)
    {
        assert(Renderer != nullptr);

        const auto [R, G, B] = Color.To8BitRGB();
        const SDL_FRect FloatRect
        {
            Rect.X,
            Rect.Y,
            Rect.Width,
            Rect.Height
        };

        if (!SDL_SetRenderTarget(Renderer, &Texture.GetTexture()))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_SetRenderDrawColor(Renderer, R, G, B, std::clamp(Alpha, 0.0f, 1.0f) * 255))
        {
            throw std::runtime_error(std::format("Failed to set render color. SDL error: {}", SDL_GetError()));
        }

        const bool Fill = BorderWidth <= 0;
        if (Fill)
        {
            if (!SDL_RenderFillRect(Renderer, &FloatRect))
            {
                throw std::runtime_error(std::format("Failed to render rect. SDL error: {}", SDL_GetError()));
            }
        }
        else
        {
            // TODO: Actually use BorderWidth
            if (!SDL_RenderRect(Renderer, &FloatRect))
            {
                throw std::runtime_error(std::format("Failed to render rect. SDL error: {}", SDL_GetError()));
            }
        }
    }

    void DrawCircle(Texture& Texture, const ColorPoint& Color, const Point& Center, int Radius, float Alpha)
    {
        // TODO: Implement 😔
    }

    void DrawPolygon(Texture& Texture, const ColorPoint& Color, const std::vector<Point>& Vertices, float Alpha)
    {
        assert(Renderer != nullptr);

        // TODO: Convert list of outer polygon Vertices into triangles & indices
        return;

        const glm::vec3 ColorRGB = Color.Eval(ColorSpace::sRGB);

        std::vector<SDL_Vertex> SDLVertices;
        for (const Point& Vertex : Vertices)
        {
            const auto [X, Y] = Vertex;
            SDLVertices.emplace_back(
                SDL_FPoint { X, Y },
                SDL_FColor { ColorRGB.r, ColorRGB.g, ColorRGB.b, Alpha },
                SDL_FPoint {}
            );
        }
        
        if (!SDL_SetRenderTarget(Renderer, &Texture.GetTexture()))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_RenderGeometry(Renderer, nullptr, SDLVertices.data(), static_cast<int>(SDLVertices.size()), nullptr, 0))
        {
            throw std::runtime_error(std::format("Failed to render polygon. SDL error: {}", SDL_GetError()));
        }
    }

    SDL_Renderer& GetRenderer()
    {
        assert(Renderer != nullptr);        
        return *Renderer;
    }
}
