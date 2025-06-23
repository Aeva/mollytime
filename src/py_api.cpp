
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
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "colors.h"

namespace py = pybind11;

using ColorTuple = std::tuple<float, float, float>;
using ColorArray = std::array<float, 3>;


ColorTuple PyConvertColor(ColorArray InColor, ColorSpace Incoding, ColorSpace Excoding)
{
	ColorPoint Color{Incoding, InColor};
	glm::vec3 Out = Color.Eval(Excoding);
	return { Out[0], Out[1], Out[2] };
}


ColorTuple PyParseColor(std::string ColorString)
{
	ColorPoint Color;
	StatusCode Result = ParseColor(ColorString, Color);
	if (Result == StatusCode::PASS)
	{
		Color.MutateEncoding(ColorSpace::sRGB);
		return { Color.Channels[0], Color.Channels[1], Color.Channels[2] };
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

	m.def("convert_color", &PyConvertColor, "color space converter");
	m.def("parse_color", &PyParseColor, "CSS color parser");
}
