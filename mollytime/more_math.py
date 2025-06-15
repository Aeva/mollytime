
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
