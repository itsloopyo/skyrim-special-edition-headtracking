#include "pch.h"
#include "sse_addresses.h"

#include "core/logger.h"

#include <Windows.h>

#pragma comment(lib, "Version.lib")

namespace SkyrimHT::SSEAddresses {

namespace {

struct VersionRVAs {
    int major, minor, patch, build;
    uintptr_t updatePickData;     // ID 39535 - CrosshairPickData raycast
    uintptr_t projectileLaunch;   // ID 44108 - Projectile::Launch(handle, LaunchData&)
    uintptr_t scaleformArraySet;   // GFx array SetElement helper
    uintptr_t scaleformInvoke;    // GFx method invoke helper
    uintptr_t compassUpdate;       // Compass::Update
};

// Adding a new SkyrimSE build:
//
// Each RVA below corresponds to a CommonLibSSE-NG RELOCATION_ID. To find the
// RVA for a different game build (e.g. when Bethesda ships a patch):
//
//   1. Install SKSE's Address Library mod for the new build - this drops
//      a Data\SKSE\Plugins\versionlib-<MAJ>-<MIN>-<PATCH>-<BUILD>.bin file.
//   2. Use a dumper (the address-library-dumper utility, or any of the
//      RVA-lookup tools on Nexus) to look up the IDs noted above.
//   3. Add a new row here with the resolved RVAs. No code changes needed
//      elsewhere - the version match in Initialize() picks the right row.
//
// We embed the RVAs at build time rather than parse the .bin at runtime so
// users don't need to install Address Library themselves. The trade-off is
// that supporting a new game build is a deliberate dev action, which is
// already the case for this project (we hard-fail on unsupported versions).
constexpr VersionRVAs kSupported[] = {
    { 1, 6, 1170, 0, 0x402C60, 0x7E46C0, 0xFADFC0, 0xFAC020, 0x9222C0 },
};

uintptr_t g_updatePickData = 0;
uintptr_t g_projectileLaunch = 0;
uintptr_t g_scaleformArraySet = 0;
uintptr_t g_scaleformInvoke = 0;
uintptr_t g_compassUpdate = 0;
bool g_initialized = false;

} // namespace

bool GetRunningExeVersion(ExeVersion& out) {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return false;

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(exePath, &handle);
    if (size == 0) return false;

    std::vector<uint8_t> buf(size);
    if (!GetFileVersionInfoW(exePath, handle, size, buf.data())) return false;

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoLen)) return false;
    if (!info) return false;

    out.major = HIWORD(info->dwFileVersionMS);
    out.minor = LOWORD(info->dwFileVersionMS);
    out.patch = HIWORD(info->dwFileVersionLS);
    out.build = LOWORD(info->dwFileVersionLS);
    return true;
}

bool Initialize() {
    if (g_initialized) return true;

    ExeVersion v{};
    if (!GetRunningExeVersion(v)) {
        Logger::Instance().Error("SSEAddresses: failed to read SkyrimSE.exe version");
        return false;
    }
    Logger::Instance().Info("SSEAddresses: SkyrimSE.exe version %d.%d.%d.%d",
        v.major, v.minor, v.patch, v.build);

    const VersionRVAs* match = nullptr;
    for (const auto& row : kSupported) {
        if (row.major == v.major && row.minor == v.minor &&
            row.patch == v.patch && row.build == v.build) {
            match = &row;
            break;
        }
    }
    if (!match) {
        Logger::Instance().Error(
            "SSEAddresses: unsupported game build %d.%d.%d.%d. "
            "Update kSupported in sse_addresses.cpp with RVAs for this build.",
            v.major, v.minor, v.patch, v.build);
        return false;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_updatePickData    = base + match->updatePickData;
    g_projectileLaunch  = base + match->projectileLaunch;
    g_scaleformArraySet = base + match->scaleformArraySet;
    g_scaleformInvoke   = base + match->scaleformInvoke;
    g_compassUpdate     = base + match->compassUpdate;
    g_initialized = true;

    Logger::Instance().Info(
        "SSEAddresses: base=0x%llX UpdatePickData=0x%llX ProjectileLaunch=0x%llX ScaleformArraySet=0x%llX ScaleformInvoke=0x%llX CompassUpdate=0x%llX",
        (unsigned long long)base,
        (unsigned long long)g_updatePickData,
        (unsigned long long)g_projectileLaunch,
        (unsigned long long)g_scaleformArraySet,
        (unsigned long long)g_scaleformInvoke,
        (unsigned long long)g_compassUpdate);

    return true;
}

bool IsInitialized() { return g_initialized; }

uintptr_t UpdatePickData_Function()    { return g_updatePickData; }
uintptr_t ProjectileLaunch_Function()  { return g_projectileLaunch; }
uintptr_t ScaleformArraySetElement_Function() { return g_scaleformArraySet; }
uintptr_t ScaleformInvoke_Function() { return g_scaleformInvoke; }
uintptr_t CompassUpdate_Function() { return g_compassUpdate; }

} // namespace SkyrimHT::SSEAddresses
