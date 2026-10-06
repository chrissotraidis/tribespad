"""Exact binary64-to-signed32 truncation using rational integer division."""
def convert(source, control):
    assert 0 <= source <= 0xffffffffffffffff and 0 <= control <= 0xffff
    sign = source >> 63
    exponent = (source >> 52) & 2047
    fraction = source & ((1 << 52) - 1)
    if exponent == 2047:
        return 0x80000000, 1
    if not exponent and control & 0x40:
        return 0, 0
    mantissa = fraction + ((1 << 52) if exponent else 0)
    power = exponent - 1075 if exponent else -1074
    numerator = mantissa << max(power, 0)
    denominator = 1 << max(-power, 0)
    whole, remainder = divmod(numerator, denominator)
    signed = -whole if sign else whole
    if signed < -(1 << 31) or signed >= 1 << 31:
        return 0x80000000, 1
    return signed & 0xffffffff, 32 if remainder else 0
