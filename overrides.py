
import os

# Correct support of HiDPI on Linux requires setting both of these environment variables as well
# as passing the desired unscaled resolution to `pygame.display.set_mode` via the `size` parameter.
os.environ["SDL_VIDEODRIVER"] = "wayland,x11"
os.environ["SDL_VIDEO_SCALE_METHOD"] = "letterbox"
#os.environ["SDL_MOUSE_TOUCH_EVENTS"] = "1"

os.environ["PYGAME_HIDE_SUPPORT_PROMPT"] = "hide"
import warnings
warnings.simplefilter("ignore")
