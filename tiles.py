
import pygame


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
        self.rects = []

    def populate(self, pip_to_rect):
        self.rects = [pip_to_rect(self.pip_min_x, self.pip_min_y, self.pip_w, self.pip_h)]


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
        self.tile_rects = [None] * tile_count

        print(tile_count)

        for i in range(tile_count):
            tile_x = i % self.tile_w
            tile_y = i // self.tile_w
            pip_x = self.pip_min_x + self.margin_pips + (self.tile_pips + self.spacing_pips) * tile_x
            pip_y = self.pip_min_y + self.margin_pips + (self.tile_pips + self.spacing_pips) * tile_y
            self.tile_rects[i] = pip_to_rect(pip_x, pip_y, self.tile_pips, self.tile_pips)

        self.rects += self.tile_rects
        print(self.rects)


class PlaySurface:
    def __init__(self, screen_size, plates):
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
        align_x = (self.screen_w - play_width) * .5
        align_y = (self.screen_h - play_height) * 1

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


if __name__ == "__main__":
    screen = 9 * 2 + 1
    plates = [
        #Plato(-2, -2, 1, 1),
        TileArray(0, 0, 2, 2),

    ]
    surface = PlaySurface((screen, screen), plates)

    pixels = ["."] * (screen * screen)
    def draw_rect(rect, char):
        for y in range(rect[1], rect[1] + rect[3]):
            for x in range(rect[0], rect[0] + rect[2]):
                i = min(y, screen-1) * screen + min(x, screen-1)
                pixels[i] = char

    for plate_index, plate in enumerate(plates):
        for rect_index, rect in enumerate(plate.rects):
            if rect_index == 0:
                draw_rect(rect, str(plate_index % 10))
            else:
                draw_rect(rect, "-")

    for y in range(screen):
        row = pixels[y*screen:(y+1)*screen]
        print(" " + " ".join(row))
