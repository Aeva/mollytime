
import overrides

import string
from importlib import resources

import pygame

import media


SURFACE_VAULT = {}
FONTS = {}

FONT_NAMES = {
    "overpass" : resources.files(media) / "overpass" / "static" / "Overpass-ExtraLight.ttf",
}


def reset_memo():
    global SURFACE_VAULT
    SURFACE_VAULT = {}


def rect(size, color):
    global SURFACE_VAULT
    key = (size, color)
    surface = SURFACE_VAULT.get(key, None)

    if surface:
        return surface

    surface = pygame.Surface(size)
    surface.fill(color)
    SURFACE_VAULT[key] = surface
    return surface


def text(font_name, font_size, label, color):
    global SURFACE_VAULT
    global FONTS

    font_key = (font_name, int(font_size))
    surface_key = (font_key, label, color)

    surface = SURFACE_VAULT.get(surface_key, None)
    if surface:
        return surface

    font = FONTS.get(font_key, None)
    if not font:
        font = pygame.font.Font(FONT_NAMES[font_name], int(font_size))
        FONTS[font_key] = font

    surface = font.render(label, True, color)
    SURFACE_VAULT[surface_key] = surface
    return surface


def text_rect(size, bg_color, font_name, font_size, label, fg_color=(0, 0, 0), align=string.digits):
    global SURFACE_VAULT
    global FONTS

    font_key = (font_name, int(font_size))
    text_key = (font_key, label, fg_color)
    rect_key = (size, bg_color)
    surface_key = (rect_key, text_key, align)

    surface = SURFACE_VAULT.get(surface_key, None)
    if surface:
        return surface

    bg = rect(size, bg_color)
    fg = text(font_name, font_size, label, fg_color)

    font = FONTS.get(font_key, None)
    assert(font)

    font_min_y = 100000
    font_max_y = -100000
    for min_x, max_x, min_y, max_y, advance in font.metrics(align):
        font_min_y = min(font_min_y, min_y)
        font_max_y = max(font_max_y, max_y)

    x = (bg.get_width() - fg.get_width()) * .5
    y = font.get_ascent() - font_max_y + font_min_y

    surface = bg.copy()
    surface.blit(fg, (x, y))
    SURFACE_VAULT[surface_key] = surface

    return surface