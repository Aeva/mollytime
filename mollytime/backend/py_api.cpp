
// Copyright 2025 Aeva Palecek
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <cstdint>
#include <stdexcept>
#include <algorithm>

#include <fmt/format.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshadow-field-in-constructor"
#pragma clang diagnostic ignored "-Wlanguage-extension-token"

#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/set.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>
#include <nanobind/stl/vector.h>
#pragma clang diagnostic pop

#include "colors.h"
#include "patch.h"
#include "audio_driver.h"
#include "perf.h"
#include "sdl.h"
#include "config.h"

#include "alsa_midi.h"
#include "mmeapi_midi.h"
#include "stub_midi.h"

#include "jack_stream.h"
#include "sdl_stream.h"
#include "stub_stream.h"
#include "wasapi_stream.h"


namespace nb = nanobind;

using ColorTuple = std::tuple<float, float, float>;
using ColorArray = std::array<float, 3>;


static int ColorPointGetItem(ColorPoint& Color, int Index)
{
	if (Index >=0 && Index < 3)
	{
		if (Color.Encoding != ColorSpace::sRGB)
		{
			Color.MutateEncoding(ColorSpace::sRGB);
		}
		return std::min(std::max(int(Color.Channels[Index] * 255.0f), 0), 255);
	}

	throw nb::index_error(fmt::format("Index out of range: {}\n", Index).c_str());
}


static std::string ColorPointRepr(ColorPoint& Color)
{
	return fmt::format("<ColorPoint {}: ({}, {}, {})>",
		ColorSpaceName(Color.Encoding),
		Color.Channels[0], Color.Channels[1], Color.Channels[2]);
}


static ColorTuple ColorPointGetChannels(ColorPoint& Color)
{
	return { Color.Channels[0], Color.Channels[1], Color.Channels[2] };
}


static ColorPoint ConvertColor(ColorArray InColor, ColorSpace Incoding, ColorSpace Excoding)
{
	ColorPoint Color{Incoding, InColor};
	return ColorPoint(Excoding, Color);
}


static ColorPoint PyParseColor(std::string ColorString)
{
	ColorPoint Color;
	StatusCode Result = ParseColor(ColorString, Color);
	if (Result != StatusCode::PASS)
	{
		throw std::domain_error(fmt::format("Invalid color string \"{}\"\n", ColorString));
	}
	return Color;
}


static ColorPoint MakeOkLAB(float L, float A, float B)
{
	return ColorPoint(ColorSpace::OkLAB, glm::vec3(L, A, B));
}


static ColorPoint MakeOkLCH(float L, float C, float H)
{
	return ColorPoint(ColorSpace::OkLCH, glm::vec3(L, C, H));
}


static ColorPoint MakeHSL(float H, float S, float L)
{
	return ColorPoint(ColorSpace::HSL, glm::vec3(H, S, L));
}


template<typename T>
static void BindAudioDriver(nb::module_& Module, const std::string_view& ClassName)
{
    if constexpr (T::IsAvailable())
    {
        nb::class_<T, AudioStream>(Module, ClassName.data())
            .def(nb::init<int>())
            .def_static("is_available", &T::IsAvailable)
            .def_static("get_name", &T::GetName);
    }
    else
    {
#pragma warning(push)
#pragma warning(disable : 5046) // "Symbol involving type with internal linkage not defined" - This is deliberate. The type cannot be instantiated.
        class UndefinedPlaceholder { UndefinedPlaceholder(); };
#pragma warning(pop)
        nb::class_<UndefinedPlaceholder>(Module, ClassName.data(), nb::never_destruct())
            .def_static("is_available", &T::IsAvailable)
            .def_static("get_name", &T::GetName);
    }
}


template<typename T>
static void BindMidiDriver(nb::module_& Module, const std::string_view& ClassName)
{
    if constexpr (T::IsAvailable())
    {
        nb::class_<T, MidiDriver>(Module, ClassName.data())
            .def(nb::init<>())
            .def_static("is_available", &T::IsAvailable)
            .def_static("get_name", &T::GetName);
    }
    else
    {
#pragma warning(push)
#pragma warning(disable : 5046) // "Symbol involving type with internal linkage not defined" - This is deliberate. The type cannot be instantiated.
        class UndefinedPlaceholder { UndefinedPlaceholder(); };
#pragma warning(pop)
        nb::class_<UndefinedPlaceholder>(Module, ClassName.data(), nb::never_destruct())
            .def_static("is_available", &T::IsAvailable)
            .def_static("get_name", &T::GetName);
    }
}


NB_MODULE(backend, m) {
	m.doc() = "mollytime c++ internals";

	nb::enum_<ColorSpace>(m, "ColorSpace")
		.value("sRGB", ColorSpace::sRGB)
		.value("LinearRGB", ColorSpace::LinearRGB)
		.value("OkLAB", ColorSpace::OkLAB)
		.value("OkLCH", ColorSpace::OkLCH)
		.value("HSL", ColorSpace::HSL);

	nb::class_<ColorPoint>(m, "ColorPoint")
		.def(nb::init<>())
        .def(nb::init<std::tuple<uint8_t, uint8_t, uint8_t>>())
        .def(nb::init<std::tuple<float, float, float>>())
		.def("__len__", [](const ColorPoint& Self) -> int { return 3; })
		.def("__getitem__", &ColorPointGetItem)
		.def("__repr__", &ColorPointRepr)
		.def_prop_ro("channels", &ColorPointGetChannels)
		.def_ro("encoding", &ColorPoint::Encoding)
		.def("encode", &ColorPoint::Encode);

	nb::class_<ColorRamp>(m, "ColorRamp")
		.def(nb::init<std::vector<ColorPoint> &>())
		.def("sample", &ColorRamp::Sample);

	m.def("convert_color", &ConvertColor, "color space converter");
	m.def("parse_color", &PyParseColor, "CSS color parser");
	m.def("oklab", &MakeOkLAB, "OkLAB color constructor");
	m.def("oklch", &MakeOkLCH, "OkLCH color constructor");
	m.def("hsl", &MakeHSL, "HSL color constructor");
	m.def("set_gamma", &SetGamma, "Change the sRGB gamma exponent");
	m.def("mix_lchab", &MixLCHAB, "Color blending in both OkLCH and OkLAB space");

	m.def("profiling_enabled", &IsProfilingEnabled);
	m.def("profiling_scope", [](const char* Name, nb::handle Thunk) -> nb::object
	{
		nb::object Result;
		PerfTrampoline(Name, Thunk, Result);
		return Result;
	});

	nb::enum_<OpCode>(m, "OpCode", nb::is_arithmetic())
		.value("GO", OpCode::GO)
		.value("CONST", OpCode::CONST)
		.value("SCOPE", OpCode::SCOPE)
		.value("IN", OpCode::IN)
		.value("OUT", OpCode::OUT)
		.value("AUX", OpCode::AUX)
		.value("SIN", OpCode::SIN)
		.value("SQR", OpCode::SQR)
		.value("TRI", OpCode::TRI)
		.value("SAW", OpCode::SAW)
		.value("NOI", OpCode::NOI)
		.value("PHASE", OpCode::PHASE)
		.value("SIN_TRAIN", OpCode::SIN_TRAIN)
		.value("TRI_TRAIN", OpCode::TRI_TRAIN)
		.value("SQR_TRAIN", OpCode::SQR_TRAIN)
		.value("SAW_TRAIN", OpCode::SAW_TRAIN)
		.value("PWM", OpCode::PWM)
		.value("ADD", OpCode::ADD)
		.value("MUL", OpCode::MUL)
		.value("RCP", OpCode::RCP)
		.value("POW", OpCode::POW)
		.value("SPOW", OpCode::SPOW)
		.value("MIN", OpCode::MIN)
		.value("MAX", OpCode::MAX)
		.value("CLAMP", OpCode::CLAMP)
		.value("FLOOR", OpCode::FLOOR)
		.value("CEIL", OpCode::CEIL)
		.value("ROUND", OpCode::ROUND)
		.value("SIGN", OpCode::SIGN)
		.value("ABS", OpCode::ABS)
		.value("FLD", OpCode::FLD)
		.value("INV", OpCode::INV)
		.value("STU", OpCode::STU)
		.value("UTS", OpCode::UTS)
		.value("MIX", OpCode::MIX)
		.value("WTN", OpCode::WTN)
		.value("BAL", OpCode::BAL)
		.value("PLS", OpCode::PLS)
		.value("FLP", OpCode::FLP)
		.value("RNG", OpCode::RNG)
		.value("GRAD", OpCode::GRAD)
		.value("TPTSVF_LOWPASS", OpCode::TPTSVF_LOWPASS)
		.value("TPTSVF_BANDPASS", OpCode::TPTSVF_BANDPASS)
		.value("TPTSVF_HIGHPASS", OpCode::TPTSVF_HIGHPASS)
		.value("TPTSVF_NOTCH", OpCode::TPTSVF_NOTCH)
		.value("ADSR", OpCode::ADSR)
		.value("QNTZ", OpCode::QNTZ)
		.value("ISQN", OpCode::ISQN)
		.value("RSQN", OpCode::RSQN)
		.value("GATE", OpCode::GATE)
		.value("NOTE", OpCode::NOTE)
		.value("VELO", OpCode::VELO)
		.value("PRES", OpCode::PRES)
		.value("CTRL", OpCode::CTRL)
		.value("CHAN", OpCode::CHAN)
		.value("KIKI", OpCode::KIKI)
		.value("BEND", OpCode::BEND)
		.value("LANE_COUNT", OpCode::LANE_COUNT)
		.value("LEAD_LANE", OpCode::LEAD_LANE)
		.value("ADD_LANES", OpCode::ADD_LANES)
		.value("MIDI_HZ", OpCode::MIDI_HZ)
		.value("LOUD_FUDGE", OpCode::LOUD_FUDGE)
		.value("BOOP", OpCode::BOOP)
		.value("TWEAK", OpCode::TWEAK)
		.value("TAPE_LOOP", OpCode::TAPE_LOOP)
		.value("MOON", OpCode::MOON)
		.value("Count", OpCode::Count);

	m.def("make_port_handle", &MakePortHandle);
	m.def("decode_port_tile", &PortHandleTilePart);
	m.def("decode_port_index", &PortHandlePortIndexPart);
	m.def("get_symbol_name", &GetDefaultName);
	m.def("set_default_polyphony", &SetDefaultPolyphony);

	m.def("get_tile_availability", &GetTileAvailability);

	nb::class_<Patch>(m, "Patch")
		.def(nb::init<>())
		.def_prop_rw("midi_lanes", &Patch::GetPolyphony, &Patch::SetPolyphony)
		.def_ro("wires", &Patch::Wires)
		.def("make_tile", [](Patch& Self, OpCode Symbol) -> TileHandle
		{
			return Self.MakeTile(Symbol);
		})
		.def("make_constant", [](Patch& Self, double Value) -> TileHandle
		{
			return Self.MakeTile(Value);
		})
		.def("erase_tile", &Patch::EraseTile)
		.def("get_all_tile_handles", &Patch::GetAllTileHandles)
		.def("get_tile_symbol", &Patch::GetTileSymbol)
		.def("get_tile_name", &Patch::GetTileName)
		.def("set_tile_name", &Patch::SetTileName)
		.def("get_constant", &Patch::GetConstant)
		.def("set_constant", &Patch::SetConstant)
		.def("get_tile_label", &Patch::GetTileLabel)
		.def("get_tile_input_ports", &Patch::GetTileInputPorts)
		.def("get_tile_output_ports", &Patch::GetTileOutputPorts)
		.def("get_input_port_name", &Patch::GetTileInputName)
		.def("get_output_port_name", &Patch::GetTileOutputName)
		.def("get_tile_polyphony", &Patch::GetTilePolyphony)
		.def("get_tile_is_constant", &Patch::GetTileIsConstant)
		.def("freeze", &Patch::Freeze)
		.def("unfreeze", &Patch::Unfreeze)
		.def("get_frozen", &Patch::GetFrozen)
		.def("connect_tiles", &Patch::Connect)
		.def("disconnect_tiles", &Patch::Disconnect)
		.def("toggle_connection", &Patch::ToggleConnection)
		.def("can_connect", &Patch::CanConnect)
		.def("get_implicit_wire", &Patch::GetImplicitWire)
		.def("read_output_probe", &Patch::ReadOutputProbe)
		.def("read_scope_probe", &Patch::ReadScopeProbe)
		.def("set_active_probe", &Patch::SetActiveProbe)
		.def("clear_active_probe", &Patch::ClearActiveProbe)
		.def("set_special_input", &Patch::SetSpecialInput)
		.def("add_special_input", &Patch::AddSpecialInput)
		.def("add_range_special_input", &Patch::AddRangeSpecialInput)
		.def("get_special_input", &Patch::GetSpecialInput)
		.def("get_channel_mask", &Patch::GetChannelMask)
		.def("set_channel_mask", &Patch::SetChannelMask);

	m.def("init_config", &Config::Init);
	m.def("get_game_data_folder", &Config::GetGameDataFolder);
	m.def("get_game_config_folder", &Config::GetGameConfigFolder);
	m.def("get_read_only_mode", &Config::GetReadOnly);

    nb::class_<AudioStream> _AudioStream(m, "AudioStream");
    BindAudioDriver<SDLStream>(m, "SDLStream");
    BindAudioDriver<JackStream>(m, "JackStream");
    BindAudioDriver<WasapiStream>(m, "WasapiStream");
    BindAudioDriver<StubStream>(m, "StubStream");

	m.def("init_audio", &Audio::Init);
	m.def("shutdown_audio", &Audio::Shutdown);
	m.def("get_temporal_pressure", &Audio::GetTemporalPressure);

    nb::class_<MidiDriver> _MidiDriver(m, "MidiDriver");
    BindMidiDriver<AlsaMidiDriver>(m, "AlsaMidiDriver");
    BindMidiDriver<MmeApiMidiDriver>(m, "MmeApiMidiDriver");
    BindMidiDriver<StubMidiDriver>(m, "StubMidiDriver");

	m.def("init_midi", &Midi::Init);
	m.def("shutdown_midi", &Midi::Shutdown);

    // ---
    // Core types
    
    nb::implicitly_convertible<std::tuple<uint8_t, uint8_t, uint8_t>, ColorPoint>();
    nb::implicitly_convertible<std::tuple<float, float, float>, ColorPoint>();

    nb::class_<Rect>(m, "Rect")
        .def(nb::init<float, float, float, float>())
        .def(nb::init<Point, Size>())
        .def("copy", [](const Rect& rect) { return Rect(rect); })
        .def_rw("x", &Rect::X)
        .def_rw("y", &Rect::Y)
        .def_rw("w", &Rect::Width)
        .def_rw("width", &Rect::Width)
        .def_rw("h", &Rect::Height)
        .def_rw("height", &Rect::Height)
        .def_prop_rw("size", &Rect::GetSize, &Rect::SetSize)
        .def_prop_rw("left", &Rect::GetLeft, &Rect::SetLeft)
        .def_prop_rw("right", &Rect::GetRight, &Rect::SetRight)
        .def_prop_rw("top", &Rect::GetTop, &Rect::SetTop)
        .def_prop_rw("bottom", &Rect::GetBottom, &Rect::SetBottom)
        .def_prop_rw("topleft", &Rect::GetTopLeft, &Rect::SetTopLeft)
        .def_prop_rw("topright", &Rect::GetTopRight, &Rect::SetTopRight)
        .def_prop_rw("bottomleft", &Rect::GetBottomLeft, &Rect::SetBottomLeft)
        .def_prop_rw("bottomright", &Rect::GetBottomRight, &Rect::SetBottomRight)
        .def_prop_rw("centerx", &Rect::GetCenterX, &Rect::SetCenterX)
        .def_prop_rw("centery", &Rect::GetCenterY, &Rect::SetCenterY)
        .def_prop_rw("center", &Rect::GetCenter, &Rect::SetCenter)
        .def("collidepoint", &Rect::ContainsPoint)
        .def("clipline", &Rect::IntersectLine)
        .def("union", &Rect::Union)
        .def("unionall", &Rect::UnionAll);
    
    // ---
    // Time

    nb::module_ time = m.def_submodule("time");
    nb::class_<Time::Clock>(time, "Clock")
        .def(nb::init<>())
        .def("tick", &Time::Clock::Tick);
    
    // ---
    // Events

    nb::module_ events = m.def_submodule("events");

    nb::enum_<Events::EventType>(events, "Type", nb::is_arithmetic())
        .value("QUIT",              Events::EventType::Quit)
        .value("WINDOWRESIZE",      Events::EventType::WindowResized)
        .value("PIXELSIZECHANGED",  Events::EventType::WindowPixelSizeChanged)
        .value("KEYDOWN",           Events::EventType::KeyDown)
        .value("KEYUP",             Events::EventType::KeyUp)
        .value("MOUSEMOTION",       Events::EventType::MouseMotion)
        .value("MOUSEBUTTONDOWN",   Events::EventType::MouseButtonDown)
        .value("MOUSEBUTTONUP",     Events::EventType::MouseButtonUp)
		.value("MOUSEWHEEL",		Events::EventType::MouseWheel)
        .value("FINGERDOWN",        Events::EventType::FingerDown)
        .value("FINGERUP",          Events::EventType::FingerUp)
        .value("FINGERMOTION",      Events::EventType::FingerMotion)
        .export_values();
    
    nb::enum_<Events::KeyCode>(events, "KeyCode", nb::is_arithmetic(), nb::is_flag())
        .value("K_ESCAPE", Events::KeyCode::Escape)
        .value("K_F", Events::KeyCode::F)
        .value("K_F11", Events::KeyCode::F11)
        .export_values();
    
    nb::enum_<Events::MouseButton>(events, "MouseButton", nb::is_arithmetic(), nb::is_flag())
        .value("BUTTON_LEFT", Events::MouseButton::Left)
        .export_values();

    nb::class_<Events::ResizeEvent>(events, "ResizeEvent")
        .def_ro("Width", &Events::ResizeEvent::Width)
        .def_ro("Height", &Events::ResizeEvent::Height);
    
    nb::class_<Events::KeyboardEvent>(events, "KeyboardEvent")
        .def_ro("key", &Events::KeyboardEvent::Key);
    
    nb::class_<Events::MouseMotionEvent>(events, "MouseMotionEvent")
        .def_ro("x", &Events::MouseMotionEvent::X)
        .def_ro("y", &Events::MouseMotionEvent::X)
        .def_ro("xrel", &Events::MouseMotionEvent::XRelative)
        .def_ro("yrel", &Events::MouseMotionEvent::YRelative)
        .def_prop_ro("pos", &Events::MouseMotionEvent::GetPosition)
        .def_prop_ro("rel", &Events::MouseMotionEvent::GetRelativePosition);
    
    nb::class_<Events::MouseButtonEvent>(events, "MouseButtonEvent")
        .def_ro("x", &Events::MouseButtonEvent::X)
        .def_ro("y", &Events::MouseButtonEvent::Y)
        .def_ro("button", &Events::MouseButtonEvent::Button)
        .def_ro("touch", &Events::MouseButtonEvent::IsTouch)
        .def_prop_ro("pos", &Events::MouseButtonEvent::GetPosition);

		nb::class_<Events::MouseWheelEvent>(events, "MouseWheelEvent")
		.def_ro("horizontal", &Events::MouseWheelEvent::Horizontal)
		.def_ro("vertical", &Events::MouseWheelEvent::Vertical)
		.def_ro("cursor_x", &Events::MouseWheelEvent::CursorX)
		.def_ro("cursor_y", &Events::MouseWheelEvent::CursorY)
		.def_prop_ro("pos", &Events::MouseWheelEvent::GetPosition);
    
    nb::class_<Events::TouchFingerEvent>(events, "TouchFingerEvent")
        .def_ro("x", &Events::TouchFingerEvent::X)
        .def_ro("y", &Events::TouchFingerEvent::Y)
        .def_ro("touch_id", &Events::TouchFingerEvent::TouchID)
        .def_ro("finger_id", &Events::TouchFingerEvent::FingerID);
    
    nb::class_<Events::Event>(events, "Event")
        .def_ro("type", &Events::Event::Type)
        .def_ro("resize", &Events::Event::Resize)
        .def_ro("key", &Events::Event::Key)
        .def_ro("motion", &Events::Event::Motion)
        .def_ro("button", &Events::Event::Button)
		.def_ro("wheel", &Events::Event::Wheel)
        .def_ro("tfinger", &Events::Event::Touch);
    
    events.def("get", &Events::Get);
    
    // ---
    // Mouse

    nb::module_ mouse = m.def_submodule("mouse")
        .def("get_pos", &Mouse::GetPosition);

    // ---
    // Display

    nb::module_ display = m.def_submodule("display");

    nb::enum_<Display::WindowFlags>(display, "WindowFlags", nb::is_arithmetic(), nb::is_flag())
        .value("FULLSCREEN", Display::WindowFlags::Fullscreen)
        .value("BORDERLESS", Display::WindowFlags::Borderless)
        .export_values();
    
    display
        .def("init", &Display::Init)
        .def("get_current_display_index", &Display::GetCurrentDisplayIndex)
        .def("get_desktop_sizes", &Display::GetDesktopSizes)
        .def("list_modes", &Display::ListModes, nb::arg("display"))
        .def("set_caption", &Display::SetCaption)
        .def("set_icon", &Display::SetIcon)
        .def("toggle_fullscreen", &Display::ToggleFullscreen)
        .def("get_resolution_scale", &Display::GetResolutionScale)
        .def("show_load_dialog", &Display::ShowLoadDialog)
        .def("show_save_dialog", &Display::ShowSaveDialog)
        .def("get_load_dialog_result", &Display::GetLoadDialogResult)
        .def("get_save_dialog_result", &Display::GetSaveDialogResult);
    
    // ---
    // Draw

    nb::module_ draw = m.def_submodule("draw");

    using BlitRectFunc = void (Draw::Texture::*)(const Draw::Texture&, const Rect&);
    using BlitPointFunc = void (Draw::Texture::*)(const Draw::Texture&, const Point&);

	nb::enum_<Draw::BlendModeType>(draw, "Type", nb::is_arithmetic(), nb::is_flag())
		.value("none", Draw::BlendModeType::None)
		.value("alpha", Draw::BlendModeType::Alpha)
		.value("premultiplied_alpha", Draw::BlendModeType::PremultipliedAlpha)
		.value("additive", Draw::BlendModeType::Additive)
		.value("premultiplied_additive", Draw::BlendModeType::PremultipliedAdditive)
		.value("modulate", Draw::BlendModeType::Modulate)
		.value("multiply", Draw::BlendModeType::Multiply)
		.value("eraser", Draw::BlendModeType::Eraser)
		.value("inverse_eraser", Draw::BlendModeType::InverseEraser)
		.export_values();

    nb::class_<Draw::Texture>(draw, "Texture")
        .def(nb::init<int, int>())
        .def(nb::init<const Size&>())
        .def("get_width", &Draw::Texture::GetWidth)
        .def("get_height", &Draw::Texture::GetHeight)
        .def("get_rect", &Draw::Texture::GetRect)
        .def("copy", &Draw::Texture::Copy)
        .def("set_alpha", &Draw::Texture::SetAlpha)
        .def("set_blend_mode", &Draw::Texture::SetBlendMode)
        .def("fill", &Draw::Texture::Fill, nb::arg("color"), nb::arg("alpha") = 1.0f)
        .def("fill_rect", &Draw::Texture::FillRect, nb::arg("color"), nb::arg("rect"), nb::arg("alpha") = 1.0f)
        .def("blit", static_cast<BlitRectFunc>(&Draw::Texture::Blit))
        .def("blit", static_cast<BlitPointFunc>(&Draw::Texture::Blit));
    
    draw
        .def("init", &Draw::Init)
        .def("flip", &Draw::Flip)
        .def("get_renderer_name", &Draw::GetRendererName)
        .def("get_renderer_ready", &Draw::GetRendererReady)
        .def("get_rendering_surface", &Draw::GetRenderingSurface)
        .def("line", &Draw::DrawLine, nb::arg("texture"), nb::arg("color"), nb::arg("start"), nb::arg("end"), nb::arg("width") = 1.0f, nb::arg("alpha") = 1.0f)
        .def("rect", &Draw::DrawRect, nb::arg("texture"), nb::arg("color"), nb::arg("rect"), nb::arg("depth") = 0, nb::arg("alpha") = 1.0f)
        .def("pie", &Draw::DrawPie, nb::arg("texture"), nb::arg("color"), nb::arg("center"), nb::arg("radius"), nb::arg("angre"), nb::arg("arc"), nb::arg("alpha") = 1.0f)
        .def("circle", &Draw::DrawCircle, nb::arg("texture"), nb::arg("color"), nb::arg("center"), nb::arg("radius"), nb::arg("alpha") = 1.0f)
        .def("polygon", &Draw::DrawPolygon, nb::arg("texture"), nb::arg("color"), nb::arg("points"), nb::arg("alpha") = 1.0f);

    // ---
    // Font

    nb::module_ font = m.def_submodule("font")
        .def("init", &Font::Init);
    
    nb::class_<Font> (font, "Font")
        .def(nb::init<const std::string_view&, float>())
        .def("get_ascent", &Font::GetAscent)
        .def("get_descent", &Font::GetDescent)
        .def("estimate_glyph_height", &Font::EstimateGlyphHeight)
        .def("render", &Font::Render);
}
