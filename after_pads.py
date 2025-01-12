
import pygame_setup
import pygame

import midi
from pads import PadTile, PadArray
from widgets import Instrument




class AfterPadTile(PadTile):


    def hold(self, x, y):
        a = (x + 1.0 - y) * .5
        #a = x if abs(x - .5) >= abs(y - .5) else 1.0 - y
        v = min(max(a * 63 + 1, 1), 64)

        midi.note_on(self.note, 127)
        midi.polyphonic_pressure(self.note, v)
        self.draw_params = [(self.held_surface, self.rect)]


    def rub(self, x, y):
        a = (x + 1.0 - y) * .5
        #a = x if abs(x - .5) >= abs(y - .5) else 1.0 - y
        v = min(max(a * 63 + 1, 1), 64)

        midi.polyphonic_pressure(self.note, v)




class AfterPadArray(PadArray):


    def __init__(self, *args, **kargs):
        super().__init__(*args, **kargs)
        self.tile_type = AfterPadTile




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
        AfterPadArray(0, 0, tile_w, tile_h, root, 1, 1, tile_pips=8)
    ]
    pad_grid = Instrument(plates)
    midi.run(pad_grid)
