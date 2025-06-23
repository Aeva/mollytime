
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

namespace py = pybind11;

using ColorTuple = std::tuple<float, float, float>;
using ColorArray = std::array<float, 3>;


struct PyColorPoint : public ColorPoint
{
	PyColorPoint()
		: ColorPoint()
	{
	}

	PyColorPoint(ColorPoint Other)
		: ColorPoint(Other.Encoding, Other.Channels)
	{
	}

	PyColorPoint(ColorSpace InEncoding, ColorPoint Other)
		: ColorPoint(InEncoding, Other)
	{
	}

	int SequenceLength()
	{
		return 3;
	}

	int GetItem(int Index)
	{
		if (Index >=0 && Index < 3)
		{
			if (Encoding != ColorSpace::sRGB)
			{
				MutateEncoding(ColorSpace::sRGB);
			}
			return std::min(std::max(int(Channels[Index] * 255.0f), 0), 255);
		}

		throw std::out_of_range(std::format("Index out of range: {}\n", Index));
	}

	std::string Repr()
	{
		return std::format("<ColorPoint {}: ({}, {}, {})>", ColorSpaceName(Encoding), Channels[0], Channels[1], Channels[2]);
	}

	ColorTuple GetChannels()
	{
		return { Channels[0], Channels[1], Channels[2] };
	}

	ColorSpace GetEncoding()
	{
		return Encoding;
	}
};


PyColorPoint PyConvertColor(ColorArray InColor, ColorSpace Incoding, ColorSpace Excoding)
{
	ColorPoint Color{Incoding, InColor};
	return PyColorPoint(Excoding, Color);
}


PyColorPoint PyParseColor(std::string ColorString)
{
	ColorPoint Color;
	StatusCode Result = ParseColor(ColorString, Color);
	if (Result == StatusCode::PASS)
	{
		return PyColorPoint(Color);
	}

	throw std::domain_error(std::format("Invalid color string \"{}\"\n", ColorString));
}


PYBIND11_MODULE(mollytime, m) {
	m.doc() = "mollytime c++ internals";

	py::enum_<ColorSpace>(m, "ColorSpace")
		.value("sRGB", ColorSpace::sRGB)
		.value("LinearRGB", ColorSpace::LinearRGB)
		.value("OkLAB", ColorSpace::OkLAB)
		.value("OkLCH", ColorSpace::OkLCH)
		.value("HSL", ColorSpace::HSL);

	py::class_<PyColorPoint>(m, "ColorPoint")
		.def(py::init<>())
		.def("__len__", &PyColorPoint::SequenceLength)
		.def("__getitem__", &PyColorPoint::GetItem)
		.def("__repr__", &PyColorPoint::Repr)
		.def_property_readonly("channels", &PyColorPoint::GetChannels)
		.def_property_readonly("encoding", &PyColorPoint::GetEncoding);

	m.def("convert_color", &PyConvertColor, "color space converter");
	m.def("parse_color", &PyParseColor, "CSS color parser");
}
