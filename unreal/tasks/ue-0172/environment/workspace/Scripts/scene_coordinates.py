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
    return [sum(a[row * 4 + k] * b[k * 4 + col] for k in range(4))
            for row in range(4) for col in range(4)]


def point(matrix, value):
    return [sum(matrix[row * 4 + k] * value[k] for k in range(3)) + matrix[row * 4 + 3] for row in range(3)]


def position(value):
    return [100 * value[2], 100 * value[0], 100 * value[1]]


def direction(value):
    return [value[2], value[0], value[1]]


def transform(matrix):
    return multiply(multiply(C, matrix), C_INVERSE)


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def subtract(a, b):
    return [x-y for x, y in zip(a, b)]


def normalized(v):
    length = math.sqrt(dot(v, v))
    if length < 1e-20:
        raise ValueError("Cannot normalize a zero vector")
    return [x/length for x in v]


def determinant(matrix):
    return dot(matrix[:3], cross(matrix[4:7], matrix[8:11]))


def normal(matrix, value):
    columns = [[matrix[i*4+j] for i in range(3)] for j in range(3)]
    cofactors = [cross(columns[1], columns[2]), cross(columns[2], columns[0]), cross(columns[0], columns[1])]
    det = determinant(matrix)
    if abs(det) < 1e-15:
        raise ValueError("Singular transform has no surface normal")
    return normalized([sum(cofactors[j][i]*value[j] for j in range(3))/det for i in range(3)])


def baked_triangle(indices, matrix):
    if len(indices) != 3:
        raise ValueError("Triangle requires three indices")
    return [indices[0], indices[2], indices[1]] if determinant(matrix) < 0 else list(indices)
