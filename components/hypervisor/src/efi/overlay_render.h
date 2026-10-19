#ifndef HYPERSINV_EFI_OVERLAY_RENDER_H
#define HYPERSINV_EFI_OVERLAY_RENDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_OVERLAY_MAX_ENTITIES UINT32_C(256)
#define HV_OVERLAY_LABEL_BYTES UINT32_C(32)
#define HV_OVERLAY_MAX_FRAMEBUFFER_BYTES UINT64_C(8388608)
#define HV_OVERLAY_FONT_GLYPHS UINT32_C(256)
#define HV_OVERLAY_FONT_WIDTH UINT32_C(8)
#define HV_OVERLAY_FONT_HEIGHT UINT32_C(16)

typedef enum HV_OVERLAY_STATUS {
    HV_OVERLAY_OK = 0,
    HV_OVERLAY_INVALID_ARGUMENT = 1,
    HV_OVERLAY_OUTSIDE_VIEW = 2
} HV_OVERLAY_STATUS;

typedef enum HV_OVERLAY_COORDINATES {
    HV_OVERLAY_SCREEN = 0,
    HV_OVERLAY_WORLD = 1
} HV_OVERLAY_COORDINATES;

/* Row-major storage, column vectors: clip = M * [x, y, z, 1].
 * Direct3D clip volume: -w <= x,y <= w; 0 <= z <= w; w > 0.
 * Floating-point projection requires a caller-owned SIMD/FPU context. */
typedef struct HV_OVERLAY_MATRIX {
    float M[4][4];
} HV_OVERLAY_MATRIX;

typedef struct HV_OVERLAY_POINT {
    int32_t X;
    int32_t Y;
} HV_OVERLAY_POINT;

typedef struct HV_OVERLAY_ENTITY {
    uint32_t Coordinates;
    int32_t X;
    int32_t Y;
    float World[3];
    uint32_t Width;
    uint32_t Height;
    uint32_t Color;
    uint32_t FillColor;
    uint32_t LabelLength;
    /* ISO-8859-1 bytes, explicit length, no terminator required. */
    uint8_t Label[HV_OVERLAY_LABEL_BYTES];
} HV_OVERLAY_ENTITY;

typedef struct HV_OVERLAY_ENTITY_LIST {
    uint32_t Count;
    HV_OVERLAY_ENTITY Entities[HV_OVERLAY_MAX_ENTITIES];
} HV_OVERLAY_ENTITY_LIST;

/* Straight source alpha over an opaque RGB destination, rounded to nearest.
 * Numeric colors are 0xAARRGGBB (BGRA bytes on little-endian hosts).
 * Alpha zero preserves all destination bits; other results have alpha 255. */
uint32_t HvOverlayBlend(uint32_t destination, uint32_t source);

/* On failure, output is unchanged. Viewport edges map to 0 and dimension-1. */
HV_OVERLAY_STATUS HvOverlayProject(
    const float world[3],
    const HV_OVERLAY_MATRIX *viewProjection,
    uint32_t width,
    uint32_t height,
    HV_OVERLAY_POINT *output);

/* Immutable 8x16 ROM glyph; bit zero is its leftmost pixel. */
const uint8_t *HvOverlayFontGlyph(uint8_t character);

/* VM diagnostic software renderer. No device access, mappings, allocation,
 * scheduling, input handling, or background work.
 *
 * The caller owns stable, exclusively writable, uint32_t-aligned ordinary RAM
 * of at least strideBytes*height bytes. Stride is in BYTES, divisible by four,
 * and at least width*4. The declared extent must be <= 8 MiB. Inputs must not
 * alias the output. This API cannot discover a pointer's allocation length.
 *
 * Entities anchor their box's top-left corner at X,Y or the projected World
 * point. Boxes are half-open [X,X+Width) x [Y,Y+Height), with a one-pixel
 * border and optional interior fill. Labels begin 16 pixels above that anchor.
 * All drawing clips to the viewport; row padding is never written. Invalid
 * inputs fail before any output writes; world anchors outside the view skip.
 * The matrix may be null for an exclusively screen-space list.
 *
 * Place caller-owned entity/framebuffer storage in private static pages when
 * embedding this in a VM diagnostic environment. The function is reentrant for
 * disjoint buffers. A VMX-root caller must preserve guest SIMD/FPU state before
 * entering the renderer and use masked floating-point exceptions; this module
 * does not install a VM-exit callback or manage extended processor state. */
HV_OVERLAY_STATUS HvOverlayRender(
    uint32_t *framebuffer,
    uint32_t strideBytes,
    uint32_t width,
    uint32_t height,
    const HV_OVERLAY_ENTITY_LIST *entities,
    const HV_OVERLAY_MATRIX *viewProjection);

#ifdef __cplusplus
}
#endif
#endif
