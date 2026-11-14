#include "trytr/math/real.h"

#include <math.h>

bool trytr_real_is_finite(TrytrReal value)
{
    return isfinite(value);
}

TrytrReal trytr_real_sqrt(TrytrReal value)
{
    return sqrt(value);
}

TrytrReal trytr_real_hypot(TrytrReal x, TrytrReal y)
{
    return hypot(x, y);
}

TrytrReal trytr_real_sin(TrytrReal radians)
{
    return sin(radians);
}

TrytrReal trytr_real_cos(TrytrReal radians)
{
    return cos(radians);
}

TrytrReal trytr_real_tan(TrytrReal radians)
{
    return tan(radians);
}

TrytrReal trytr_real_asin(TrytrReal value)
{
    return asin(value);
}

TrytrReal trytr_real_acos(TrytrReal value)
{
    return acos(value);
}

TrytrReal trytr_real_atan(TrytrReal value)
{
    return atan(value);
}

TrytrReal trytr_real_atan2(TrytrReal y, TrytrReal x)
{
    return atan2(y, x);
}

TrytrReal trytr_real_exp(TrytrReal value)
{
    return exp(value);
}

TrytrReal trytr_real_log(TrytrReal value)
{
    return log(value);
}

TrytrReal trytr_real_log10(TrytrReal value)
{
    return log10(value);
}

TrytrReal trytr_real_pow(TrytrReal base, TrytrReal exponent)
{
    return pow(base, exponent);
}

TrytrReal trytr_real_sinh(TrytrReal value)
{
    return sinh(value);
}

TrytrReal trytr_real_cosh(TrytrReal value)
{
    return cosh(value);
}

TrytrReal trytr_real_tanh(TrytrReal value)
{
    return tanh(value);
}
