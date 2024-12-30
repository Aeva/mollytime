
import math


tile_count = 9

origin_tile_x = tile_count // 2
origin_tile_y = tile_count // 2


def sign(x):
    return (x >= 0) * 2 - 1


class basic_note_grid:
    def __init__(self, origin_note, row_offset, col_offset):
        self.origin = origin_note
        self.row = row_offset
        self.col = col_offset

    def __call__(self, x, y):
        return self.origin + self.row * x + self.col * y


class scales_grid:
    def __init__(self, origin_note, x_scale, y_scale):
        self.origin = origin_note
        self.x_scale = x_scale
        self.y_scale = y_scale

    def __call__(self, x, y):
        if x == 0 and y == 0:
            return self.origin
        elif y < 0:
            return self(x, y + 1) + self.y_scale[y % len(self.y_scale)] * sign(y)
        elif y > 0:
            return self(x, y - 1) + self.y_scale[y % len(self.y_scale)] * sign(y)
        elif x < 0:
            return self(x + 1, y) + self.x_scale[x % len(self.x_scale)] * sign(x)
        elif x > 0:
            return self(x - 1, y) + self.x_scale[x % len(self.x_scale)] * sign(x)
        else:
            assert(False)
