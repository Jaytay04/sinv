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
}


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
    uint64_t camera_manager = 0;
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
        {(float)rm[0][0], (float)rm[1][0], (float)rm[2][0], -(float)(rm[0][0]*loc[0]+rm[1][0]*loc[1]+rm[2][0]*loc[2])},
        {(float)rm[0][1], (float)rm[1][1], (float)rm[2][1], -(float)(rm[0][1]*loc[0]+rm[1][1]*loc[1]+rm[2][1]*loc[2])},
        {(float)rm[0][2], (float)rm[1][2], (float)rm[2][2], -(float)(rm[0][2]*loc[0]+rm[1][2]*loc[1]+rm[2][2]*loc[2])},
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
}; // anon

bool EspInitialize(const EspConfig* config) {
    if (config == nullptr || config->target_pid == 0) {
        return false;
    }
    if (!HvBridgeOpen(nullptr)) {
        return false;
    }
    if (!HvAnalysisBackendStart(config->target_pid)) {
        HvBridgeClose();
        return false;
    }
    g_session.pid = config->target_pid;
    /* UWorld from its known RVA. */
    g_session.uworld = arc_raiders::kWORLD_RVA;
    /* Walk: UWorld -> GameInstance -> LocalPlayers -> PlayerController. */
    uint64_t game_instance = 0;
    if (!ReadPtr(g_session.pid, g_session.uworld + arc_raiders::kGAME_WORLD_ALT, &game_instance) || game_instance == 0) {
        HvAnalysisBackendStop(g_session.pid);
        HvBridgeClose();
        return false;
    }
    uint64_t players_data = 0;
    int32_t players_count = 0;
    if (!ReadArray(g_session.pid, game_instance + arc_raiders::kGI_LOCAL_PLAYERS, &players_data, &players_count, 8) ||
        players_data == 0) {
        HvAnalysisBackendStop(g_session.pid);
        HvBridgeClose();
        return false;
    }
    uint64_t local_player = 0;
    if (!ReadPtr(g_session.pid, players_data, &local_player) || local_player == 0) {
        HvAnalysisBackendStop(g_session.pid);
        HvBridgeClose();
        return false;
    }
    if (!ReadPtr(g_session.pid, local_player + arc_raiders::kGI_PLAYER_CONTROLLER, &g_session.player_controller) ||
        g_session.player_controller == 0) {
        HvAnalysisBackendStop(g_session.pid);
        HvBridgeClose();
        return false;
    }
    /* PCM is live-pinned (per the post, +0x4E0 is CheatClass until re-scanned). */
    g_session.camera_manager = g_session.player_controller + arc_raiders::kPC_PLAYER_CAMERA_MGR;
    /* Prime the view-projection with a neutral camera so ESP boxes still project. */
    double zero_loc[3] = {};
    float zero_rot[3] = {};
    BuildVP(zero_loc, zero_rot, 90.0f, g_session.viewport_w, g_session.viewport_h, g_session.vp);
    EntityCacheInitialize();
    g_session.active = true;
    return true;
}

bool EspUpdate() {
    if (!g_session.active || g_session.pid == 0) {
        return false;
    }
    /* Camera: PCM +0xCA0 FVector3d, +0xCC0 FRotator, +0xCE8 FOV. */
    if (g_session.camera_manager != 0) {
        double loc[3] = {};
        float rot[3] = {};
        float fov = 0.0f;
        const bool ok_loc = ReadBlob(g_session.pid, g_session.camera_manager + arc_raiders::kPCM_CAMERA_LOCATION, loc, sizeof(loc));
        const bool ok_rot = ok_loc && ReadBlob(g_session.pid, g_session.camera_manager + arc_raiders::kPCM_CAMERA_ROTATION, rot, sizeof(rot));
        const bool ok_fov = ok_rot && ReadBlob(g_session.pid, g_session.camera_manager + arc_raiders::kPCM_CAMERA_FOV, &fov, sizeof(fov));
        if (ok_loc && ok_rot && ok_fov) {
            memcpy(g_session.cam_loc, loc, sizeof(loc));
            memcpy(g_session.cam_rot, rot, sizeof(rot));
            if (fov > 0.0f && fov < 180.0f) {
                g_session.fov = fov;
            }
            /* Recompute VP with the live camera. */
            BuildVP(g_session.cam_loc, g_session.cam_rot, g_session.fov,
                    g_session.viewport_w, g_session.viewport_h, g_session.vp);
            g_session.camera_ok = true;
        }
    }
    /* Read UWorld -> PersistentLevel. */
    uint64_t level = 0;
    if (!ReadPtr(g_session.pid, g_session.uworld + arc_raiders::kPERSISTENT_LEVEL, &level) || level == 0) {
        return false;
    }
    /* PersistentLevel +0x98 = TArray<AActor*> (classic). */
    uint64_t actors_data = 0;
    int32_t actors_count = 0;
    if (!ReadArray(g_session.pid, level + arc_raiders::kLEVEL_Actors, &actors_data, &actors_count, arc_raiders::kMAX_ACTORS_PER_FRAME) ||
        actors_data == 0) {
        return false;
    }
    EntityCacheClear();
    for (int32_t i = 0; i < actors_count; ++i) {
        uint64_t actor = 0;
        if (!ReadPtr(g_session.pid, actors_data + static_cast<size_t>(i) * sizeof(uint64_t), &actor) || actor == 0) {
            continue;
        }
        /* AActor +0x250 = RootComponent. */
        uint64_t root = 0;
        if (!ReadPtr(g_session.pid, actor + arc_raiders::kACTOR_ROOT_COMPONENT, &root) || root == 0) {
            continue;
        }
        /* FTransform (96B): +0x2E0, translation at +0x20 (double x,y,z). */
        double xform[arc_raiders::kTRANSFORM_SIZE / sizeof(double)] = {};
        if (!ReadBlob(g_session.pid, root + arc_raiders::kCOMPONENT_TO_WORLD, xform, sizeof(xform))) {
            continue;
        }
        /* Entity slot */
        Entity e = {};
        e.world_x = static_cast<float>(xform[4]); /* Trans.X */
        e.world_y = static_cast<float>(xform[5]); /* Trans.Y */
        e.world_z = static_cast<float>(xform[6]); /* Trans.Z */
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
