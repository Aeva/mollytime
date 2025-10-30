#include "sdl.h"

#include <SDL3/SDL_events.h>
#include <chrono>


using SteadyClock = std::chrono::steady_clock;
static SteadyClock::time_point LastTouchOrPen = SteadyClock::time_point();
static int LastPointerType = 0; // 0 = mouse, 1 = touch, 2 = pen


static bool AllowMouseEvent()
{
    const SteadyClock::duration IgnoreThreshold = std::chrono::seconds(1);
    static SteadyClock::time_point Now = SteadyClock::now();
    return LastPointerType == 0 || (LastTouchOrPen - Now) > IgnoreThreshold;
}


namespace Events
{
    std::vector<Event> Get()
    {
        std::vector<Event> Events;

        SDL_Event Next;
        while (SDL_PollEvent(&Next))
        {
            switch (Next.type)
            {
                case SDL_EVENT_QUIT:
                    Events.push_back(
                    {
                        .Type = EventType::Quit
                    });
                    break;
                case SDL_EVENT_KEY_DOWN:
                    Events.push_back(
                    {
                        .Key = 
                        {
                            EventType::KeyDown,
                            static_cast<KeyCode>(Next.key.key)
                        }
                    });
                    break;
                case SDL_EVENT_KEY_UP:
                    Events.push_back(
                    {
                        .Key = 
                        {
                            EventType::KeyUp,
                            static_cast<KeyCode>(Next.key.key)
                        }
                    });
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (Next.button.which != SDL_TOUCH_MOUSEID && Next.button.which != SDL_PEN_MOUSEID && AllowMouseEvent())
                    {
                        LastPointerType = 0;
                        Events.push_back(
                        {
                            .Button =
                            {
                                EventType::MouseButtonDown,
                                Next.button.x,
                                Next.button.y,
                                static_cast<MouseButton>(Next.button.button),
                                Next.button.which == SDL_TOUCH_MOUSEID
                            }
                        });
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (Next.button.which != SDL_TOUCH_MOUSEID && Next.button.which != SDL_PEN_MOUSEID && AllowMouseEvent())
                    {
                        LastPointerType = 0;
                        Events.push_back(
                        {
                            .Button =
                            {
                                EventType::MouseButtonUp,
                                Next.button.x,
                                Next.button.y,
                                static_cast<MouseButton>(Next.button.button),
                                Next.button.which == SDL_TOUCH_MOUSEID
                            }
                        });
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    if (Next.motion.which != SDL_TOUCH_MOUSEID && Next.motion.which != SDL_PEN_MOUSEID && AllowMouseEvent())
                    {
                        LastPointerType = 0;
                        Events.push_back(
                        {
                            .Motion =
                            {
                                EventType::MouseMotion,
                                Next.motion.x,
                                Next.motion.y,
                                Next.motion.xrel,
                                Next.motion.yrel
                            }
                        });
                    }
                    break;
                case SDL_EVENT_FINGER_DOWN:
                    LastPointerType = 1;
                    LastTouchOrPen = SteadyClock::now();
                    Events.push_back(
                    {
                        .Touch =
                        {
                            EventType::FingerDown,
                            Next.tfinger.x,
                            Next.tfinger.y,
                            Next.tfinger.touchID,
                            Next.tfinger.fingerID
                        }
                    });
                    break;
                case SDL_EVENT_FINGER_UP:
                    LastPointerType = 1;
                    LastTouchOrPen = SteadyClock::now();
                    Events.push_back(
                    {
                        .Touch =
                        {
                            EventType::FingerUp,
                            Next.tfinger.x,
                            Next.tfinger.y,
                            Next.tfinger.touchID,
                            Next.tfinger.fingerID
                        }
                    });
                    break;
                case SDL_EVENT_FINGER_MOTION:
                    LastPointerType = 1;
                    LastTouchOrPen = SteadyClock::now();
                    Events.push_back(
                    {
                        .Touch =
                        {
                            EventType::FingerMotion,
                            Next.tfinger.x,
                            Next.tfinger.y,
                            Next.tfinger.touchID,
                            Next.tfinger.fingerID
                        }
                    });
                    break;
                case SDL_EVENT_PEN_DOWN:
                case SDL_EVENT_PEN_UP:
                    LastPointerType = 2;
                    LastTouchOrPen = SteadyClock::now();
                    Events.push_back(
                        {
                            .Button =
                            {
                                Next.ptouch.down ? EventType::MouseButtonDown : EventType::MouseButtonUp,
                                Next.ptouch.x,
                                Next.ptouch.y,
                                static_cast<MouseButton>(SDL_BUTTON_LEFT),
                                false
                            }
                        });
                    break;
                case SDL_EVENT_PEN_MOTION:
                    LastPointerType = 2;
                    LastTouchOrPen = SteadyClock::now();
                    Events.push_back(
                        {
                            .Motion =
                            {
                                EventType::MouseMotion,
                                Next.pmotion.x,
                                Next.pmotion.y,
                                0,
                                0
                            }
                        });
                    break;
            }
        }

        return Events;
    }
}
