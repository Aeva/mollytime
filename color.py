

import math
import random


def random_color():
    """
    Returns a random byte color.
    """

    held_color = [random.randint(64, 192), random.randint(0, 128), random.randint(128, 255)]
    random.shuffle(held_color)
    return tuple(held_color)


def hue_to_rgb(h):
    """
    This function takes a value between 0 and 1 and returns a saturated floating point
    color.  Note that pygame does not accept floating point colors.
    """

    h = (h * 6) % 6
    a = h - math.floor(h)
    r = 0
    g = 0
    b = 0
    if h < 1:
        r = 1
        g = a
    elif h < 2:
        r = 1 - a
        g = 1
    elif h < 3:
        g = 1
        b = a
    elif h < 4:
        g = 1 - a
        b = 1
    elif h < 5:
        r = a
        b = 1
    else:
        r = 1
        b = 1 - a
    return (r, g, b)


def byte_color(float_color):
    """
    Converts a floating point color tripple to a pygame color.
    """
    return tuple([min(max(int(f * 255), 0), 255) for f in float_color])


def rainbow_gradient(value, low, high):
    """
    Takes a value and it's bounding range and return a saturated byte color.
    """
    a = (value - low) / abs(high - low)
    return byte_color(hue_to_rgb(a))
