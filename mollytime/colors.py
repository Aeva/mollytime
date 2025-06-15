
import os
import enum
import ctypes

COLORS_BACKEND = ctypes.cdll.LoadLibrary(os.path.abspath("colors/colors.so"))
c_vec3 = ctypes.c_float * 3


class ColorSpace(enum.IntEnum):
	sRGB = 0
	LinearRGB = enum.auto()
	OkLAB = enum.auto()
	OkLCH = enum.auto()
	HSL = enum.auto()


def convert_color(color, incoding, excoding):
    in_color = c_vec3(*color)
    out_color = c_vec3(0, 0, 0)
    COLORS_BACKEND.convert_color(in_color, ctypes.c_uint8(incoding), out_color, ctypes.c_uint8(excoding))
    return [c for c in out_color]


def float_color(r, g, b):
    return [int(min(max(c, 0), 1) * 255) for c in (r, g, b)]


def parse_color(color_str):
    out_color = c_vec3(0, 0, 0)
    error = COLORS_BACKEND.parse_color(ctypes.c_char_p(color_str.encode("utf-8")), out_color)
    if error:
        raise ValueError(f"Invalid color string: {color_str}")
    else:
        return float_color(*out_color)


def oklab(l, A, B):
    return float_color(*convert_color((l, A, B), ColorSpace.OkLAB, ColorSpace.sRGB))


def oklch(l, c, h):
    return float_color(*convert_color((l, c, h), ColorSpace.OkLCH, ColorSpace.sRGB))


def lch_prism(color):
    return convert_color([i / 255 for i in color], ColorSpace.sRGB, ColorSpace.OkLCH)


def lch_swizzle(LC_part, H_Part, swizzle):
    assert(len(swizzle) == 3)
    LCH1 = lch_prism(LC_part)
    LCH2 = lch_prism(H_Part)
    LCH = [(1 - a) * lhs + a * rhs for a, lhs, rhs in zip(swizzle, LCH1, LCH2)]
    return oklch(*LCH)
