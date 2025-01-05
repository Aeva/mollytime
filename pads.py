
import string

import pygame_setup
import pygame

import midi
import surface_tools
from color import random_color, rainbow_gradient
from widgets import Tile, Plato, Instrument




class PadTile(Tile):
    """
    This implements an interactive MIDI pad that plays one note at maximum velocity.
    """

    def __init__(self, rect, note, idle_color, hold_color=None):
        self.rect = rect
        self.note = note

        size = (rect.w, rect.h)
        font_size = rect.h * .5
        text = midi.simple_note_name(note)
        hold_color = hold_color or (255, 255, 255)

        text_args = ["gentium_book_plus", font_size, text, (0, 0, 0)]

        self.idle_surface = surface_tools.text_rect(size, idle_color, *text_args, v_align=.64, exemplar=string.digits+"ABCDEFG♭♯")
        self.held_surface = surface_tools.text_rect(size, hold_color, *text_args, v_align=.64, exemplar=string.digits+"ABCDEFG♭♯")

        self.draw_params = [(self.idle_surface, self.rect)]


    def hold(self):
        midi.note_on(self.note, 127)
        self.draw_params = [(self.held_surface, self.rect)]


    def release(self):
        midi.note_off(self.note)
        self.draw_params = [(self.idle_surface, self.rect)]



class PadArray(Plato):
    """
    This implements a 2D array of PadTile objects with an isomorphic note layout.
    """

    def __init__(self, x, y, w, h, center_note, x_offset, y_offset, tile_pips=4, margin_pips=1, spacing_pips=1):
        """
        Args `x`, `y`, `tile_pips`, `margin_pips`, and `spacing_pips are specified as pip counts.
        Args `w`, and `h`, are specified in tile counts.
        Arg `center_note` is a MIDI note number.
        Args `x_offset` and `y_offset are relative MIDI note offsets.

        This is a cold path.
        """

        self.tile_w = int(abs(w))
        self.tile_h = int(abs(h))

        self.tile_pips = int(abs(tile_pips))
        self.margin_pips = int(abs(margin_pips))
        self.spacing_pips = int(abs(spacing_pips))

        pip_w = self.tile_w * self.tile_pips + max(self.tile_w - 1, 0) * self.spacing_pips + 2 * self.margin_pips
        pip_h = self.tile_h * self.tile_pips + max(self.tile_h - 1, 0) * self.spacing_pips + 2 * self.margin_pips

        if w < 0:
            x -= pip_w
        if h < 0:
            y -= pip_h

        super().__init__(x, y, pip_w, pip_h)

        # The pad notes are calculated here instead of in populate to avoid
        # storing parameters persistently as extra object attributes

        center_x = self.tile_w // 2
        center_y = self.tile_h // 2
        tile_count = self.tile_w * self.tile_h
        self.note_lut = [0] * tile_count

        for tile_index in range(tile_count):
            tile_x = tile_index % self.tile_w
            tile_y = tile_index // self.tile_w

            note = center_note
            note += (tile_x - center_x) * x_offset
            note -= (tile_y - center_y) * y_offset

            self.note_lut[tile_index] = note


    def populate(self, pip_to_rect):
        """
        Create the PadTile instances belonging to this plate.

        This is a cold path.
        """
        super().populate(pip_to_rect, (0, 0, 0))

        tile_count = self.tile_w * self.tile_h

        min_note = min(*self.note_lut)
        max_note = max(*self.note_lut)

        self.tiles = [None] * tile_count

        for tile_index in range(tile_count):
            tile_x = tile_index % self.tile_w
            tile_y = tile_index // self.tile_w
            pip_x = self.pip_min_x + self.margin_pips + (self.tile_pips + self.spacing_pips) * tile_x
            pip_y = self.pip_min_y + self.margin_pips + (self.tile_pips + self.spacing_pips) * tile_y

            rect = pip_to_rect(pip_x, pip_y, self.tile_pips, self.tile_pips)
            note = self.note_lut[tile_index]
            idle_color = rainbow_gradient(note, min_note, max_note)
            hold_color = None

            tile = PadTile(rect, note, idle_color, hold_color)
            self.tiles[tile_index] = tile




if __name__ == "__main__":
    pygame.init()
    w, h = pygame.display.get_desktop_sizes()[-1]
    ratio = w/h

    root = 60
    tile_h = 9

    # try to match the screen ratio, but always select an odd number
    tile_w = int(tile_h * ratio)
    if tile_w % 2 == 0:
        tile_w -= 1

    plates = [
        PadArray(0, 0, tile_w, tile_h, root, 2, 3)
    ]
    pad_grid = Instrument(plates)
    midi.run(pad_grid)
