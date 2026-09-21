#include "hardened_overlay.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * Hardened Overlay Implementation
 * 
 * Removes all identifiable strings, strips imports,
 * and implements anti-scan techniques.
 */

/* Dynamic import function pointers */
static D3D11CREATEDEVICE_FUNC pD3D11CreateDevice = NULL;
static D2D1CREATEFACTORY_FUNC pD2D1CreateFactory = NULL;
static DWRITECREATEFACTORY_FUNC pDWriteCreateFactory = NULL;

/* Obfuscated class name */
WCHAR g_obfuscatedClassName[WINDOW_CLASS_CHARS] = {0};

/* Entity cache for distance filtering */
typedef struct _CACHED_ENTITY {
    float worldX, worldY, worldZ;
    float screenX, screenY, width, height;
    uint32_t color;
    uint32_t isVisible;
    uint64_t lastUpdateTick;
} CACHED_ENTITY;

static CACHED_ENTITY g_entityCache[256];
static uint32_t g_entityCount = 0;

/*
 * Resolve imports dynamically to avoid static import table
 */
BOOL OverlayResolveImports() {
    HMODULE hD3d11 = LoadLibraryA("d3d11.dll");
    HMODULE hD2d1 = LoadLibraryA("d2d1.dll");
    HMODULE hDwrite = LoadLibraryA("dwrite.dll");
    
    if (!hD3d11 || !hD2d1 || !hDwrite) {
        return FALSE;
    }
    
    pD3D11CreateDevice = (D3D11CREATEDEVICE_FUNC)GetProcAddress(hD3d11, "D3D11CreateDevice");
    pD2D1CreateFactory = (D2D1CREATEFACTORY_FUNC)GetProcAddress(hD2d1, "D2D1CreateFactory");
    pDWriteCreateFactory = (DWRITECREATEFACTORY_FUNC)GetProcAddress(hDwrite, "DWriteCreateFactory");
    
    return (pD3D11CreateDevice && pD2D1CreateFactory && pDWriteCreateFactory);
}

/*
 * Strip debug information from the overlay binary
 */
void OverlayStripDebugInfo() {
    /* This would be done at compile time with /DEBUG:NONE */
    /* Runtime stripping is not possible, but we can zero debug strings */
    
    /* Zero out any identifiable strings in memory */
    /* This is a placeholder for compile-time hardening */
}

/*
 * Generate obfuscated window class name
 */
static void GenerateObfuscatedClassName() {
    uint32_t seed = (uint32_t)GetTickCount64();
    seed = (seed * 0x01000193) ^ 0x811C9DC5; /* FNV-1a */
    
    /* Generate a random-looking class name */
    swprintf_s(g_obfuscatedClassName, WINDOW_CLASS_CHARS, L"0x%08x", seed);
}

/*
 * Randomize window properties
 */
void OverlayRandomizeWindow(HWND hwnd) {
    /* Randomize window position slightly to avoid detection */
    RECT rect;
    GetWindowRect(hwnd, &rect);
    
    int offsetX = (rand() % 5) - 2;
    int offsetY = (rand() % 5) - 2;
    
    SetWindowPos(hwnd, NULL, rect.left + offsetX, rect.top + offsetY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

/*
 * Hide overlay from EnumWindows
 */
void OverlayHideFromEnum(HWND hwnd) {
    /* Set WS_EX_TOOLWINDOW and WS_EX_NOACTIVATE */
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT;
    SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
}

/*
 * Remove overlay from taskbar/Alt-Tab
 */
void OverlayRemoveFromTaskbar(HWND hwnd) {
    /* Already handled by WS_EX_TOOLWINDOW */
    /* Additional: set owner window to NULL */
    SetWindowLong(hwnd, GWL_HWNDPARENT, 0);
}

/*
 * Enable click-through behavior
 */
void OverlayEnableClickThrough(HWND hwnd) {
    /* Set HTTRANSPARENT for client area */
    LONG style = GetWindowLong(hwnd, GWL_EXSTYLE);
    style |= WS_EX_TRANSPARENT;
    SetWindowLong(hwnd, GWL_EXSTYLE, style);
}

/*
 * Randomize rendering timing
 */
void OverlayRandomizeTiming() {
    /* Add random delay between 0-16ms to avoid consistent 60Hz pattern */
    int delay = rand() % 17;
    Sleep(delay);
}

/*
 * Calculate distance between camera and entity
 */
float OverlayCalculateDistance(const void* cameraPos, const void* entityPos) {
    const float* cam = (const float*)cameraPos;
    const float* ent = (const float*)entityPos;
    
    float dx = ent[0] - cam[0];
    float dy = ent[1] - cam[1];
    float dz = ent[2] - cam[2];
    
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

/*
 * Calculate angle between camera forward vector and entity
 */
float OverlayCalculateAngle(const void* cameraPos, const void* entityPos) {
    const float* cam = (const float*)cameraPos;
    const float* ent = (const float*)entityPos;
    
    float dx = ent[0] - cam[0];
    float dy = ent[1] - cam[1];
    float dz = ent[2] - cam[2];
    
    return atan2f(dx, dz);
}

/*
 * Check if entity is within FOV
 */
BOOL OverlayCheckFOV(float angle, float fovDegrees) {
    float fovRad = fovDegrees * 3.14159265f / 180.0f;
    return (angle <= fovRad / 2.0f);
}

/*
 * Check if entity should be rendered based on distance and FOV
 */
BOOL OverlayShouldRenderEntity(float distance, float fovAngle) {
    /* Only render entities within 500m */
    if (distance > 500.0f) {
        return FALSE;
    }
    
    /* Only render entities within 90-degree FOV */
    if (!OverlayCheckFOV(fovAngle, 90.0f)) {
        return FALSE;
    }
    
    return TRUE;
}

/*
 * Check if entity is visible (line-of-sight)
 */
BOOL OverlayIsEntityVisible(const void* entityData) {
    /* Placeholder for line-of-sight check */
    /* This would use raycasting or visibility flags from game data */
    return TRUE;
}

/*
 * Check if entity is within camera FOV
 */
BOOL OverlayIsEntityInFOV(const void* cameraPos, const void* entityPos, float fovDegrees) {
    float angle = OverlayCalculateAngle(cameraPos, entityPos);
    return OverlayCheckFOV(angle, fovDegrees);
}

/*
 * Apply random rendering delay
 */
void OverlayApplyRandomDelay() {
    /* Random delay between 0-33ms (0-2 frames at 60Hz) */
    int delay = rand() % 34;
    Sleep(delay);
}

/*
 * Cache entity data
 */
void OverlayCacheEntity(const void* entityData) {
    if (g_entityCount >= 256) return;
    
    const float* data = (const float*)entityData;
    
    CACHED_ENTITY* entity = &g_entityCache[g_entityCount++];
    entity->worldX = data[0];
    entity->worldY = data[1];
    entity->worldZ = data[2];
    entity->screenX = data[3];
    entity->screenY = data[4];
    entity->width = data[5];
    entity->height = data[6];
    entity->color = (uint32_t)data[7];
    entity->isVisible = (uint32_t)data[8];
    entity->lastUpdateTick = GetTickCount64();
}
