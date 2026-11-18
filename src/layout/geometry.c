#include "geometry.h"

bool trytr_layout_geometry_bounds(const TrytrDocumentElement *e,
                                   int64_t tx, int64_t ty,
                                   TrytrLayoutBounds *bounds)
{
    switch (e->kind) {
        case TRYTR_ELEMENT_RECT:
        case TRYTR_ELEMENT_ROUNDRECT:
            *bounds = (TrytrLayoutBounds){e->x, e->y, e->x + e->width, e->y + e->height};
            break;
        case TRYTR_ELEMENT_LINE:
            *bounds = (TrytrLayoutBounds){
                e->x < e->x2 ? e->x : e->x2, e->y < e->y2 ? e->y : e->y2,
                e->x > e->x2 ? e->x : e->x2, e->y > e->y2 ? e->y : e->y2};
            break;
        case TRYTR_ELEMENT_CIRCLE:
            *bounds = (TrytrLayoutBounds){e->x - e->radius, e->y - e->radius,
                                         e->x + e->radius, e->y + e->radius};
            break;
        case TRYTR_ELEMENT_ELLIPSE:
            *bounds = (TrytrLayoutBounds){e->x - e->radius_x, e->y - e->radius_y,
                                         e->x + e->radius_x, e->y + e->radius_y};
            break;
        case TRYTR_ELEMENT_PATH:
        case TRYTR_ELEMENT_POLYGON:
            *bounds = (TrytrLayoutBounds){e->min_x, e->min_y, e->max_x, e->max_y};
            break;
        case TRYTR_ELEMENT_PLOT:
            /* Tick labels extend beyond the chart box. */
            *bounds = (TrytrLayoutBounds){e->x - 48, e->y,
                                         e->x + e->width + 48, e->y + e->height + 24};
            break;
        default:
            return false;
    }
    bounds->min_x += tx;
    bounds->max_x += tx;
    bounds->min_y += ty;
    bounds->max_y += ty;
    return true;
}
