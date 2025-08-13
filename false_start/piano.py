
import math
import string
import itertools

import pygame_setup
import pygame

import midi
import surface_tools
from color import random_color
from widgets import Tile, Plato, Instrument




class PianoTile(Tile):
    """
    Implements an interactive MIDI piano key which is played at maximum velocity.  May
    be a white key or a black key.
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
                v_align=1, exemplar=exemplar)
        else:
            self.idle_surface = surface_tools.rect(size, color)
        self.held_surface = surface_tools.rect(size, random_color())

        self.draw_params = [(self.idle_surface, self.rect)]


    def hold(self, x, y):
        midi.note_on(self.note, 127)
        self.draw_params = [(self.held_surface, self.rect)]


    def release(self):
        midi.note_off(self.note)
        self.draw_params = [(self.idle_surface, self.rect)]




class Piano(Plato):
    """
    Implements a row of interactive piano keys with an arbitrary root note and scale.
    """


    def __init__(self, x, y, root=60, scale=[2, 2, 1, 2, 2, 2, 1], notes=13, wht_w=3, blk_h=5, wht_h=8, spill_mode=3):
        """
        Args `x`, `y`, `wht_w`, `blk_h`, and `wht_h` are specified as pip counts.
        Arg `root` is the midi note corresponding on the left-most key.
        Arg `scale` is the list of midi offsets needed to enumerate the white keys, and
        each interval must be either a 1 or a 2.
        Arg `spill_mode` indicates how to adjust if the last key in the sequence is a
        black key.

        The valid spill modes are:

         - 0: Let it dangle outside the plate's bounding rect.

         - 1: Clamp the key to the plate's bounding rect.

         - 2: Expand the plate's bounding rect.

         - 3: Delete the spilling key.

        The default spill mode is 3 (delete) so as to match the appearance of standard
        piano keyboards.
        """

        for note in scale:
            assert(note == 1 or note == 2)

        self.root = root
        self.scale = scale

        distance = 0
        for i in range(notes):
            distance += scale[i % len(scale)]
            if distance >= notes:
                self.scale = (scale * math.ceil(i / len(scale) + 1))[:i+1]
                break

        if sum(self.scale) > notes:
            self.scale[-1] = 1

        self.blk_h = int(blk_h)
        self.wht_w = int(wht_w)
        self.wht_h = int(wht_h)

        self.spill_mode = spill_mode

        pip_w = len(self.scale) * self.wht_w
        pip_h = self.wht_h

        super().__init__(x, y, pip_w, pip_h)


    def populate(self, pip_to_rect):
        super().populate(pip_to_rect)

        wht_keys = [None] * len(self.scale)
        offset = 0
        for index, note in enumerate(self.scale):
            rect = pip_to_rect(self.pip_min_x + offset, self.pip_min_y, self.wht_w, self.wht_h)
            wht_keys[index] = rect
            offset += self.wht_w

        blk_keys = []

        wht_ref = pip_to_rect(0, 0, self.wht_w, self.wht_h)
        blk_ref = pip_to_rect(0, 0, 0, self.blk_h)
        unit_ref = pip_to_rect(0, 0, 1, 1)

        blk_keys = [[]]

        for index, note in enumerate(self.scale):
            flat = wht_keys[index]
            is_last_index = (index + 1) == len(self.scale)
            if note == 2:
                rect = pygame.Rect(flat.x, flat.y, 1, blk_ref.h)
                blk_keys[-1].append(rect)
            else:
                blk_keys.append([])

        for group in blk_keys:
            if len(group) >= 1:
                full_span = wht_ref.w * (len(group) + 1)
                spacing = full_span / (len(group) * 2 + 1)

                x_min = group[0].x + spacing
                x_max = group[0].x + full_span - spacing

                if len(group) > 1:
                    for index, rect in enumerate(group):
                        last_index = len(group) - 1
                        alpha = index / last_index
                        pivot = spacing * alpha
                        x = ((1 - alpha) * x_min + alpha * x_max) - pivot
                        new_rect = pygame.Rect(x, rect.y, spacing, rect.h)
                        group[index] = new_rect

                else:
                    rect = group[0]
                    new_rect = pygame.Rect(x_min, rect.y, spacing, rect.h)
                    group[0] = new_rect

        blk_keys = list(itertools.chain(*blk_keys))

        if blk_keys[-1].x + blk_keys[-1].w > self.frame.x + self.frame.w:
            if self.spill_mode == 1:
                # clamp on spill
                blk_keys[-1] = blk_keys[-1].clip(self.frame)
            elif self.spill_mode == 2:
                # expand on spill
                self.frame.union_ip(blk_keys[-1])
            elif self.spill_mode == 3:
                # discard on spill
                blk_keys.pop()

        note = self.root
        blk_notes = []
        wht_notes = []
        for interval in self.scale:
            if interval == 1:
                wht_notes.append(note)
            elif interval == 2:
                wht_notes.append(note)
                blk_notes.append(note + 1)
            note += interval

        # note = self.root
        # spelling = []
        # for interval in self.scale:
        #     spelling.append(midi_octave_labels[note % 12])
        #     note += interval
        #
        # for lhs_i, chs_i, rhs_i in zip(range(0, len(spelling)-2), range(1, len(spelling)-2), range(2, len(spelling))):
        #     lhs = spelling[lhs_i]
        #     chs = spelling[chs_i]
        #     rhs = spelling[rhs_i]
        #     if len(chs) == 2:
        #         if lhs[0] == chs[0][0]:
        #             if lhs[0] == chs[0][0]:

        wht_colors = [(240, 240, 240), (224, 224, 224)]
        for index, (wht_key, note) in enumerate(zip(wht_keys, wht_notes)):
            text = midi.simple_note_name(note, tie=-1)
            text_color = None
            if (note - self.root) % 12 == 0:
                text_color = (128, 128, 128)
            else:
                text_color = (192, 192, 192)
            self.tiles.append(PianoTile(wht_key, wht_colors[index % len(wht_colors)], note, text, text_color))

        for blk_key, note in zip(blk_keys, blk_notes):
            text = midi.simple_note_name(note)
            self.tiles.append(PianoTile(blk_key, (32, 32, 32), note, text, (128, 128, 128)))




if __name__ == "__main__":
    root = 60

    scale = [2, 1, 2, 2, 2, 1, 2]
    notes = 12 * 2 + 1

    plates = [
        Piano(0, 0, root - 12, scale, notes),
        Piano(0, 9, root, scale, notes),
        Piano(0, 18, root + 12, scale, notes)
    ]
    keyboards = Instrument(plates)
    midi.run(keyboards)
