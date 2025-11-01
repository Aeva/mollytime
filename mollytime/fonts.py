
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

from . import mollytime
from .colors import *

AFACAD_REGULAR = "afacad/static/Afacad-Regular.ttf"
NATIONAL_PARK_LIGHT = "national_park/NationalPark-Light.ttf"
NATIONAL_PARK_REGULAR = "national_park/NationalPark-Regular.ttf"

MEDIA_DIR = os.path.join(os.path.split(__file__)[0], "media")


FONT_CACHE = {}
def get_font(font_path, size):
    size = int(size)
    if font_path:
        font_path = os.path.join(MEDIA_DIR, font_path)
        assert(os.path.isfile(font_path))
    key = (font_path, size)
    found = FONT_CACHE.get(key)
    if found:
        return found
    font = mollytime.font.Font(font_path, size)
    FONT_CACHE[key] = font
    return font


TEXT_SURFACE_CACHE = {}
def render_text(font_path, size, color, text):
    size = int(size)
    color = tuple(color)
    key = (font_path, size, color, text)
    found = TEXT_SURFACE_CACHE.get(key)
    if found:
        return found
    surface = get_font(font_path, size).render(text, color)
    TEXT_SURFACE_CACHE[key] = surface
    return surface


def estimate_font_M_height(font_path, size):
    font = get_font(font_path, int(size))
    min_y, max_y = font.estimate_glyph_height("M")
    return abs(max_y - min_y)


def estimate_font_M_center(font_path, size):
    """Offset from the top of the rect to the M center line."""
    font = get_font(font_path, size)
    M_height = estimate_font_M_height(font_path, size)
    return int(font.get_ascent() - (M_height * .5))


def estimate_font_x_height(font_path, size):
    font = get_font(font_path, int(size))
    min_y, max_y = font.estimate_glyph_height("x")
    return abs(max_y - min_y)


def estimate_font_x_center(font_path, size):
    """Offset from the top of the rect to the x center line."""
    font = get_font(font_path, size)
    x_height = estimate_font_x_height(font_path, size)
    return int(font.get_ascent() - (x_height * .5))


def estimate_font_cap_line(font_path, size):
    """Distance from top of rect to cap line."""
    font = get_font(font_path, int(size))
    M_height = estimate_font_M_height(font_path, size)
    return font.get_ascent() - M_height


def estimate_font_mean_line(font_path, size):
    """Distance from top of rect to mean line."""
    font = get_font(font_path, int(size))
    x_height = estimate_font_x_height(font_path, size)
    return font.get_ascent() - x_height


def get_font_baseline(font_path, size):
    """Distance from the top of the rendered text rect to the baseline."""
    font = get_font(font_path, int(size))
    return font.get_ascent()


def get_font_descent(font_path, size):
    """Distance from the baseline to the bottom of the rendered text rect."""
    font = get_font(font_path, int(size))
    return abs(font.get_descent())


def font_debug_surface(screen, font_path = AFACAD_REGULAR, size = 100):
    fg_color = parse_color("#FFF")
    bg_color = parse_color("#000")
    asc_color = parse_color("#00F")
    dsc_color = parse_color("#F00")
    x_color = parse_color("#F0F")
    font = get_font(font_path, size)
    font_surf = render_text(font_path, size, fg_color, "Mollytime Font Debug")
    font_rect = font_surf.get_rect()
    mollytime.draw.rect(screen, bg_color, font_rect)

    asc_rect = font_rect.copy()
    asc_rect.top = 0
    asc_rect.height = font.get_ascent()
    mollytime.draw.rect(screen, asc_color, asc_rect)

    dsc_rect = font_rect.copy()
    dsc_rect.height = abs(font.get_descent())
    dsc_rect.top = font_rect.height - dsc_rect.height
    dsc_rect.left = 100
    dsc_rect.width -= 100
    mollytime.draw.rect(screen, dsc_color, dsc_rect)

    x_rect = font_rect.copy()
    x_rect.height = estimate_font_x_height(font_path, size)
    x_rect.width -= x_rect.height
    x_rect.left = x_rect.height
    x_rect.top = asc_rect.height - x_rect.height
    mollytime.draw.rect(screen, x_color, x_rect)

    screen.blit(font_surf, font_rect)

    x_center = estimate_font_x_center(font_path, size)
    mollytime.draw.line(screen, parse_color("#0F0"), (x_rect.x, x_center), (x_rect.x + x_rect.w, x_center))
