
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
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma warning(push)
#pragma warning(disable : 4191 4355 4371 4464 4686 4868 5039)
#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>
#pragma warning(pop)
#pragma GCC diagnostic pop
#pragma clang diagnostic pop

#include "colors.h"
#include "patch.h"
#include "audio_driver.h"
#include "alsa_midi.h"
#include "perf.h"
#include "sdl.h"
#include "config.h"

namespace py = pybind11;

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

	throw pybind11::index_error(fmt::format("Index out of range: {}\n", Index));
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


// HACK: Suppress an otherwise-unsuppressable GCC `-pedantic `warning by passing an (unused) value
// to PYBIND11_MODULE's variadic macro parameter. (If this isn't one of pybind11's defined
// options, it won't alter program beahvior.)
//
// The warning is: "ISO C++11 requires at least one argument for the "..." in a variadic macro"
// This is nonetheless supported in every C and C++ compiler since forever. Clang and MSVC allow
// us to suppress this with individual pragmas, but GCC doesn't.
PYBIND11_MODULE(backend, m, 0) {
	m.doc() = "mollytime c++ internals";

	py::native_enum<ColorSpace>(m, "ColorSpace", "enum.Enum")
		.value("sRGB", ColorSpace::sRGB)
		.value("LinearRGB", ColorSpace::LinearRGB)
		.value("OkLAB", ColorSpace::OkLAB)
		.value("OkLCH", ColorSpace::OkLCH)
		.value("HSL", ColorSpace::HSL)
		.finalize();

	py::class_<ColorPoint>(m, "ColorPoint")
		.def(py::init<>())
        .def(py::init<std::tuple<uint8_t, uint8_t, uint8_t>>())
        .def(py::init<std::tuple<float, float, float>>())
		.def("__len__", [](const ColorPoint& Self) -> int { return 3; })
		.def("__getitem__", &ColorPointGetItem)
		.def("__repr__", &ColorPointRepr)
		.def_property_readonly("channels", &ColorPointGetChannels)
		.def_readonly("encoding", &ColorPoint::Encoding)
		.def("encode", &ColorPoint::Encode);

	py::class_<ColorRamp>(m, "ColorRamp")
		.def(py::init<std::vector<ColorPoint> &>())
		.def("sample", &ColorRamp::Sample);

	m.def("convert_color", &ConvertColor, "color space converter");
	m.def("parse_color", &PyParseColor, "CSS color parser");
	m.def("oklab", &MakeOkLAB, "OkLAB color constructor");
	m.def("oklch", &MakeOkLCH, "OkLCH color constructor");
	m.def("hsl", &MakeHSL, "HSL color constructor");
	m.def("set_gamma", &SetGamma, "Change the sRGB gamma exponent");
	m.def("mix_lchab", &MixLCHAB, "Color blending in both OkLCH and OkLAB space");

	m.def("profiling_enabled", &IsProfilingEnabled);
	m.def("profiling_scope", [](const char* Name, py::function Thunk) -> py::object
	{
		py::object Result;
		PerfTrampoline(Name, Thunk, Result);
		return Result;
	});

	py::native_enum<OpCode>(m, "OpCode", "enum.IntEnum")
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
		.value("Count", OpCode::Count)
		.finalize();

	m.def("make_port_handle", &MakePortHandle);
	m.def("decode_port_tile", &PortHandleTilePart);
	m.def("decode_port_index", &PortHandlePortIndexPart);
	m.def("get_symbol_name", &GetDefaultName);
	m.def("set_default_polyphony", &SetDefaultPolyphony);

	py::class_<Patch>(m, "Patch")
		.def(py::init<>())
		.def_property("midi_lanes", &Patch::GetPolyphony, &Patch::SetPolyphony)
		.def_readonly("wires", &Patch::Wires)
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

	m.def("init_audio", &Audio::Init);
	m.def("shutdown_audio", &Audio::Shutdown);
	m.def("get_temporal_pressure", &Audio::GetTemporalPressure);

	m.def("init_midi", &Midi::Init);
	m.def("shutdown_midi", &Midi::Shutdown);

    // ---
    // Core types
    
    py::implicitly_convertible<std::tuple<uint8_t, uint8_t, uint8_t>, ColorPoint>();
    py::implicitly_convertible<std::tuple<float, float, float>, ColorPoint>();

    py::class_<Rect>(m, "Rect")
        .def(py::init<float, float, float, float>())
        .def(py::init<Point, Size>())
        .def("copy", [](const Rect& rect) { return Rect(rect); })
        .def_readwrite("x", &Rect::X)
        .def_readwrite("y", &Rect::Y)
        .def_readwrite("w", &Rect::Width)
        .def_readwrite("width", &Rect::Width)
        .def_readwrite("h", &Rect::Height)
        .def_readwrite("height", &Rect::Height)
        .def_property("size", &Rect::GetSize, &Rect::SetSize)
        .def_property("left", &Rect::GetLeft, &Rect::SetLeft)
        .def_property("right", &Rect::GetRight, &Rect::SetRight)
        .def_property("top", &Rect::GetTop, &Rect::SetTop)
        .def_property("bottom", &Rect::GetBottom, &Rect::SetBottom)
        .def_property("topleft", &Rect::GetTopLeft, &Rect::SetTopLeft)
        .def_property("topright", &Rect::GetTopRight, &Rect::SetTopRight)
        .def_property("bottomleft", &Rect::GetBottomLeft, &Rect::SetBottomLeft)
        .def_property("bottomright", &Rect::GetBottomRight, &Rect::SetBottomRight)
        .def_property("centerx", &Rect::GetCenterX, &Rect::SetCenterX)
        .def_property("centery", &Rect::GetCenterY, &Rect::SetCenterY)
        .def_property("center", &Rect::GetCenter, &Rect::SetCenter)
        .def("collidepoint", &Rect::ContainsPoint)
        .def("clipline", &Rect::IntersectLine)
        .def("union", &Rect::Union)
        .def("unionall", &Rect::UnionAll);
    
    // ---
    // Time

    py::module_ time = m.def_submodule("time");
    py::class_<Time::Clock>(time, "Clock")
        .def(py::init<>())
        .def("tick", &Time::Clock::Tick);
    
    // ---
    // Events

    py::module_ events = m.def_submodule("events");

    py::native_enum<Events::EventType>(events, "Type", "enum.IntEnum")
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
        .export_values()
        .finalize();
    
    py::native_enum<Events::KeyCode>(events, "KeyCode", "enum.IntFlag")
        .value("K_ESCAPE", Events::KeyCode::Escape)
        .value("K_F", Events::KeyCode::F)
        .value("K_F11", Events::KeyCode::F11)
        .export_values()
        .finalize();
    
    py::native_enum<Events::MouseButton>(events, "MouseButton", "enum.IntFlag")
        .value("BUTTON_LEFT", Events::MouseButton::Left)
        .export_values()
        .finalize();

    py::class_<Events::ResizeEvent>(events, "ResizeEvent")
        .def_readonly("Width", &Events::ResizeEvent::Width)
        .def_readonly("Height", &Events::ResizeEvent::Height);
    
    py::class_<Events::KeyboardEvent>(events, "KeyboardEvent")
        .def_readonly("key", &Events::KeyboardEvent::Key);
    
    py::class_<Events::MouseMotionEvent>(events, "MouseMotionEvent")
        .def_readonly("x", &Events::MouseMotionEvent::X)
        .def_readonly("y", &Events::MouseMotionEvent::X)
        .def_readonly("xrel", &Events::MouseMotionEvent::XRelative)
        .def_readonly("yrel", &Events::MouseMotionEvent::YRelative)
        .def_property_readonly("pos", &Events::MouseMotionEvent::GetPosition)
        .def_property_readonly("rel", &Events::MouseMotionEvent::GetRelativePosition);
    
    py::class_<Events::MouseButtonEvent>(events, "MouseButtonEvent")
        .def_readonly("x", &Events::MouseButtonEvent::X)
        .def_readonly("y", &Events::MouseButtonEvent::Y)
        .def_readonly("button", &Events::MouseButtonEvent::Button)
        .def_readonly("touch", &Events::MouseButtonEvent::IsTouch)
        .def_property_readonly("pos", &Events::MouseButtonEvent::GetPosition);

		py::class_<Events::MouseWheelEvent>(events, "MouseWheelEvent")
		.def_readonly("horizontal", &Events::MouseWheelEvent::Horizontal)
		.def_readonly("vertical", &Events::MouseWheelEvent::Vertical)
		.def_readonly("cursor_x", &Events::MouseWheelEvent::CursorX)
		.def_readonly("cursor_y", &Events::MouseWheelEvent::CursorY)
		.def_property_readonly("pos", &Events::MouseWheelEvent::GetPosition);
    
    py::class_<Events::TouchFingerEvent>(events, "TouchFingerEvent")
        .def_readonly("x", &Events::TouchFingerEvent::X)
        .def_readonly("y", &Events::TouchFingerEvent::Y)
        .def_readonly("touch_id", &Events::TouchFingerEvent::TouchID)
        .def_readonly("finger_id", &Events::TouchFingerEvent::FingerID);
    
    py::class_<Events::Event>(events, "Event")
        .def_readonly("type", &Events::Event::Type)
        .def_readonly("resize", &Events::Event::Resize)
        .def_readonly("key", &Events::Event::Key)
        .def_readonly("motion", &Events::Event::Motion)
        .def_readonly("button", &Events::Event::Button)
		.def_readonly("wheel", &Events::Event::Wheel)
        .def_readonly("tfinger", &Events::Event::Touch);
    
    events.def("get", &Events::Get);
    
    // ---
    // Mouse

    py::module_ mouse = m.def_submodule("mouse")
        .def("get_pos", &Mouse::GetPosition);

    // ---
    // Display

    py::module_ display = m.def_submodule("display");

    py::native_enum<Display::WindowFlags>(display, "WindowFlags", "enum.IntFlag")
        .value("FULLSCREEN", Display::WindowFlags::Fullscreen)
        .value("BORDERLESS", Display::WindowFlags::Borderless)
        .export_values()
        .finalize();
    
    display
        .def("init", &Display::Init)
        .def("get_current_display_index", &Display::GetCurrentDisplayIndex)
        .def("get_desktop_sizes", &Display::GetDesktopSizes)
        .def("list_modes", &Display::ListModes, py::arg("display"))
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

    py::module_ draw = m.def_submodule("draw");

    using BlitRectFunc = void (Draw::Texture::*)(const Draw::Texture&, const Rect&);
    using BlitPointFunc = void (Draw::Texture::*)(const Draw::Texture&, const Point&);

	py::native_enum<Draw::BlendModeType>(draw, "Type", "enum.IntEnum")
		.value("none", Draw::BlendModeType::None)
		.value("alpha", Draw::BlendModeType::Alpha)
		.value("premultiplied_alpha", Draw::BlendModeType::PremultipliedAlpha)
		.value("additive", Draw::BlendModeType::Additive)
		.value("premultiplied_additive", Draw::BlendModeType::PremultipliedAdditive)
		.value("modulate", Draw::BlendModeType::Modulate)
		.value("multiply", Draw::BlendModeType::Multiply)
		.value("eraser", Draw::BlendModeType::Eraser)
		.value("inverse_eraser", Draw::BlendModeType::InverseEraser)
		.export_values()
		.finalize();

    py::class_<Draw::Texture>(draw, "Texture")
        .def(py::init<int, int>())
        .def(py::init<const Size&>())
        .def("get_width", &Draw::Texture::GetWidth)
        .def("get_height", &Draw::Texture::GetHeight)
        .def("get_rect", &Draw::Texture::GetRect)
        .def("copy", &Draw::Texture::Copy)
        .def("set_alpha", &Draw::Texture::SetAlpha)
        .def("set_blend_mode", &Draw::Texture::SetBlendMode)
        .def("fill", &Draw::Texture::Fill, py::arg("color"), py::arg("alpha") = 1.0f)
        .def("fill_rect", &Draw::Texture::FillRect, py::arg("color"), py::arg("rect"), py::arg("alpha") = 1.0f)
        .def("blit", static_cast<BlitRectFunc>(&Draw::Texture::Blit))
        .def("blit", static_cast<BlitPointFunc>(&Draw::Texture::Blit));
    
    draw
        .def("init", &Draw::Init)
        .def("flip", &Draw::Flip)
        .def("get_renderer_name", &Draw::GetRendererName)
        .def("get_renderer_ready", &Draw::GetRendererReady)
        .def("get_rendering_surface", &Draw::GetRenderingSurface)
        .def("line", &Draw::DrawLine, py::arg("texture"), py::arg("color"), py::arg("start"), py::arg("end"), py::arg("width") = 1.0f, py::arg("alpha") = 1.0f)
        .def("rect", &Draw::DrawRect, py::arg("texture"), py::arg("color"), py::arg("rect"), py::arg("depth") = 0, py::arg("alpha") = 1.0f)
        .def("pie", &Draw::DrawPie, py::arg("texture"), py::arg("color"), py::arg("center"), py::arg("radius"), py::arg("angre"), py::arg("arc"), py::arg("alpha") = 1.0f)
        .def("circle", &Draw::DrawCircle, py::arg("texture"), py::arg("color"), py::arg("center"), py::arg("radius"), py::arg("alpha") = 1.0f)
        .def("polygon", &Draw::DrawPolygon, py::arg("texture"), py::arg("color"), py::arg("points"), py::arg("alpha") = 1.0f);

    // ---
    // Font

    py::module_ font = m.def_submodule("font")
        .def("init", &Font::Init);
    
    py::class_<Font> (font, "Font")
        .def(py::init<const std::string_view&, float>())
        .def("get_ascent", &Font::GetAscent)
        .def("get_descent", &Font::GetDescent)
        .def("estimate_glyph_height", &Font::EstimateGlyphHeight)
        .def("render", &Font::Render);
}
