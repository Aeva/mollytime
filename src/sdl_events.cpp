#include "sdl.h"
#include "midi.h"

#include <SDL3/SDL_events.h>

#define DEBUG_EVENTS 0
#if DEBUG_EVENTS
#include <print>
#endif


namespace Events
{
    std::vector<Event> Get()
    {
#if DEBUG_EVENTS
        int WindowW = 0;
        int WindowH = 0;
        SDL_GetWindowSizeInPixels(Display::GetWindow(), &WindowW, &WindowH);
        static uint32_t FrameNumber = 0;
#endif
        std::vector<Event> Events;

        static bool SentMidiReset = false;

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
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    Events.push_back(
                    {
                        .Resize =
                        {
                            // Theoretically these are semantically separate events.
                            static_cast<EventType>(Next.type),
                            static_cast<int>(Next.window.data1),
                            static_cast<int>(Next.window.data2)
                        }
                    });
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (Next.key.key == SDLK_C)
                    {
                        if (!SentMidiReset)
                        {
                            Midi::Reset();
                            SentMidiReset = true;
                        }
                    }
                    else
                    {
                        Events.push_back(
                        {
                            .Key =
                            {
                                EventType::KeyDown,
                                static_cast<KeyCode>(Next.key.key)
                            }
                        });
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    if (Next.key.key == SDLK_C)
                    {
                        if (SentMidiReset)
                        {
                            SentMidiReset = false;
                        }
                    }
                    else
                    {
                        Events.push_back(
                        {
                            .Key =
                            {
                                EventType::KeyUp,
                                static_cast<KeyCode>(Next.key.key)
                            }
                        });
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
#if DEBUG_EVENTS
                    std::print("{}: {} MOUSE {} DOWN: {}, {}\n",
                               FrameNumber,
                               Next.button.timestamp,
                               Next.button.which,
                               Next.button.x,
                               Next.button.y);
#endif
                    if (Next.button.which != SDL_TOUCH_MOUSEID && Next.button.which != SDL_PEN_MOUSEID)
                    {
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
#if DEBUG_EVENTS
                    std::print("{}: {} MOUSE {} UP: {}, {}\n",
                               FrameNumber,
                               Next.button.timestamp,
                               Next.button.which,
                               Next.button.x,
                               Next.button.y);
#endif
                    if (Next.button.which != SDL_TOUCH_MOUSEID && Next.button.which != SDL_PEN_MOUSEID)
                    {
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
#if DEBUG_EVENTS
                    std::print("{}: {} MOUSE {} MOTION: {}, {}\n",
                               FrameNumber,
                               Next.motion.timestamp,
                               Next.motion.which,
                               Next.motion.x,
                               Next.motion.y);
#endif
                    if (Next.motion.which != SDL_TOUCH_MOUSEID && Next.motion.which != SDL_PEN_MOUSEID)
                    {
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
                case SDL_EVENT_MOUSE_WHEEL:
                    Events.push_back(
                        {
                            .Wheel =
                            {
                                EventType::MouseWheel,
                                Next.wheel.x,
                                Next.wheel.y,
                                Next.wheel.mouse_x,
                                Next.wheel.mouse_y
                            }
                        });
                    break;
                case SDL_EVENT_FINGER_DOWN:
#if DEBUG_EVENTS
                    std::print("{}: {} FINGER {}({}) DOWN: {}, {}, pressure: {}\n",
                               FrameNumber,
                               Next.tfinger.timestamp,
                               Next.tfinger.touchID,
                               Next.tfinger.fingerID,
                               Next.tfinger.x * float(WindowW),
                               Next.tfinger.y * float(WindowH),
                               Next.tfinger.pressure);
#endif
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
#if DEBUG_EVENTS
                    std::print("{}: {} FINGER {}({}) UP: {}, {}, pressure: {}\n",
                               FrameNumber,
                               Next.tfinger.timestamp,
                               Next.tfinger.touchID,
                               Next.tfinger.fingerID,
                               Next.tfinger.x * float(WindowW),
                               Next.tfinger.y * float(WindowH),
                               Next.tfinger.pressure);
#endif
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
#if DEBUG_EVENTS
                    std::print("{}: {} FINGER {}({}) MOVE: {}, {}, pressure: {}\n",
                               FrameNumber,
                               Next.tfinger.timestamp,
                               Next.tfinger.touchID,
                               Next.tfinger.fingerID,
                               Next.tfinger.x * float(WindowW),
                               Next.tfinger.y * float(WindowH),
                               Next.tfinger.pressure);
#endif
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
#if DEBUG_EVENTS
                    std::print("{}: {} PEN {} DOWN: {}, {}\n",
                               FrameNumber,
                               Next.ptouch.timestamp,
                               Next.ptouch.which,
                               Next.ptouch.x,
                               Next.ptouch.y);
#endif
                    Events.push_back(
                        {
                            .Button =
                            {
                                EventType::MouseButtonDown,
                                Next.ptouch.x,
                                Next.ptouch.y,
                                static_cast<MouseButton>(SDL_BUTTON_LEFT),
                                     false
                            }
                        });
                    break;
                case SDL_EVENT_PEN_UP:
#if DEBUG_EVENTS
                    std::print("{}: {} PEN {} UP: {}, {}\n",
                               FrameNumber,
                               Next.ptouch.timestamp,
                               Next.ptouch.which,
                               Next.ptouch.x,
                               Next.ptouch.y);
#endif
                    Events.push_back(
                        {
                            .Button =
                            {
                                EventType::MouseButtonUp,
                                Next.ptouch.x,
                                Next.ptouch.y,
                                static_cast<MouseButton>(SDL_BUTTON_LEFT),
                                false
                            }
                        });
                    break;
                case SDL_EVENT_PEN_MOTION:
#if DEBUG_EVENTS
                    std::print("{}: {} PEN {} MOVE: {}, {}\n",
                               FrameNumber,
                               Next.pmotion.timestamp,
                               Next.pmotion.which,
                               Next.pmotion.x,
                               Next.pmotion.y);
#endif
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
                case SDL_EVENT_PEN_AXIS:
#if DEBUG_EVENTS
                    // NOTE: the hardware I have on hand only reports pressure, xtilt, and ytilt.
                    std::string Label = "???";
                    switch (Next.paxis.axis)
                    {
                        case SDL_PEN_AXIS_PRESSURE:
                            Label = "pressure";
                            break;
                        case SDL_PEN_AXIS_XTILT:
                            Label = "tilt_X";
                            break;
                        case SDL_PEN_AXIS_YTILT:
                            Label = "tilt_y";
                            break;
                        case SDL_PEN_AXIS_DISTANCE:
                            Label = "dist";
                            break;
                        case SDL_PEN_AXIS_ROTATION:
                            Label = "rotation";
                            break;
                        case SDL_PEN_AXIS_SLIDER:
                            Label = "slider";
                            break;
                        case SDL_PEN_AXIS_TANGENTIAL_PRESSURE:
                            Label = "squeeze";
                            break;
                    }
                    std::print("{}: {} PEN {} AXIS: {}, {}, {}: {}\n",
                               FrameNumber,
                               Next.paxis.timestamp,
                               Next.paxis.which,
                               Next.paxis.x,
                               Next.paxis.y,
                               Label,
                               Next.paxis.value);
#endif
                    break;
            }
        }
#if DEBUG_EVENTS
        ++FrameNumber;
#endif
        return Events;
    }
}
