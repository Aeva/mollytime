
import time
import math
import os
import ctypes

import pygame_setup
import pygame

import midi


CHROMA_KEY = (0, 0, 0)
NATIONAL_PARK_LIGHT = "media/national_park/NationalPark-Light.ttf"
THUMB = None
BACKEND = ctypes.cdll.LoadLibrary(os.path.abspath("wobillation.so"))


class synth_op:
    def __init__(self, handle):
        self.handle = handle

    def __mul__(self, other):
        assert(issubclass(type(other), synth_op))
        return synth_op(BACKEND.push_mul(ctypes.c_uint16(self.handle), ctypes.c_uint16(other.handle)))

    def __add__(self, other):
        assert(issubclass(type(other), synth_op))
        return synth_op(BACKEND.push_add(ctypes.c_uint16(self.handle), ctypes.c_uint16(other.handle)))


class synth_var(synth_op):
    def __init__(self, value):
        super().__init__(BACKEND.push_var(ctypes.c_double(value)))
        self.value = value

    def get(self):
        return self.value

    def set(self, value):
        self.value = value
        BACKEND.set_var(ctypes.c_uint16(self.handle), ctypes.c_double(value));


class synth_sin(synth_op):
    def __init__(self, frequency):
        assert(issubclass(type(frequency), synth_op))
        super().__init__(BACKEND.push_sin(ctypes.c_uint16(frequency.handle)))


class synth_min(synth_op):
    def __init__(self, lhs, rhs):
        assert(issubclass(type(lhs), synth_op))
        assert(issubclass(type(rhs), synth_op))
        super().__init__(BACKEND.push_min(ctypes.c_uint16(lhs.handle), ctypes.c_uint16(rhs.handle)))


class synth_max(synth_op):
    def __init__(self, lhs, rhs):
        assert(issubclass(type(lhs), synth_op))
        assert(issubclass(type(rhs), synth_op))
        super().__init__(BACKEND.push_max(ctypes.c_uint16(lhs.handle), ctypes.c_uint16(rhs.handle)))


class fm_synth():
    def __init__(self):
        BACKEND.init()

        BACKEND.clear()

        self.carrier_hz = synth_var(440)
        self.modulator_ratio1 = synth_var(5/3)
        self.modulator_ratio2 = synth_var(5/3)
        self.mod_amount1 = synth_var(0.0)
        self.mod_amount2 = synth_var(0.0)
        self.volume = synth_var(0.25)
        loud = synth_var(100.0)
        one = synth_var(1)
        minus_one = synth_var(-1)


        modulator_hz1 = self.carrier_hz * self.modulator_ratio1
        modulator_hz2 = modulator_hz1 * self.modulator_ratio2

        modulator_phase2 = synth_sin(modulator_hz2)

        mod_amount_mod = (self.mod_amount1 + self.mod_amount1 * modulator_phase2 * self.mod_amount2)

        modulator_phase1 = synth_sin(modulator_hz1)

        carrier_phase = synth_sin(
            self.carrier_hz + self.carrier_hz * modulator_phase1 * mod_amount_mod)

        #carrier_phase = synth_min(synth_max(carrier_phase * loud, minus_one), one)

        output_sample = self.volume * carrier_phase

        BACKEND.commit_program()

    def halt(self):
        BACKEND.halt()


TEXT_SURFACE_CACHE = {}
def render_text(font_path, size, color, text):
    if font_path:
        font_path = os.path.abspath(font_path)
        assert(os.path.isfile(font_path))
    key = (font_path, size, color, text)
    found = TEXT_SURFACE_CACHE.get(key)
    if found:
        return found

    font = pygame.font.Font(font_path, size)
    surface = font.render(text, False, color, CHROMA_KEY)
    surface.set_colorkey(CHROMA_KEY)
    TEXT_SURFACE_CACHE[key] = surface
    return surface


class dial:
    def __init__(self, x, y, r, highlight, label=None):
        self.highlight = highlight
        x = int(x)
        y = int(y)
        r = int(r)
        self.pivot = (x, y)
        self.r3 = (r + 16)
        self.r2 = (r + 8)
        self.r1 = r
        self.claimed = False
        self.label = label

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

        self.update()


    def update(self):
        self.value = self.angle


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

            pygame.draw.circle(self.surf1, CHROMA_KEY, point_b, THUMB * .5)
            pygame.draw.line(self.surf1, line_color, point_a, point_b, 1)

            if i == 0:
                r = 4
                line_r3 = self.r1 - r
                point_c = (pivot[0] + v[0] * line_r3, pivot[1] + v[1] * line_r3)
                pygame.draw.circle(self.surf1, line_color, point_c, r)


        layers = [(self.surf3, self.pos3), (self.surf2, self.pos2), (self.surf1, self.pos1)]

        if self.label:
            text = None
            if type(self.label) is str:
                text = self.label.format_map(self.__dict__)
            else:
                text = str(self.label)
            surf = render_text(NATIONAL_PARK_LIGHT, 32, (255, 255, 255), text)
            dest = surf.get_rect().copy()
            dest.top = self.pivot[1] + self.r3 + 16
            dest.centerx = self.pivot[0]
            layers.insert(0, (surf, dest))

        screen.blits(layers)


class input_tracker:
    def __init__(self):
        self.pos = None
        self.rel = None
        self.widget = None
        self.update = False

    def move(self, pos):
        self.pos = pos
        self.update = self.widget is not None

    def press(self, pos, widgets):
        self.pos = pos
        self.widget = None
        self.update = False
        for widget in widgets:
            if not widget.claimed:
                self.rel = widget.overlap(self.pos)
                if self.rel is not None:
                    widget.claimed = True
                    self.widget = widget
                    self.update = True
                    break

    def release(self):
        if self.widget:
            self.widget.claimed = False
        self.widget = None
        self.update = False


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

    synth = fm_synth()
    carrier_hz = synth.carrier_hz.get()

    class frequency_dial(dial):
        def __init__(self, x, y):
            label = "{value:.2f} hz"
            super().__init__(x, y, ring_r, (0, 255, 255), label)

        def update(self):
            turns = self.angle / (math.pi * 2)
            self.value = carrier_hz * math.pow(2, turns)
            note = math.log2(self.value / 440) * 12 + 69
            self.label = f"{self.value:.2f} hz ({note:.2f})"


    class ratio_dial(dial):
        def __init__(self, x, y):
            label = "{value:.2f} x"
            super().__init__(x, y, ring_r, (255, 0, 255), label)

        def update(self):
            turns = self.angle / (math.pi * 2)
            if turns >= 0:
                self.value = turns + 1
            else:
                self.value = 1 / (abs(turns) + 1)

    class scalar_dial(dial):
        def __init__(self, x, y, turns = 0):
            label = "{value:.2f}"
            super().__init__(x, y, ring_r, (255, 0, 255), label)
            self.angle = math.pi * 2 * turns
            self.update()

        def update(self):
            self.value = self.angle / (math.pi * 2)

    carrier_dial = frequency_dial(w * (1/4), h / 2)

    mod1_ratio_dial = ratio_dial(w * (2/4), h * (1/3))
    mod2_ratio_dial = ratio_dial(w * (2/4), h * (2/3))

    mod1_amount_dial = scalar_dial(w * (3/4), h * (1/3))
    mod2_amount_dial = scalar_dial(w * (3/4), h * (2/3))

    widgets = [
        carrier_dial,
        mod1_ratio_dial,
        mod1_amount_dial,
        mod2_ratio_dial,
        mod2_amount_dial]

    touch = {}
    touch['mouse'] = input_tracker()

    def get_tracker(event):
        key = (event.touch_id, event.finger_id)
        if key not in touch:
            touch[key] = input_tracker()
        return touch[key]

    live = True
    while live:
        for event in pygame.event.get():
            if event.type == pygame.QUIT or (event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE):
                live = False

            elif event.type == pygame.FINGERMOTION:
                pos = (int(event.x * w), int(event.y * h))
                get_tracker(event).move(pos)

            elif event.type == pygame.FINGERDOWN:
                pos = (int(event.x * w), int(event.y * h))
                get_tracker(event).press(pos, widgets)

            elif event.type == pygame.FINGERUP:
                get_tracker(event).release()

            elif event.type == pygame.MOUSEMOTION and not event.touch and (abs(event.rel[0]) > 0 or abs(event.rel[1]) > 0):
                touch['mouse'].move(event.pos)

            elif event.type == pygame.MOUSEBUTTONDOWN and not event.touch and event.button == pygame.BUTTON_LEFT:
                touch['mouse'].press(event.pos, widgets)

            elif event.type == pygame.MOUSEBUTTONUP and not event.touch and event.button == pygame.BUTTON_LEFT:
                touch['mouse'].release()

        for name, digit in touch.items():
            if not digit.update:
                continue

            digit.update = False
            test_rel = digit.widget.toward(digit.pos)
            if test_rel is not None:
                dot = (digit.rel[0] * test_rel[0] + digit.rel[1] * test_rel[1])
                dot = min(abs(dot), 1.0)
                if dot > 0:
                    offset = math.acos(dot)
                    rel_rel = (test_rel[0] - digit.rel[0], test_rel[1] - digit.rel[1])

                    if digit.rel[0] >= 0.0 and test_rel[0] >= 0.0 and rel_rel[1] < 0:
                        offset = -offset
                    elif digit.rel[0] <= 0.0 and test_rel[0] <= 0.0 and rel_rel[1] > 0:
                        offset = -offset
                    elif digit.rel[1] >= 0.0 and test_rel[1] >= 0.0 and rel_rel[0] > 0:
                        offset = -offset
                    elif digit.rel[1] <= 0.0 and test_rel[1] <= 0.0 and rel_rel[0] < 0:
                        offset = -offset

                    digit.widget.angle += offset
                    digit.rel = test_rel
                    digit.widget.update()

        screen.fill("black")

        synth.carrier_hz.set(carrier_dial.value)
        synth.modulator_ratio1.set(mod1_ratio_dial.value)
        synth.modulator_ratio2.set(mod2_ratio_dial.value)
        synth.mod_amount1.set(mod1_amount_dial.value)
        synth.mod_amount2.set(mod2_amount_dial.value)

        for widget in widgets:
            widget.draw(screen, widget.claimed)

        pygame.display.flip()

    synth.halt()
    pygame.quit()


if __name__ == "__main__":
    midi.run(main)
