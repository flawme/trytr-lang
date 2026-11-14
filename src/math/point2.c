#include "trytr/math/point2.h"

TrytrPoint2 trytr_point2_make(TrytrReal x, TrytrReal y)
{
    return (TrytrPoint2){.x = x, .y = y};
}

bool trytr_point2_is_finite(TrytrPoint2 point)
{
    return trytr_real_is_finite(point.x) && trytr_real_is_finite(point.y);
}

TrytrPoint2 trytr_point2_translate(TrytrPoint2 point, TrytrVector2 offset)
{
    return trytr_point2_make(point.x + offset.x, point.y + offset.y);
}

TrytrVector2 trytr_point2_difference(TrytrPoint2 left, TrytrPoint2 right)
{
    return trytr_vector2_make(left.x - right.x, left.y - right.y);
}
