
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
#include "tiles.h"

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

	py::class_<MagicTile>(m, "magic_tile")
		.def("__repr__", &MagicTile::Hint)
		.def("__str__", &MagicTile::Label)
		.def_readwrite("name",&MagicTile::Name)
		.def_readonly("id", &MagicTile::Id)
		.def_property_readonly("combiner", &MagicTile::Combiner)
		.def_property_readonly("inputs", &MagicTile::Inputs)
		.def_property_readonly("outputs", &MagicTile::Outputs);

	py::class_<ConstTile, MagicTile>(m, "const_tile")
		.def(py::init<float &>())
		.def_readwrite("value",&ConstTile::Value);

	py::class_<OutTile, MagicTile>(m, "out_tile")
		.def(py::init<>());

	py::class_<SinTile, MagicTile>(m, "sin_tile")
		.def(py::init<>());

	py::class_<AddTile, MagicTile>(m, "add_tile")
		.def(py::init<>());

	py::class_<MulTile, MagicTile>(m, "mul_tile")
		.def(py::init<>());

	py::class_<MinTile, MagicTile>(m, "min_tile")
		.def(py::init<>());

	py::class_<MaxTile, MagicTile>(m, "max_tile")
		.def(py::init<>());
}
