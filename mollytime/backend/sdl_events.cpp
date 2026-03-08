#include "sdl.h"
#include "midi.h"

#include <SDL3/SDL_events.h>

#define DEBUG_EVENTS 0
#if DEBUG_EVENTS
#include <fmt/format.h>
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

        static bool SentPatchReset = false;
        static bool SentMidiRelease = false;

        SDL_Event Next;
        while (SDL_PollEvent(&Next))
        {
            Event Event;

            switch (Next.type)
            {
                case SDL_EVENT_QUIT:
                    Event.Type = EventType::Quit;
                    Events.push_back(Event);
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    // Theoretically these are semantically separate events.
                    Event.Type = static_cast<EventType>(Next.type);
                    Event.Resize.Width = static_cast<int>(Next.window.data1);
                    Event.Resize.Height = static_cast<int>(Next.window.data2);
                    Events.push_back(Event);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (Next.key.key == SDLK_BACKSPACE)
                    {
                        if (!SentPatchReset)
                        {
                            Midi::PatchReset();
                            SentPatchReset = true;
                        }
                    }
                    else if (Next.key.key == SDLK_R)
                    {
                        if (!SentMidiRelease)
                        {
                            Midi::ReleaseHeldNotes();
                            SentMidiRelease = true;
                        }
                    }
                    else
                    {
                        Event.Type = EventType::KeyDown;
                        Event.Key.Key = static_cast<KeyCode>(Next.key.key);
                        Events.push_back(Event);
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    if (Next.key.key == SDLK_BACKSPACE)
                    {
                        if (SentPatchReset)
                        {
                            SentPatchReset = false;
                        }
                    }
                    else if (Next.key.key == SDLK_R)
                    {
                        if (SentMidiRelease)
                        {
                            SentMidiRelease = false;
                        }
                    }
                    else
                    {
                        Event.Type = EventType::KeyUp;
                        Event.Key.Key = static_cast<KeyCode>(Next.key.key);
                        Events.push_back(Event);
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
#if DEBUG_EVENTS
                    fmt::print("{}: {} MOUSE {} DOWN: {}, {}\n",
                               FrameNumber,
                               Next.button.timestamp,
                               Next.button.which,
                               Next.button.x,
                               Next.button.y);
#endif
                    if (Next.button.which != SDL_TOUCH_MOUSEID && Next.button.which != SDL_PEN_MOUSEID)
                    {
                        Event.Type = EventType::MouseButtonDown;
                        Event.Button.X = Next.button.x;
                        Event.Button.Y = Next.button.y;
                        Event.Button.Button = static_cast<MouseButton>(Next.button.button),
                        Event.Button.IsTouch = Next.button.which == SDL_TOUCH_MOUSEID;
                        Events.push_back(Event);
                    }
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
#if DEBUG_EVENTS
                    fmt::print("{}: {} MOUSE {} UP: {}, {}\n",
                               FrameNumber,
                               Next.button.timestamp,
                               Next.button.which,
                               Next.button.x,
                               Next.button.y);
#endif
                    if (Next.button.which != SDL_TOUCH_MOUSEID && Next.button.which != SDL_PEN_MOUSEID)
                    {
                        Event.Type = EventType::MouseButtonUp;
                        Event.Button.X = Next.button.x;
                        Event.Button.Y = Next.button.y;
                        Event.Button.Button = static_cast<MouseButton>(Next.button.button),
                        Event.Button.IsTouch = Next.button.which == SDL_TOUCH_MOUSEID;
                        Events.push_back(Event);
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
#if DEBUG_EVENTS
                    fmt::print("{}: {} MOUSE {} MOTION: {}, {}\n",
                               FrameNumber,
                               Next.motion.timestamp,
                               Next.motion.which,
                               Next.motion.x,
                               Next.motion.y);
#endif
                    if (Next.motion.which != SDL_TOUCH_MOUSEID && Next.motion.which != SDL_PEN_MOUSEID)
                    {
                        Event.Type = EventType::MouseMotion;
                        Event.Motion.X = Next.motion.x;
                        Event.Motion.Y = Next.motion.y;
                        Event.Motion.XRelative = Next.motion.xrel;
                        Event.Motion.YRelative = Next.motion.yrel;
                        Events.push_back(Event);
                    }
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    Event.Type = EventType::MouseWheel;
                    Event.Wheel.Horizontal = Next.wheel.x;
                    Event.Wheel.Vertical = Next.wheel.y;
                    Event.Wheel.CursorX = Next.wheel.mouse_x;
                    Event.Wheel.CursorY = Next.wheel.mouse_y;
                    Events.push_back(Event);
                    break;
                case SDL_EVENT_FINGER_DOWN:
#if DEBUG_EVENTS
                    fmt::print("{}: {} FINGER {}({}) DOWN: {}, {}, pressure: {}\n",
                               FrameNumber,
                               Next.tfinger.timestamp,
                               Next.tfinger.touchID,
                               Next.tfinger.fingerID,
                               Next.tfinger.x * float(WindowW),
                               Next.tfinger.y * float(WindowH),
                               Next.tfinger.pressure);
#endif
                    Event.Type = EventType::FingerDown;
                    Event.Touch.X = Next.tfinger.x;
                    Event.Touch.Y = Next.tfinger.y;
                    Event.Touch.TouchID = Next.tfinger.touchID;
                    Event.Touch.FingerID =  Next.tfinger.fingerID;
                    Events.push_back(Event);
                    break;
                case SDL_EVENT_FINGER_UP:
#if DEBUG_EVENTS
                    fmt::print("{}: {} FINGER {}({}) UP: {}, {}, pressure: {}\n",
                               FrameNumber,
                               Next.tfinger.timestamp,
                               Next.tfinger.touchID,
                               Next.tfinger.fingerID,
                               Next.tfinger.x * float(WindowW),
                               Next.tfinger.y * float(WindowH),
                               Next.tfinger.pressure);
#endif
                    Event.Type = EventType::FingerUp;
                    Event.Touch.X = Next.tfinger.x;
                    Event.Touch.Y = Next.tfinger.y;
                    Event.Touch.TouchID = Next.tfinger.touchID;
                    Event.Touch.FingerID =  Next.tfinger.fingerID;
                    Events.push_back(Event);
                    break;
                case SDL_EVENT_FINGER_MOTION:
#if DEBUG_EVENTS
                    fmt::print("{}: {} FINGER {}({}) MOVE: {}, {}, pressure: {}\n",
                               FrameNumber,
                               Next.tfinger.timestamp,
                               Next.tfinger.touchID,
                               Next.tfinger.fingerID,
                               Next.tfinger.x * float(WindowW),
                               Next.tfinger.y * float(WindowH),
                               Next.tfinger.pressure);
#endif
                    Event.Type = EventType::FingerMotion;
                    Event.Touch.X = Next.tfinger.x;
                    Event.Touch.Y = Next.tfinger.y;
                    Event.Touch.TouchID = Next.tfinger.touchID;
                    Event.Touch.FingerID =  Next.tfinger.fingerID;
                    Events.push_back(Event);
                    break;
                case SDL_EVENT_PEN_DOWN:
#if DEBUG_EVENTS
                    fmt::print("{}: {} PEN {} DOWN: {}, {}\n",
                               FrameNumber,
                               Next.ptouch.timestamp,
                               Next.ptouch.which,
                               Next.ptouch.x,
                               Next.ptouch.y);
#endif
                    Event.Type = EventType::MouseButtonDown;
                    Event.Button.X = Next.ptouch.x;
                    Event.Button.Y = Next.ptouch.y;
                    Event.Button.Button = static_cast<MouseButton>(SDL_BUTTON_LEFT);
                    Event.Button.IsTouch = false;
                    Events.push_back(Event);
                    break;
                case SDL_EVENT_PEN_UP:
#if DEBUG_EVENTS
                    fmt::print("{}: {} PEN {} UP: {}, {}\n",
                               FrameNumber,
                               Next.ptouch.timestamp,
                               Next.ptouch.which,
                               Next.ptouch.x,
                               Next.ptouch.y);
#endif
                    Event.Type = EventType::MouseButtonUp;
                    Event.Button.X = Next.ptouch.x;
                    Event.Button.Y = Next.ptouch.y;
                    Event.Button.Button = static_cast<MouseButton>(SDL_BUTTON_LEFT);
                    Event.Button.IsTouch = false;
                    Events.push_back(Event);
                    break;
                case SDL_EVENT_PEN_MOTION:
#if DEBUG_EVENTS
                    fmt::print("{}: {} PEN {} MOVE: {}, {}\n",
                               FrameNumber,
                               Next.pmotion.timestamp,
                               Next.pmotion.which,
                               Next.pmotion.x,
                               Next.pmotion.y);
#endif
                    Event.Type = EventType::MouseMotion;
                    Event.Motion.X = Next.pmotion.x;
                    Event.Motion.Y = Next.pmotion.y;
                    Event.Motion.XRelative = 0.0f;
                    Event.Motion.YRelative = 0.0f;
                    Events.push_back(Event);
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
                    fmt::print("{}: {} PEN {} AXIS: {}, {}, {}: {}\n",
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
