
# Copyright 2025 Aeva Palecek
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import sys
import glob
import mollytime


ColorSpace = mollytime.ColorSpace
parse_color = mollytime.parse_color
oklab = mollytime.oklab
oklch = mollytime.oklch


def lch_prism(color):
    return mollytime.convert_color([color[i] / 255 for i in range(3)], ColorSpace.sRGB, ColorSpace.OkLCH).channels


def lch_swizzle(LC_part, H_Part, swizzle):
    assert(len(swizzle) == 3)
    LCH1 = lch_prism(LC_part)
    LCH2 = lch_prism(H_Part)
    LCH = [(1 - a) * lhs + a * rhs for a, lhs, rhs in zip(swizzle, LCH1, LCH2)]
    return oklch(*LCH)


def color_ramp(*color_points):
    return mollytime.ColorRamp(color_points)
