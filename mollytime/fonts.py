
import os

import pygame_setup
import pygame

AFACAD_REGULAR = "media/afacad/static/Afacad-Regular.ttf"
NATIONAL_PARK_LIGHT = "media/national_park/NationalPark-Light.ttf"
NATIONAL_PARK_REGULAR = "media/national_park/NationalPark-Regular.ttf"


FONT_CACHE = {}
def get_font(font_path, size):
    size = int(size)
    if font_path:
        font_path = os.path.abspath(font_path)
        assert(os.path.isfile(font_path))
    key = (font_path, size)
    found = FONT_CACHE.get(key)
    if found:
        return found
    font = pygame.font.Font(font_path, size)
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
    surface = get_font(font_path, size).render(text, True, color)
    TEXT_SURFACE_CACHE[key] = surface
    return surface


def estimate_font_x_height(font_path, size):
    font = get_font(font_path, int(size))
    min_x, max_x, min_y, max_y, advance = font.metrics("x")[0]
    return abs(max_y - min_y)


def estimate_font_x_center(font_path, size):
    font = get_font(font_path, size)
    x_height = estimate_font_x_height(font_path, size)
    return int(font.get_ascent() - (x_height * .5))


def estimate_font_mean_line(font_path, size):
    font = get_font(font_path, int(size))
    x_height = estimate_font_x_height(font_path, size)
    return font.get_ascent() - x_height


def get_font_baseline(font_path, size):
    font = get_font(font_path, int(size))
    return font.get_ascent()


def font_debug_surface(screen, font_path = AFACAD_REGULAR, size = 100):
    fg_color = parse_color("#FFF")
    bg_color = parse_color("#000")
    asc_color = parse_color("#00F")
    dsc_color = parse_color("#F00")
    x_color = parse_color("#F0F")
    font = get_font(font_path, size)
    font_surf = render_text(font_path, size, fg_color, "Mollytime Font Debug")
    font_rect = font_surf.get_rect()
    pygame.draw.rect(screen, bg_color, font_rect)

    asc_rect = font_rect.copy()
    asc_rect.top = 0
    asc_rect.height = font.get_ascent()
    pygame.draw.rect(screen, asc_color, asc_rect)

    dsc_rect = font_rect.copy()
    dsc_rect.height = abs(font.get_descent())
    dsc_rect.top = font_rect.height - dsc_rect.height
    pygame.draw.rect(screen, dsc_color, dsc_rect)

    x_rect = font_rect.copy()
    x_rect.height = estimate_font_x_height(font_path, size)
    x_rect.width -= x_rect.height
    x_rect.left = x_rect.height
    x_rect.top = asc_rect.height - x_rect.height
    pygame.draw.rect(screen, x_color, x_rect)

    screen.blit(font_surf, font_rect)

    x_center = estimate_font_x_center(font_path, size)
    pygame.draw.line(screen, parse_color("#0F0"), (x_rect.x, x_center), (x_rect.x + x_rect.w, x_center))
