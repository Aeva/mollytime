#include "sdl.h"
#include "colors.h"

#include <SDL3/SDL_render.h>

#include <cmath>
#include <numbers>
#include <algorithm>
#include <cassert>
#include <format>
#include <stdexcept>
#include <print>


constexpr double Tau = std::numbers::pi * 2.0;

namespace Draw
{
    static SDL_FPoint SDLPoint(glm::vec2 Pt)
    {
        return {Pt.x, Pt.y};
    }

    static glm::vec2 GLMPoint(Point Pt)
    {
        const auto [X, Y] = Pt;
        return {X, Y};
    }

    static glm::vec2 SunwiseBy90(glm::vec2 In)
    {
        // Rotates a 2D vector around the origin by 90 degrees clockwise.
        return glm::vec2(-In.y, In.x);
    }

    static glm::vec2 WiddershinsBy90(glm::vec2 In)
    {
        // Rotates a 2D vector around the origin by 90 degrees counterclockwise.
        return glm::vec2(In.y, -In.x);
    }

    TextureCaddy::TextureCaddy(SDL_Texture* InSDLTexture)
        : SDLTexture(InSDLTexture)
    {
    }

    TextureCaddy::~TextureCaddy()
    {
        if (SDLTexture != nullptr)
        {
            SDL_DestroyTexture(SDLTexture);
            SDLTexture = nullptr;
        }
    }

    Texture::Texture(SDL_Window* Window, int InWidth, int InHeight)
        : Width(InWidth)
        , Height(InHeight)
    {
        // SDL_GetWindowSize is claiming the window size is (1, 1) on Linux for some reason,
        // but we don't really need to call it since the window size is already known.
    }

    Texture::Texture(SDL_Surface* Surface)
    {
        assert(Surface != nullptr);

        Handle = std::make_shared<TextureCaddy>(SDL_CreateTextureFromSurface(&GetRenderer(), Surface));
        if (!GetTexture())
        {
            throw std::runtime_error(std::format("Failed to create texture from surface. SDL error: {}", SDL_GetError()));
        }

        float WidthF, HeightF;
        if (!SDL_GetTextureSize(GetTexture(), &WidthF, &HeightF))
        {
            throw std::runtime_error(std::format("Failed to get texture size. SDL error: {}", SDL_GetError()));
        }

        Width = static_cast<int>(WidthF);
        Height = static_cast<int>(HeightF);

        SetBlendMode();
    }

    Texture::Texture(int Width, int Height) :
        Width(Width),
        Height(Height)
    {
        assert(Width + Height >= 1);

        Handle = std::make_shared<TextureCaddy>(SDL_CreateTexture(
            &Draw::GetRenderer(),
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET,
            Width,
            Height
        ));

        if (!GetTexture())
        {
            throw std::runtime_error(std::format("Failed to create texture. SDL error: {}", SDL_GetError()));
        }

        SetBlendMode();
    }

    Texture::Texture(const Size& Size) :
        Texture(std::get<0>(Size), std::get<1>(Size))
    { }

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
        if (!SDL_SetTextureAlphaMod(GetTexture(), static_cast<Uint8>(Alpha)))
        {
            throw std::runtime_error(std::format("Failed to set texture alpha. SDL error: {}", SDL_GetError()));
        }
    }

    void Texture::SetBlendMode(BlendModeType BlendMode)
    {
        if (!SDL_SetTextureBlendMode(GetTexture(), (Uint32)BlendMode))
        {
            throw std::runtime_error(std::format("Failed to set blend mode. SDL error: {}", SDL_GetError()));
        }
    }

    void Texture::Fill(ColorPoint& Color, float Alpha)
    {
        glm::vec3 RGB = Color.Eval(ColorSpace::sRGB);

        if (!SDL_SetRenderTarget(&Draw::GetRenderer(), GetTexture()))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_SetRenderDrawColorFloat(&Draw::GetRenderer(), RGB.x, RGB.y, RGB.z, Alpha))
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

        if (!SDL_SetRenderTarget(&Draw::GetRenderer(), GetTexture()))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }
        
        if (!SDL_RenderTexture(&Draw::GetRenderer(), Source.GetTexture(), &SourceRect, &DestRect))
        {
            throw std::runtime_error(std::format("Failed to render texture. SDL error: {}", SDL_GetError()));
        }
    }

    void Texture::Blit(const Texture& Source, const Point& Offset)
    {
        const auto [X, Y] = Offset;
        Blit(Source, { X, Y, float(Source.Width), float(Source.Height) });
    }

    SDL_Texture* Texture::GetTexture() const
    {
        if (Handle)
        {
            return Handle->SDLTexture;
        }
        else
        {
            return nullptr;
        }
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
    
    void DrawLine(Texture& Texture, const ColorPoint& Color, const Point& Start, const Point& End, float Width, float Alpha)
    {
        assert(Renderer != nullptr);

        const auto [X1, Y1] = Start;
        const auto [X2, Y2] = End;

        if (Alpha < 1.0f)
        {
            SDL_SetRenderDrawBlendMode(&Draw::GetRenderer(), SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        }
        else
        {
            SDL_SetRenderDrawBlendMode(&Draw::GetRenderer(), SDL_BLENDMODE_NONE);
        }

        if (Width > 1.0)
        {
            float Radius = Width * 0.5;
            glm::vec2 PointA = GLMPoint(Start);
            glm::vec2 PointB = GLMPoint(End);
            glm::vec2 Offset = glm::normalize(PointB - PointA) * Radius;
            glm::vec2 OffsetW = WiddershinsBy90(Offset);
            glm::vec2 OffsetS = SunwiseBy90(Offset);
            SDL_FPoint CornerAW = SDLPoint(PointA + OffsetW);
            SDL_FPoint CornerBW = SDLPoint(PointB + OffsetW);
            SDL_FPoint CornerBS = SDLPoint(PointB + OffsetS);
            SDL_FPoint CornerAS = SDLPoint(PointA + OffsetS);
            glm::vec3 RGB = Color.Eval(ColorSpace::sRGB) * Alpha;

            SDL_Vertex Vertices[6];
            Vertices[0].position = CornerAW;
            Vertices[1].position = CornerBW;
            Vertices[2].position = CornerBS;
            Vertices[3].position = CornerBS;
            Vertices[4].position = CornerAS;
            Vertices[5].position = CornerAW;
            for (SDL_Vertex& Vertex : Vertices)
            {
                Vertex.color = {RGB.x, RGB.y, RGB.z, Alpha};
            }

            if (!SDL_SetRenderTarget(Renderer, Texture.GetTexture()))
            {
                throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
            }

            if (!SDL_RenderGeometry(Renderer, nullptr, Vertices, 6, nullptr, 0))
            {
                throw std::runtime_error(std::format("Failed to render polygon. SDL error: {}", SDL_GetError()));
            }
        }
        else
        {
            if (!SDL_SetRenderTarget(Renderer, Texture.GetTexture()))
            {
                throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
            }

            glm::vec3 RGB = Color.Eval(ColorSpace::sRGB) * Alpha;
            if (!SDL_SetRenderDrawColorFloat(&Draw::GetRenderer(), RGB.x, RGB.y, RGB.z, Alpha))
            {
                throw std::runtime_error(std::format("Failed to set render color. SDL error: {}", SDL_GetError()));
            }

            if (!SDL_RenderLine(Renderer, static_cast<float>(X1), static_cast<float>(Y1), static_cast<float>(X2), static_cast<float>(Y2)))
            {
                throw std::runtime_error(std::format("Failed to render line. SDL error: {}", SDL_GetError()));
            }
        }

        SDL_SetRenderDrawBlendMode(&Draw::GetRenderer(), SDL_BLENDMODE_NONE);
    }

    void DrawRect(Texture& Texture, const ColorPoint& Color, const Rect& Rect, int BorderWidth, float Alpha)
    {
        assert(Renderer != nullptr);

        SDL_FRect FloatRect
        {
            Rect.X,
            Rect.Y,
            Rect.Width,
            Rect.Height
        };

        if (!SDL_SetRenderTarget(Renderer, Texture.GetTexture()))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }

        if (Alpha < 1.0f)
        {
            SDL_SetRenderDrawBlendMode(&Draw::GetRenderer(), SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        }
        else
        {
            SDL_SetRenderDrawBlendMode(&Draw::GetRenderer(), SDL_BLENDMODE_NONE);
        }

        // TODO Blend mode suggests that we should be premultiplying alpha into the RGB channels, but
        // if we actually do that the color is clearly wrong (seen in the pick and place mode).  Does that
        // mean we *shouldn't* be doing that anywhere?
        glm::vec3 RGB = Color.Eval(ColorSpace::sRGB);
        if (!SDL_SetRenderDrawColorFloat(&Draw::GetRenderer(), RGB.x, RGB.y, RGB.z, Alpha))
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
            for (int Iteration = 0; Iteration < BorderWidth; ++Iteration)
            {
                if (!SDL_RenderRect(Renderer, &FloatRect))
                {
                    throw std::runtime_error(std::format("Failed to render rect. SDL error: {}", SDL_GetError()));
                }
                FloatRect.x += 1.0f;
                FloatRect.y += 1.0f;
                FloatRect.w -= 2.0f;
                FloatRect.h -= 2.0f;
            }
        }

        SDL_SetRenderDrawBlendMode(&Draw::GetRenderer(), SDL_BLENDMODE_NONE);
    }

    static void PrepareConvexHull(
        const std::vector<Point>& Points, const ColorPoint& Color, float Alpha,
        std::vector<SDL_Vertex>& OutVertices, std::vector<int>& OutIndices)
    {
        OutVertices.clear();
        OutIndices.clear();
        if (Points.size() == 0)
        {
            return;
        }
        {
            glm::vec2 Center(0.0f, 0.0f);
            OutVertices.reserve(Points.size() + 1);
            {
                // Placeholder for the center vertex, which is calculated later.
                SDL_Vertex CenterVertexTBD;
                Color.Eval(ColorSpace::sRGB, CenterVertexTBD.color);
                CenterVertexTBD.color.a = Alpha;
                OutVertices.push_back(CenterVertexTBD);
            }
            for (const Point& Corner : Points)
            {
                SDL_Vertex Vertex;
                glm::vec2 Pt = GLMPoint(Corner);
                Center += Pt;
                Vertex.position = SDLPoint(Pt);
                Vertex.color = OutVertices[0].color;
                OutVertices.push_back(Vertex);
            }
            Center /= float(Points.size());
            OutVertices[0].position = SDLPoint(Center);
        }
        {
            glm::vec3 Center(OutVertices[0].position.x, OutVertices[0].position.y, 0.0f);
            OutIndices.reserve(Points.size() * 3);
            for (int IndexA = 0; IndexA < (int)Points.size(); ++IndexA)
            {
                int IndexB = (IndexA + 1) % (int)Points.size();

                glm::vec3 PointA = glm::vec3(OutVertices[IndexA].position.x, OutVertices[IndexA].position.y, 0.0f) - Center;
                glm::vec3 PointB = glm::vec3(OutVertices[IndexB].position.x, OutVertices[IndexB].position.y, 0.0f) - Center;
                glm::vec3 Norm = glm::cross(PointA, PointB);
                if (Norm.z < 0.0f)
                {
                    OutIndices.push_back(IndexA + 1);
                    OutIndices.push_back(IndexB + 1);
                }
                else
                {
                    OutIndices.push_back(IndexB + 1);
                    OutIndices.push_back(IndexA + 1);
                }
                OutIndices.push_back(0);
            }
        }
    }

    static std::vector<Point> UnitCircle(const int Count)
    {
        std::vector<Point> Points;
        Points.resize(Count);
        for (int Index = 0; Index < Count; ++Index)
        {
            const float Alpha = float(Index) / float(Count);
            const float Angle = Tau * Alpha;
            Points[Index] = { std::cos(Angle), -std::sin(Angle) };
        }
        return Points;
    }

    void DrawCircle(Texture& Texture, const ColorPoint& Color, const Point& Center, float Radius, float Alpha)
    {
        assert(Renderer != nullptr);

        static std::vector<Point> CircleTemplate = UnitCircle(8);
        std::vector<Point> Points;
        Points.reserve(CircleTemplate.size());
        const auto [CenterX, CenterY] = Center;
        for (const auto [UnitX, UnitY] : CircleTemplate)
        {
            Points.emplace_back(UnitX * Radius + CenterX, UnitY * Radius + CenterY);
        }

        std::vector<SDL_Vertex> Vertices;
        std::vector<int> Indices;
        PrepareConvexHull(Points, Color, Alpha, Vertices, Indices);

        if (!SDL_SetRenderTarget(Renderer, Texture.GetTexture()))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_RenderGeometry(Renderer, nullptr, Vertices.data(), Vertices.size(), Indices.data(), Indices.size()))
        {
            throw std::runtime_error(std::format("Failed to render circle. SDL error: {}", SDL_GetError()));
        }
    }

    void DrawPolygon(Texture& Texture, const ColorPoint& Color, const std::vector<Point>& Points, float Alpha)
    {
        assert(Renderer != nullptr);

        std::vector<SDL_Vertex> Vertices;
        std::vector<int> Indices;
        PrepareConvexHull(Points, Color, Alpha, Vertices, Indices);

        if (!SDL_SetRenderTarget(Renderer, Texture.GetTexture()))
        {
            throw std::runtime_error(std::format("Failed to set render texture. SDL error: {}", SDL_GetError()));
        }

        if (!SDL_RenderGeometry(Renderer, nullptr, Vertices.data(), Vertices.size(), Indices.data(), Indices.size()))
        {
            throw std::runtime_error(std::format("Failed to render circle. SDL error: {}", SDL_GetError()));
        }
    }

    SDL_Renderer& GetRenderer()
    {
        assert(Renderer != nullptr);
        return *Renderer;
    }
}
