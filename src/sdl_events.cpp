#include "sdl.h"

#include <SDL3/SDL_events.h>
#include <print>

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
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
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
                    break;
                case SDL_EVENT_MOUSE_MOTION:
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
                    break;
                case SDL_EVENT_FINGER_DOWN:
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
#if 1
                case SDL_EVENT_PEN_DOWN:
                case SDL_EVENT_PEN_UP:
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
#endif
#if 0
                case SDL_EVENT_MOUSE_MOTION:
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
                    break;
#endif
#if 0
                case SDL_EVENT_PEN_DOWN:
                case SDL_EVENT_PEN_UP:
                    if (Next.ptouch.down)
                    {
                        std::print("pen down: {}, {}\n", Next.ptouch.x, Next.ptouch.y);
                    }
                    else
                    {
                        std::print("pen up: {}, {}\n", Next.ptouch.x, Next.ptouch.y);
                    }
                    break;
                case SDL_EVENT_PEN_MOTION:
                    // SDL pen_state flags also support five buttons and an eraser
                    if ((Next.pmotion.pen_state & SDL_PEN_INPUT_DOWN) == SDL_PEN_INPUT_DOWN)
                    {
                        std::print("pen motion: {}, {}\n", Next.pmotion.x, Next.pmotion.y);
                    }
                    break;
#endif
#if 0
                // nice-to-haves for future reference
                case SDL_EVENT_PEN_BUTTON_DOWN:
                    std::print("pen button down\n");
                    break;
                case SDL_EVENT_PEN_BUTTON_UP:
                    std::print("pen button up\n");
                    break;
                case SDL_EVENT_PEN_AXIS:
                    //std::print("pen axis\n");
                    break;
#endif
            }
        }

        return Events;
    }
}
