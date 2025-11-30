
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

import re
import platform
import subprocess

from . import mollytime


def calculate_dpi(vertical_inches=None):
    display_index = mollytime.display.get_current_display_index()
    sizes = mollytime.display.get_desktop_sizes()
    scaled_display_size = sizes[display_index]
    unscaled_display_size = mollytime.display.list_modes(display=display_index)[0]

    dpi = None

    if vertical_inches is None:
        vertical_inches = 7.5

        if platform.system() == "Linux":
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
                #print("Unable to calculate screen DPI via xrandr!")
                pass
        else:
            #print("Unable to automatically determine the screen DPI!")
            pass
    else:
        #print("Automatic DPI detection skipped at operator request.")
        pass

    if dpi is None:
        in_y = vertical_inches
        res_y = min(scaled_display_size)
        dpi = round(res_y / in_y)
        #print(f"DPI assuming smallest physical screen dimension is {in_y} inches: {dpi} dpi")

    return int(dpi * (max(unscaled_display_size) / max(scaled_display_size)))

