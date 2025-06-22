
import math


def sunwise_by_90(vec):
    """
    Rotates a 2D vector around the origin by 90 degrees clockwise.
    """
    return (-vec[1], vec[0])


def widdershins_by_90(vec):
    """
    Rotates a 2D vector around the origin by 90 degrees counterclockwise.
    """
    return (vec[1], -vec[0])


def vec_add(lhs, rhs):
    """
    Componentwise add two vectors of arbitrary size.
    """
    return tuple(lhs + rhs for lhs, rhs in zip(lhs, rhs))


def vec_sub(lhs, rhs):
    """
    Componentwise subtract two vectors of arbitrary size.
    """
    return tuple(lhs - rhs for lhs, rhs in zip(lhs, rhs))


def vec_mul(lhs, rhs):
    """
    Componentwise multiply two vectors of arbitrary size.
    """
    return tuple(lhs * rhs for lhs, rhs in zip(lhs, rhs))


def vec_div(lhs, rhs):
    """
    Componentwise divide two vectors of arbitrary size.
    """
    return tuple(lhs / rhs for lhs, rhs in zip(lhs, rhs))


def vec_scale(vec, scale):
    """
    Multiply a vector of arbitrary size by a scalar value.
    """
    return tuple(i * scale for i in vec)


def vec_lerp(lhs, rhs, alpha):
    """
    Interpolate two vectors of arbitrary size by a scalar value.
    """
    inv_a = 1.0 - alpha
    return vec_add(vec_scale(lhs, inv_a), vec_scale(rhs, alpha))


def dot(lhs, rhs):
    """
    Dot product of two vectors of arbitrary size.
    """
    return sum(lhs * rhs for lhs, rhs in zip(lhs, rhs))


def length(vec):
    """
    Returns the length of a vector of abritrary size.
    """
    return math.sqrt(dot(vec, vec))


def normalize(vec):
    """
    Normalize a vector of arbitrary size.
    """
    mag = length(vec)
    if mag > 0:
        return tuple(i / mag for i in vec)
    else:
        return vec


def rotate_point(point, degrees):
    """
    Rotates a point around the origin.
    """
    radians = math.radians(degrees)
    s = math.sin(radians)
    c = math.cos(radians)
    x, y = point
    return (x * c - y * s, x * s + y * c)
