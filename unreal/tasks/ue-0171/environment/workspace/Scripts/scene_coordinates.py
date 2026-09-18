"""Scene export coordinate contract. Python 3.11+, no third-party dependencies.

Matrices are row-major, act on column vectors and retain shear. C is a cyclic
axis permutation with determinant +1. Thus C alone never reverses triangles.
When baking a mirrored hierarchy into mesh vertices, reverse triangle winding
and tangent handedness once. If the hierarchy's negative scale is retained on
the actor, do not also reverse the asset's triangles.
"""
import math

IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]
C = [0., 0., 100., 0., 100., 0., 0., 0., 0., 100., 0., 0., 0., 0., 0., 1.]
C_INVERSE = [0., .01, 0., 0., 0., 0., .01, 0., .01, 0., 0., 0., 0., 0., 0., 1.]


def multiply(a, b):
    raise NotImplementedError("Restore scene coordinate operation")


def point(matrix, value):
    raise NotImplementedError("Restore scene coordinate operation")


def position(value):
    raise NotImplementedError("Restore scene coordinate operation")


def direction(value):
    raise NotImplementedError("Restore scene coordinate operation")


def transform(matrix):
    raise NotImplementedError("Restore scene coordinate operation")


def cross(a, b):
    raise NotImplementedError("Restore scene coordinate operation")


def dot(a, b):
    raise NotImplementedError("Restore scene coordinate operation")


def subtract(a, b):
    raise NotImplementedError("Restore scene coordinate operation")


def normalized(v):
    raise NotImplementedError("Restore scene coordinate operation")


def determinant(matrix):
    raise NotImplementedError("Restore scene coordinate operation")


def normal(matrix, value):
    raise NotImplementedError("Restore scene coordinate operation")


def baked_triangle(indices, matrix):
    raise NotImplementedError("Restore scene coordinate operation")
