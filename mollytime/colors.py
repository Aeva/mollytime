
import os
import sys
import glob
import mollytime


ColorSpace = mollytime.ColorSpace
parse_color = mollytime.parse_color


def oklab(l, A, B):
    return mollytime.convert_color((l, A, B), ColorSpace.OkLAB, ColorSpace.sRGB)


def oklch(l, c, h):
    return mollytime.convert_color((l, c, h), ColorSpace.OkLCH, ColorSpace.sRGB)


def lch_prism(color):
    return mollytime.convert_color([i / 255 for i in color], ColorSpace.sRGB, ColorSpace.OkLCH).channels


def lch_swizzle(LC_part, H_Part, swizzle):
    assert(len(swizzle) == 3)
    LCH1 = lch_prism(LC_part)
    LCH2 = lch_prism(H_Part)
    LCH = [(1 - a) * lhs + a * rhs for a, lhs, rhs in zip(swizzle, LCH1, LCH2)]
    return oklch(*LCH)
