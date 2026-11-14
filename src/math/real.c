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

TrytrReal trytr_real_atan2(TrytrReal y, TrytrReal x)
{
    return atan2(y, x);
}
