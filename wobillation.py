
import time
import math
import os
import ctypes

import pygame_setup
import pygame

import midi


CHROMA_KEY = (0, 0, 0)
THUMB = None


class backend:
    def __init__(self):
        self.backend = ctypes.cdll.LoadLibrary(os.path.abspath("wobillation.so"))
        self.backend.init()

    def clear(self):
        return self.backend.clear();

    def push_var(self, value):
        return self.backend.push_var(ctypes.c_double(value));

    def push_sin(self, frequency):
        return self.backend.push_sin(ctypes.c_uint16(frequency));

    def push_mul(self, lhs, rhs):
        return self.backend.push_mul(ctypes.c_uint16(lhs), ctypes.c_uint16(rhs));

    def push_add(self, lhs, rhs):
        return self.backend.push_add(ctypes.c_uint16(lhs), ctypes.c_uint16(rhs));

    def commit(self):
        self.backend.commit_program();

    def halt(self):
        self.backend.halt()


class fm_synth(backend):
    def __init__(self):
        super().__init__()

        self.clear()

        self.__carier_hz = self.push_var(440)
        self.__modulator_hz = self.push_var(440 * 0.75)
        self.__mod_amount = self.push_var(0.5)
        self.__volume = self.push_var(0.25)

        self.push_mul(
            self.__volume,
            self.push_sin(
                self.push_add(
                    self.__carier_hz,
                    self.push_mul(
                        self.__carier_hz,
                        self.push_mul(
                            self.push_sin(self.__modulator_hz),
                            self.__mod_amount)))))

        self.commit()


class dial:
    def __init__(self, x, y, r, highlight):
        self.highlight = highlight
        x = int(x)
        y = int(y)
        r = int(r)
        self.pivot = (x, y)
        self.r3 = (r + 16)
        self.r2 = (r + 8)
        self.r1 = r

        self.angle = 0

        self.line_r1 = self.r1 * (1/3)
        self.line_r2 = self.r1 - 2

        self.pos3 = (x - self.r3, y - self.r3)
        self.pos2 = (x - self.r2, y - self.r2)
        self.pos1 = (x - self.r1, y - self.r1)

        self.rect = pygame.Rect(x - self.r3, y - self.r3, x + self.r3, y + self.r3)

        self.surf3 = pygame.Surface(((r + 16) * 2, (r + 16) * 2))
        self.surf2 = pygame.Surface(((r + 8) * 2, (r + 8) * 2))
        self.surf1 = pygame.Surface((r * 2, r * 2))

        self.surf3.set_colorkey(CHROMA_KEY)
        self.surf2.set_colorkey(CHROMA_KEY)
        self.surf1.set_colorkey(CHROMA_KEY)


    def overlap(self, pos):
        rel = (pos[0] - self.pivot[0], pos[1] - self.pivot[1])
        d = math.sqrt(rel[0] * rel[0] + rel[1] * rel[1])
        if d > 0 and d < self.r1 + 16:
            return (rel[0] / d, rel[1] / d)
        else:
            return None


    def toward(self, pos):
        rel = (pos[0] - self.pivot[0], pos[1] - self.pivot[1])
        d = math.sqrt(rel[0] * rel[0] + rel[1] * rel[1])
        if d > 0:
            return (rel[0] / d, rel[1] / d)
        else:
            return None


    def draw(self, screen, focused):
        self.surf3.fill(CHROMA_KEY)
        a = self.angle * 1 - math.pi * .5
        v = (math.cos(a) * (self.r3 - 2), math.sin(a) * (self.r3 - 2))
        pygame.draw.circle(self.surf3, (64, 64, 64), (self.r3, self.r3), self.r3, 4)
        pygame.draw.circle(self.surf3, CHROMA_KEY, (self.r3 + v[0], self.r3 + v[1]), self.r3 * (5/3))

        self.surf2.fill(CHROMA_KEY)
        a = self.angle * -2 - math.pi * .5
        v = (math.cos(a) * (self.r2 - 2), math.sin(a) * (self.r2 - 2))
        pygame.draw.circle(self.surf2, (96, 96, 96), (self.r2, self.r2), self.r2, 4)
        pygame.draw.circle(self.surf2, CHROMA_KEY, (self.r2 + v[0], self.r2 + v[1]), self.r2 * (4/3))

        self.surf1.fill(CHROMA_KEY)
        pivot = (self.r1, self.r1)
        pygame.draw.circle(self.surf1, (128, 128, 128), pivot, self.r1, 4)

        line_color = self.highlight if focused else (128, 128, 128)

        for i in range(5):
            a = self.angle + math.pi * .5 + math.pi * 2 * (i / 5)
            v = (math.cos(a), math.sin(a))
            point_a = (pivot[0] + v[0] * self.line_r1, pivot[1] + v[1] * self.line_r1)
            point_b = (pivot[0] + v[0] * self.line_r2, pivot[1] + v[1] * self.line_r2)
            point_c = ((point_a[0] + point_b[0]) * 0.5, (point_a[1] + point_b[1]) * 0.5)

            pygame.draw.circle(self.surf1, CHROMA_KEY, point_b, THUMB * .5)
            pygame.draw.line(self.surf1, line_color, point_a, point_b, 1)

        screen.blits(((self.surf3, self.pos3), (self.surf2, self.pos2), (self.surf1, self.pos1)))


def main():
    global THUMB

    pygame.init()

    sizes = pygame.display.get_desktop_sizes()
    display_index = len(sizes) - 1
    display_size = sizes[display_index]
    screen = pygame.display.set_mode(size=display_size, display=display_index, flags=pygame.FULLSCREEN)
    w, h = display_size

    THUMB = max(min(w, h) // 40, 8)

    ring_r = THUMB * 3

    widgets = [
        dial(int(w * (1/4)), h // 2, ring_r, (0, 255, 255)),
        dial(int(w * (2/4)), h // 2, ring_r, (255, 0, 255)),
        dial(int(w * (3/4)), h // 2, ring_r, (255, 255, 0))]

    mouse_grab = -1
    mouse_pos = None
    grab_rel = None
    update_ctrl = -1

    carrier_hz = 440
    modulator_ratio = 5.0 / 3.0
    modulator_hz = carrier_hz * modulator_ratio

    synth = fm_synth()

    live = True
    while live:
        for event in pygame.event.get():
            if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                live = False

            elif event.type == pygame.MOUSEMOTION and (abs(event.rel[0]) > 0 or abs(event.rel[1]) > 0):
                mouse_pos = event.pos
                update_ctrl = mouse_grab

            elif event.type == pygame.MOUSEBUTTONDOWN and event.button == pygame.BUTTON_LEFT:
                mouse_pos = event.pos
                mouse_grab = -1
                update_ctrl = -1
                for i in range(len(widgets)):
                    grab_rel = widgets[i].overlap(mouse_pos)
                    if grab_rel is not None:
                        mouse_grab = i
                        update_ctrl = i
                        break

            elif event.type == pygame.MOUSEBUTTONUP and event.button == pygame.BUTTON_LEFT:
                mouse_grab = -1
                update_ctrl = -1

        if update_ctrl > -1:
            update_ctrl = False
            test_rel = widgets[mouse_grab].toward(mouse_pos)
            if test_rel is not None:
                dot = (grab_rel[0] * test_rel[0] + grab_rel[1] * test_rel[1])
                dot = min(abs(dot), 1.0)
                if dot > 0:
                    offset = math.acos(dot)
                    rel_rel = (test_rel[0] - grab_rel[0], test_rel[1] - grab_rel[1])

                    if grab_rel[0] >= 0.0 and test_rel[0] >= 0.0 and rel_rel[1] < 0:
                        offset = -offset
                    elif grab_rel[0] <= 0.0 and test_rel[0] <= 0.0 and rel_rel[1] > 0:
                        offset = -offset
                    elif grab_rel[1] >= 0.0 and test_rel[1] >= 0.0 and rel_rel[0] > 0:
                        offset = -offset
                    elif grab_rel[1] <= 0.0 and test_rel[1] <= 0.0 and rel_rel[0] < 0:
                        offset = -offset

                    widgets[mouse_grab].angle += offset
                    grab_rel = test_rel

        screen.fill("black")
        #synth.tune(0, carrier_hz * math.pow(2, widgets[0].angle / 12))

        modulator_hz = carrier_hz * (modulator_ratio + widgets[1].angle * 0.1)
        #synth.tune(1, modulator_hz)

        #synth.set_feedback(widgets[2].angle / (math.pi * 2))

        for i, widget in enumerate(widgets):
            widget.draw(screen, mouse_grab == i)



        pygame.display.flip()

    synth.halt()
    pygame.quit()


if __name__ == "__main__":
    midi.run(main)
