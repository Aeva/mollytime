
import overrides

import math

import pygame

import midi
from tiles import TileArray, Instrument


def hue_to_rgb(h):
    h = (h * 6) % 6
    a = h - math.floor(h)
    r = 0
    g = 0
    b = 0
    if h < 1:
        r = 1
        g = a
    elif h < 2:
        r = 1 - a
        g = 1
    elif h < 3:
        g = 1
        b = a
    elif h < 4:
        g = 1 - a
        b = 1
    elif h < 5:
        r = a
        b = 1
    else:
        r = 1
        b = 1 - a
    return (r, g, b)


def byte_color(float_color):
    return tuple([min(max(int(f * 255), 0), 255) for f in float_color])


def heat_map(note, low, high):
    midi_range = abs(high - low)
    a = (note - low) / midi_range
    return byte_color(hue_to_rgb(a))


class PadArray(TileArray):
    def __init__(self, x, y, w, h, center_note, x_offset, y_offset):
        super().__init__(x, y, w, h)
        self.center_note = center_note
        self.note_x_offset = x_offset
        self.note_y_offset = y_offset
        self.center_x = self.tile_w // 2
        self.center_y = self.tile_h // 2

        self.note_lut = []

        tile_count = self.tile_w * self.tile_h
        for i in range(tile_count):
            tile_x = i % self.tile_w
            tile_y = i // self.tile_w
            note = self.center_note \
                + (tile_x - self.center_x) * self.note_x_offset \
                - (tile_y - self.center_y) * self.note_y_offset

            self.note_lut.append(note)

        min_note = min(*self.note_lut)
        max_note = max(*self.note_lut)

        self.color_lut = [heat_map(note, min_note, max_note) for note in self.note_lut]

    def get_note(self, i, tile_x, tile_y):
        return self.note_lut[i]

    def get_idle_color(self, i, tile_x, tile_y):
        return self.color_lut[i]


if __name__ == "__main__":
    pygame.init()
    w, h = pygame.display.get_desktop_sizes()[-1]
    ratio = w/h

    root = 60
    tile_h = 9

    tile_w = int(tile_h * ratio)
    if tile_w % 2 == 0:
        tile_w -= 1

    plates = [
        PadArray(0, 0, tile_w, tile_h, root, 2, 3)
    ]
    keyboards = Instrument(plates)
    midi.run(keyboards)
