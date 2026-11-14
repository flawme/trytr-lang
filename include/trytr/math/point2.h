#ifndef TRYTR_MATH_POINT2_H
#define TRYTR_MATH_POINT2_H

#include "trytr/math/vector2.h"

#include <stdbool.h>

typedef struct TrytrPoint2 {
    TrytrReal x;
    TrytrReal y;
} TrytrPoint2;

TrytrPoint2 trytr_point2_make(TrytrReal x, TrytrReal y);
bool trytr_point2_is_finite(TrytrPoint2 point);
TrytrPoint2 trytr_point2_translate(TrytrPoint2 point, TrytrVector2 offset);
TrytrVector2 trytr_point2_difference(TrytrPoint2 left, TrytrPoint2 right);

#endif
