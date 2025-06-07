
import time
import math
import os
import ctypes
import random
import enum

import pygame_setup
import pygame


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
        for view_tile_y in range(y_count):
            for view_tile_x in range(x_count):
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
    def __init__(self, grid, color):
        L, C, H = convert_color([i / 255 for i in color], ColorSpace.sRGB, ColorSpace.OkLCH)
        self.color_base = color
        self.color_top    = oklch(L - 0.0183, C, H)
        self.color_sides  = oklch(L - 0.0986, C, H)
        self.color_bottom = oklch(L - 0.2914, C, H + 3.8415)

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


class main_view:

    def __init__(self, screen):
        self.screen = screen
        self.clock = pygame.time.Clock()

        self.press_start = None
        self.update_play_area = True
        self.update_sidebar = True
        self.focus_x = 0
        self.focus_y = 0

        self.touch = {}
        self.touch['mouse'] = {}


        self.live = True

        screen_w = self.screen.get_rect().width
        screen_h = self.screen.get_rect().height

        self.grid_size = int(screen_h / 8 / 3)

        side_bar_w = self.grid_size * 3
        side_bar_h = screen_h

        self.play_rect = pygame.Rect(0, 0, screen_w - side_bar_w, screen_h)
        self.play_area = tile_grid_bg(self.play_rect, self.grid_size)

        self.side_bar_rect = pygame.Rect(screen_w - side_bar_w, 0, side_bar_w, side_bar_h)
        self.side_bar = side_bar_bg(self.side_bar_rect, self.grid_size)

        self.tile_bg = plate_bg(self.grid_size, parse_color("#dee5e8"))


        self.tool_tiles = [plate_bg(self.grid_size, color) for color in [oklch(0.7, 0.2, 360 * (i / 5)) for i in range(5)]]


        # # create some fake buttons
        # x_count = math.ceil(play_rect.w / grid_size)
        # y_count = math.ceil(play_rect.h / grid_size)
        # x_offset = (play_rect.w - x_count * grid_size) // 2
        # y_offset = (play_rect.h - y_count * grid_size) // 2
        #
        # tile_count_x = play_rect.w // (grid_size * 3)
        # tile_count_y = play_rect.h // (grid_size * 3)
        # tiles = {}
        # for tile_y in range(tile_count_y):
        #     for tile_x in range(tile_count_x):
        #         if not random.randint(1, 4) < 3:
        #             continue
        #         tiles[(tile_x, tile_y)] = {}
        self.tiles = [(-1, 0), (1, 0), (0, -1), (0, 1)]

        self.start_time = time.time()
        while self.live:
            self.loop()

    def get_tracker(event):
        key = (event.touch_id, event.finger_id)
        if key not in self.touch:
            self.touch[key] = {}
        return key

    def on_move(self, touch_id, pos):
        if not self.press_start:
            return

        move_x = pos[0] - self.press_start[0]
        move_y = pos[1] - self.press_start[1]

        if move_x != 0 or move_y != 0:
            self.focus_x -= move_x
            self.focus_y -= move_y
            self.update_play_area = True

        self.press_start = pos

    def on_press(self, touch_id, pos):
        self.press_start = pos

    def on_release(self, touch_id):
        self.press_start = None
        self.touch[touch_id] = {}

    def loop(self):
        seconds = time.time() - self.start_time
        update_anything = False

        for event in pygame.event.get():
            if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                self.live = False

            elif event.type == pygame.FINGERMOTION:
                pos = (int(event.x * w), int(event.y * h))
                self.on_move(get_tracker(event), pos)

            elif event.type == pygame.FINGERDOWN:
                pos = (int(event.x * w), int(event.y * h))
                self.on_press(get_tracker(event), pos)

            elif event.type == pygame.FINGERUP:
                self.on_release(get_tracker(event))

            elif event.type == pygame.MOUSEMOTION and not event.touch and (abs(event.rel[0]) > 0 or abs(event.rel[1]) > 0):
                self.on_move('mouse', event.pos)

            elif event.type == pygame.MOUSEBUTTONDOWN and not event.touch and event.button == pygame.BUTTON_LEFT:
                self.on_press('mouse', event.pos)

            elif event.type == pygame.MOUSEBUTTONUP and not event.touch and event.button == pygame.BUTTON_LEFT:
                self.on_release('mouse')

        # draw the play area
        if self.update_play_area:
            self.update_play_area = False
            update_anything = True

            self.play_area.focus_x = self.focus_x
            self.play_area.focus_y = self.focus_y
            self.play_area.redraw()

            frame = self.play_area.surface.copy()
            for (tile_x, tile_y) in self.tiles:
                rect = pygame.Rect(
                    self.play_rect.centerx - self.focus_x - self.grid_size + tile_x * self.grid_size * 3,
                    self.play_rect.centery - self.focus_y - self.grid_size + tile_y * self.grid_size * 3,
                    self.grid_size * 2, self.grid_size * 2)

                frame.blit(self.tile_bg.surface, rect)

            self.screen.blit(frame, self.play_area.viewport)

        # draw sidebar
        if self.update_sidebar:
            self.update_sidebar = False
            update_anything = True

            frame = self.side_bar.surface.copy()
            for tile_index in range(9):
                tile_y = tile_index * 3

                rect = pygame.Rect(
                    self.grid_size,
                    tile_y * self.grid_size,
                    self.grid_size * 2, self.grid_size * 2)

                tool = self.tool_tiles[tile_index % len(self.tool_tiles)]
                frame.blit(tool.surface, rect)

            self.screen.blit(frame, self.side_bar.viewport)

        if update_anything:
            pygame.display.flip()
        else:
            self.clock.tick(60)


def init():
    pygame.init()

    sizes = pygame.display.get_desktop_sizes()
    display_index = len(sizes) - 1
    display_size = sizes[display_index]
    screen = pygame.display.set_mode(size=display_size, display=display_index, flags=pygame.FULLSCREEN)
    ui = main_view(screen)


if __name__ == "__main__":
    init()
