
import overrides

import math
import time
import random
import itertools

import pygame

import surface_tools
import midi


def random_color():
    held_color = [random.randint(64, 192), random.randint(0, 128), random.randint(128, 255)]
    random.shuffle(held_color)
    return tuple(held_color)


class Tile:
    def __init__(self, rect, color):
        if not color:
            color = (random.randint(0, 255), random.randint(0, 255), random.randint(0, 255))
        surface = surface_tools.rect((rect.w, rect.h), color)
        self.draw_params = [(surface, rect)]

    def hold(self):
        pass

    def release(self):
        pass


class PianoTile(Tile):
    def __init__(self, rect, color, note):
        self.note = note
        self.rect = rect

        self.idle_surface = surface_tools.rect((rect.w, rect.h), color)
        self.held_surface = surface_tools.rect((rect.w, rect.h), random_color())

        self.draw_params = [(self.idle_surface, self.rect)]

    def hold(self):
        midi.note_on(self.note, 127)
        self.draw_params = [(self.held_surface, self.rect)]

    def release(self):
        midi.note_off(self.note)
        self.draw_params = [(self.idle_surface, self.rect)]


class PadTile(Tile):
    def __init__(self, rect, note):
        self.rect = rect
        self.note = note

        size = (rect.w, rect.h)
        font_size = rect.h * .75
        text = str(note)
        idle_color = random_color()
        hold_color = (255, 255, 255)

        self.idle_surface = surface_tools.text_rect(size, idle_color, "overpass", font_size, text)
        self.held_surface = surface_tools.text_rect(size, hold_color, "overpass", font_size, text)

        self.draw_params = [(self.idle_surface, self.rect)]

    def hold(self):
        midi.note_on(self.note, 127)
        self.draw_params = [(self.held_surface, self.rect)]

    def release(self):
        midi.note_off(self.note)
        self.draw_params = [(self.idle_surface, self.rect)]


class Plato:
    def __init__(self, x, y, w, h):
        """
        Args `x`, `y`, `w`, and `h` are specified as pip counts.

        Coordinates are as-if the origin were in the top left corner of the
        screen (same as pygame), but the play surface will fit this into actual
        screen space, thus negative coordinates are legal and useful.
        """

        self.pip_min_x = int(x)
        self.pip_min_y = int(y)
        self.pip_w = int(w)
        self.pip_h = int(h)

        if self.pip_w < 0:
            self.pip_w = abs(self.pip_w)
            self.pip_min_x -= self.pip_w

        if self.pip_h < 0:
            self.pip_h = abs(self.pip_h)
            self.pip_min_y -= self.pip_h

        self.pip_w = max(self.pip_w, 1)
        self.pip_h = max(self.pip_h, 1)

        self.pip_max_x = self.pip_min_x + self.pip_w - 1
        self.pip_max_y = self.pip_min_y + self.pip_h - 1

        # Rects needed to draw the widget, in order.  Usually filled in by populate.
        self.frame = None
        self.tiles = []

    def populate(self, pip_to_rect):
        self.frame = pip_to_rect(self.pip_min_x, self.pip_min_y, self.pip_w, self.pip_h)
        self.tiles = [Tile(self.frame, (64, 64, 64))]

    def match(self, point):
        if self.frame.collidepoint(point):
            for tile in reversed(self.tiles):
                for surface, rect in tile.draw_params:
                    if rect.collidepoint(point):
                        return tile
        return None


class Piano(Plato):
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

        wht_colors = [(240, 240, 240), (224, 224, 224)]
        for index, (wht_key, note) in enumerate(zip(wht_keys, wht_notes)):
            self.tiles.append(PianoTile(wht_key, wht_colors[index % len(wht_colors)], note))

        for blk_key, note in zip(blk_keys, blk_notes):
            self.tiles.append(PianoTile(blk_key, (32, 32, 32), note))


class TileArray(Plato):
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


    def populate(self, pip_to_rect):
        super().populate(pip_to_rect)

        tile_count = self.tile_w * self.tile_h

        for i in range(tile_count):
            tile_x = i % self.tile_w
            tile_y = i // self.tile_w
            pip_x = self.pip_min_x + self.margin_pips + (self.tile_pips + self.spacing_pips) * tile_x
            pip_y = self.pip_min_y + self.margin_pips + (self.tile_pips + self.spacing_pips) * tile_y
            note = random.randint(60, 84)
            self.tiles.append(PadTile(pip_to_rect(pip_x, pip_y, self.tile_pips, self.tile_pips), note))


class PlaySurface:
    def __init__(self, screen_size, plates, horizontal_align=.5, vertical_align=.5):
        self.screen_w, self.screen_h = screen_size
        self.plates = plates

        pip_min_x = plates[0].pip_min_x
        pip_min_y = plates[0].pip_min_y
        pip_max_x = plates[0].pip_max_x
        pip_max_y = plates[0].pip_max_y

        for board in plates[1:]:
            pip_min_x = min(pip_min_x, board.pip_min_x)
            pip_min_y = min(pip_min_y, board.pip_min_y)
            pip_max_x = max(pip_max_x, board.pip_max_x)
            pip_max_y = max(pip_max_y, board.pip_max_y)

        pip_width = abs(pip_max_x - pip_min_x) + 1
        pip_height = abs(pip_max_y - pip_min_y) + 1

        pip_size = min(self.screen_w // pip_width, self.screen_h // pip_height)
        play_width = pip_size * pip_width
        play_height = pip_size * pip_height

        # Align to bottom-center:
        align_x = (self.screen_w - play_width) * horizontal_align
        align_y = (self.screen_h - play_height) * vertical_align

        def pip_to_screen(pip_x, pip_y):
            pip_x -= pip_min_x
            pip_y -= pip_min_y
            return (align_x + pip_x * pip_size, align_y + pip_y * pip_size)

        def pip_rect(pip_x, pip_y, pip_w, pip_h):
            return pygame.Rect(
                pip_to_screen(pip_x, pip_y),
                (pip_w * pip_size, pip_h * pip_size))

        for plate in plates:
            plate.populate(pip_rect)

        self.fingers = {}
        self.last_held = set()
        self.mouse_state = False

    def test_point(self, point):
        for plate in self.plates:
            if tile := plate.match(point):
                return tile
        return None

    def input_event(self, event):
        if event.type == pygame.FINGERDOWN or event.type == pygame.FINGERMOTION:
            point = (round(event.x * (screen_w - 1)), round(event.y * (screen_h - 1)))
            if tile := self.test_point(point):
                self.fingers[finger_id] = tile
            elif self.fingers.get(event.finger_id) is not None:
                del self.fingers[event.finger_id]

        elif event.type == pygame.FINGERUP:
            if self.fingers.get(event.finger_id) is not None:
                del self.fingers[event.finger_id]

        elif event.type == pygame.MOUSEBUTTONDOWN:
            self.mouse_state = True
            finger_id = "m"
            if tile := self.test_point(event.pos):
                self.fingers[finger_id] = tile
            elif self.fingers.get(finger_id) is not None:
                del self.fingers[finger_id]

        elif event.type == pygame.MOUSEMOTION and self.mouse_state:
            finger_id = "m"
            if tile := self.test_point(event.pos):
                self.fingers[finger_id] = tile
            elif self.fingers.get(finger_id) is not None:
                del self.fingers[finger_id]

        elif event.type == pygame.MOUSEBUTTONUP:
            self.mouse_state = False
            finger_id = "m"
            if self.fingers.get(finger_id) is not None:
                del self.fingers[finger_id]

    def draw(self):
        update_rects = []
        blit_sequence = []

        for plate in self.plates:
            update_rects.append(plate.frame)
            for tile in plate.tiles:
                blit_sequence += tile.draw_params

        return update_rects, blit_sequence

    def crank(self):
        held = set(self.fingers.values())

        released = self.last_held - held
        pressed = held - self.last_held

        self.last_held = held

        for tile in released:
            tile.release()

        for tile in pressed:
            tile.hold()

        if pressed or released:
            midi.flush()
            return self.draw()

        else:
            return None, None


def main():
    pygame.init()

    sizes = pygame.display.get_desktop_sizes()
    display_index = len(sizes) - 1
    display_size = sizes[display_index]
    screen = pygame.display.set_mode(size=display_size, display=display_index, flags=pygame.FULLSCREEN)

    plates = [
        TileArray(0, 0, 3, 2),
        Piano(0, -18, notes=12*2+1),
        Piano(0, -9, scale=[2, 1, 2, 2, 2, 1, 2], notes=12*2+1), #, spill_mode=0),
        Plato(17, 0, 1, 1),
    ]

    screen.fill((0, 0, 0))
    pygame.display.flip()

    play_surface = PlaySurface(display_size, plates)

    update_rects, blit_sequence = play_surface.draw()
    screen.blits(blit_sequence=blit_sequence)
    pygame.display.flip()

    while True:
        live = True

        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                live = False

            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                live = False

            else:
                play_surface.input_event(event)

        if not live:
            break

        update_rects, blit_sequence = play_surface.crank()

        if blit_sequence:
            screen.blits(blit_sequence=blit_sequence)
            pygame.display.update(update_rects)
        else:
            time.sleep(1e-9)

    surface_tools.reset_memo()
    pygame.quit()


if __name__ == "__main__":
    midi.run(main)

