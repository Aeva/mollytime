
import string

import pygame_setup
import pygame

import midi
import surface_tools
from color import random_color, rainbow_gradient
from widgets import Tile, Plato, Instrument


AFTERTOUCH_MIN = 0
AFTERTOUCH_MAX = 127
AFTERTOUCH_RANGE = AFTERTOUCH_MAX - AFTERTOUCH_MIN




class RollerTile(Tile):
    """
    This implements an interactive MIDI pad which uses polyphonic aftertouch on channel 1
    to bend the pitch and polyphonic aftertouch on channel 2 to change the velocity.
    """


    def __init__(self, rect, color, note, text, text_color=None):
        self.note = note
        self.rect = rect

        size = (rect.w, rect.h)
        if text and text_color:
            exemplar = string.digits +"ABCDEFG♭♯"
            font_size = rect.w * .5
            self.idle_surface = surface_tools.text_rect(
                size, color, "gentium_book_plus", font_size, text, text_color,
                v_align=.65, exemplar=exemplar)
        else:
            self.idle_surface = surface_tools.rect(size, color)
        self.held_surface = surface_tools.rect(size, random_color())

        self.draw_params = [(self.idle_surface, self.rect)]


    def hold(self, x, y):
        midi.note_on(self.note, 127, channel=1)
        midi.note_on(self.note, 127, channel=2)
        self.draw_params = [(self.held_surface, self.rect)]
        self.rub(x, y)


    def rub(self, x, y):
        y = (1.0 - abs(y * 2 - 1))
        pitch = min(max(x * AFTERTOUCH_RANGE + AFTERTOUCH_MIN, AFTERTOUCH_MIN), AFTERTOUCH_MAX)
        volume = min(max(y * AFTERTOUCH_RANGE + AFTERTOUCH_MIN, AFTERTOUCH_MIN), AFTERTOUCH_MAX)
        midi.polyphonic_pressure(self.note, pitch, channel=1)
        midi.polyphonic_pressure(self.note, volume, channel=2)


    def release(self):
        midi.note_off(self.note, channel=1)
        midi.note_off(self.note, channel=2)
        self.draw_params = [(self.idle_surface, self.rect)]




class RollerPlate(Plato):
    """
    This implements a row of interactive keys supporting polyphonic aftetouch for pitch bending and
    velocity changes.
    """


    def __init__(self, x, y, root=60, notes=13, tile_w=12, tile_h=12, margin=1):
        self.notes = [root + i for i in range(notes)]
        self.tile_w = tile_w
        self.tile_h = tile_h

        pip_w = self.tile_w * notes
        pip_h = self.tile_h
        super().__init__(x, y, pip_w, pip_h)


    def populate(self, pip_to_rect):
        super().populate(pip_to_rect)

        bg_colors = [(240, 240, 240), (224, 224, 224), (32, 32, 32)]
        fg_color = (128, 128, 128)

        for index, note in enumerate(self.notes):
            rect = pip_to_rect(self.pip_min_x + index * self.tile_w, self.pip_min_y, self.tile_w, self.tile_h)
            name = midi.simple_note_name(note)
            bg_color = bg_colors[2 if name[1] in "♭♯" else index % 2]
            tile = RollerTile(rect, bg_color, note, name, fg_color)
            self.tiles.append(tile)


if __name__ == "__main__":
    root = 60 - 12 * 3
    notes = 13
    rows = 7

    roller_plates = [RollerPlate(0, 13 * -i, root + 12 * i, notes) for i in range(rows)]

    keyboards = Instrument(roller_plates)
    midi.run(keyboards)
