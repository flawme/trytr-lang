#include "trytr/math/affine2.h"

#include <stddef.h>

static TrytrAffine2 affine2_zero(void)
{
    return (TrytrAffine2){
        .linear = {0.0, 0.0, 0.0, 0.0},
        .translation = {0.0, 0.0}
    };
}

TrytrAffine2 trytr_affine2_identity(void)
{
    return (TrytrAffine2){
        .linear = {1.0, 0.0, 0.0, 1.0},
        .translation = {0.0, 0.0}
    };
}

TrytrAffine2 trytr_affine2_translation(TrytrVector2 offset)
{
    return (TrytrAffine2){
        .linear = {1.0, 0.0, 0.0, 1.0},
        .translation = offset
    };
}

TrytrAffine2 trytr_affine2_scaling(TrytrReal x_scale, TrytrReal y_scale)
{
    return (TrytrAffine2){
        .linear = {x_scale, 0.0, 0.0, y_scale},
        .translation = {0.0, 0.0}
    };
}

bool trytr_affine2_rotation(TrytrReal angle_radians,
                            TrytrAffine2 *out_transform)
{
    if (out_transform == NULL) return false;
    *out_transform = affine2_zero();
    TrytrComplex unit_rotation;
    if (!trytr_complex_from_polar(1.0, angle_radians, &unit_rotation))
        return false;

    *out_transform = (TrytrAffine2){
        .linear = {
            unit_rotation.real, -unit_rotation.imaginary,
            unit_rotation.imaginary, unit_rotation.real
        },
        .translation = {0.0, 0.0}
    };
    return true;
}

bool trytr_affine2_is_finite(TrytrAffine2 transform)
{
    return trytr_matrix2_is_finite(transform.linear) &&
           trytr_vector2_is_finite(transform.translation);
}

bool trytr_affine2_compose(TrytrAffine2 left, TrytrAffine2 right,
                           TrytrAffine2 *out_transform)
{
    if (out_transform == NULL) return false;
    *out_transform = affine2_zero();
    if (!trytr_affine2_is_finite(left) || !trytr_affine2_is_finite(right))
        return false;

    TrytrMatrix2 linear;
    TrytrVector2 transformed_offset;
    if (!trytr_matrix2_multiply(left.linear, right.linear, &linear) ||
        !trytr_matrix2_apply(left.linear, right.translation,
                             &transformed_offset)) {
        return false;
    }
    const TrytrVector2 translation =
        trytr_vector2_add(transformed_offset, left.translation);
    if (!trytr_vector2_is_finite(translation)) return false;

    *out_transform = (TrytrAffine2){linear, translation};
    return true;
}

bool trytr_affine2_apply(TrytrAffine2 transform, TrytrPoint2 point,
                         TrytrPoint2 *out_point)
{
    if (out_point == NULL) return false;
    *out_point = trytr_point2_make(0.0, 0.0);
    if (!trytr_affine2_is_finite(transform) || !trytr_point2_is_finite(point))
        return false;

    TrytrVector2 mapped;
    if (!trytr_matrix2_apply(transform.linear,
                             trytr_vector2_make(point.x, point.y), &mapped)) {
        return false;
    }
    const TrytrPoint2 result = trytr_point2_translate(
        trytr_point2_make(mapped.x, mapped.y), transform.translation);
    if (!trytr_point2_is_finite(result)) return false;
    *out_point = result;
    return true;
}

bool trytr_affine2_inverse(TrytrAffine2 transform,
                           TrytrAffine2 *out_inverse)
{
    if (out_inverse == NULL) return false;
    *out_inverse = affine2_zero();
    if (!trytr_affine2_is_finite(transform)) return false;

    TrytrMatrix2 inverse_linear;
    if (!trytr_matrix2_inverse(transform.linear, &inverse_linear)) return false;
    TrytrVector2 mapped_offset;
    if (!trytr_matrix2_apply(inverse_linear, transform.translation,
                             &mapped_offset)) {
        return false;
    }
    const TrytrVector2 inverse_translation =
        trytr_vector2_scale(mapped_offset, -1.0);
    if (!trytr_vector2_is_finite(inverse_translation)) return false;

    *out_inverse = (TrytrAffine2){inverse_linear, inverse_translation};
    return true;
}
