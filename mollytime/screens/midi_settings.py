
# Copyright 2025 Aeva Palecek
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import math
from .. import mollytime
#from ..mollytime import OpCode, get_symbol_name
from .common import *


CATEGORIES = (
    "piano",
    "pitched percussion",
    "organ",
    "guitar",
    "bass",
    "strings",
    "ensemble",
    "brass",
    "reed",
    "pipe",
    "synth lead",
    "synth pad",
    "synth effects",
    "world music",
    "percussive",
    "sound effects")

INSTRUMENTS = (
    # piano
    "piano 1",
    "piano 2",
    "piano 3",
    "janky piano",
    "electric piano 1",
    "electric piano 2",
    "harpsichord",
    "clavinet",

    # percussion
    "celesta",
    "glockenspiel",
    "music box",
    "vibraphone",
    "marimba",
    "xylophone",
    "tubular bells",
    "dulcimer",

    # organ
    "organ 1",
    "organ 2",
    "organ 3",
    "pipe organ",
    "reed organ",
    "accordion",
    "harmonica",
    "bandoneon",

    # guitar
    "nylon guitar",
    "steel guitar",
    "electric guitar 1",
    "electric guitar 2",
    "electric guitar 3",
    "overdriven guitar",
    "distortion guitar",
    "guitar harmonics",

    # bass
    "acoustic bass",
    "electric bass 1",
    "electric bass 2",
    "fretless bass",
    "slap bass 1",
    "slap bass 2",
    "synth bass 1",
    "synth bass 2",

    # strings
    "violin",
    "viola",
    "cello",
    "contrabass",
    "tremolo strings",
    "pizzicato strings",
    "orchestral harp",
    "timpani",

    # ensemble
    "string ensemble 1",
    "string ensemble 2",
    "synth strings 1",
    "synth strings 2",
    "aah",
    "ooh",
    "synth voice",
    "orchestra hit",

    # brass
    "trumpet",
    "trombone",
    "tuba",
    "muted trumptet",
    "french horn",
    "brass section",
    "synth brass 1",
    "synth brass 2",

    # reed
    "soprano sax",
    "alto sax",
    "tenor sax",
    "baritone sax",
    "oboe",
    "english horn",
    "bassoon",
    "clarinet",

    # pipe
    "piccolo",
    "flute",
    "recorder",
    "pan flute",
    "bottle",
    "shakuhachi",
    "whistle",
    "ocarina",

    # synth lead
    "square",
    "sawtooth",
    "calliope",
    "chiff",
    "charang",
    "voice",
    "fifths",
    "bass and lead",

    # synth pad
    "new age",
    "warm",
    "polysynth",
    "choir",
    "bowed glass",
    "metallic",
    "halo",
    "sweep",

    # synth effects
    "rain",
    "soundtrack",
    "crystal",
    "atmosphere",
    "brightness",
    "goblins",
    "echo drops",
    "sci-fi",

    # world music
    "sitar",
    "banjo",
    "shamisen",
    "koto",
    "kalimba",
    "bag pipe",
    "fiddle",
    "shanai",

    # percussive
    "tinkle bell",
    "agogô",
    "steel drums",
    "woodblock",
    "taiko drum",
    "melodic tom",
    "synth drum",
    "reverse cymbal",

    # sound effects
    "guitar fret",
    "breath",
    "seashore",
    "bird",
    "telephone",
    "helicopter",
    "applause",
    "gunshot")

assert(len(INSTRUMENTS) == 128)

BY_CATEGORY = {
    k : tuple(range(i*8, i*8+8))
    for i, k in enumerate(CATEGORIES)
}

assert(INSTRUMENTS[BY_CATEGORY["world music"][0]] == "sitar")
assert(INSTRUMENTS[BY_CATEGORY["world music"][-1]] == "shanai")


class midi_settings_screen(editor_screen):
    def setup(self, editor):
        self.cursor_pos = mollytime.mouse.get_pos()
        self.press_start = None
        self.resize_screen(editor)

    def resize_screen(self, editor):
        self.repopulate_sidebar(editor)

    def repopulate_sidebar(self, editor):
        self.update_sidebar = True

        goto_inspect_rect = mollytime.Rect(
            editor.grid_size,
            0 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        goto_inspect_icon = editor.inspect_target

        active_rect = mollytime.Rect(
            editor.grid_size,
            1 * editor.grid_size * 3,
            editor.grid_size * 2, editor.grid_size * 2)

        active_icon = editor.settings_active

        self.side_bar_targets = [
            (goto_inspect_rect, goto_inspect_icon, self.goto_inspect_screen),
            (active_rect, active_icon, None)]

    def goto_inspect_screen(self, editor):
        self.live = False

    def on_move(self, editor, pos, event):
        self.cursor_pos = pos

    def on_press(self, editor, pos, event):
        self.cursor_pos = pos

        if editor.side_bar_rect.collidepoint(pos):
            # test side bar targets
            rel_pos = (pos[0] - editor.side_bar.viewport.x, pos[1] - editor.side_bar.viewport.y)
            for rect, surface, action in self.side_bar_targets:
                if action is not None and rect.collidepoint(rel_pos):
                    action(editor)
                    return

    def on_release(self, editor, pos, event):
        self.press_start = None

    def draw(self, editor):
        update_anything = False

        # draw the play area
        if self.update_play_area or self.force_redraw:
            self.update_play_area = False
            update_anything = True

            frame = editor.reset_settings_area()

        # draw sidebar
        if self.update_sidebar or self.force_redraw:
            self.update_sidebar = False
            update_anything = True

            frame = editor.reset_side_bar()
            for rect, plate, action in self.side_bar_targets:
                frame.blit(plate.surface, rect)

            self.draw_system_status(editor, frame)

        if update_anything:
            self.force_redraw = False
            editor.present()
        else:
            editor.clock.tick(60)
