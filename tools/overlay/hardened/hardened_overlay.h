#pragma once

/*
 * Hardened Overlay Implementation
 * 
 * Removes all identifiable strings, strips imports,
 * and implements anti-scan techniques.
 */

#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>

/* Anti-scan: dynamic import resolution */
typedef HRESULT (WINAPI *D3D11CREATEDEVICE_FUNC)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**
);

typedef HRESULT (WINAPI *D2D1CREATEFACTORY_FUNC)(
    D2D1_FACTORY_TYPE, REFIID, const D2D1_FACTORY_OPTIONS*, IUnknown**
);

typedef HRESULT (WINAPI *DWRITECREATEFACTORY_FUNC)(
    DWRITE_FACTORY_TYPE, REFIID, IUnknown**
);

/* Obfuscated window class name */
#define WINDOW_CLASS_CHARS 16
extern WCHAR g_obfuscatedClassName[WINDOW_CLASS_CHARS];

/* Anti-scan: strip debug info and imports */
void OverlayStripDebugInfo();

/* Anti-scan: resolve imports dynamically */
BOOL OverlayResolveImports();

/* Anti-scan: randomize window properties */
void OverlayRandomizeWindow(HWND hwnd);

/* Anti-scan: hide overlay from EnumWindows */
void OverlayHideFromEnum(HWND hwnd);

/* Anti-scan: remove overlay from taskbar/Alt-Tab */
void OverlayRemoveFromTaskbar(HWND hwnd);

/* Anti-scan: implement click-through behavior */
void OverlayEnableClickThrough(HWND hwnd);

/* Anti-scan: randomize rendering timing */
void OverlayRandomizeTiming();

/* Anti-scan: implement distance-based rendering */
BOOL OverlayShouldRenderEntity(float distance, float fovAngle);

/* Anti-scan: implement visibility checks */
BOOL OverlayIsEntityVisible(const void* entityData);

/* Anti-scan: implement FOV cone checks */
BOOL OverlayIsEntityInFOV(const void* cameraPos, const void* entityPos, float fovDegrees);

/* Anti-scan: implement random rendering delays */
void OverlayApplyRandomDelay();

/* Anti-scan: implement entity caching */
void OverlayCacheEntity(const void* entityData);

/* Anti-scan: implement distance filtering */
float OverlayCalculateDistance(const void* cameraPos, const void* entityPos);

/* Anti-scan: implement angle calculation */
float OverlayCalculateAngle(const void* cameraPos, const void* entityPos);

/* Anti-scan: implement FOV check */
BOOL OverlayCheckFOV(float angle, float fovDegrees);
