#pragma once

#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>

struct ColorPoint;

struct SDL_Renderer;
struct SDL_Surface;
struct SDL_Texture;
struct SDL_Window;
struct TTF_Font;

// ---

using Point = std::tuple<float, float>;
using Size = std::tuple<float, float>;

struct Rect
{
    float X;
    float Y;
    float Width;
    float Height;
    
    Rect(float X, float Y, float Width, float Height);
    Rect(const Point& Position, const Size& Size);
    
    Size GetSize() const;
    void SetSize(const Size& Size);
    
    float GetLeft() const;
    void SetLeft(float Left);
    
    float GetRight() const;
    void SetRight(float Right);
    
    float GetTop() const;
    void SetTop(float Top);
    
    float GetBottom() const;
    void SetBottom(float Bottom);

    Point GetTopLeft() const;
    void SetTopLeft(const Point& TopLeft);
    
    Point GetTopRight() const;
    void SetTopRight(const Point& TopRight);
    
    Point GetBottomLeft() const;
    void SetBottomLeft(const Point& BottomLeft);
    
    Point GetBottomRight() const;
    void SetBottomRight(const Point& BottomRight);

    float GetCenterX() const;
    void SetCenterX(float CenterX);

    float GetCenterY() const;
    void SetCenterY(float CenterY);

    Point GetCenter() const;
    void SetCenter(const Point& Center);
    
    bool ContainsPoint(const Point& Point) const;
    
    std::tuple<Point, Point> IntersectLine(const Point& Start, const Point& End) const;
    
    Rect Union(const Rect& Other) const;
    Rect UnionAll(const std::vector<Rect>& Others) const;
};

namespace Time
{
    class Clock
    {
        uint64_t LastTick;
        
    public:
        Clock();
        
        void Tick(double Framerate);
    };
}

namespace Events
{
    enum class EventType
    {
        // From SDL_EventType
        Quit            = 0x100,
        KeyDown         = 0x300,
        KeyUp           = 0x301,
        MouseMotion     = 0x400,
        MouseButtonDown = 0x401,
        MouseButtonUp   = 0x402,
        FingerDown      = 0x700,
        FingerUp        = 0x701,
        FingerMotion    = 0x702
    };

    enum class KeyCode : uint32_t
    {
        // From SDL_keycode.h
        Escape = 0x0000001bu    /**< '\x1B' */
    };

    enum class MouseButton : uint32_t
    {
        // From SDL_mouse.h / SDL_touch.h
        Left = 1,
        Touch = static_cast<uint32_t>(-1)
    };

    struct KeyboardEvent
    {
        EventType Type;

        KeyCode Key;
    };

    struct MouseMotionEvent
    {
        EventType Type;

        float X;
        float Y;
        float XRelative;
        float YRelative;

        Point GetPosition() const { return { X, Y }; }
        Point GetRelativePosition() const { return { XRelative, YRelative }; }
    };

    struct MouseButtonEvent
    {
        EventType Type;

        float X;
        float Y;

        MouseButton Button;
        bool IsTouch;

        Point GetPosition() const { return { X, Y }; }
    };

    struct TouchFingerEvent
    {
        EventType Type;

        float X;
        float Y;

        uint64_t TouchID;
        uint64_t FingerID;
    };

    union Event
    {
        EventType Type;

        KeyboardEvent Key;
        MouseMotionEvent Motion;
        MouseButtonEvent Button;
        TouchFingerEvent Touch;
    };

    std::vector<Event> Get();
}

namespace Mouse
{
    std::tuple<float, float> GetPosition();
}

namespace Draw
{
    class Texture
    {
        SDL_Texture* SDLTexture;
        int Width, Height;
        
    public:
        explicit Texture(SDL_Window* Window, int Width, int Height);
        explicit Texture(SDL_Surface* Surface);
        explicit Texture(int Width, int Height);
        explicit Texture(const Size& Size);
        
        ~Texture();

        float GetWidth() const;
        float GetHeight() const;
        Rect GetRect() const;
        Texture Copy() const;
        
        void SetAlpha(int Alpha);
        void Fill(ColorPoint& Color);
        void Blit(const Texture& Source, const Rect& Region);
        void Blit(const Texture& Source, const Point& Offset);

        // Internal
        SDL_Texture& GetTexture();
    };

    void Init();
    void Flip();
    
    void DrawLine(Texture& Texture, const ColorPoint& Color, const Point& Start, const Point& End, float Width, float Alpha = 1.0f);
    void DrawRect(Texture& Texture, const ColorPoint& Color, const Rect& Rect, int BorderWidth = 0, float Alpha = 1.0f); // Fills if BorderWidth <= 0
    void DrawCircle(Texture& Texture, const ColorPoint& Color, const Point& Center, float Radius, float Alpha = 1.0f);
    void DrawPolygon(Texture& Texture, const ColorPoint& Color, const std::vector<Point>& Points, float Alpha = 1.0f);

    // Internal
    SDL_Renderer& GetRenderer();
}

namespace Display
{
    enum class WindowFlags : uint32_t
    {
        Fullscreen = (1 << 0),
        Borderless = (1 << 1)
    };
    
    void Init();
    
    std::vector<Size> GetDesktopSizes();
    std::vector<Size> ListModes(int DisplayIndex);
    
    void SetCaption(const std::string_view& Title);
    void SetIcon(const Draw::Texture& Texture);

    // Returns a Texture representing the new window surface.
    Draw::Texture SetMode(int DisplayIndex, const Size& Size, WindowFlags Flags);
    
    // Internal
    SDL_Window& GetWindow();
}

class Font
{
    TTF_Font* SDLFont;

public:
    static void Init();
    
    explicit Font(const std::string_view& FilePath, float Size);

    ~Font();
    
    int GetAscent() const;
    int GetDescent() const;
    std::tuple<int, int> EstimateGlyphHeight(char Glyph) const;

    Draw::Texture Render(const std::string_view& Text, const ColorPoint& Color) const;
};
