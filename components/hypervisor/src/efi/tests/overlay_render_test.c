#include "overlay_render.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

#define TEST_WIDTH UINT32_C(16)
#define TEST_HEIGHT UINT32_C(16)
#define TEST_STRIDE_PIXELS UINT32_C(24)
#define TEST_FRAME_SENTINEL UINT32_C(0xff123456)

static uint32_t gFrameStorage[1 + TEST_STRIDE_PIXELS * TEST_HEIGHT + 1];
static uint32_t gLargeExtentFrame[
    HV_OVERLAY_MAX_FRAMEBUFFER_BYTES / sizeof(uint32_t) + 2];
static HV_OVERLAY_ENTITY_LIST gEntities;

static uint32_t *FramePixels(void)
{
    return &gFrameStorage[1];
}

static void FillFrame(uint32_t value)
{
    size_t index;
    for (index = 0; index < sizeof(gFrameStorage) / sizeof(gFrameStorage[0]);
         ++index) {
        gFrameStorage[index] = value;
    }
}

static uint32_t FramePixel(uint32_t x, uint32_t y)
{
    return FramePixels()[y * TEST_STRIDE_PIXELS + x];
}

static int FrameIsSentinel(uint32_t width, uint32_t height)
{
    uint32_t x, y;
    if (gFrameStorage[0] != TEST_FRAME_SENTINEL ||
        gFrameStorage[sizeof(gFrameStorage) / sizeof(gFrameStorage[0]) - 1] !=
            TEST_FRAME_SENTINEL) {
        return 0;
    }
    for (y = 0; y < height; ++y) {
        for (x = 0; x < TEST_STRIDE_PIXELS; ++x) {
            if (FramePixel(x, y) != TEST_FRAME_SENTINEL) return 0;
        }
    }
    return width <= TEST_STRIDE_PIXELS && height <= TEST_HEIGHT;
}

static int FramePaddingAndGuardsAreSentinel(uint32_t width, uint32_t height)
{
    uint32_t x, y;
    if (gFrameStorage[0] != TEST_FRAME_SENTINEL ||
        gFrameStorage[sizeof(gFrameStorage) / sizeof(gFrameStorage[0]) - 1] !=
            TEST_FRAME_SENTINEL) {
        return 0;
    }
    for (y = 0; y < height; ++y) {
        for (x = width; x < TEST_STRIDE_PIXELS; ++x) {
            if (FramePixel(x, y) != TEST_FRAME_SENTINEL) return 0;
        }
    }
    return 1;
}

static void SetScreenEntity(
    HV_OVERLAY_ENTITY *entity,
    int32_t x,
    int32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color)
{
    memset(entity, 0, sizeof(*entity));
    entity->Coordinates = HV_OVERLAY_SCREEN;
    entity->X = x;
    entity->Y = y;
    entity->Width = width;
    entity->Height = height;
    entity->Color = color;
}

static uint32_t BlendChannel(uint32_t destination, uint32_t source, uint32_t alpha)
{
    return (source * alpha + destination * (UINT32_C(255) - alpha) +
            UINT32_C(127)) / UINT32_C(255);
}

static int TestBlend(void)
{
    uint32_t alpha, source, destination;
    uint32_t sourceRed, destinationRed;
    uint32_t expected, actual;
    int passed = 1;

    if (expect(HvOverlayBlend(UINT32_C(0x12345678), UINT32_C(0x00112233)) ==
                   UINT32_C(0x12345678),
               "zero-alpha blend preserves every destination bit")) {
        return 1;
    }
    if (expect(HvOverlayBlend(UINT32_C(0x10203040), UINT32_C(0xffabcdef)) ==
                   UINT32_C(0xffabcdef),
               "opaque source replaces the destination")) {
        return 1;
    }

    /* Exhaust every alpha/source/destination channel triple in all channels. */
    for (alpha = 0; alpha < UINT32_C(256) && passed; ++alpha) {
        for (sourceRed = 0; sourceRed < UINT32_C(256) && passed; ++sourceRed) {
            for (destinationRed = 0; destinationRed < UINT32_C(256);
                 ++destinationRed) {
                source = (alpha << 24) | (sourceRed << 16) |
                    (destinationRed << 8) | (UINT32_C(255) - sourceRed);
                destination = UINT32_C(0xff000000) |
                    (destinationRed << 16) | (sourceRed << 8) |
                    (UINT32_C(255) - destinationRed);
                expected = UINT32_C(0xff000000) |
                    (BlendChannel(destinationRed, sourceRed, alpha) << 16) |
                    (BlendChannel(sourceRed, destinationRed, alpha) << 8) |
                    BlendChannel(UINT32_C(255) - destinationRed,
                                 UINT32_C(255) - sourceRed, alpha);
                actual = HvOverlayBlend(destination, source);
                if (actual != expected) {
                    passed = 0;
                    break;
                }
            }
        }
    }
    return expect(passed,
                  "round-nearest blend matches every alpha and channel pair");
}

static int ExpectProjectFailure(
    const float world[3],
    const HV_OVERLAY_MATRIX *matrix,
    uint32_t width,
    uint32_t height,
    HV_OVERLAY_STATUS expectedStatus,
    const char *message)
{
    HV_OVERLAY_POINT point = { 12345, -23456 };
    HV_OVERLAY_STATUS status = HvOverlayProject(
        world, matrix, width, height, &point);
    return expect(status == expectedStatus && point.X == 12345 &&
                      point.Y == -23456,
                  message);
}

static int TestProjection(void)
{
    HV_OVERLAY_MATRIX identity = { { { 1.0f, 0.0f, 0.0f, 0.0f },
                                      { 0.0f, 1.0f, 0.0f, 0.0f },
                                      { 0.0f, 0.0f, 1.0f, 0.0f },
                                      { 0.0f, 0.0f, 0.0f, 1.0f } } };
    HV_OVERLAY_MATRIX perspective = { { { 1.0f, 0.0f, 0.0f, 0.0f },
                                         { 0.0f, 1.0f, 0.0f, 0.0f },
                                         { 0.0f, 0.0f, 1.0f, 0.0f },
                                         { 0.0f, 0.0f, 1.0f, 0.0f } } };
    HV_OVERLAY_MATRIX zeroW = identity;
    HV_OVERLAY_MATRIX negativeW = identity;
    HV_OVERLAY_MATRIX nonFinite = identity;
    const float center[3] = { 0.0f, 0.0f, 0.5f };
    const float topLeft[3] = { -1.0f, 1.0f, 0.0f };
    const float bottomRight[3] = { 1.0f, -1.0f, 1.0f };
    const float perspectivePoint[3] = { 1.0f, 0.5f, 2.0f };
    const float badNan[3] = { NAN, 0.0f, 0.5f };
    const float badInfinity[3] = { 0.0f, INFINITY, 0.5f };
    const float outsideX[3] = { 1.01f, 0.0f, 0.5f };
    const float outsideY[3] = { 0.0f, -1.01f, 0.5f };
    const float outsideZ[3] = { 0.0f, 0.0f, -0.01f };
    HV_OVERLAY_POINT point;
    int failures = 0;

    point.X = -1;
    point.Y = -1;
    failures += expect(HvOverlayProject(center, &identity, 101, 51, &point) ==
                           HV_OVERLAY_OK && point.X == 50 && point.Y == 25,
                       "identity projection maps the viewport center");
    failures += expect(HvOverlayProject(topLeft, &identity, 101, 51, &point) ==
                           HV_OVERLAY_OK && point.X == 0 && point.Y == 0,
                       "Direct3D top-left clip edge maps to viewport origin");
    failures += expect(HvOverlayProject(bottomRight, &identity, 101, 51,
                                        &point) == HV_OVERLAY_OK &&
                           point.X == 100 && point.Y == 50,
                       "Direct3D bottom-right clip edge maps to final pixel");
    failures += expect(HvOverlayProject(perspectivePoint, &perspective,
                                        101, 41, &point) == HV_OVERLAY_OK &&
                           point.X == 75 && point.Y == 15,
                       "projection divides by W and flips positive Y upward");

    zeroW.M[3][0] = 0.0f;
    zeroW.M[3][1] = 0.0f;
    zeroW.M[3][2] = 0.0f;
    zeroW.M[3][3] = 0.0f;
    negativeW.M[3][0] = 0.0f;
    negativeW.M[3][1] = 0.0f;
    negativeW.M[3][2] = 0.0f;
    negativeW.M[3][3] = -1.0f;
    nonFinite.M[0][0] = INFINITY;
    failures += ExpectProjectFailure(NULL, &identity, 10, 10,
        HV_OVERLAY_INVALID_ARGUMENT, "null world input preserves projection output");
    failures += ExpectProjectFailure(center, NULL, 10, 10,
        HV_OVERLAY_INVALID_ARGUMENT, "null matrix preserves projection output");
    failures += expect(HvOverlayProject(center, &identity, 10, 10, NULL) ==
                           HV_OVERLAY_INVALID_ARGUMENT,
                       "null projection output is rejected");
    failures += ExpectProjectFailure(center, &identity, 0, 10,
        HV_OVERLAY_INVALID_ARGUMENT, "zero viewport width preserves output");
    failures += ExpectProjectFailure(center, &identity, 10, 0,
        HV_OVERLAY_INVALID_ARGUMENT, "zero viewport height preserves output");
    failures += ExpectProjectFailure(badNan, &identity, 10, 10,
        HV_OVERLAY_INVALID_ARGUMENT, "NaN world coordinate preserves output");
    failures += ExpectProjectFailure(badInfinity, &identity, 10, 10,
        HV_OVERLAY_INVALID_ARGUMENT, "infinite world coordinate preserves output");
    failures += ExpectProjectFailure(center, &nonFinite, 10, 10,
        HV_OVERLAY_INVALID_ARGUMENT, "non-finite matrix preserves output");
    failures += ExpectProjectFailure(center, &zeroW, 10, 10,
        HV_OVERLAY_OUTSIDE_VIEW, "zero clip W preserves projection output");
    failures += ExpectProjectFailure(center, &negativeW, 10, 10,
        HV_OVERLAY_OUTSIDE_VIEW, "negative clip W preserves projection output");
    failures += ExpectProjectFailure(outsideX, &identity, 10, 10,
        HV_OVERLAY_OUTSIDE_VIEW, "point beyond horizontal clip volume is skipped");
    failures += ExpectProjectFailure(outsideY, &identity, 10, 10,
        HV_OVERLAY_OUTSIDE_VIEW, "point beyond vertical clip volume is skipped");
    failures += ExpectProjectFailure(outsideZ, &identity, 10, 10,
        HV_OVERLAY_OUTSIDE_VIEW, "point before Direct3D near plane is skipped");
    return failures;
}

static int TestClippedBoxes(void)
{
    const uint32_t border = UINT32_C(0xffe00000);
    const uint32_t fill = UINT32_C(0xff00e000);
    int failures = 0;

    memset(&gEntities, 0, sizeof(gEntities));
    gEntities.Count = 1;
    FillFrame(TEST_FRAME_SENTINEL);
    SetScreenEntity(&gEntities.Entities[0], 4, -1, 4, 4, border);
    gEntities.Entities[0].FillColor = fill;
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK &&
                           FramePixel(4, 0) == border &&
                           FramePixel(5, 0) == fill &&
                           FramePixel(5, 1) == fill &&
                           FramePixel(5, 2) == border,
                       "top-clipped box keeps border and interior roles");
    failures += expect(FramePaddingAndGuardsAreSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "top clipping preserves row padding and guard canaries");

    FillFrame(TEST_FRAME_SENTINEL);
    SetScreenEntity(&gEntities.Entities[0], 4, 14, 4, 4, border);
    gEntities.Entities[0].FillColor = fill;
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK &&
                           FramePixel(4, 14) == border &&
                           FramePixel(5, 15) == fill &&
                           FramePixel(7, 15) == border,
                       "bottom-clipped box draws visible border and fill");
    failures += expect(FramePaddingAndGuardsAreSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "bottom clipping preserves row padding and guard canaries");

    FillFrame(TEST_FRAME_SENTINEL);
    SetScreenEntity(&gEntities.Entities[0], -1, 4, 4, 4, border);
    gEntities.Entities[0].FillColor = fill;
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK &&
                           FramePixel(0, 4) == border &&
                           FramePixel(0, 5) == fill &&
                           FramePixel(2, 5) == border,
                       "left-clipped box keeps visible side and interior");
    failures += expect(FramePaddingAndGuardsAreSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "left clipping preserves row padding and guard canaries");

    FillFrame(TEST_FRAME_SENTINEL);
    SetScreenEntity(&gEntities.Entities[0], 14, 4, 4, 4, border);
    gEntities.Entities[0].FillColor = fill;
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK &&
                           FramePixel(14, 5) == border &&
                           FramePixel(15, 5) == fill &&
                           FramePixel(14, 4) == border,
                       "right-clipped box keeps visible side and interior");
    failures += expect(FramePaddingAndGuardsAreSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "right clipping preserves row padding and guard canaries");
    return failures;
}

static int TestBoxCompositionAndThinBoxes(void)
{
    const uint32_t border = UINT32_C(0x80ff0000);
    const uint32_t fill = UINT32_C(0x8000ff00);
    const uint32_t oneRedBlend = UINT32_C(0xff800000);
    const uint32_t oneGreenBlend = UINT32_C(0xff008000);
    uint32_t index;
    int failures = 0;

    memset(&gEntities, 0, sizeof(gEntities));
    gEntities.Count = 1;
    FillFrame(UINT32_C(0xff000000));
    SetScreenEntity(&gEntities.Entities[0], 3, 3, 4, 4, border);
    gEntities.Entities[0].FillColor = fill;
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK &&
                           FramePixel(3, 3) == oneRedBlend &&
                           FramePixel(6, 6) == oneRedBlend &&
                           FramePixel(4, 4) == oneGreenBlend &&
                           FramePixel(5, 5) == oneGreenBlend &&
                           FramePixel(4, 3) == oneRedBlend,
                       "box corners blend once and fill stays inside its border");

    FillFrame(UINT32_C(0xff000000));
    SetScreenEntity(&gEntities.Entities[0], 2, 2, 1, 5, border);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK,
                       "one-by-N box renders successfully");
    for (index = 0; index < 5; ++index) {
        if (FramePixel(2, 2 + index) != oneRedBlend) {
            failures += expect(0, "one-by-N border pixels blend once");
            break;
        }
    }

    FillFrame(UINT32_C(0xff000000));
    SetScreenEntity(&gEntities.Entities[0], 2, 2, 5, 1, border);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK,
                       "N-by-one box renders successfully");
    for (index = 0; index < 5; ++index) {
        if (FramePixel(2 + index, 2) != oneRedBlend) {
            failures += expect(0, "N-by-one border pixels blend once");
            break;
        }
    }
    return failures;
}

static int GlyphIsBlank(const uint8_t *glyph)
{
    uint32_t row;
    for (row = 0; row < HV_OVERLAY_FONT_HEIGHT; ++row) {
        if (glyph[row] != 0) return 0;
    }
    return 1;
}

static int FrameMatchesLabel(
    int32_t originX,
    int32_t originY,
    const uint8_t *label,
    uint32_t length,
    uint32_t color)
{
    int32_t x, y;
    for (y = 0; y < (int32_t)TEST_HEIGHT; ++y) {
        for (x = 0; x < (int32_t)TEST_WIDTH; ++x) {
            uint32_t index, expected = TEST_FRAME_SENTINEL;
            for (index = 0; index < length; ++index) {
                int32_t glyphX = originX + (int32_t)index *
                    (int32_t)HV_OVERLAY_FONT_WIDTH;
                int32_t glyphY = originY;
                const uint8_t *glyph = HvOverlayFontGlyph(label[index]);
                int32_t column = x - glyphX;
                int32_t row = y - glyphY;
                if (column >= 0 && column < (int32_t)HV_OVERLAY_FONT_WIDTH &&
                    row >= 0 && row < (int32_t)HV_OVERLAY_FONT_HEIGHT &&
                    (glyph[row] & (UINT32_C(1) << column)) != 0) {
                    expected = color;
                }
            }
            if (FramePixel((uint32_t)x, (uint32_t)y) != expected) return 0;
        }
    }
    return 1;
}

static int RenderAndCheckLabel(
    int32_t originX,
    int32_t originY,
    const char *message)
{
    const uint32_t color = UINT32_C(0xffffa500);
    int failures = 0;
    memset(&gEntities, 0, sizeof(gEntities));
    FillFrame(TEST_FRAME_SENTINEL);
    gEntities.Count = 1;
    SetScreenEntity(&gEntities.Entities[0], originX, originY + 16,
                    0, 0, color);
    gEntities.Entities[0].LabelLength = 2;
    gEntities.Entities[0].Label[0] = (uint8_t)'A';
    gEntities.Entities[0].Label[1] = UINT8_C(0xc9);
    memset(gEntities.Entities[0].Label + 2, (int)'Z',
           HV_OVERLAY_LABEL_BYTES - 2);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK,
                       message);
    failures += expect(FrameMatchesLabel(originX, originY,
                                          gEntities.Entities[0].Label,
                                          gEntities.Entities[0].LabelLength,
                                          color),
                       "label rendering honors explicit length and glyph bits");
    failures += expect(FramePaddingAndGuardsAreSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "clipped text preserves row padding and guard canaries");
    return failures;
}

static int TestFontAndText(void)
{
    static const uint8_t expectedA[HV_OVERLAY_FONT_HEIGHT] = {
        0x0c, 0x0c, 0x1e, 0x1e, 0x33, 0x33, 0x33, 0x33,
        0x3f, 0x3f, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00
    };
    uint32_t character, row;
    int failures = 0;
    int controlsBlank = 1;
    int highByteHasInk = 0;
    int glyphStrideValid = 1;
    int duplicatedRows = 1;
    uint32_t rasterChecksum = 0;

    for (character = 0; character < HV_OVERLAY_FONT_GLYPHS; ++character) {
        const uint8_t *glyph = HvOverlayFontGlyph((uint8_t)character);
        if (glyph == NULL) {
            controlsBlank = 0;
            break;
        }
        if (character != 0) {
            const uint8_t *previous = HvOverlayFontGlyph(
                (uint8_t)(character - 1));
            if ((uintptr_t)glyph !=
                (uintptr_t)previous + HV_OVERLAY_FONT_HEIGHT) {
                glyphStrideValid = 0;
            }
        }
        for (row = 0; row < HV_OVERLAY_FONT_HEIGHT; ++row) {
            rasterChecksum = rasterChecksum * UINT32_C(33) + glyph[row] +
                character + row;
        }
        for (row = 0; row < HV_OVERLAY_FONT_HEIGHT; row += 2) {
            if (glyph[row] != glyph[row + 1]) duplicatedRows = 0;
        }
        if (character == (uint32_t)'A') {
            failures += expect(memcmp(glyph, expectedA, sizeof(expectedA)) == 0,
                               "ROM glyph A has the documented 8x16 raster");
        }
        if ((character <= UINT32_C(0x1f)) ||
            (character >= UINT32_C(0x7f) && character <= UINT32_C(0x9f)) ||
            character == UINT32_C(0xa0)) {
            controlsBlank = controlsBlank && GlyphIsBlank(glyph);
        }
        if (character == UINT32_C(0xc9)) {
            for (row = 0; row < HV_OVERLAY_FONT_HEIGHT; ++row) {
                if (glyph[row] != 0) highByteHasInk = 1;
            }
        }
    }
    failures += expect(HV_OVERLAY_FONT_GLYPHS == 256 &&
                           HV_OVERLAY_FONT_WIDTH == 8 &&
                           HV_OVERLAY_FONT_HEIGHT == 16,
                       "font API exposes the public 256 by 8 by 16 dimensions");
    failures += expect(character == HV_OVERLAY_FONT_GLYPHS,
                       "font accessor returns all 256 glyphs");
    failures += expect(glyphStrideValid,
                       "font accessor glyphs occupy contiguous 16-byte slots");
    failures += expect(duplicatedRows,
                       "8x8 source glyph rows are duplicated into 8x16 ROM");
    failures += expect(rasterChecksum == UINT32_C(0x49ee3ca2),
                       "all 256 glyph rasters match the pinned ROM checksum");
    failures += expect(controlsBlank,
                       "control-code and nonbreaking-space ROM slots are blank");
    failures += expect(highByteHasInk,
                       "Latin-1 high-byte glyph has a populated raster");
    failures += RenderAndCheckLabel(-4, 0,
                       "left-clipped explicit-length Latin-1 label renders");
    failures += RenderAndCheckLabel(12, 0,
                       "right-clipped explicit-length label renders");
    failures += RenderAndCheckLabel(4, -14,
                       "top-clipped explicit-length label renders");
    failures += RenderAndCheckLabel(4, 14,
                       "bottom-clipped explicit-length label renders");
    return failures;
}

static int TestEntityBoundsAndPreflight(void)
{
    uint32_t index;
    int failures = 0;
    const uint32_t invalidExtentStride =
        (uint32_t)(HV_OVERLAY_MAX_FRAMEBUFFER_BYTES / 2 + 4);

    memset(&gEntities, 0, sizeof(gEntities));
    gEntities.Count = HV_OVERLAY_MAX_ENTITIES;
    for (index = 0; index < HV_OVERLAY_MAX_ENTITIES; ++index) {
        SetScreenEntity(&gEntities.Entities[index], (int32_t)(index % 16),
                        (int32_t)(index / 16), 1, 1,
                        UINT32_C(0xffabcdef));
    }
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_OK,
                       "renderer accepts the maximum 256-entity list");
    for (index = 0; index < HV_OVERLAY_MAX_ENTITIES; ++index) {
        if (FramePixel(index % 16, index / 16) != UINT32_C(0xffabcdef)) {
            failures += expect(0, "all 256 maximum-list entities are rendered");
            break;
        }
    }
    failures += expect(FramePaddingAndGuardsAreSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "maximum entity list preserves stride padding and guards");

    gEntities.Count = HV_OVERLAY_MAX_ENTITIES + 1;
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_INVALID_ARGUMENT &&
                           FrameIsSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "257 entities reject before any framebuffer write");

    memset(&gEntities, 0, sizeof(gEntities));
    gEntities.Count = 2;
    SetScreenEntity(&gEntities.Entities[0], 1, 1, 1, 1,
                    UINT32_C(0xffabcdef));
    SetScreenEntity(&gEntities.Entities[1], 2, 2, 1, 1,
                    UINT32_C(0xffabcdef));
    gEntities.Entities[1].LabelLength = HV_OVERLAY_LABEL_BYTES + 1;
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_INVALID_ARGUMENT &&
                           FrameIsSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "invalid second-entity label rejects before first draw");

    gEntities.Entities[1].LabelLength = 0;
    gEntities.Entities[1].Coordinates = UINT32_C(2);
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_INVALID_ARGUMENT &&
                           FrameIsSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "invalid second-entity coordinate kind rejects before draw");

    gEntities.Entities[1].Coordinates = HV_OVERLAY_SCREEN;
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_WIDTH * 4 - 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_INVALID_ARGUMENT &&
                           FrameIsSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "stride shorter than visible rows rejects before writes");
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_WIDTH * 4 + 1,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_INVALID_ARGUMENT &&
                           FrameIsSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "non-four-byte stride rejects before writes");

    for (index = 0;
         index < (uint32_t)(sizeof(gLargeExtentFrame) /
                            sizeof(gLargeExtentFrame[0]));
         ++index) {
        gLargeExtentFrame[index] = TEST_FRAME_SENTINEL;
    }
    failures += expect(HvOverlayRender(gLargeExtentFrame, invalidExtentStride,
                                       1, 2, &gEntities, NULL) ==
                           HV_OVERLAY_INVALID_ARGUMENT,
                       "framebuffer extent above 8 MiB is rejected");
    for (index = 0;
         index < (uint32_t)(sizeof(gLargeExtentFrame) /
                            sizeof(gLargeExtentFrame[0]));
         ++index) {
        if (gLargeExtentFrame[index] != TEST_FRAME_SENTINEL) {
            failures += expect(0,
                "oversized extent rejects before touching caller memory");
            break;
        }
    }

    memset(&gEntities, 0, sizeof(gEntities));
    gEntities.Count = 1;
    gEntities.Entities[0].Coordinates = HV_OVERLAY_WORLD;
    gEntities.Entities[0].World[0] = 0.0f;
    gEntities.Entities[0].World[1] = 0.0f;
    gEntities.Entities[0].World[2] = 0.5f;
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       TEST_WIDTH, TEST_HEIGHT, &gEntities,
                                       NULL) == HV_OVERLAY_INVALID_ARGUMENT &&
                           FrameIsSentinel(TEST_WIDTH, TEST_HEIGHT),
                       "world entity without a matrix rejects before writes");
    return failures;
}

static int TestWorldComposition(void)
{
    HV_OVERLAY_MATRIX identity = { { { 1.0f, 0.0f, 0.0f, 0.0f },
                                      { 0.0f, 1.0f, 0.0f, 0.0f },
                                      { 0.0f, 0.0f, 1.0f, 0.0f },
                                      { 0.0f, 0.0f, 0.0f, 1.0f } } };
    int failures = 0;

    memset(&gEntities, 0, sizeof(gEntities));
    gEntities.Count = 2;
    SetScreenEntity(&gEntities.Entities[0], 1, 1, 1, 1,
                    UINT32_C(0xffff0000));
    memset(&gEntities.Entities[1], 0, sizeof(gEntities.Entities[1]));
    gEntities.Entities[1].Coordinates = HV_OVERLAY_WORLD;
    gEntities.Entities[1].World[0] = 0.0f;
    gEntities.Entities[1].World[1] = 0.0f;
    gEntities.Entities[1].World[2] = 0.5f;
    gEntities.Entities[1].Width = 1;
    gEntities.Entities[1].Height = 1;
    gEntities.Entities[1].Color = UINT32_C(0xff00ff00);
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
                                       21, 13, &gEntities, &identity) ==
                           HV_OVERLAY_OK &&
                           FramePixel(1, 1) == UINT32_C(0xffff0000) &&
                           FramePixel(10, 6) == UINT32_C(0xff00ff00),
                       "screen and projected world entities compose in one frame");
    failures += expect(FramePaddingAndGuardsAreSentinel(21, 13),
                       "world composition preserves padded rows and guards");
    return failures;
}

static int TestExtremeExtents(void)
{
    uint32_t x, y;
    int failures = 0;
    memset(&gEntities, 0, sizeof(gEntities));
    gEntities.Count = 1;
    SetScreenEntity(&gEntities.Entities[0], INT32_MIN, INT32_MIN,
                    UINT32_MAX, UINT32_MAX, UINT32_C(0xffff0000));
    gEntities.Entities[0].FillColor = UINT32_C(0xff00ff00);
    FillFrame(TEST_FRAME_SENTINEL);
    failures += expect(HvOverlayRender(FramePixels(), TEST_STRIDE_PIXELS * 4,
        TEST_WIDTH, TEST_HEIGHT, &gEntities, NULL) == HV_OVERLAY_OK,
        "extreme signed anchors and unsigned extents clip without overflow");
    for (y = 0; y < TEST_HEIGHT; ++y) {
        for (x = 0; x < TEST_WIDTH; ++x) {
            if (FramePixel(x, y) != UINT32_C(0xff00ff00)) {
                failures += expect(0, "extreme rectangle fills only its visible interior");
                return failures;
            }
        }
    }
    failures += expect(FramePaddingAndGuardsAreSentinel(TEST_WIDTH, TEST_HEIGHT),
        "extreme extents preserve row padding and canaries");
    gEntities.Count = 0;
    failures += expect(HvOverlayRender(gLargeExtentFrame,
        (uint32_t)HV_OVERLAY_MAX_FRAMEBUFFER_BYTES, 1, 1, &gEntities, NULL) ==
        HV_OVERLAY_OK, "exactly 8 MiB declared extent is accepted");
    failures += expect(HvOverlayRender(NULL, 4, 1, 1, &gEntities, NULL) ==
        HV_OVERLAY_INVALID_ARGUMENT, "null framebuffer is rejected");
    failures += expect(HvOverlayRender((uint32_t *)((uint8_t *)FramePixels() + 1),
        TEST_STRIDE_PIXELS * 4, TEST_WIDTH, TEST_HEIGHT, &gEntities, NULL) ==
        HV_OVERLAY_INVALID_ARGUMENT, "unaligned framebuffer is rejected");
    return failures;
}

int main(void)
{
    int failures = 0;
    failures += TestBlend();
    failures += TestProjection();
    failures += TestClippedBoxes();
    failures += TestBoxCompositionAndThinBoxes();
    failures += TestFontAndText();
    failures += TestEntityBoundsAndPreflight();
    failures += TestWorldComposition();
    failures += TestExtremeExtents();
    if (failures != 0) {
        fprintf(stderr, "%d overlay renderer checks failed\n", failures);
        return 1;
    }
    puts("overlay renderer checks passed");
    return 0;
}
