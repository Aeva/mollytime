#include "sdl.h"

#include <SDL3/SDL_rect.h>

#include <cassert>

Rect::Rect(float X, float Y, float Width, float Height) :
    X(X),
    Y(Y),
    Width(Width),
    Height(Height)
{ }

Rect::Rect(const Point& Position, const Size& Size) :
    X(std::get<0>(Position)),
    Y(std::get<1>(Position)),
    Width(std::get<0>(Size)),
    Height(std::get<1>(Size))
{ }

Size Rect::GetSize() const
{
    return { Width, Height };
}

void Rect::SetSize(const Size& Size)
{
    Width = std::get<0>(Size);
    Height = std::get<1>(Size);
}

float Rect::GetLeft() const
{
    return X;
}

void Rect::SetLeft(float Left)
{
    X = Left;
}

float Rect::GetRight() const
{
    return X + Width;
}

void Rect::SetRight(float Right)
{
    X = Right - Width;
}

float Rect::GetTop() const
{
    return Y;
}

void Rect::SetTop(float Top)
{
    Y = Top;
}

float Rect::GetBottom() const
{
    return Y + Height;
}

void Rect::SetBottom(float Bottom)
{
    Y = Bottom - Height;
}

Point Rect::GetTopLeft() const
{
    return { GetLeft(), GetTop() };
}

void Rect::SetTopLeft(const Point& TopLeft)
{
    SetLeft(std::get<0>(TopLeft));
    SetTop(std::get<1>(TopLeft));
}

Point Rect::GetTopRight() const
{
    return { GetRight(), GetTop() };
}

void Rect::SetTopRight(const Point& TopRight)
{
    SetRight(std::get<0>(TopRight));
    SetTop(std::get<1>(TopRight));
}

Point Rect::GetBottomLeft() const
{
    return { GetLeft(), GetBottom() };
}

void Rect::SetBottomLeft(const Point& BottomLeft)
{
    SetLeft(std::get<0>(BottomLeft));
    SetBottom(std::get<1>(BottomLeft));
}

Point Rect::GetBottomRight() const
{
    return { GetRight(), GetBottom() };
}

void Rect::SetBottomRight(const Point& BottomRight)
{
    SetRight(std::get<0>(BottomRight));
    SetBottom(std::get<1>(BottomRight));
}

float Rect::GetCenterX() const
{
    return X + (Width / 2);
}

void Rect::SetCenterX(float CenterX)
{
    X = CenterX - (Width / 2);
}

float Rect::GetCenterY() const
{
    return Y + (Height / 2);
}

void Rect::SetCenterY(float CenterY)
{
    Y = CenterY - (Height / 2);
}

Point Rect::GetCenter() const
{
    return { GetCenterX(), GetCenterY() };
}

void Rect::SetCenter(const Point& Center)
{
    const auto [CenterX, CenterY] = Center;
    
    SetCenterX(CenterX);
    SetCenterY(CenterY);
}

bool Rect::ContainsPoint(const Point& Point) const
{
    const auto [PointX, PointY] = Point;
    const SDL_FPoint SDLPoint { PointX, PointY };
    const SDL_FRect SDLRect { X, Y, Width, Height };

    return SDL_PointInRectFloat(&SDLPoint, &SDLRect);
}

std::tuple<Point, Point> Rect::IntersectLine(const Point& Start, const Point& End) const
{
    const SDL_FRect SDLRect { X, Y, Width, Height };

    auto [X1, Y1] = Start;
    auto [X2, Y2] = End;
    if (!SDL_GetRectAndLineIntersectionFloat(&SDLRect, &X1, &Y1, &X2, &Y2))
    {
        return { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    }

    return { { X1, Y1 }, { X2, Y2 } };
}

Rect Rect::Union(const Rect& Other) const
{
    const SDL_FRect SDLRect { X, Y, Width, Height };
    const SDL_FRect SDLRectOther { Other.X, Other.Y, Other.Width, Other.Height };
    
    SDL_FRect Result;
    if (!SDL_GetRectUnionFloat(&SDLRect, &SDLRectOther, &Result))
    {
        return { 0, 0, 0, 0 };
    }

    return { Result.x, Result.y, Result.w, Result.h };
}

Rect Rect::UnionAll(const std::vector<Rect>& Others) const
{
    Rect Result = *this;
    for (const Rect& Other : Others)
    {
        Result = Result.Union(Other);
    }

    return Result;
}