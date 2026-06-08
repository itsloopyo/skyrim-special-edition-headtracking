#include "pch.h"
#include "sse_addresses.h"

#include "core/logger.h"

#include <Windows.h>
#include <cstdio>

#pragma comment(lib, "Version.lib")

namespace SkyrimHT::SSEAddresses {

namespace {

// Append-only build profile: one row per shipped SkyrimSE build. Match is by
// the build number (parsed from the EXE's StringFileInfo "FileVersion"); RVAs
// are added to the loaded module base at runtime.
//
// Adding a new build (Bethesda patch, or a downgrade users run): add a NEW row,
// never edit an existing one - users on the old build keep matching their row,
// users on the new build match the new one, both from the same binary. Derive
// the RVAs from headless Ghidra on that build's SkyrimSE.exe (the two functions
// below), or from SKSE's Address Library .bin for that build.
struct BuildProfile {
    int major, minor, patch, build;
    uintptr_t updatePickData;     // CrosshairPickData raycast (crosshair override)
    uintptr_t projectileLaunch;   // Projectile::Launch(handle, LaunchData&)
};

constexpr BuildProfile kSupported[] = {
    // AE 1.6.1170
    { 1, 6, 1170, 0, 0x402C60, 0x7E46C0 },
};

uintptr_t g_updatePickData = 0;
uintptr_t g_projectileLaunch = 0;
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

    // Bethesda ships the real build number ONLY in the StringFileInfo
    // "FileVersion" string ("1.5.97.0", "1.6.1170.0"). The binary
    // VS_FIXEDFILEINFO numeric fields read 1.0.0.0 on the 1.5.97 (SE) build, so
    // we must parse the string - not the fixed-info numbers - to tell builds
    // apart. The string version is also the key SKSE's Address Library uses.
    struct LangCodepage { WORD lang; WORD codepage; };
    LangCodepage* trans = nullptr;
    UINT transLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\VarFileInfo\\Translation",
                        reinterpret_cast<LPVOID*>(&trans), &transLen) ||
        !trans || transLen < sizeof(LangCodepage)) {
        return false;
    }

    wchar_t subBlock[64];
    swprintf_s(subBlock, L"\\StringFileInfo\\%04x%04x\\FileVersion",
               trans->lang, trans->codepage);

    wchar_t* verStr = nullptr;
    UINT verLen = 0;
    if (!VerQueryValueW(buf.data(), subBlock,
                        reinterpret_cast<LPVOID*>(&verStr), &verLen) ||
        !verStr) {
        return false;
    }

    out.major = out.minor = out.patch = out.build = 0;
    return swscanf_s(verStr, L"%d.%d.%d.%d",
                     &out.major, &out.minor, &out.patch, &out.build) >= 3;
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

    const BuildProfile* match = nullptr;
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
            "Add a profile in sse_addresses.cpp with RVAs for this build.",
            v.major, v.minor, v.patch, v.build);
        return false;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    g_updatePickData    = base + match->updatePickData;
    g_projectileLaunch  = base + match->projectileLaunch;
    g_initialized = true;

    Logger::Instance().Info(
        "SSEAddresses: base=0x%llX UpdatePickData=0x%llX ProjectileLaunch=0x%llX",
        (unsigned long long)base,
        (unsigned long long)g_updatePickData,
        (unsigned long long)g_projectileLaunch);

    return true;
}

bool IsInitialized() { return g_initialized; }

uintptr_t UpdatePickData_Function()    { return g_updatePickData; }
uintptr_t ProjectileLaunch_Function()  { return g_projectileLaunch; }

} // namespace SkyrimHT::SSEAddresses
