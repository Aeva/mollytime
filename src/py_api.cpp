
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
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "colors.h"
#include "patch.h"
#include "pipewire.h"

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

	throw std::out_of_range(std::format("Index out of range: {}\n", Index));
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
		.def("__len__", [](const ColorPoint &Self) -> int { return 3; })
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

	py::enum_<OpCode>(m, "OpCode")
		.value("CONST", OpCode::CONST)
		.value("OUT", OpCode::OUT)
		.value("SIN", OpCode::SIN)
		.value("SQR", OpCode::SQR)
		.value("TRI", OpCode::TRI)
		.value("ADD", OpCode::ADD)
		.value("MUL", OpCode::MUL)
		.value("MIN", OpCode::MIN)
		.value("MAX", OpCode::MAX);

	m.def("decode_port_tile", &PortHandleTilePart);
	m.def("decode_port_index", &PortHandlePortIndexPart);

	py::class_<Patch>(m, "Patch")
		.def(py::init<>())
		.def_readonly("wires", &Patch::Wires)
		.def("make_tile", [](Patch& Self, OpCode Symbol) -> TileHandle
		{
			return Self.MakeTile(Symbol);
		})
		.def("make_constant", [](Patch& Self, float Value) -> TileHandle
		{
			return Self.MakeTile(Value);
		})
		.def("erase_tile", &Patch::EraseTile)
		.def("get_tile_symbol", &Patch::GetTileSymbol)
		.def("get_tile_name", &Patch::GetTileName)
		.def("set_tile_name", &Patch::SetTileName)
		.def("get_constant", &Patch::GetConstant)
		.def("set_constant", &Patch::GetConstant)
		.def("get_tile_label", &Patch::GetTileLabel)
		.def("get_tile_input_ports", &Patch::GetTileInputPorts)
		.def("get_tile_output_ports", &Patch::GetTileOutputPorts)
		.def("get_input_port_name", &Patch::GetTileInputName)
		.def("get_output_port_name", &Patch::GetTileOutputName)
		.def("connect_tiles", &Patch::Connect)
		.def("disconnect_tiles", &Patch::Disconnect)
		.def("toggle_connection", &Patch::ToggleConnection)
		.def("can_connect", &Patch::CanConnect)
		.def("get_implicit_wire", &Patch::GetImplicitWire);

	m.def("init_audio", &AudioStream::Init);
	m.def("shutdown_audio", &AudioStream::Shutdown);
}
