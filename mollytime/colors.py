
import os
import sys
import glob
import mollytime


ColorSpace = mollytime.ColorSpace
parse_color = mollytime.parse_color
oklab = mollytime.oklab
oklch = mollytime.oklch


def lch_prism(color):
    return mollytime.convert_color([i / 255 for i in color], ColorSpace.sRGB, ColorSpace.OkLCH).channels


def lch_swizzle(LC_part, H_Part, swizzle):
    assert(len(swizzle) == 3)
    LCH1 = lch_prism(LC_part)
    LCH2 = lch_prism(H_Part)
    LCH = [(1 - a) * lhs + a * rhs for a, lhs, rhs in zip(swizzle, LCH1, LCH2)]
    return oklch(*LCH)


def color_ramp(*color_points):
    return mollytime.ColorRamp(color_points)
