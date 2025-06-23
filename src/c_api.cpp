
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

#include "c_api.h"
#include "colors.h"


void convert_color(float in_color[3], std::uint8_t incoding, float out_color[3], std::uint8_t excoding)
{
    ColorPoint Color = ColorPoint((ColorSpace)incoding, glm::vec3(in_color[0], in_color[1], in_color[2]));
    glm::vec3 Out = Color.Eval((ColorSpace)excoding);
    out_color[0] = Out[0];
    out_color[1] = Out[1];
    out_color[2] = Out[2];
}


int parse_color(char* color_string, float out_color[3])
{
    std::string ColorString{color_string};
    ColorPoint Color;
    StatusCode Result = ParseColor(ColorString, Color);
    if (Result == StatusCode::PASS)
    {
        Color.MutateEncoding(ColorSpace::sRGB);
        out_color[0] = Color.Channels[0];
        out_color[1] = Color.Channels[1];
        out_color[2] = Color.Channels[2];
    }
    else
    {
        out_color[0] = 0.0f;
        out_color[1] = 0.0f;
        out_color[2] = 0.0f;
    }
    return (int)Result;
}

