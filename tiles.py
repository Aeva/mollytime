
import pygame


class BaseBoard:
    def __init__(self, x, y, w, h, tile=(4, 1)):
        """
        Args `x`, `y`, `z`, `w`, and `h` are specified as all tile counts.
        Arg `tile` is a tuple describing the tile size and margin in number of
        pips.

        Coordinates are as-if the origin were in the top left corner of the
        screen (same as pygame), but the play surface will fit this into actual
        screen space, thus negative coordinates are legal.
        """

        self.tile_x = int(x)
        self.tile_y = int(y)
        self.tile_w = int(w)
        self.tile_h = int(h)
        self.tile_pips = int(tile[0])
        self.margin_pips = int(tile[1])
        self.stride = self.tile_pips + self.margin_pips
        self.pip_min_x = self.tile_x * self.stride
        self.pip_min_y = self.tile_y * self.stride
        self.pip_w = self.tile_w * self.stride
        self.pip_h = self.tile_h * self.stride
        self.pip_max_x = self.pip_min_x + self.pip_w
        self.pip_max_y = self.pip_min_y + self.pip_h

        # filled in by play surface
        self.rect = None
        self.tile_rects = [None] * (self.tile_h * self.tile_w)


class PlaySurface:
    def __init__(self, screen_size, boards):
        self.screen_w, self.screen_h = screen_size
        self.boards = boards

        pip_min_x = boards[0].pip_min_x
        pip_min_y = boards[0].pip_min_y
        pip_max_x = boards[0].pip_max_x
        pip_max_y = boards[0].pip_max_y

        for board in boards[1:]:
            pip_min_x = min(pip_min_x, board.pip_min_x)
            pip_min_y = min(pip_min_y, board.pip_min_y)
            pip_max_x = max(pip_max_x, board.pip_max_x)
            pip_max_y = max(pip_max_y, board.pip_max_y)

        pip_min_x -= 1
        pip_min_y -= 1

        pip_width = abs(pip_max_x - pip_min_x)
        pip_height = abs(pip_max_y - pip_min_y)

        pip_size = min(self.screen_w // pip_width, self.screen_h // pip_height)
        play_width = pip_size * pip_width
        play_height = pip_size * pip_height
        align_x = (self.screen_w - play_width) // 2 # center align
        align_y = (self.screen_h - play_height) # bottom align

        def pip_to_screen(pip_x, pip_y):
            pip_x -= pip_min_x
            pip_y -= pip_min_y
            return (align_x + pip_x * pip_size, align_y + pip_y * pip_size)

        def pip_rect(pip_x, pip_y, pip_w, pip_h):
            return pygame.Rect(
                pip_to_screen(pip_x, pip_y),
                (pip_w * pip_size, pip_h * pip_size))

        for board in boards:
            board.rect = pip_rect(board.pip_min_x - 1, board.pip_min_y - 1, board.pip_w + 1, board.pip_h + 1)

            i = 0
            for y in range(board.tile_h):
                pip_y = y * board.stride + board.pip_min_y
                for x in range(board.tile_w):
                    pip_x = x * board.stride + board.pip_min_x
                    board.tile_rects[i] = pip_rect(pip_x, pip_y, board.tile_pips, board.tile_pips)
                    i += 1


if __name__ == "__main__":
    screen = 9 * 5 + 1
    boards = [
        BaseBoard(2, -2, 6, 1),
        BaseBoard(0, 0, 5, 2),
        BaseBoard(4, -1, 2, 2),
    ]
    surface = PlaySurface((screen, screen), boards)

    pixels = ["."] * (screen * screen)
    def draw_rect(rect, char):
        for y in range(rect[1], rect[1] + rect[3]):
            for x in range(rect[0], rect[0] + rect[2]):
                i = y * screen + x
                pixels[i] = char

    for board_index, board in enumerate(boards):
        draw_rect(board.rect, str(board_index % 10))
        for tile_index, rect in enumerate(board.tile_rects):
            #draw_rect(rect, str(tile_index % 10))
            draw_rect(rect, "-")

    for y in range(screen):
        row = pixels[y*screen:(y+1)*screen]
        print(" " + " ".join(row))
