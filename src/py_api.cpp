
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
#include <pybind11/stl.h>
#pragma warning(pop)
#pragma clang diagnostic pop

#include "colors.h"
#include "patch.h"
#include "audio_backend.h"
#include "alsa_midi.h"
#include "perf.h"

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
		.def("__len__", [](const ColorPoint& Self) -> int { return 3; })
		.def("__getitem__", &ColorPointGetItem)
		.def("__repr__", &ColorPointRepr)
		.def_property_readonly("channels", &ColorPointGetChannels)
		.def_readonly("encoding", &ColorPoint::Encoding);

	py::class_<ColorRamp>(m, "ColorRamp")
		.def(py::init<std::vector<ColorPoint> &>())
		.def("sample", &ColorRamp::Sample);

	m.def("convert_color", &ConvertColor, "color space converter");
	m.def("parse_color", &PyParseColor, "CSS color parser");
	m.def("oklab", &MakeOkLAB, "OkLAB color constructor");
	m.def("oklch", &MakeOkLCH, "OkLCH color constructor");

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
		.value("PLS", OpCode::PLS)
		.value("FLP", OpCode::FLP)
		.value("RNG", OpCode::RNG)
		.value("GRAD", OpCode::GRAD)
		.value("DSVF", OpCode::DSVF)
		.value("TPTSVF_LOWPASS", OpCode::TPTSVF_LOWPASS)
		.value("ADSR", OpCode::ADSR)
		.value("GATE", OpCode::GATE)
		.value("NOTE", OpCode::NOTE)
		.value("VELO", OpCode::VELO)
		.value("PRES", OpCode::PRES)
		.value("MIDI_HZ", OpCode::MIDI_HZ)
		.value("LOUD_FUDGE", OpCode::LOUD_FUDGE)
		.value("BOOP", OpCode::BOOP)
		.value("TAPE_LOOP", OpCode::TAPE_LOOP)
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
}
