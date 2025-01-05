
import pygame_setup
import pygame

import midi
import surface_tools
from color import hsv
from widgets import Tile, Plato, Instrument




class SpookyTile(Tile):

    def __init__(self, rect, note, bend, loud, polyphonic_aftertouch, idle_color):
        self.rect = rect
        self.note = note
        self.bend = bend
        self.loud = loud
        self.poly = polyphonic_aftertouch
        size = (rect.w, rect.h)
        hold_color = (255, 255, 255)

        self.idle_surface = surface_tools.rect(size, idle_color)
        self.held_surface = surface_tools.rect(size, hold_color)

        self.draw_params = [(self.idle_surface, self.rect)]


    def hold(self):
        midi.note_on(self.note, self.loud)
        self.draw_params = [(self.held_surface, self.rect)]


    def release(self):
        midi.note_off(self.note)
        self.draw_params = [(self.idle_surface, self.rect)]




class SpookyPlate(Plato):

    def __init__(self, x, y, w, h, low_note=60, high_note=96, polyphonic_aftertouch=True):
        super().__init__(x, y, w, h)
        self.low_note = low_note
        self.high_note = high_note
        self.polyphonic_aftertouch = polyphonic_aftertouch

    def populate(self, pip_to_rect):
        self.frame = pip_to_rect(self.pip_min_x, self.pip_min_y, self.pip_w, self.pip_h)

        note_range = abs(self.high_note - self.low_note)
        tile_count = self.pip_w * self.pip_h
        self.tiles = [None] * tile_count

        for tile_index in range(tile_count):
            tile_x = tile_index % self.pip_w
            tile_y = tile_index // self.pip_w

            hue = (tile_x / (self.pip_w - 1))
            val = 1.0 - (tile_y / (self.pip_h - 1))

            note = self.low_note + note_range * hue
            bend = note - int(note)
            note = int(note)
            loud = int(val * 127)

            rect = pip_to_rect(self.pip_min_x + tile_x, self.pip_min_y + tile_y, 1, 1)

            idle_color = hsv(hue, 1, val)

            self.tiles[tile_index] = SpookyTile(rect, note, bend, loud, self.polyphonic_aftertouch, idle_color)



if __name__ == "__main__":
    pygame.init()
    w, h = pygame.display.get_desktop_sizes()[-1]
    ratio = w/h

    tile_h = int(h / 10)
    tile_w = int(tile_h * ratio)

    plates = [
        SpookyPlate(0, 0, tile_w, tile_h)
    ]
    theramin = Instrument(plates)

    midi.run(theramin)
