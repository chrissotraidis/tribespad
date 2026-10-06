"""Scalar SSE selection/comparison via exact rational values, no host floats."""
from fractions import Fraction

def layout(width):
    return (23,8,127) if width==32 else (52,11,1023)

def classify(bits,width):
    fraction_bits,exponent_bits,bias=layout(width)
    sign=bits>>(width-1);exponent=(bits>>fraction_bits)&((1<<exponent_bits)-1);fraction=bits&((1<<fraction_bits)-1)
    return sign,exponent,fraction

def numeric(bits,width):
    fb,eb,bias=layout(width);sign,exponent,fraction=classify(bits,width)
    if exponent==(1<<eb)-1:
        assert not fraction
        return (-1 if sign else 1,Fraction(0))
    mantissa=fraction+((1<<fb) if exponent else 0);power=(exponent if exponent else 1)-bias-fb
    value=Fraction(mantissa<<max(power,0),1<<max(-power,0))
    return (0,-value if sign else value)

def evaluate(op,left,right,control):
    assert op in (0,1,2)
    width=64 if op==2 else 32;fb,eb,_=layout(width)
    sa,ea,fa=classify(left,width);sb,ebv,fbv=classify(right,width)
    nan=(ea==(1<<eb)-1 and fa!=0) or (ebv==(1<<eb)-1 and fbv!=0)
    dena=ea==0 and fa!=0;denb=ebv==0 and fbv!=0
    if control&0x40:
        if dena:left=sa<<(width-1)
        if denb:right=sb<<(width-1)
    status=1 if nan else 2 if (dena or denb) and not control&0x40 else 0
    if op==2:
        if nan:return 0x45,status
        a,b=numeric(left,width),numeric(right,width)
        return 0x40 if a==b else 1 if a<b else 0,status
    if nan:return right,status
    a,b=numeric(left,width),numeric(right,width)
    take_left=a<b if op==0 else a>b
    return left if take_left else right,status
