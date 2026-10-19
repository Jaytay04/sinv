#include "overlay_render.h"

#include <stddef.h>

#if defined(_MSC_VER)
#pragma code_seg(push, ".hvscan$m")
#pragma const_seg(push, ".hvscan$p")
#pragma section(".hvscan$p", read)
#define HV_OVERLAY_CONSTANT __declspec(allocate(".hvscan$p"))
#else
#define HV_OVERLAY_CONSTANT
#endif

#include "font_rom.h"

/* Volatile reads prevent MSVC from substituting pooled .rdata literals outside
 * the copied resident section. These objects are immutable resident data. */
HV_OVERLAY_CONSTANT static const volatile double kProjectionOne = 1.0;
HV_OVERLAY_CONSTANT static const volatile double kProjectionHalf = 0.5;

static int FiniteFloat(float value)
{
    union { float Value; uint32_t Bits; } representation;
    representation.Value = value;
    return (representation.Bits & UINT32_C(0x7f800000)) !=
        UINT32_C(0x7f800000);
}

static int ValidMatrix(const HV_OVERLAY_MATRIX *matrix)
{
    uint32_t row, column;
    if (matrix == NULL) return 0;
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column) {
            if (!FiniteFloat(matrix->M[row][column])) return 0;
        }
    }
    return 1;
}

uint32_t HvOverlayBlend(uint32_t destination, uint32_t source)
{
    const uint32_t alpha = source >> 24;
    const uint32_t inverse = UINT32_C(255) - alpha;
    uint32_t shift, result = UINT32_C(0xff000000);
    if (alpha == 0) return destination;
    if (alpha == 255) return source;
    for (shift = 0; shift < 24; shift += 8) {
        const uint32_t from = (source >> shift) & UINT32_C(255);
        const uint32_t to = (destination >> shift) & UINT32_C(255);
        const uint32_t blended =
            (from * alpha + to * inverse + UINT32_C(127)) / UINT32_C(255);
        result |= blended << shift;
    }
    return result;
}

HV_OVERLAY_STATUS HvOverlayProject(
    const float world[3],
    const HV_OVERLAY_MATRIX *viewProjection,
    uint32_t width,
    uint32_t height,
    HV_OVERLAY_POINT *output)
{
    double clip[4];
    uint32_t row;
    if (world == NULL || output == NULL || !ValidMatrix(viewProjection) ||
        width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX ||
        !FiniteFloat(world[0]) || !FiniteFloat(world[1]) ||
        !FiniteFloat(world[2])) {
        return HV_OVERLAY_INVALID_ARGUMENT;
    }
    /* Double accumulation keeps products of finite float inputs finite.
     * No math-library calls or floating-to-integer helper are needed on x64. */
    for (row = 0; row < 4; ++row) {
        clip[row] = (double)viewProjection->M[row][0] * world[0] +
            (double)viewProjection->M[row][1] * world[1] +
            (double)viewProjection->M[row][2] * world[2] +
            (double)viewProjection->M[row][3];
    }
    /* Addition avoids a compiler-generated sign-mask pool for unary minus. */
    if (clip[3] <= 0.0 || clip[0] + clip[3] < 0.0 || clip[0] > clip[3] ||
        clip[1] + clip[3] < 0.0 || clip[1] > clip[3] ||
        clip[2] < 0.0 || clip[2] > clip[3]) {
        return HV_OVERLAY_OUTSIDE_VIEW;
    }
    output->X = (int32_t)((clip[0] / clip[3] + kProjectionOne) *
        (double)(width - 1) * kProjectionHalf);
    output->Y = (int32_t)((kProjectionOne - clip[1] / clip[3]) *
        (double)(height - 1) * kProjectionHalf);
    return HV_OVERLAY_OK;
}

const uint8_t *HvOverlayFontGlyph(uint8_t character)
{
    return HvOverlayFontRom[character];
}

static int ValidFramebuffer(
    const uint32_t *pixels, uint32_t stride, uint32_t width, uint32_t height)
{
    return pixels != NULL && ((uintptr_t)pixels & UINT32_C(3)) == 0 &&
        width != 0 && height != 0 && (stride & UINT32_C(3)) == 0 &&
        (uint64_t)width * sizeof(uint32_t) <= stride &&
        (uint64_t)stride * height <= HV_OVERLAY_MAX_FRAMEBUFFER_BYTES;
}

static void FillRect(
    uint32_t *pixels, uint32_t stride, uint32_t width, uint32_t height,
    int64_t left, int64_t top, int64_t right, int64_t bottom, uint32_t color)
{
    uint32_t x, y;
    if ((color >> 24) == 0 || right <= 0 || bottom <= 0 ||
        left >= (int64_t)width || top >= (int64_t)height) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (int64_t)width) right = width;
    if (bottom > (int64_t)height) bottom = height;
    for (y = (uint32_t)top; (int64_t)y < bottom; ++y) {
        uint32_t *row = pixels + (size_t)y * (stride / sizeof(uint32_t));
        for (x = (uint32_t)left; (int64_t)x < right; ++x) {
            row[x] = HvOverlayBlend(row[x], color);
        }
    }
}

static void DrawBox(
    uint32_t *pixels, uint32_t stride, uint32_t width, uint32_t height,
    int64_t x, int64_t y, const HV_OVERLAY_ENTITY *entity)
{
    const int64_t right = x + entity->Width;
    const int64_t bottom = y + entity->Height;
    if (entity->Width == 0 || entity->Height == 0) return;
    if (entity->Width > 2 && entity->Height > 2) {
        FillRect(pixels, stride, width, height, x + 1, y + 1,
            right - 1, bottom - 1, entity->FillColor);
    }
    FillRect(pixels, stride, width, height, x, y, right, y + 1, entity->Color);
    if (entity->Height > 1) {
        FillRect(pixels, stride, width, height, x, bottom - 1,
            right, bottom, entity->Color);
    }
    if (entity->Height > 2) {
        FillRect(pixels, stride, width, height, x, y + 1,
            x + 1, bottom - 1, entity->Color);
        if (entity->Width > 1) {
            FillRect(pixels, stride, width, height, right - 1, y + 1,
                right, bottom - 1, entity->Color);
        }
    }
}

static void DrawText(
    uint32_t *pixels, uint32_t stride, uint32_t width, uint32_t height,
    int64_t x, int64_t y, const HV_OVERLAY_ENTITY *entity)
{
    uint32_t index, row, column;
    if ((entity->Color >> 24) == 0) return;
    for (index = 0; index < entity->LabelLength; ++index) {
        const uint8_t *glyph = HvOverlayFontGlyph(entity->Label[index]);
        const int64_t glyphX = x + (int64_t)index * HV_OVERLAY_FONT_WIDTH;
        if (glyphX >= (int64_t)width) break;
        if (glyphX + HV_OVERLAY_FONT_WIDTH <= 0) continue;
        for (row = 0; row < HV_OVERLAY_FONT_HEIGHT; ++row) {
            const int64_t py = y + row;
            if (py < 0 || py >= (int64_t)height) continue;
            for (column = 0; column < HV_OVERLAY_FONT_WIDTH; ++column) {
                const int64_t px = glyphX + column;
                if (px >= 0 && px < (int64_t)width &&
                    (glyph[row] & (UINT32_C(1) << column)) != 0) {
                    uint32_t *pixel = pixels +
                        (size_t)py * (stride / sizeof(uint32_t)) + (size_t)px;
                    *pixel = HvOverlayBlend(*pixel, entity->Color);
                }
            }
        }
    }
}

HV_OVERLAY_STATUS HvOverlayRender(
    uint32_t *framebuffer,
    uint32_t strideBytes,
    uint32_t width,
    uint32_t height,
    const HV_OVERLAY_ENTITY_LIST *entities,
    const HV_OVERLAY_MATRIX *viewProjection)
{
    uint32_t index;
    int matrixChecked = 0;
    if (!ValidFramebuffer(framebuffer, strideBytes, width, height) ||
        entities == NULL || entities->Count > HV_OVERLAY_MAX_ENTITIES) {
        return HV_OVERLAY_INVALID_ARGUMENT;
    }
    /* Validate the complete list before touching any caller-owned pixels. */
    for (index = 0; index < entities->Count; ++index) {
        const HV_OVERLAY_ENTITY *entity = &entities->Entities[index];
        if (entity->LabelLength > HV_OVERLAY_LABEL_BYTES ||
            (entity->Coordinates != HV_OVERLAY_SCREEN &&
             entity->Coordinates != HV_OVERLAY_WORLD)) {
            return HV_OVERLAY_INVALID_ARGUMENT;
        }
        if (entity->Coordinates == HV_OVERLAY_WORLD) {
            if ((!matrixChecked && !ValidMatrix(viewProjection)) ||
                !FiniteFloat(entity->World[0]) || !FiniteFloat(entity->World[1]) ||
                !FiniteFloat(entity->World[2])) return HV_OVERLAY_INVALID_ARGUMENT;
            matrixChecked = 1;
        }
    }
    for (index = 0; index < entities->Count; ++index) {
        const HV_OVERLAY_ENTITY *entity = &entities->Entities[index];
        HV_OVERLAY_POINT anchor;
        anchor.X = entity->X;
        anchor.Y = entity->Y;
        if (entity->Coordinates == HV_OVERLAY_WORLD &&
            HvOverlayProject(entity->World, viewProjection, width, height,
                &anchor) != HV_OVERLAY_OK) continue;
        DrawBox(framebuffer, strideBytes, width, height, anchor.X, anchor.Y, entity);
        DrawText(framebuffer, strideBytes, width, height, anchor.X,
            (int64_t)anchor.Y - HV_OVERLAY_FONT_HEIGHT, entity);
    }
    return HV_OVERLAY_OK;
}

#if defined(_MSC_VER)
#pragma const_seg(pop)
#pragma code_seg(pop)
#endif
