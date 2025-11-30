
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
import sys
import platform
import time

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
    vertical_inches = None

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
        except:
            print(f"\"{vertical_inches_arg}\" is not a valid vertical distance.  Defaulting to \"{vertical_inches}\".")

    scaled_display_size = sizes[display_index]
    unscaled_display_size = mollytime.display.list_modes(display=display_index)[0]

    # The BORDERLESS parameter is needed for borderless fullscreen and to prevent display mode setting
    # on Windows and X11 Linux.  Wayland doesn't strictly need it, but it doesn't hurt.
    window_flags = mollytime.display.FULLSCREEN | mollytime.display.BORDERLESS

    mollytime.display.set_mode(size=unscaled_display_size, display=display_index, flags=window_flags)

    # The rendering surface doesn't get created right away on Linux (and possibly other platforms).
    # This code ensures that it is fully created before we advance to creating the UI.
    wait_start = time.time()
    flush = mollytime.events.get()
    while not mollytime.draw.get_renderer_ready():
        flush = mollytime.events.get()
        if wait_start - time.time() < 1:
            time.sleep(0.01)
        else:
            # the display manager had its chance
            break

    editor = program_card(vertical_inches)
    ui = inspect_screen(editor)

    mollytime.shutdown_audio()
    mollytime.shutdown_midi()

if __name__ == "__main__":
    main()
