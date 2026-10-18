#ifdef MODULE_ESP
#include "esp_analysis.h"
#include "arc_raiders/offsets.h"
#include "hv_bridge/hv_bridge.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <windows.h>

/* Forward decl: world-to-screen (worldtoscreen.cpp). */
namespace hypersinv::esp::arc_raiders {
bool ProjectBoundingBox(float wx, float wy, float wz, float w, float h, float d, const float* vp, float vw, float vh, float* sx, float* sy, float* sw, float* sh);
};


#include "../../components/hypervisor/src/efi/runtime_analysis_abi.h"

namespace hypersinv::esp {
using namespace hypersinv::esp::arc_raiders;

/* Entity cache (entity_cache.cpp). */
void EntityCacheInitialize();
void EntityCacheClear();
bool EntityCacheAdd(const Entity* entity);
void EntityCacheGet(Entity* out_buffer, size_t max_count, size_t* out_count);
void EntityCacheShutdown();

namespace {
constexpr size_t kChunk = HV_EFI_ANALYSIS_MAX_STRING_BYTES;
bool ReadBlob(uint32_t pid, uint64_t address, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (size > 0) {
        const size_t n = size < kChunk ? size : kChunk;
        if (!HvAnalysisBackendRead(pid, address, out, static_cast<uint32_t>(n))) {
            return false;
        }
        out += n;
        address += n;
        size -= n;
    }
    return true;
}
bool ReadPtr(uint32_t pid, uint64_t address, uint64_t* out) {
    return ReadBlob(pid, address, out, sizeof(uint64_t));
}
bool ReadArray(uint32_t pid, uint64_t address, uint64_t* data, int32_t* count, size_t cap) {
    struct TArray {
        uint64_t Data;
        int32_t Count;
        int32_t Max;
    };
    TArray a{};
    if (!ReadBlob(pid, address, &a, sizeof(a))) {
        return false;
    }
    if (a.Data == 0 || a.Count <= 0 || a.Count > a.Max ||
        a.Count > static_cast<int32_t>(cap)) {
        return false;
    }
    *data = a.Data;
    *count = a.Count;
    return true;
}
}; // anon helpers
namespace {
struct EspSession {
    bool active = false;
    uint32_t pid = 0;
    uint64_t uworld = 0;
    uint64_t player_controller = 0;
    double cam_loc[3] = {};
    float cam_rot[3] = {};
    float fov = 90.0f;
    float vp[16] = {};
    float viewport_w = 1920.0f;
    float viewport_h = 1080.0f;
    bool camera_ok = false;
};
static EspSession g_session;
}

namespace {
float Rad(float d) {
    return d * static_cast<float>(3.14159265358979323846) / 180.0f;
}
void BuildVP(const double loc[3], const float rot[3], float fov, float vw, float vh, float vp[16]) {
    memset(vp, 0, sizeof(float) * 16);
    const double p = Rad(rot[0]) * (double)1.0;
    const double y = Rad(rot[1]) * (double)1.0;
    const double r = Rad(rot[2]) * (double)1.0;
    const double sp = sin(p), cp = cos(p);
    const double sy = sin(y), cy = cos(y);
    const double sr = sin(r), cr = cos(r);
    const double rm[3][3] = {
        {cp * cy, cp * sy, sp},
        {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
        {cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp}
    };
    const float vm[4][4] = {
        {(float)rm[0][0], (float)rm[1][0], (float)rm[2][0], -(float)(rm[0][0]*loc[0] + rm[1][0]*loc[1] + rm[2][0]*loc[2])},
        {(float)rm[0][1], (float)rm[1][1], (float)rm[2][1], -(float)(rm[0][1]*loc[0] + rm[1][1]*loc[1] + rm[2][1]*loc[2])},
        {(float)rm[0][2], (float)rm[1][2], (float)rm[2][2], -(float)(rm[0][2]*loc[0] + rm[1][2]*loc[1] + rm[2][2]*loc[2])},
        {0.0f, 0.0f, 0.0f, 1.0f}
    };
    const double fov_rad = Rad(fov);
    const double aspect = vw / vh;
    const double th = tan(fov_rad / 2.0);
    const float pm[4][4] = {
        {1.0f / (float)(aspect * th), 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f / (float)th, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double acc = 0.0;
            for (int k = 0; k < 4; ++k) {
                acc += vm[i][k] * pm[k][j];
            }
            vp[i * 4 + j] = static_cast<float>(acc);
        }
    }
}
};

bool EspInitialize(const EspConfig* config) {
    if (config == nullptr || config->target_pid == 0) {
        return false;
    }
    
    g_session.pid = config->target_pid;
    g_session.uworld = config->uworld_signature;
    
    /* Open the HV bridge before starting the analysis backend. */
    HvBridgeOpen(nullptr);
    
    /* Start the HV analysis session */
    if (!HvAnalysisBackendStart(g_session.pid)) {
        HvBridgeClose();
        return false;
    }
    
    /* Read persistent level to get actor array */
    uint64_t pl = 0;
    if (g_session.uworld != 0) {
        if (!ReadPtr(g_session.pid, g_session.uworld + kPERSISTENT_LEVEL, &pl)) {
            HvBridgeClose();
            return false;
        }
    }
    
    if (pl == 0) {
        HvBridgeClose();
        return false;
    }
    
    /* Read actor array from persistent level */
    uint64_t actors_data = 0;
    int32_t actors_count = 0;
    if (!ReadArray(g_session.pid, pl + kACTOR_ARRAY, &actors_data, &actors_count, kMAX_ACTORS_PER_FRAME)) {
        HvBridgeClose();
        return false;
    }
    
    /* Read player controller chain */
    uint64_t gi = 0;
    if (g_session.uworld != 0) {
        if (!ReadPtr(g_session.pid, g_session.uworld + kUWORLD_GAME_INSTANCE, &gi)) {
            HvBridgeClose();
            return false;
        }
    }
    if (gi != 0) {
        uint64_t lp = 0;
        if (!ReadPtr(g_session.pid, gi + kGI_LOCAL_PLAYERS, &lp)) {
            HvBridgeClose();
            return false;
        }
        if (lp != 0) {
            if (!ReadPtr(g_session.pid, lp + kLP_PLAYER_CONTROLLER, &g_session.player_controller)) {
                HvBridgeClose();
                return false;
            }
        }
    }
    

    /* Prime the view-projection with a neutral camera so ESP boxes still project. */
    double zero_loc[3] = {};
    float zero_rot[3] = {};
    BuildVP(zero_loc, zero_rot, 90.0f, g_session.viewport_w, g_session.viewport_h, g_session.vp);
    EntityCacheInitialize();
    
    g_session.active = true;
    return true;
}

bool EspUpdate() {
    if (!g_session.active) return false;
    
    /* Read camera position and rotation via CL-1426848 chain:
     * player_controller + kVIEW_TARGET (0x4C0) -> view_target
     * view_target + 0x10 -> FMinimalViewInfo
     *   Location @ +0x08 (FVector3d), Rotation @ +0x28 (FRotator), FOV @ +0x48 (float) */
    if (g_session.player_controller != 0) {
        uint64_t view_target = 0;
        if (ReadPtr(g_session.pid, g_session.player_controller + arc_raiders::kVIEW_TARGET, &view_target) && view_target != 0) {
            double cameraLocation[3] = {};
            if (HvAnalysisBackendRead(g_session.pid,
                                     view_target + 0x10 + 0x08,
                                     reinterpret_cast<uint8_t*>(cameraLocation), sizeof(cameraLocation))) {
                g_session.cam_loc[0] = cameraLocation[0];
                g_session.cam_loc[1] = cameraLocation[1];
                g_session.cam_loc[2] = cameraLocation[2];
            }
            
            float cameraRotation[3] = {0};
            if (HvAnalysisBackendRead(g_session.pid,
                                     view_target + 0x10 + 0x28,
                                     reinterpret_cast<uint8_t*>(cameraRotation), sizeof(cameraRotation))) {
                g_session.cam_rot[0] = cameraRotation[0];
                g_session.cam_rot[1] = cameraRotation[1];
                g_session.cam_rot[2] = cameraRotation[2];
            }
            
            float fovVal = 0.0f;
            if (HvAnalysisBackendRead(g_session.pid,
                                     view_target + 0x10 + 0x48,
                                     reinterpret_cast<uint8_t*>(&fovVal), sizeof(fovVal))) {
                g_session.fov = fovVal;
            }
            
            g_session.camera_ok = true;
            BuildVP(g_session.cam_loc, g_session.cam_rot, g_session.fov,
                    g_session.viewport_w, g_session.viewport_h, g_session.vp);
        }
    }
    
    /* Read persistent level for actor array */
    uint64_t pl = 0;
    if (g_session.uworld != 0) {
        ReadPtr(g_session.pid, g_session.uworld + kPERSISTENT_LEVEL, &pl);
    }
    
    if (pl == 0) return false;
    
    uint64_t actors_data = 0;
    int32_t actors_count = 0;
    if (!ReadArray(g_session.pid, pl + kACTOR_ARRAY, &actors_data, &actors_count, kMAX_ACTORS_PER_FRAME)) {
        return false;
    }
    
    EntityCacheClear();
    for (int32_t i = 0; i < actors_count; ++i) {
        uint64_t actor = 0;
        if (!ReadPtr(g_session.pid, actors_data + static_cast<size_t>(i) * sizeof(uint64_t), &actor) || actor == 0) {
            continue;
        }
        /* AActor +0x240 = RootComponent (CL-1426848). */
        uint64_t root = 0;
        if (!ReadPtr(g_session.pid, actor + arc_raiders::kACTOR_ROOT_COMPONENT, &root) || root == 0) {
            continue;
        }
        /* CL-1426848: world position = translation FVector3d @0x340 (rotation quat @0x320). */
        double trans[3] = {};
        if (!ReadBlob(g_session.pid, root + arc_raiders::kCOMPONENT_C2W_TRANSLATION, trans, sizeof(trans))) {
            continue;
        }
        /* Entity slot */
        Entity e = {};
        e.world_x = static_cast<float>(trans[0]); /* X */
        e.world_y = static_cast<float>(trans[1]); /* Y */
        e.world_z = static_cast<float>(trans[2]); /* Z */
        e.coordinate_space = 1; /* world */
        e.is_visible = 1;
        e.color = arc_raiders::kCOLOR_AI; /* default */
        /* TODO: read actor name + classify (player/AI/vehicle/loot). */
        swprintf_s(e.name, 32, L"actor_%d", i);
        /* Bounding box: 1m x 1.8m x 1m centred on the root component. */
        float sx = 0, sy = 0, sw = 0, sh = 0;
        if (ProjectBoundingBox(e.world_x, e.world_y, e.world_z, 1.0f, 1.8f, 1.0f, g_session.vp, g_session.viewport_w, g_session.viewport_h, &sx, &sy, &sw, &sh)) {
            e.screen_x = sx;
            e.screen_y = sy;
            e.width = sw;
            e.height = sh;
        } else {
            e.is_visible = 0;
        }
        EntityCacheAdd(&e);
    }
    return true;
}

void EspViewportSize(float vw, float vh) {
    if (!g_session.active || vw <= 0.0f || vh <= 0.0f) {
        return;
    }
    if (g_session.viewport_w != vw || g_session.viewport_h != vh) {
        g_session.viewport_w = vw;
        g_session.viewport_h = vh;
        if (g_session.camera_ok) {
            BuildVP(g_session.cam_loc, g_session.cam_rot, g_session.fov, vw, vh,
                    g_session.vp);
        } else {
            /* Keep the neutral-camera fallback matrix in sync. */
            double zero_loc[3] = {};
            float zero_rot[3] = {};
            BuildVP(zero_loc, zero_rot, 90.0f, vw, vh, g_session.vp);
        }
    }
}

void EspGetEntities(Entity* out_buffer, size_t max_count, size_t* out_count) {
    if (out_count != nullptr) {
        *out_count = 0;
    }
    if (out_buffer == nullptr || max_count == 0 || !g_session.active) {
        return;
    }
    EntityCacheGet(out_buffer, max_count, out_count);
}

void EspShutdown() {
    if (!g_session.active) {
        return;
    }
    (void)HvAnalysisBackendStop(g_session.pid);
    HvBridgeClose();
    EntityCacheShutdown();
    memset(&g_session, 0, sizeof(g_session));
}

}; /* hypersinv::esp */

#endif // MODULE_ESP
