
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
import os
import re
import subprocess

import pygame_setup
import pygame

import mollytime

from fonts import *
from colors import *
from patterns import *
from screens.common import program_card
from screens.inspect import inspect_screen

mollytime.init_midi()
mollytime.init_audio(48000)
pygame.display.init()
pygame.font.init()

sizes = pygame.display.get_desktop_sizes()
display_index = len(sizes) - 1

scaled_display_size = sizes[display_index]
unscaled_display_size = pygame.display.list_modes(display=display_index)[0]

screen = pygame.display.set_mode(size=unscaled_display_size, display=display_index, flags=pygame.FULLSCREEN)

dpi = None

xrandr = subprocess.run("xrandr", stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
if xrandr.returncode == 0:
    try:
        report = xrandr.stdout.decode()
        regex = r"Screen (\d+):.+current (\d+) x (\d+).+\n.+ (\d+)mm x (\d+)mm"
        found = [list(map(int, r)) for r in re.findall(regex, report)]
        assert(len(found) == len(sizes))
        for index, (reported_index, res_x, res_y, mm_x, mm_y) in enumerate(found):
            index = reported_index
            assert(sizes[index][0] == res_x)
            assert(sizes[index][1] == res_y)
        reported_index, res_x, res_y, mm_x, mm_y = list(map(int, found[display_index]))
        in_x = mm_x / 25.4
        in_y = mm_y / 25.4
        dpi_x = res_x / in_x
        dpi_y = res_y / in_y
        dpi = round((dpi_x + dpi_y) / 2)
    except AssertionError:
        print("Cannot determine DPI: information reported by xrandr contradicts pygame.")
        dpi = None

if dpi is None:
    in_y = 7.5
    res_y = min(scaled_display_size)
    dpi = round(res_y / in_y)
    print(f"DPI assuming smallest physical screen dimension is 7.5 inches: {dpi} dpi")

dpi = int(dpi * (max(unscaled_display_size) / max(scaled_display_size)))

editor = program_card(screen, dpi)
ui = inspect_screen(editor)

mollytime.shutdown_audio()
mollytime.shutdown_midi()
