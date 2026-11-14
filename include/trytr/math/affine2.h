#ifndef TRYTR_MATH_AFFINE2_H
#define TRYTR_MATH_AFFINE2_H

#include "trytr/math/matrix2.h"
#include "trytr/math/point2.h"

#include <stdbool.h>

typedef struct TrytrAffine2 {
    TrytrMatrix2 linear;
    TrytrVector2 translation;
} TrytrAffine2;

TrytrAffine2 trytr_affine2_identity(void);
TrytrAffine2 trytr_affine2_translation(TrytrVector2 offset);
TrytrAffine2 trytr_affine2_scaling(TrytrReal x_scale, TrytrReal y_scale);
bool trytr_affine2_rotation(TrytrReal angle_radians, TrytrAffine2 *out_transform);
bool trytr_affine2_is_finite(TrytrAffine2 transform);

/* Composition left*right applies right first, then left. */
bool trytr_affine2_compose(TrytrAffine2 left, TrytrAffine2 right,
                           TrytrAffine2 *out_transform);
bool trytr_affine2_apply(TrytrAffine2 transform, TrytrPoint2 point,
                         TrytrPoint2 *out_point);
bool trytr_affine2_inverse(TrytrAffine2 transform,
                           TrytrAffine2 *out_inverse);

#endif
