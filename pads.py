
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




class TileArray(Plato):
    """
    This is a generic-ish subclass of Plato for creating 2D grids of pads with uniform
    spacing.  This doesn't strictly need to be separate from PadArray, but all of the
    overrided functions are cold paths.  Might be useful for other widgets to use, but
    some refactoring would be required to change the Tile type that is instanced.
    """

    def __init__(self, x, y, w, h, tile_pips=4, margin_pips=1, spacing_pips=1):
        """
        Args `x`, and `y`, are specified as pip counts.
        Args `w`, and `h`, are specified in tile counts.
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


    def get_note(self, tile_index, tile_x, tile_y):
        """
        Return the MIDI note number corresponding to a given tile location.
        Used for constructing the PadTile instances.
        """

        return random.randint(60, 84)


    def get_idle_color(self, tile_index, tile_x, tile_y):
        """
        Return the idle color associated to a given tile location.
        """

        return random_color()


    def get_hold_color(self, tile_index, tile_x, tile_y):
        """
        Return the hold color associated to a given tile location.
        """

        return None


    def populate(self, pip_to_rect):
        super().populate(pip_to_rect, (0, 0, 0))

        tile_count = self.tile_w * self.tile_h

        for i in range(tile_count):
            tile_x = i % self.tile_w
            tile_y = i // self.tile_w
            pip_x = self.pip_min_x + self.margin_pips + (self.tile_pips + self.spacing_pips) * tile_x
            pip_y = self.pip_min_y + self.margin_pips + (self.tile_pips + self.spacing_pips) * tile_y
            note = self.get_note(i, tile_x, tile_y)
            idle_color = self.get_idle_color(i, tile_x, tile_y)
            hold_color = self.get_hold_color(i, tile_x, tile_y)
            self.tiles.append(PadTile(pip_to_rect(pip_x, pip_y, self.tile_pips, self.tile_pips), note, idle_color, hold_color))




class PadArray(TileArray):
    """
    This implements the pad grid Plato object.
    """

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

        self.color_lut = [rainbow_gradient(note, min_note, max_note) for note in self.note_lut]


    def get_note(self, tile_index, tile_x, tile_y):
        return self.note_lut[tile_index]


    def get_idle_color(self, tile_index, tile_x, tile_y):
        return self.color_lut[tile_index]




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
