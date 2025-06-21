
import pygame_setup
import pygame

from fonts import *
from colors import *
from more_math import *


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

        if self.text:
            self.draw_label(self.surface, rect, self.text)

    def draw_label(self, target, rect, label):
        assert(type(label) == str)
        font_path, size = NATIONAL_PARK_REGULAR, max(10, self.size * .24)
        lines = [i for i in map(str.strip, label.split("\n")) if i]
        surfaces = [render_text(font_path, size, self.text_color, i) for i in lines]
        spacing = int(size)
        y_offset = len(surfaces) // 2 * -spacing * .5
        for text_surface in surfaces:
            text_rect = text_surface.get_rect().copy()
            text_rect.centerx = rect.centerx
            text_rect.top = rect.centery - estimate_font_x_center(font_path, size) + y_offset
            target.blit(text_surface, text_rect)
            y_offset += spacing

    def draw(self, target, rect, label=None):
        target.blit(self.surface, rect)
        if label:
            self.draw_label(target, rect, label)


def draw_line(target, color, start, end, radius):
    offset = vec_scale(normalize(vec_sub(end, start)), radius)
    points = [
        vec_add(start, widdershins_by_90(offset)),
        vec_add(end, widdershins_by_90(offset)),
        vec_add(end, sunwise_by_90(offset)),
        vec_add(start, sunwise_by_90(offset))]
    pygame.draw.polygon(target, color, points)
