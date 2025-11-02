
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma warning(push)
#pragma warning(disable : 4191 4355 4371 4464 4686 4868 5039)
#include <pybind11/pybind11.h>
#include <pybind11/native_enum.h>
#include <pybind11/stl.h>
#pragma warning(pop)
#pragma clang diagnostic pop

#include "colors.h"
#include "patch.h"
#include "audio_backend.h"
#include "alsa_midi.h"
#include "perf.h"
#include "sdl.h"

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

	throw pybind11::index_error(std::format("Index out of range: {}\n", Index));
}


static std::string ColorPointRepr(ColorPoint& Color)
{
	return std::format("<ColorPoint {}: ({}, {}, {})>",
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
		throw std::domain_error(std::format("Invalid color string \"{}\"\n", ColorString));
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


PYBIND11_MODULE(mollytime, m) {
	m.doc() = "mollytime c++ internals";

	py::enum_<ColorSpace>(m, "ColorSpace")
		.value("sRGB", ColorSpace::sRGB)
		.value("LinearRGB", ColorSpace::LinearRGB)
		.value("OkLAB", ColorSpace::OkLAB)
		.value("OkLCH", ColorSpace::OkLCH)
		.value("HSL", ColorSpace::HSL);

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
	m.def("set_gamma", &SetGamma, "Change the sRGB gamma exponent");
	m.def("mix_lchab", &MixLCHAB, "Color blending in both OkLCH and OkLAB space");

	m.def("profiling_enabled", &IsProfilingEnabled);
	m.def("profiling_scope", [](const char* Name, py::function Thunk) -> py::object
	{
		py::object Result;
		PerfTrampoline(Name, Thunk, Result);
		return Result;
	});

	py::enum_<OpCode>(m, "OpCode")
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
		.value("ADD", OpCode::ADD)
		.value("MUL", OpCode::MUL)
		.value("RCP", OpCode::RCP)
		.value("MIN", OpCode::MIN)
		.value("MAX", OpCode::MAX)
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
		.value("GATE", OpCode::GATE)
		.value("NOTE", OpCode::NOTE)
		.value("VELO", OpCode::VELO)
		.value("PRES", OpCode::PRES)
		.value("CTRL", OpCode::CTRL)
		.value("MIDI_HZ", OpCode::MIDI_HZ)
		.value("LOUD_FUDGE", OpCode::LOUD_FUDGE)
		.value("BOOP", OpCode::BOOP)
		.value("TAPE_LOOP", OpCode::TAPE_LOOP)
		.value("MOON", OpCode::MOON)
		.value("Count", OpCode::Count);

	m.def("make_port_handle", &MakePortHandle);
	m.def("decode_port_tile", &PortHandleTilePart);
	m.def("decode_port_index", &PortHandlePortIndexPart);
	m.def("get_symbol_name", &GetDefaultName);

	py::class_<Patch>(m, "Patch")
		.def(py::init<>())
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
		.def("set_special_input", &Patch::SetSpecialInput);

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
        .value("QUIT", Events::EventType::Quit)
        .value("KEYDOWN", Events::EventType::KeyDown)
        .value("KEYUP", Events::EventType::KeyUp)
        .value("MOUSEMOTION", Events::EventType::MouseMotion)
        .value("MOUSEBUTTONDOWN", Events::EventType::MouseButtonDown)
        .value("MOUSEBUTTONUP", Events::EventType::MouseButtonUp)
        .value("FINGERDOWN", Events::EventType::FingerDown)
        .value("FINGERUP", Events::EventType::FingerUp)
        .value("FINGERMOTION", Events::EventType::FingerMotion)
        .export_values()
        .finalize();
    
    py::native_enum<Events::KeyCode>(events, "KeyCode", "enum.IntFlag")
        .value("K_ESCAPE", Events::KeyCode::Escape)
        .export_values()
        .finalize();
    
    py::native_enum<Events::MouseButton>(events, "MouseButton", "enum.IntFlag")
        .value("BUTTON_LEFT", Events::MouseButton::Left)
        .export_values()
        .finalize();
    
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
    
    py::class_<Events::TouchFingerEvent>(events, "TouchFingerEvent")
        .def_readonly("x", &Events::TouchFingerEvent::X)
        .def_readonly("y", &Events::TouchFingerEvent::Y)
        .def_readonly("touch_id", &Events::TouchFingerEvent::TouchID)
        .def_readonly("finger_id", &Events::TouchFingerEvent::FingerID);
    
    py::class_<Events::Event>(events, "Event")
        .def_readonly("type", &Events::Event::Type)
        .def_readonly("key", &Events::Event::Key)
        .def_readonly("motion", &Events::Event::Motion)
        .def_readonly("button", &Events::Event::Button)
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
        .def("get_desktop_sizes", &Display::GetDesktopSizes)
        .def("list_modes", &Display::ListModes, py::arg("display"))
        .def("set_caption", &Display::SetCaption)
        .def("set_icon", &Display::SetIcon)
        .def("set_mode", &Display::SetMode, py::arg("display"), py::arg("size"), py::arg("flags"))
        .def("show_load_dialog", &Display::ShowLoadDialog)
        .def("show_save_dialog", &Display::ShowSaveDialog)
        .def("get_load_dialog_result", &Display::GetLoadDialogResult)
        .def("get_save_dialog_result", &Display::GetSaveDialogResult);
    
    // ---
    // Draw

    py::module_ draw = m.def_submodule("draw");

    using BlitRectFunc = void (Draw::Texture::*)(const Draw::Texture&, const Rect&);
    using BlitPointFunc = void (Draw::Texture::*)(const Draw::Texture&, const Point&);

    py::class_<Draw::Texture>(draw, "Texture")
        .def(py::init<int, int>())
        .def(py::init<const Size&>())
        .def("get_width", &Draw::Texture::GetWidth)
        .def("get_height", &Draw::Texture::GetHeight)
        .def("get_rect", &Draw::Texture::GetRect)
        .def("copy", &Draw::Texture::Copy)
        .def("set_alpha", &Draw::Texture::SetAlpha)
        .def("fill", &Draw::Texture::Fill, py::arg("color"), py::arg("alpha") = 1.0f)
        .def("blit", static_cast<BlitRectFunc>(&Draw::Texture::Blit))
        .def("blit", static_cast<BlitPointFunc>(&Draw::Texture::Blit));
    
    draw
        .def("init", &Draw::Init)
        .def("flip", &Draw::Flip)
        .def("line", &Draw::DrawLine, py::arg("texture"), py::arg("color"), py::arg("start"), py::arg("end"), py::arg("width") = 1.0f, py::arg("alpha") = 1.0f)
        .def("rect", &Draw::DrawRect, py::arg("texture"), py::arg("color"), py::arg("rect"), py::arg("depth") = 0, py::arg("alpha") = 1.0f)
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
