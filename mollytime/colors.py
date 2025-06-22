
import os
import sys
import glob
sys.path.append(os.path.split(os.path.abspath(glob.glob("**/color_spaces*.so")[0]))[0])
import color_spaces


ColorSpace = color_spaces.ColorSpace


def float_color(r, g, b):
    return [int(min(max(c, 0), 1) * 255) for c in (r, g, b)]


def parse_color(color_str):
    return float_color(*color_spaces.parse_color(color_str))


def oklab(l, A, B):
    return float_color(*color_spaces.convert_color((l, A, B), ColorSpace.OkLAB, ColorSpace.sRGB))


def oklch(l, c, h):
    return float_color(*color_spaces.convert_color((l, c, h), ColorSpace.OkLCH, ColorSpace.sRGB))


def lch_prism(color):
    return color_spaces.convert_color([i / 255 for i in color], ColorSpace.sRGB, ColorSpace.OkLCH)


def lch_swizzle(LC_part, H_Part, swizzle):
    assert(len(swizzle) == 3)
    LCH1 = lch_prism(LC_part)
    LCH2 = lch_prism(H_Part)
    LCH = [(1 - a) * lhs + a * rhs for a, lhs, rhs in zip(swizzle, LCH1, LCH2)]
    return oklch(*LCH)
