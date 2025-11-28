
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
import re
import os
import sys
import platform
import subprocess


from . import mollytime

from .fonts import *
from .colors import *
from .patterns import *
from .screens.common import program_card
from .screens.inspect import inspect_screen


def main():
    operating_system = platform.system()

    if operating_system == "Windows":
        import ctypes
        # needed for highdpi to work correctly
        ctypes.windll.user32.SetProcessDPIAware()

    mollytime.init_midi()
    mollytime.init_audio(48000)
    mollytime.display.init()
    mollytime.draw.init()
    mollytime.font.init()

    print(f'SDL3 selected the "{mollytime.draw.get_renderer_name()}" rendering backend.')

    dump_icon = False
    if operating_system == "Windows":
        icon_size = 32
    else:
        icon_size = 512
    if len(sys.argv) >= 2 and sys.argv[1] == "icon":
        icon_size = int((sys.argv[2:] + ["256"])[0])
        dump_icon = True
    
    # TODO: Implement mollytime.draw.Texture.save()
    # # According to the docs, the program icon must be set before calling "pygame.display.set_mode".
    # # However this does not seem to do anything on Linux, probably due to Wayland nonsense to add security.
    program_icon = plate_bg(icon_size // 2, parse_color("#dee5e8"), "moll-\nytime")
    # if dump_icon:
    #     # pygame.image.save(program_icon.surface, "mollytime.png")
    #     exit()
    # else:

    assert(program_icon.surface.get_rect().w == icon_size)
    assert(program_icon.surface.get_rect().h == icon_size)
    mollytime.display.set_icon(program_icon.surface)
    mollytime.display.set_caption("mollytime")

    display_index_arg, vertical_inches_arg = (sys.argv[1:] + [None, None])[:2]
    display_index = 0
    vertical_inches = 7.5
    skip_dpi_detection = False

    sizes = mollytime.display.get_desktop_sizes()

    if display_index_arg is not None:
        try:
            override_display_index = int(display_index_arg)
            assert(override_display_index > -1 and override_display_index < len(sizes))
            display_index = override_display_index
        except:
            print(f"\"{display_index_arg}\" is not a valid display index.  Defaulting to \"{display_index}\".")

    if vertical_inches_arg is not None:
        try:
            override_vertical_inches = float(vertical_inches_arg)
            assert(override_vertical_inches > 0)
            vertical_inches = override_vertical_inches
            skip_dpi_detection = True
        except:
            print(f"\"{vertical_inches_arg}\" is not a valid vertical distance.  Defaulting to \"{vertical_inches}\".")

    scaled_display_size = sizes[display_index]
    unscaled_display_size = mollytime.display.list_modes(display=display_index)[0]

    # The BORDERLESS parameter is needed for borderless fullscreen and to prevent display mode setting
    # on Windows and X11 Linux.  Wayland doesn't strictly need it, but it doesn't hurt.
    window_flags = mollytime.display.FULLSCREEN | mollytime.display.BORDERLESS

    screen = mollytime.display.set_mode(size=unscaled_display_size, display=display_index, flags=window_flags)

    dpi = None

    if not skip_dpi_detection:
        if operating_system == "Linux":
            xrandr_dpi = None

            xrandr = subprocess.run(("xrandr", "--listactivemonitors"), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if xrandr.returncode == 0:
                try:
                    report = xrandr.stdout.decode().split("\n")
                    regex = r'^Monitors: (\d)$'
                    found = re.findall(regex, report[0], re.M)
                    assert(len(found) == 1)
                    assert(int(found[0]) == len(sizes))

                    monitor_info = report[1 + display_index]
                    regex = r'^\s+(\d+):.+ (\d+)/(\d+)x(\d+)/(\d+)'
                    found = re.findall(regex, monitor_info, re.M)
                    assert(len(found) == 1)
                    reported_index, res_x, mm_x, res_y, mm_y = list(map(int, found[0]))
                    #print(f"monitor {display_index}: {res_x}x{res_y} ({mm_x}mm by {mm_y}mm)")

                    assert(len(found) == 1)

                    # xrandr may report the scaled or unscaled resolution, and so it cannot be relied upon for DPI calculation
                    res_x, res_y = sizes[display_index]

                    in_x = mm_x / 25.4
                    in_y = mm_y / 25.4
                    dpi_x = res_x / in_x
                    dpi_y = res_y / in_y
                    xrandr_dpi = round((dpi_x + dpi_y) / 2)
                except AssertionError:
                    xrandr_dpi = None

            if xrandr_dpi:
                dpi = xrandr_dpi
            else:
                print("Unable to calculate screen DPI via xrandr!")
        else:
            print("Unable to automatically determine the screen DPI!")
    else:
        print("Automatic DPI detection skipped at operator request.")

    if dpi is None:
        in_y = vertical_inches
        res_y = min(scaled_display_size)
        dpi = round(res_y / in_y)
        print(f"DPI assuming smallest physical screen dimension is {in_y} inches: {dpi} dpi")

    dpi = int(dpi * (max(unscaled_display_size) / max(scaled_display_size)))

    editor = program_card(screen, dpi)
    ui = inspect_screen(editor)

    mollytime.shutdown_audio()
    mollytime.shutdown_midi()

if __name__ == "__main__":
    main()
