
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
import glob
import platform


def cat(path):
    with open(path, "r") as in_file:
        return in_file.read().strip()


def poll_battery():
    operating_system = platform.system()
    if not operating_system == "Linux":
        return None

    batteries = []

    regex = re.compile(r'^/sys/class/power_supply/(.+)/type$')

    # see https://www.kernel.org/doc/Documentation/ABI/testing/sysfs-class-power
    devices = glob.glob("/sys/class/power_supply/*/type")
    for device_type_path in devices:
        device_name = regex.match(device_type_path)[1]
        device_type = cat(device_type_path)

        if device_type == "Battery":
            batteries.append((
                bool(cat(f"/sys/class/power_supply/{device_name}/status") == "Discharging"),
                int(cat(f"/sys/class/power_supply/{device_name}/capacity"))))

    any_discharging = False
    worst_percent = 100
    for discharging, percent in batteries:
        if discharging:
            any_discharging = True
            worst_percent = min(worst_percent, percent)

    if any_discharging and worst_percent < 100:
        return worst_percent
    else:
        return None
