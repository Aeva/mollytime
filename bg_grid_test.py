
import time
import math
import os
import ctypes
import random

import pygame_setup
import pygame


def rgbhex(string):
    return (int(string[1:3], 16), int(string[3:5], 16), int(string[5:7], 16))


def loop(screen, clock):
    live = True

    while live:
        for event in pygame.event.get():
            if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                live = False

            # elif event.type == pygame.FINGERMOTION:
            #     pos = (int(event.x * w), int(event.y * h))
            #     get_tracker(event).move(pos)
            #
            # elif event.type == pygame.FINGERDOWN:
            #     pos = (int(event.x * w), int(event.y * h))
            #     get_tracker(event).press(pos, widgets)
            #
            # elif event.type == pygame.FINGERUP:
            #     get_tracker(event).release()
            #
            # elif event.type == pygame.MOUSEMOTION and not event.touch and (abs(event.rel[0]) > 0 or abs(event.rel[1]) > 0):
            #     touch['mouse'].move(event.pos)
            #
            # elif event.type == pygame.MOUSEBUTTONDOWN and not event.touch and event.button == pygame.BUTTON_LEFT:
            #     touch['mouse'].press(event.pos, widgets)
            #
            # elif event.type == pygame.MOUSEBUTTONUP and not event.touch and event.button == pygame.BUTTON_LEFT:
            #     touch['mouse'].release()

        w = screen.get_rect().width
        h = screen.get_rect().height
        tile_size = h // 8
        x_count = math.ceil(w / tile_size)
        y_count = math.ceil(h / tile_size)
        x_offset = (w - x_count * tile_size) // 2
        y_offset = (h - y_count * tile_size) // 2

        #mini_tile = tile_size * 3 // 4
        mini_tile = tile_size // 2
        mini_offset = (tile_size - mini_tile) // 2

        gradient_center = (w // 2, h)
        rel = (gradient_center[0], gradient_center[1] * (2/3))
        max_magsqr = rel[0] * rel[0] + rel[1] * rel[1]

        # bg_ramp_x = (rgbhex("#3c5297"), rgbhex("#6b4287"))
        # bg_ramp_y = (rgbhex("#6b4287"), rgbhex("#3c5297"))

        #bg_ramp_x = (rgbhex("#160300"), rgbhex("#2a1500"))
        bg_ramp_x = (rgbhex("#160300"), rgbhex("#473100"))
        bg_ramp_y = (rgbhex("#37352c"), rgbhex("#160300"))
        #bg_ramp_y = (bg_ramp_x[1], bg_ramp_x[0])


        bg_ramp_x = (rgbhex("#25221a"), rgbhex("#473100"))
        bg_ramp_y = (rgbhex("#37352c"), rgbhex("#160300"))





        random.seed(0)
        for tile_y in range(y_count):
            for tile_x in range(x_count):
                rect = pygame.Rect(
                    tile_x * tile_size + x_offset,
                    tile_y * tile_size + y_offset,
                    tile_size, tile_size)

                inv_a = rect.centerx / w
                inv_a = abs(inv_a * 2 - 1)
                alpha = 1.0 - inv_a
                color_x = [int(inv_a * bg_ramp_x[0][i] + alpha * bg_ramp_x[1][i]) for i in range(3)]

                inv_a = rect.centery / h
                #inv_a = abs(inv_a * 2 - 1)
                alpha = 1.0 - inv_a
                color_y = [int(inv_a * bg_ramp_y[0][i] + alpha * bg_ramp_y[1][i]) for i in range(3)]

                i = ((tile_x % 2) + (tile_y % 2)) % 2
                color = (color_x, color_y)[i]

                pygame.draw.rect(screen, color, rect)

                if random.randint(1, 4) < 4:
                    continue

                rect = pygame.Rect(rect.x + mini_offset, rect.y + mini_offset, mini_tile, mini_tile)

                pygame.draw.rect(screen, rgbhex("#bfedff"), rect)
                pygame.draw.rect(screen, rgbhex("#8cbaff"), rect, 4)

        #pygame.draw.rect(screen, (0x1c, 0x1d, 0x21), pygame.Rect(0, 0, 100, 100))
        #pygame.draw.rect(screen, (0x2e, 0x2f, 0x32), pygame.Rect(100, 0, 100, 100))
        #pygame.draw.rect(screen, (0x41, 0x42, 0x46), pygame.Rect(100, 100, 100, 100))

        #pygame.draw.rect(screen, (0xae, 0xaf, 0xb3), pygame.Rect(100, 100, 100, 100))

        pygame.display.flip()
        clock.tick(60)


def init():
    pygame.init()

    sizes = pygame.display.get_desktop_sizes()
    display_index = len(sizes) - 1
    display_size = sizes[display_index]
    screen = pygame.display.set_mode(size=display_size, display=display_index, flags=pygame.FULLSCREEN)
    clock = pygame.time.Clock()

    loop(screen, clock)


if __name__ == "__main__":
    init()
