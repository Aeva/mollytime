
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

import os
import platform
operating_system = platform.system()


# Correct support of HiDPI on Linux requires setting both of these environment variables as well
# as passing the desired unscaled resolution to `pygame.display.set_mode` via the `size` parameter.
if operating_system == "Linux":
	os.environ["SDL_VIDEODRIVER"] = "wayland,x11"

os.environ["SDL_VIDEO_SCALE_METHOD"] = "letterbox"
#os.environ["SDL_MOUSE_TOUCH_EVENTS"] = "1"

os.environ["PYGAME_HIDE_SUPPORT_PROMPT"] = "hide"
import warnings
warnings.simplefilter("ignore")
