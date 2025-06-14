
import time
import math
import os
import ctypes
import random
import enum
import re
import string
import subprocess

import pygame_setup
import pygame

AFACAD_REGULAR = "media/afacad/static/Afacad-Regular.ttf"
NATIONAL_PARK_LIGHT = "media/national_park/NationalPark-Light.ttf"
NATIONAL_PARK_REGULAR = "media/national_park/NationalPark-Regular.ttf"


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


class tile_viewport:
    def __init__(self, viewport, grid):
        self.grid = -1
        self.viewport = pygame.Rect(0, 0, 0, 0)
        self.resize(viewport, grid)

    def resize(self, viewport, grid):
        if viewport == self.viewport and grid == self.grid:
            return
        self.grid = grid
        self.viewport = viewport
        self.surface = pygame.Surface(viewport.size)
        self.redraw()

    def redraw(self):
        pass


class tile_grid_bg(tile_viewport):

    def __init__(self, *args, **kargs):
        self.focus_x = 0
        self.focus_y = 0
        super().__init__(*args, **kargs)

    def resize(self, viewport, grid):
        # gradient stuff
        self.light = (viewport.width / 2, viewport.height)
        self.light_span = math.sqrt(sum([i * i for i in self.light]))
        self.bg_ramp_x = (parse_color("#a6a8ad"), parse_color("#b1b3b8"))
        self.bg_ramp_y = (parse_color("#b1b3b8"), parse_color("#a3a9bb"))
        super().resize(viewport, grid)

    def bg_color(self, tile_x, tile_y, rect):
        weird = (rect.centery / self.viewport.h)
        weird = weird / 3 + (1.0 - weird)

        pos = (rect.centerx, rect.centery)
        rel = [LHS - RHS for LHS, RHS in zip(pos, self.light)]
        rel[0] *= weird
        mag = math.sqrt(sum([i * i for i in rel]))

        alpha = min(max(mag / self.light_span, 0), 1)
        alpha *= alpha
        inv_a = 1.0 - alpha

        color_x = [int(inv_a * self.bg_ramp_x[0][i] + alpha * self.bg_ramp_x[1][i]) for i in range(3)]
        color_y = [int(inv_a * self.bg_ramp_y[0][i] + alpha * self.bg_ramp_y[1][i]) for i in range(3)]

        checker = ((int(tile_x) % 2) + (int(tile_y) % 2)) % 2
        return (color_x, color_y)[checker]

    def redraw(self):
        x_count = math.ceil(self.viewport.w / self.grid) + 1
        y_count = math.ceil(self.viewport.h / self.grid) + 1

        half_w = self.viewport.w / 2
        half_h = self.viewport.h / 2
        crop_min_x = -half_w + self.focus_x
        crop_min_y = -half_h + self.focus_y

        x_offset = math.floor(crop_min_x / self.grid) * self.grid - crop_min_x
        y_offset = math.floor(crop_min_y / self.grid) * self.grid - crop_min_y

        # fine grid
        for view_tile_y in range(y_count):
            for view_tile_x in range(x_count):
                tile_x = crop_min_x // self.grid + view_tile_x
                tile_y = crop_min_y // self.grid + view_tile_y
                view_x = view_tile_x * self.grid + x_offset
                view_y = view_tile_y * self.grid + y_offset
                rect = pygame.Rect(view_x, view_y, self.grid, self.grid)
                color = self.bg_color(tile_x, tile_y, rect)
                pygame.draw.rect(self.surface, color, rect)

        # coarse grid
        for view_tile_y in range(-1, y_count):
            for view_tile_x in range(-1, x_count):
                tile_x = crop_min_x // self.grid + view_tile_x
                tile_y = crop_min_y // self.grid + view_tile_y
                if (tile_x % 3) != 2 or (tile_y % 3) != 2:
                    continue
                view_x = view_tile_x * self.grid + x_offset
                view_y = view_tile_y * self.grid + y_offset
                rect = pygame.Rect(view_x, view_y, self.grid * 2, self.grid * 2)
                color = self.bg_color(tile_x, tile_y, rect)
                pygame.draw.rect(self.surface, color, rect)


class side_bar_bg(tile_viewport):
    def resize(self, viewport, grid):
        super().resize(viewport, grid)

    def redraw(self):
        ramp_a = (0.4, 0.04, 0)
        ramp_b = (0.4, 0.04, 360)

        # sidebar color ramp
        steps = self.viewport.w // 8
        for i in range(steps):
            alpha = i / (steps - 1)
            inv_a = 1.0 - alpha
            params = [LHS * alpha + RHS * inv_a for LHS, RHS in zip(ramp_a, ramp_b)]
            color = oklch(*params)

            alpha = i / steps
            inv_a = 1.0 - alpha

            rect = pygame.Rect(0, 0, self.viewport.w * inv_a, self.viewport.h)
            pygame.draw.rect(self.surface, color, rect)


class plate_bg:
    def __init__(self, grid, color, label = None):
        L, C, H = lch_prism(color)
        self.color_base = color
        self.color_top    = oklch(L - 0.0183, C, H)
        self.color_sides  = oklch(L - 0.0986, C, H)
        self.color_bottom = oklch(L - 0.2914, C, H + 3.8415)
        self.text = label
        self.text_color = oklch(1 - L, 0, 0)

        self.grid = -1
        self.resize(grid)

    def resize(self, grid):
        if grid == self.grid:
            return
        self.grid = grid
        self.size = grid * 2
        self.surface = pygame.Surface((self.size, self.size))
        self.redraw()

    def redraw(self):
        rect = pygame.Rect(0, 0, self.size, self.size)
        depth = 6

        pygame.draw.rect(self.surface, self.color_base, rect)
        pygame.draw.rect(self.surface, self.color_sides, rect, depth)

        for i in range(0, depth):
            a = (rect.topleft[0] + i, rect.topleft[1] + i)
            b = (rect.topright[0] - i - 1, rect.topright[1] + i)
            pygame.draw.line(self.surface, self.color_top, a, b, 1)

            a = (rect.bottomleft[0] + i, rect.bottomleft[1] - i - 1)
            b = (rect.bottomright[0] - i - 1, rect.bottomright[1] - i - 1)
            pygame.draw.line(self.surface, self.color_bottom, a, b, 1)

        font_path, size = NATIONAL_PARK_REGULAR, max(10, self.size * .24)
        text_surface = render_text(font_path, size, self.text_color, self.text)
        text_rect = text_surface.get_rect().copy()
        text_rect.centerx = rect.centerx
        text_rect.top = rect.centery - estimate_font_x_center(font_path, size)
        self.surface.blit(text_surface, text_rect)


class node_graph_card:
    def __init__(self, screen, dpi):
        self.focus_x = 0
        self.focus_y = 0
        self.tiles = [(-1, 0), (1, 0), (0, -1), (0, 1)]
        self.selected = []

        self.clock = pygame.time.Clock()
        self.resize(screen, dpi)

    def toggle_selection(self, tile):
        assert(tile in self.tiles)
        if tile in self.selected:
            self.selected = [select for select in self.selected if select != tile]
        else:
            if len(self.selected) < 2:
                self.selected.append(tile)
            else:
                assert(len(self.selected) == 2)
                self.selected = [self.selected[1], tile]

    def clear_selection(self):
        self.selected = []

    def is_selected(self, tile):
        return tile in self.selected

    def any_selected(self):
        return len(self.selected) != 0

    def resize(self, screen, dpi):
        self.screen = screen
        self.dpi = dpi

        screen_w = screen.get_rect().width
        screen_h = screen.get_rect().height

        self.grid_size = dpi // 3

        side_bar_w = self.grid_size * 3
        side_bar_h = screen_h

        self.play_rect = pygame.Rect(0, 0, screen_w - side_bar_w, screen_h)
        self.play_area = tile_grid_bg(self.play_rect, self.grid_size)

        self.side_bar_rect = pygame.Rect(screen_w - side_bar_w, 0, side_bar_w, side_bar_h)
        self.side_bar = side_bar_bg(self.side_bar_rect, self.grid_size)

        tile_color = parse_color("#dee5e8")
        self.tile_bg = plate_bg(self.grid_size, tile_color)

        select_color = lch_swizzle(tile_color, parse_color("#880000"), (.5, .75, 0))
        self.selected_tile_bg = plate_bg(self.grid_size, select_color)

        self.inspect_target = plate_bg(self.grid_size, tile_color, "inspect")
        self.inspect_active = plate_bg(self.grid_size, select_color, "inspect")

        self.select_target = plate_bg(self.grid_size, tile_color, "select")
        self.select_active = plate_bg(self.grid_size, select_color, "select")

        self.placeholder_target = plate_bg(self.grid_size, tile_color, "magic")


class editor_screen:
    def __init__(self, editor):
        self.setup(editor)
        self.update_play_area = True
        self.update_sidebar = True

        self.live = True
        self.purge_events()
        self.draw(editor)

        while self.live:
            self.process_events(editor)
            self.draw(editor)

    def set_screen_label(self, editor, text):
        inner_w = (editor.play_area.viewport.w // editor.grid_size) * editor.grid_size
        inner_h = (editor.play_area.viewport.h // editor.grid_size) * editor.grid_size
        margin_x = (editor.play_area.viewport.w - inner_w) // 2
        margin_y = (editor.play_area.viewport.h - inner_h) // 2

        font_path, size = AFACAD_REGULAR, editor.grid_size
        self.screen_label_surface = render_text(font_path, size, parse_color("#000"), text)
        self.screen_label_surface.set_alpha(int(.4 * 255))
        self.screen_label_rect = self.screen_label_surface.get_rect().copy()
        self.screen_label_rect.left = margin_x
        self.screen_label_rect.top = margin_y + editor.grid_size - get_font_baseline(font_path, size)

    def purge_events(self):
        for event in pygame.event.get():
            if (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                self.live = False
            elif event.type == pygame.QUIT:
                exit(0)

    def process_events(self, editor):
        for event in pygame.event.get():
            if (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                self.live = False

            elif event.type == pygame.MOUSEMOTION and (abs(event.rel[0]) > 0 or abs(event.rel[1]) > 0):
                self.on_move(editor, event.pos)

            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == pygame.BUTTON_LEFT:
                self.on_press(editor, event.pos)

            elif event.type == pygame.MOUSEBUTTONUP and event.button == pygame.BUTTON_LEFT:
                self.on_release(editor)

            elif event.type == pygame.QUIT:
                exit(0)

    def draw(self, editor):
        pass


class select_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect > select")
        self.repopulate_sidebar(editor)

    def repopulate_sidebar(self, editor):
        self.update_sidebar = True

        goto_inspect_rect = pygame.Rect(
            editor.grid_size,
            0 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_inspect_icon = editor.inspect_target

        active_rect = pygame.Rect(
            editor.grid_size,
            3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.select_active

        placeholder_rect = pygame.Rect(
            editor.grid_size,
            6 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        placeholder_icon = editor.placeholder_target

        self.side_bar_targets = [
            (goto_inspect_rect, goto_inspect_icon, self.goto_inspect_screen),
            (active_rect, active_icon, None)]

        if editor.any_selected():
            self.side_bar_targets.append((placeholder_rect, placeholder_icon, None))

    def goto_inspect_screen(self, editor):
        self.live = False

    def on_move(self, editor, pos):
        self.cursor_pos = pos

        if not self.press_start:
            return

        move_x = pos[0] - self.press_start[0]
        move_y = pos[1] - self.press_start[1]

        if move_x != 0 or move_y != 0:
            editor.focus_x -= move_x
            editor.focus_y -= move_y
            self.update_play_area = True

        self.press_start = pos

    def on_press(self, editor, pos):
        if editor.play_rect.collidepoint(pos):
            something_happened = False
            for (tile_x, tile_y) in editor.tiles:
                rect = pygame.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)
                if rect.collidepoint(pos):
                    something_happened = True
                    self.update_play_area = True
                    state = editor.toggle_selection((tile_x, tile_y))
                    self.repopulate_sidebar(editor)
                    break

            if not something_happened:
                self.press_start = pos

        elif editor.side_bar_rect.collidepoint(pos):
            # test side bar targets
            rel_pos = (pos[0] - editor.side_bar.viewport.x, pos[1] - editor.side_bar.viewport.y)
            for rect, surface, action in self.side_bar_targets:
                if action is not None and rect.collidepoint(rel_pos):
                    action(editor)
                    return

    def on_release(self, editor):
        self.press_start = None

    def draw(self, editor):
        update_anything = False

        # draw the play area
        if self.update_play_area:
            self.update_play_area = False
            update_anything = True

            editor.play_area.focus_x = editor.focus_x
            editor.play_area.focus_y = editor.focus_y
            editor.play_area.redraw()

            frame = editor.play_area.surface.copy()
            for (tile_x, tile_y) in editor.tiles:
                rect = pygame.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)

                sprite = editor.selected_tile_bg if editor.is_selected((tile_x, tile_y)) else editor.tile_bg
                frame.blit(sprite.surface, rect)

            frame.blit(self.screen_label_surface, self.screen_label_rect)
            editor.screen.blit(frame, editor.play_area.viewport)

        # draw sidebar
        if self.update_sidebar:
            self.update_sidebar = False
            update_anything = True

            frame = editor.side_bar.surface.copy()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            editor.screen.blit(frame, editor.side_bar.viewport)

        if update_anything:
            pygame.display.flip()
        else:
            editor.clock.tick(60)


class inspect_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = pygame.mouse.get_pos()
        self.press_start = None
        self.set_screen_label(editor, "inspect")

        active_rect = pygame.Rect(
            editor.grid_size,
            0 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.inspect_active

        goto_select_rect = pygame.Rect(
            editor.grid_size,
            3 * editor.grid_size,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_select_icon = editor.select_target

        self.side_bar_targets = [
            (active_rect, active_icon, None),
            (goto_select_rect, goto_select_icon, self.goto_select_screen)]

    def goto_select_screen(self, editor):
        overlay = select_screen(editor)
        self.purge_events()
        self.update_play_area = True
        self.update_sidebar = True
        editor.clear_selection()

    def on_move(self, editor, pos):
        self.cursor_pos = pos

        if not self.press_start:
            return

        move_x = pos[0] - self.press_start[0]
        move_y = pos[1] - self.press_start[1]

        if move_x != 0 or move_y != 0:
            editor.focus_x -= move_x
            editor.focus_y -= move_y
            self.update_play_area = True

        self.press_start = pos

    def on_press(self, editor, pos):
        if editor.play_rect.collidepoint(pos):
            # begin play are view panning
            self.press_start = pos

        elif editor.side_bar_rect.collidepoint(pos):
            # test side bar targets
            rel_pos = (pos[0] - editor.side_bar.viewport.x, pos[1] - editor.side_bar.viewport.y)
            for rect, surface, action in self.side_bar_targets:
                if action is not None and rect.collidepoint(rel_pos):
                    action(editor)
                    return

    def on_release(self, editor):
        self.press_start = None

    def draw(self, editor):
        update_anything = False

        # draw the play area
        if self.update_play_area:
            self.update_play_area = False
            update_anything = True

            editor.play_area.focus_x = editor.focus_x
            editor.play_area.focus_y = editor.focus_y
            editor.play_area.redraw()

            frame = editor.play_area.surface.copy()
            for (tile_x, tile_y) in editor.tiles:
                rect = pygame.Rect(
                    editor.play_rect.centerx - editor.focus_x - editor.grid_size + tile_x * editor.grid_size * 3,
                    editor.play_rect.centery - editor.focus_y - editor.grid_size + tile_y * editor.grid_size * 3,
                    editor.grid_size * 2, editor.grid_size * 2)

                frame.blit(editor.tile_bg.surface, rect)

            frame.blit(self.screen_label_surface, self.screen_label_rect)
            editor.screen.blit(frame, editor.play_area.viewport)

        # draw sidebar
        if self.update_sidebar:
            self.update_sidebar = False
            update_anything = True

            frame = editor.side_bar.surface.copy()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            editor.screen.blit(frame, editor.side_bar.viewport)

        if update_anything:
            #font_debug_surface(editor.screen)
            pygame.display.flip()
        else:
            editor.clock.tick(60)


def init():
    pygame.init()

    sizes = pygame.display.get_desktop_sizes()
    display_index = len(sizes) - 1
    display_size = sizes[display_index]
    screen = pygame.display.set_mode(size=display_size, display=display_index, flags=pygame.FULLSCREEN)

    dpi = None

    xrandr = subprocess.run("xrandr", stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if xrandr.returncode == 0:
        try:
            report = xrandr.stdout.decode()
            regex = r"Screen (\d+):.+current (\d+) x (\d+).+\n.+ (\d+)mm x (\d+)mm"
            found = [list(map(int, r)) for r in re.findall(regex, report)]
            assert(len(found) == len(sizes))
            for index, (reported_index, res_x, res_y, mm_x, mm_y) in enumerate(found):
                index = reported_index
                assert(sizes[index][0] == res_x)
                assert(sizes[index][1] == res_y)
            reported_index, res_x, res_y, mm_x, mm_y = list(map(int, found[display_index]))
            in_x = mm_x / 25.4
            in_y = mm_y / 25.4
            dpi_x = res_x / in_x
            dpi_y = res_y / in_y
            dpi = round((dpi_x + dpi_y) / 2)
        except AssertionError:
            print("Cannot determine DPI: information reported by xrandr contradicts pygame.")
            dpi = None

    if dpi is None:
        in_y = 7.5
        res_y = min(display_size)
        dpi = round(res_y / in_y)
        print(f"DPI assuming smallest physical screen dimension is 7.5 inches: {dpi} dpi")

    editor = node_graph_card(screen, dpi)
    ui = inspect_screen(editor)


if __name__ == "__main__":
    init()
