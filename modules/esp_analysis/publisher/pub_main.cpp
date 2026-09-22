#ifdef MODULE_ESP

/*
 * hypersinv_pub: headless ESP -> HVO2 named-mapping publisher.
 *
 * Out-of-process consumer of the ESP analysis library. Talks to the
 * hypervisor companion only through the existing HVR1 authenticated
 * mailbox, so no code is ever loaded into, or poked inside, the game
 * process.
 *
 * Anti-detection posture (Arc Raiders: EAC + Denuvo):
 *   - no DLL injection, hooks, or game-image writes; all guest reads
 *     happen in VMX root via the companion device
 *   - console process: no windows, no sockets, no threads
 *   - exactly one named object: the HVO2 frame mapping
 *   - randomized frame pacing (jitter), no fixed heartbeat
 *   - no file or network activity after startup
 *
 * Usage:
 *   hypersinv_pub.exe --pid <GAME_PID> [--hwnd <HEX> | --title <SUBSTR>]
 *                     [--mapping "Global\NAME"] [--hz <1..60>] [--once]
 */

#include <windows.h>
#include <sddl.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <random>
#include <string>
#include <thread>

#include "esp_analysis.h"
#include "hv_bridge/hv_bridge.h"
#include "overlay_protocol.h"

/* Compat shims for legacy Hv* symbols referenced by library objects. */
extern "C" {
bool HvAnalysisStart(uint32_t) { return false; }
bool HvScanProcess(uint32_t, const void*, const void*, size_t, void**, size_t*) {
    return false;
}
bool HvReadGuestMemory(uint32_t pid, uint64_t address, void* buffer, size_t size) {
    if (buffer == nullptr || size == 0) return false;
    return HvAnalysisBackendRead(pid, address, static_cast<uint8_t*>(buffer),
                                 static_cast<uint32_t>(size));
}
bool HvWriteSharedMemory(const void* data, size_t size) {
    /* Legacy primitive; this process owns the real mapping. */
    if (data == nullptr || size == 0) return false;
    return true;
}
}

namespace {
using namespace hypersinv::esp;
namespace ov = hypersinv::overlay;

struct Options {
    uint32_t pid = 0;
    uint64_t hwnd = 0;
    std::wstring title;
    std::wstring mapping = L"Global\\HypersinvOverlayDataV2";
    uint32_t hz = 60;
    bool once = false;
};

bool ParseNumber(const wchar_t* text, uint64_t& value) {
    if (text == nullptr || text[0] == L'\0') return false;
    wchar_t* end = nullptr;
    const unsigned long long parsed = wcstoull(text, &end, 0);
    if (end == text || *end != L'\0') return false;
    value = static_cast<uint64_t>(parsed);
    return true;
}

void ShowUsage() {
    fprintf(stderr,
        "usage: hypersinv_pub.exe --pid <GAME_PID> "
        "[--hwnd <HEX> | --title <SUBSTR>] "
        "[--mapping \"Global\\NAME\"] [--hz <1..60>] [--once]\n");
}

bool ParseArgs(int argc, wchar_t** argv, Options& o) {
    if (argc < 3) { ShowUsage(); return false; }
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--help" || a == L"-h") { ShowUsage(); return false; }
        else if ((a == L"--pid" || a == L"--hwnd") && i + 1 < argc) {
            uint64_t v = 0;
            if (!ParseNumber(argv[++i], v)) return false;
            if (a == L"--pid") {
                if (v == 0 || v > MAXDWORD) return false;
                o.pid = static_cast<uint32_t>(v);
            } else {
                o.hwnd = v;
            }
        } else if ((a == L"--title" || a == L"--mapping") && i + 1 < argc) {
            if (a == L"--title") o.title = argv[++i];
            else o.mapping = argv[++i];
        } else if (a == L"--hz" && i + 1 < argc) {
            uint64_t v = 0;
            if (!ParseNumber(argv[++i], v) || v < 1 || v > 60) return false;
            o.hz = static_cast<uint32_t>(v);
        } else if (a == L"--once") {
            o.once = true;
        } else {
            return false;
        }
    }
    if (o.pid == 0) {
        fprintf(stderr, "error: --pid <GAME_PID> is required\n");
        return false;
    }
    return true;
}

HWND ResolveWindow(const Options& o) {
    if (o.hwnd != 0) {
        HWND h = reinterpret_cast<HWND>(static_cast<uintptr_t>(o.hwnd));
        return IsWindow(h) ? h : nullptr;
    }
    struct Search { uint32_t pid; std::wstring title; HWND found; };
    auto cb = [](HWND h, LPARAM p) -> BOOL {
        auto* s = reinterpret_cast<Search*>(p);
        if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER) != nullptr) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != s->pid) return TRUE;
        if (!s->title.empty()) {
            wchar_t title[512] = {};
            GetWindowTextW(h, title, ARRAYSIZE(title));
            std::wstring cand = title;
            std::wstring want = s->title;
            for (auto& c : cand) c = towlower(c);
            for (auto& w : want) w = towlower(w);
            if (cand.find(want) == std::wstring::npos) return TRUE;
        }
        s->found = h;
        return FALSE;
    };
    Search s { o.pid, o.title, nullptr };
    EnumWindows(cb, reinterpret_cast<LPARAM>(&s));
    return s.found;
}

volatile LONG g_stop = 0;
BOOL WINAPI CtrlHandler(DWORD) {
    InterlockedExchange(&g_stop, 1);
    return TRUE;
}

} // namespace

using namespace hypersinv::esp;
namespace ov = hypersinv::overlay;

int main() {
    Options opts{};
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return 2;
    if (!ParseArgs(argc, argv, opts)) {
        LocalFree(argv);
        return 2;
    }
    LocalFree(argv);

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    if (!HvBridgeOpen(nullptr)) {
        fprintf(stderr, "bridge: cannot open device (is the EFI companion "
                        "service running?)\n");
        return 1;
    }
    EspConfig cfg{};
    cfg.target_pid = opts.pid;
    if (!EspInitialize(&cfg)) {
        HvBridgeClose();
        fprintf(stderr, "esp: initialize failed (anchors may not match "
                        "the running game build)\n");
        return 1;
    }

    const HWND target = ResolveWindow(opts);
    float vw = 1920.0f, vh = 1080.0f;
    if (target != nullptr) {
        RECT r = {};
        if (GetClientRect(target, &r) && r.right > 0 && r.bottom > 0) {
            vw = static_cast<float>(r.right);
            vh = static_cast<float>(r.bottom);
        }
    } else {
        fprintf(stderr, "window: no game window found; using default "
                        "%.0fx%.0f projection viewport\n", vw, vh);
    }
    EspViewportSize(vw, vh);

    /* Exactly one named object: the HVO2 frame mapping. Global names get
       an authenticated-users-read DACL so an interactive overlay can map
       it read-only. */
    SECURITY_ATTRIBUTES sa {};
    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES* psa = nullptr;
    const bool isGlobal = opts.mapping.rfind(L"Global\\", 0) == 0;
    if (isGlobal) {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;;GA;;;SY)(A;;GR;;;AU)", SDDL_REVISION_1,
                &sd, nullptr)) {
            fprintf(stderr, "mapping: DACL conversion failed\n");
            EspShutdown();
            HvBridgeClose();
            return 1;
        }
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = sd;
        psa = &sa;
    }
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, psa, PAGE_READWRITE,
                                        0, sizeof(ov::AnalysisResult),
                                        opts.mapping.c_str());
    if (sd != nullptr) LocalFree(sd);
    ov::AnalysisResult* view = nullptr;
    if (mapping != nullptr) {
        view = static_cast<ov::AnalysisResult*>(MapViewOfFile(
            mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
            sizeof(ov::AnalysisResult)));
        if (view == nullptr) CloseHandle(mapping);
    }
    if (view == nullptr) {
        fprintf(stderr, "mapping: cannot create/open %ls\n", opts.mapping.c_str());
        EspShutdown();
        HvBridgeClose();
        return 1;
    }

    LARGE_INTEGER freq = {};
    QueryPerformanceFrequency(&freq);
    const std::int64_t minInterval =
        (freq.QuadPart + opts.hz - 1) / opts.hz;

    std::random_device rd;
    std::mt19937 rng(rd());
    std::uniform_int_distribution<std::int64_t> jitter(-15, 15);
    std::uniform_int_distribution<std::int64_t> pace(80, 160);

    LARGE_INTEGER last = {};
    LARGE_INTEGER stat = {};
    bool haveLast = false;
    bool warned = false;
    uint32_t frames = 0;

    fprintf(stderr, "pub: pid=%u mapping=%ls hz=%u once=%d\n",
            opts.pid, opts.mapping.c_str(), opts.hz, opts.once ? 1 : 0);

    for (;;) {
        if (g_stop) break;

        if (target != nullptr && !IsWindow(target)) {
            fprintf(stderr, "pub: game window closed\n");
            break;
        }
        if (target != nullptr) {
            RECT r = {};
            if (GetClientRect(target, &r) && r.right > 0 && r.bottom > 0) {
                if (vw != static_cast<float>(r.right) ||
                    vh != static_cast<float>(r.bottom)) {
                    vw = static_cast<float>(r.right);
                    vh = static_cast<float>(r.bottom);
                    EspViewportSize(vw, vh);
                }
            }
        }

        if (!EspUpdate()) {
            if (!warned) {
                fprintf(stderr, "pub: EspUpdate failed (offsets may be "
                                "stale for this game build)\n");
                warned = true;
            }
        }

        Entity ents[ov::kMaximumEntities] = {};
        size_t n = 0;
        EspGetEntities(ents, ov::kMaximumEntities, &n);

        ov::AnalysisResult frame {};
        auto& h = frame.Header;
        h.Magic = ov::kOverlayMagic;
        h.Version = ov::kOverlayVersion;
        h.HeaderSize = sizeof(ov::SharedOverlayHeader);
        h.EntityStride = sizeof(ov::EntityData);
        h.Capacity = static_cast<uint32_t>(ov::kMaximumEntities);
        h.EntityCount = static_cast<uint32_t>(n);
        h.Reserved0 = 0;
        LARGE_INTEGER qpc = {};
        QueryPerformanceCounter(&qpc);
        h.ProducerTimestampQpc = static_cast<uint64_t>(qpc.QuadPart);
        h.TargetWindow = target != nullptr
            ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(target)) : 0;
        h.ProducerProcessId = GetCurrentProcessId();
        h.SceneFlags = 0; /* screen-space entities; no VP in frame */
        h.ViewportWidth = vw;
        h.ViewportHeight = vh;
        for (size_t i = 0; i < n; ++i) {
            ov::EntityData d {};
            d.screenX = ents[i].screen_x;
            d.screenY = ents[i].screen_y;
            d.width = ents[i].width;
            d.height = ents[i].height;
            d.worldX = ents[i].world_x;
            d.worldY = ents[i].world_y;
            d.worldZ = ents[i].world_z;
            d.coordinateSpace = 0; /* screen */
            memcpy(&d.name, ents[i].name, sizeof(d.name));
            d.color = ents[i].color;
            d.isVisible = ents[i].is_visible;
            frame.Entries[i] = d;
        }
        if (!ov::IsHeaderValid(frame.Header)) continue;

        LARGE_INTEGER now = {};
        QueryPerformanceCounter(&now);
        if (haveLast && now.QuadPart - last.QuadPart < minInterval) {
            Sleep(1);
            continue;
        }
        last = now;
        haveLast = true;

        /* seqlock publish: odd = writing, even = stable. */
        volatile LONG64* seq = reinterpret_cast<volatile LONG64*>(&view->Header.Sequence);
        const LONG64 observed =
            static_cast<LONG64>(InterlockedCompareExchange64(seq, 0, 0));
        const LONG64 stable = (observed & 1) != 0 ? observed + 1 : observed;
        InterlockedExchange64(seq, stable + 1);
        MemoryBarrier();
        constexpr size_t seqOff = offsetof(ov::SharedOverlayHeader, Sequence);
        memcpy(&view->Header, &frame.Header, seqOff);
        memcpy(reinterpret_cast<uint8_t*>(&view->Header) + seqOff + 8,
               reinterpret_cast<const uint8_t*>(&frame.Header) + seqOff + 8,
               sizeof(ov::SharedOverlayHeader) - seqOff - 8);
        memcpy(view->Entries, frame.Entries,
               h.EntityCount * sizeof(ov::EntityData));
        if (h.EntityCount < ov::kMaximumEntities) {
            memset(view->Entries + h.EntityCount, 0,
                   (ov::kMaximumEntities - h.EntityCount) *
                       sizeof(ov::EntityData));
        }
        MemoryBarrier();
        InterlockedExchange64(seq, stable + 2);

        ++frames;
        LARGE_INTEGER stnow = {};
        QueryPerformanceCounter(&stnow);
        if (!stat.QuadPart) stat = stnow;
        if (stnow.QuadPart - stat.QuadPart >= freq.QuadPart / 2) {
            stat = stnow;
            fprintf(stderr, "pub: frame=%u entities=%zu\n", frames, n);
        }

        if (opts.once) break;

        /* Randomized pacing: interval +/- jitter plus small noise. */
        const std::int64_t interval =
            (minInterval * (100 + jitter(rng))) / 100;
        const DWORD ms = static_cast<DWORD>(
            interval / freq.QuadPart * 1000 + pace(rng));
        Sleep(ms < 1 ? 1 : ms);
    }

    fprintf(stderr, "pub: shutdown (frames=%u)\n", frames);
    if (view != nullptr) UnmapViewOfFile(view);
    if (mapping != nullptr) CloseHandle(mapping);
    EspShutdown();
    HvBridgeClose();
    return 0;
}

#endif // MODULE_ESP
