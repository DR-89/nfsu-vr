#ifndef NFSU_TOUCH_GEOMETRY_H
#define NFSU_TOUCH_GEOMETRY_H
#include <stdint.h>
#include <math.h>

/* Rendered geometry in the game's logical canvas, never physical pixels. */
typedef struct { uint32_t object; float left, top, right, bottom; } TouchBounds;

static inline int touch_bounds_contains(const TouchBounds *bounds, float x, float y)
{
    return x >= bounds->left && x < bounds->right && y >= bounds->top && y < bounds->bottom;
}

static inline void touch_bounds_include(TouchBounds *bounds, const TouchBounds *other)
{
    bounds->left = fminf(bounds->left, other->left);
    bounds->right = fmaxf(bounds->right, other->right);
    bounds->top = fminf(bounds->top, other->top);
    bounds->bottom = fmaxf(bounds->bottom, other->bottom);
}
#endif
