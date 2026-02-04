
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

from . import backend

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

    args = list(sys.argv[1:])
    force_fullscreen = 0
    dump_icon = False
    vertical_inches = None

    while args:
        arg = args.pop(0)
        if arg in ("-w" "--windowed"):
            force_fullscreen = -1
        elif arg in ("-f" "--fullscreen"):
            force_fullscreen = 1
        elif arg == "--dump-icon":
            dump_icon = True
        elif arg == "--vertical-inches":
            vertical_inches = float(args.pop(0))
        elif arg == "-p":
            backend.set_default_polyphony(int(args.pop(0)))
        else:
            print(f"Ignoring unknown arg: {arg}")

    backend.init_midi()
    backend.init_audio(48000)

    backend.display.init(force_fullscreen)
    backend.draw.init()
    backend.font.init()

    print(f'SDL3 selected the "{backend.draw.get_renderer_name()}" rendering backend.')

    if operating_system == "Windows":
        icon_size = 32
    else:
        icon_size = 512
    
    # TODO: Implement backend.draw.Texture.save()
    # # According to the docs, the program icon must be set before calling "pygame.display.set_mode".
    # # However this does not seem to do anything on Linux, probably due to Wayland nonsense to add security.
    program_icon = plate_bg(icon_size // 2, parse_color("#dee5e8"), "moll-\nytime")
    # if dump_icon:
    #     # pygame.image.save(program_icon.surface, "mollytime.png")
    #     exit()
    # else:

    assert(program_icon.surface.get_rect().w == icon_size)
    assert(program_icon.surface.get_rect().h == icon_size)
    backend.display.set_icon(program_icon.surface)
    backend.display.set_caption("mollytime")

    # The rendering surface doesn't get created right away on Linux (and possibly other platforms).
    # This code ensures that it is fully created before we advance to creating the UI.
    wait_start = time.time()
    flush = backend.events.get()
    while not backend.draw.get_renderer_ready():
        flush = backend.events.get()
        if wait_start - time.time() < 1:
            time.sleep(0.01)
        else:
            # the display manager had its chance
            break

    editor = program_card(vertical_inches)
    ui = inspect_screen(editor)

    backend.shutdown_audio()
    backend.shutdown_midi()

if __name__ == "__main__":
    main()
