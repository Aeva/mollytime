#pragma once

#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>
#include <memory>

#include <SDL3/SDL_events.h>

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
        Quit                    = SDL_EVENT_QUIT,
        WindowResized           = SDL_EVENT_WINDOW_RESIZED,
        WindowPixelSizeChanged  = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
        KeyDown                 = SDL_EVENT_KEY_DOWN,
        KeyUp                   = SDL_EVENT_KEY_UP,
        MouseMotion             = SDL_EVENT_MOUSE_MOTION,
        MouseButtonDown         = SDL_EVENT_MOUSE_BUTTON_DOWN,
        MouseButtonUp           = SDL_EVENT_MOUSE_BUTTON_UP,
        FingerDown              = SDL_EVENT_FINGER_DOWN,
        FingerUp                = SDL_EVENT_FINGER_UP,
        FingerMotion            = SDL_EVENT_FINGER_MOTION,
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

    struct ResizeEvent
    {
        EventType Type;

        int Width;
        int Height;
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

        ResizeEvent Resize;
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
    enum class BlendModeType
    {
        None                    = 0x00000000u, // SDL_BLENDMODE_NONE
        Alpha                   = 0x00000001u, // SDL_BLENDMODE_BLEND, default
        PremultipliedAlpha      = 0x00000010u, // SDL_BLENDMODE_BLEND_PREMULTIPLIED
        Additive                = 0x00000002u, // SDL_BLENDMODE_ADD
        PremultipliedAdditive   = 0x00000020u, // SDL_BLENDMODE_BLEND_PREMULTIPLIED
        Modulate                = 0x00000004u, // SDL_BLENDMODE_MOD
        Multiply                = 0x00000008u, // SDL_BLENDMODE_MUL

        Eraser                  = 0xFFFFu - 1u, // this is not a SDL blend mode, and will be substituted
        InverseEraser           = 0xFFFFu - 2u, // this is not a SDL blend mode, and will be substituted
    };

    struct TextureCaddy
    {
        SDL_Texture* SDLTexture = nullptr;
        TextureCaddy(SDL_Texture* InSDLTexture);
        ~TextureCaddy();
    };

    class Texture
    {
        std::shared_ptr<TextureCaddy> Handle;
        int Width, Height;
        
    public:
        // When passed no arguments, this allows drawing and blitting on the rendering surface.
        Texture();
        Texture(SDL_Surface* Surface);
        Texture(int Width, int Height);
        Texture(const Size& Size);
        
        float GetWidth() const;
        float GetHeight() const;
        Rect GetRect() const;
        Texture Copy() const;
        
        void SetAlpha(float Alpha);
        void SetBlendMode(BlendModeType BlendMode = BlendModeType::Alpha);
        void Fill(ColorPoint& Color, float Alpha = 1.0f);
        void FillRect(ColorPoint& Color, const Rect& Region, float Alpha = 1.0f);
        void Blit(const Texture& Source, const Rect& Region);
        void Blit(const Texture& Source, const Point& Offset);

        // Internal
        SDL_Texture* GetTexture() const;
    };

    void Init();
    void Flip();
    const std::string_view GetRendererName();
    Texture GetRenderingSurface();
    
    void DrawLine(Texture& Texture, const ColorPoint& Color, const Point& Start, const Point& End, float Width, float Alpha = 1.0f);
    void DrawRect(Texture& Texture, const ColorPoint& Color, const Rect& Rect, int BorderWidth = 0, float Alpha = 1.0f); // Fills if BorderWidth <= 0
    void DrawCircle(Texture& Texture, const ColorPoint& Color, const Point& Center, float Radius, float Alpha = 1.0f);
    void DrawPolygon(Texture& Texture, const ColorPoint& Color, const std::vector<Point>& Points, float Alpha = 1.0f);

    // Internal
    SDL_Renderer* GetRenderer();
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
    void SetMode(int DisplayIndex, const Size& Size, WindowFlags Flags);
    
    // Internal
    SDL_Window* GetWindow();

    void ShowLoadDialog(const std::string_view& PatchDir);
    void ShowSaveDialog(const std::string_view& PatchDir);
    std::tuple<int, std::string_view> GetLoadDialogResult();
    std::tuple<int, std::string_view> GetSaveDialogResult();
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
