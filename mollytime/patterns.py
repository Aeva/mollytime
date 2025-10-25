
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

from . import mollytime

from .fonts import *
from .colors import *
from .more_math import *


class tile_viewport:
    def __init__(self, viewport, grid):
        self.grid = -1
        self.viewport = mollytime.Rect(0, 0, 0, 0)
        self.resize(viewport, grid)

    def resize(self, viewport, grid):
        if viewport == self.viewport and grid == self.grid:
            return
        self.grid = grid
        self.viewport = viewport
        self.surface = mollytime.draw.Texture(viewport.size)
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
        self.bg_ramp_x = color_ramp(parse_color("#a6a8ad"), parse_color("#b1b3b8"))
        self.bg_ramp_y = color_ramp(parse_color("#b1b3b8"), parse_color("#a3a9bb"))
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

        color_x = self.bg_ramp_x.sample(alpha)
        color_y = self.bg_ramp_y.sample(alpha)

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
                rect = mollytime.Rect(view_x, view_y, self.grid, self.grid)
                color = self.bg_color(tile_x, tile_y, rect)
                mollytime.draw.rect(self.surface, color, rect)

        # coarse grid
        for view_tile_y in range(-1, y_count):
            for view_tile_x in range(-1, x_count):
                tile_x = crop_min_x // self.grid + view_tile_x
                tile_y = crop_min_y // self.grid + view_tile_y
                if (tile_x % 3) != 2 or (tile_y % 3) != 2:
                    continue
                view_x = view_tile_x * self.grid + x_offset
                view_y = view_tile_y * self.grid + y_offset
                rect = mollytime.Rect(view_x, view_y, self.grid * 2, self.grid * 2)
                color = self.bg_color(tile_x, tile_y, rect)
                mollytime.draw.rect(self.surface, color, rect)


class side_bar_bg(tile_viewport):
    def resize(self, viewport, grid):
        super().resize(viewport, grid)

    def redraw(self):
        ramp_a = (0.4, 0.04, 0)
        ramp_b = (0.4, 0.04, 360)
        # ramp_a = (0.5, 0.15, 0)
        # ramp_b = (0.5, 0.15, 360)

        # sidebar color ramp
        steps = int(self.viewport.w) // 8
        for i in range(steps):
            alpha = i / (steps - 1)
            inv_a = 1.0 - alpha
            params = [LHS * alpha + RHS * inv_a for LHS, RHS in zip(ramp_a, ramp_b)]
            color = oklch(*params)

            alpha = i / steps
            inv_a = 1.0 - alpha

            rect = mollytime.Rect(0, 0, self.viewport.w * inv_a, self.viewport.h)
            mollytime.draw.rect(self.surface, color, rect)


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
        self.surface = mollytime.draw.Texture((self.size, self.size))
        self.redraw()

    def redraw(self):
        rect = mollytime.Rect(0, 0, self.size, self.size)
        depth = max(round(self.size / 22.6), 1)

        mollytime.draw.rect(self.surface, self.color_base, rect)
        mollytime.draw.rect(self.surface, self.color_sides, rect, depth)

        for i in range(0, depth):
            a = (rect.topleft[0] + i, rect.topleft[1] + i)
            b = (rect.topright[0] - i - 1, rect.topright[1] + i)
            mollytime.draw.line(self.surface, self.color_top, a, b, 1)

            a = (rect.bottomleft[0] + i, rect.bottomleft[1] - i - 1)
            b = (rect.bottomright[0] - i - 1, rect.bottomright[1] - i - 1)
            mollytime.draw.line(self.surface, self.color_bottom, a, b, 1)

        if self.text:
            self.draw_label(self.surface, rect, self.text)

    def draw_label(self, target, rect, label):
        assert(type(label) == str)
        font_path, size = NATIONAL_PARK_REGULAR, max(10, self.size * .24)
        lines = [i for i in map(str.strip, label.split("\n")) if i]
        surfaces = [render_text(font_path, size, self.text_color, i) for i in lines]
        rects = []
        spacing = int(size)
        y_offset = 0
        for text_surface in surfaces:
            text_rect = text_surface.get_rect().copy()
            text_rect.centerx = rect.centerx
            text_rect.top = y_offset
            rects.append(text_rect)
            y_offset += spacing
        combined_rect = rects[0].unionall(rects[1:])

        if len(lines) > 0 and lines[0].upper() == lines[0]:
            top = estimate_font_cap_line(font_path, size)
        else:
            top = estimate_font_mean_line(font_path, size)

        descent = get_font_descent(font_path, size)
        combined_rect.top += top
        combined_rect.height -= top
        combined_rect.height -= descent

        y_offset = rect.centery - combined_rect.centery
        for text_surface, text_rect in zip(surfaces, rects):
            text_rect.top += y_offset
            target.blit(text_surface, text_rect)

    def draw(self, target, rect, label=None):
        target.blit(self.surface, rect)
        if label:
            self.draw_label(target, rect, label)


def draw_arrow(target, color, start, end, radius, inset=.5):
    start_pt = start.center if type(start) == mollytime.Rect else start
    end_pt = end.center if type(end) == mollytime.Rect else end

    inset = int(radius * inset)

    if type(start) == mollytime.Rect:
        start = start.copy()
        start.x += inset
        start.y += inset
        start.w -= inset * 2
        start.h -= inset * 2
        if line := start.clipline(start_pt, end_pt):
            start_pt = line[1]

    if type(end) == mollytime.Rect:
        end = end.copy()
        end.x += inset
        end.y += inset
        end.w -= inset * 2
        end.h -= inset * 2
        if line := end.clipline(start_pt, end_pt):
            end_pt = line[0]

    mollytime.draw.line(target, color, start_pt, end_pt, radius * 2)
    mollytime.draw.circle(target, color, start_pt, radius)
    mollytime.draw.circle(target, color, end_pt, radius)

    point = vec_scale(normalize(vec_sub(start_pt, end_pt)), radius * 4)
    for angle in [-35, 35]:
        arrow_pt = vec_add(end_pt, rotate_point(point, angle))
        mollytime.draw.line(target, color, end_pt, arrow_pt, radius * 2)
        mollytime.draw.circle(target, color, arrow_pt, radius)


class plate_outline(plate_bg):
    def __init__(self, grid, color, cross_out = False):
        self.cross_out = cross_out
        super().__init__(grid, color)
        self.text_color = color

    def redraw(self):
        self.surface = mollytime.draw.Texture((self.size, self.size))
        line_radius = max(int(self.size / 67), 1)
        inset = line_radius * 2
        rect = mollytime.Rect(0, 0, self.size, self.size)
        corners = [
            vec_add(rect.topleft, (inset, inset)),
            vec_add(rect.topright, (-inset, inset)),
            vec_add(rect.bottomright, (-inset, -inset)),
            vec_add(rect.bottomleft, (inset, -inset))]

        fill_rect = mollytime.Rect(inset, inset, self.size - inset * 2, self.size - inset * 2)
        mollytime.draw.rect(self.surface, self.color_base, fill_rect, alpha = .1)

        for edge in range(4):
            a = corners[edge]
            b = corners[(edge + 1) % 4]
            mollytime.draw.line(self.surface, self.color_base, a, b, line_radius * 2)
        if self.cross_out:
            inset = max(int(self.size / 3), 8)
            more_corners = [
                vec_add(rect.topleft, (inset, inset)),
                vec_add(rect.topright, (-inset, inset)),
                vec_add(rect.bottomright, (-inset, -inset)),
                vec_add(rect.bottomleft, (inset, -inset))]
            for edge in range(4):
                a = corners[edge]
                b = more_corners[edge]
                mollytime.draw.line(self.surface, self.color_base, a, b, line_radius * 2)
