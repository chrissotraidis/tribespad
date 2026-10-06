"""Exact binary32 SSE square root, using only integer arithmetic."""
import math

def root(source, control):
    assert 0 <= source <= 0xffffffff and 0 <= control <= 0xffff
    sign = source >> 31
    magnitude = source & 0x7fffffff
    fraction = source & 0x007fffff
    exponent = (source >> 23) & 255
    if exponent == 255 and fraction:
        return source | 0x00400000, 0 if fraction & 0x00400000 else 1
    if exponent == 0 and fraction and control & 0x40:
        return sign << 31, 0
    if sign and magnitude:
        return 0xffc00000, 1
    if not magnitude or exponent == 255:
        return source, 0
    mantissa = fraction | (1 << 23) if exponent else fraction
    power = exponent - 150 if exponent else -149
    result_exponent = (mantissa.bit_length() - 1 + power) // 2
    shift = power - 2 * result_exponent + 46
    assert shift >= 0
    radicand = mantissa << shift
    value = math.isqrt(radicand)
    inexact = value * value != radicand
    mode = (control >> 13) & 3
    # Compare exactly with the midpoint; ties retain the even significand.
    midpoint = 4 * radicand - (2 * value + 1) ** 2
    if mode == 2 and inexact or mode == 0 and (midpoint > 0 or midpoint == 0 and value & 1):
        value += 1
    if value == 1 << 24:
        value >>= 1
        result_exponent += 1
    assert 1 << 23 <= value < 1 << 24 and -126 <= result_exponent <= 127
    return ((result_exponent + 127) << 23) | (value - (1 << 23)), (2 if not exponent else 0) | (32 if inexact else 0)
