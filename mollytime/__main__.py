
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

def _select_audio_driver(driver_name, sample_rate):
    assert backend.StubStream.is_available()

    # Determine the default driver for this platform & build.
    if platform.system() == "Linux" and backend.JackStream.is_available():
        default_driver_type = backend.JackStream
    elif platform.system() == "Windows" and backend.WasapiStream.is_available():
        default_driver_type = backend.WasapiStream
    elif backend.SDLStream.is_available():
        default_driver_type = backend.SDLStream
    else:
        default_driver_type = backend.StubStream
    
    # If no driver was requested, we can just use the default.
    if driver_name is None:
        return default_driver_type(sample_rate)
    
    # Find the driver that matches the requested name.
    driver_types = (backend.SDLStream, backend.JackStream, backend.WasapiStream, backend.StubStream)
    driver_type = None
    for type in driver_types:
        if type.get_name().casefold() == driver_name.casefold():
            driver_type = type
            break
    
    # If we got a match, great! Use that.
    if driver_type is not None and driver_type.is_available():
        return driver_type(sample_rate)
    
    # We didn't get a match. Inform the user of their wrongness, and use the default driver.
    fallback_reason = "not valid" if driver_type is None else "not available"
    print(f"Audio driver \"{driver_name}\" is {fallback_reason}.  Falling back to {default_driver_type.get_name()}.")
    print("The following are the audio drivers available on this system:")
    for type in driver_types:
        if type.is_available():
            print(f"- {type.get_name()}")
    
    return default_driver_type(sample_rate)

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
    audio_driver_name = None

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
        elif arg in ("-a" "--audio-driver"):
            audio_driver_name = args.pop(0)
        else:
            print(f"Ignoring unknown arg: {arg}")

    backend.init_config("mollytime")
    #print(f"data folder: {backend.get_game_data_folder()}")
    #print(f"config folder: {backend.get_game_config_folder()}")
    #print(f"read only mode: {backend.get_read_only_mode()}")

    backend.init_midi()

    audio_driver = _select_audio_driver(audio_driver_name, 48000)
    print(f"Audio driver: {audio_driver.get_name()}")
    backend.init_audio(audio_driver)

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
