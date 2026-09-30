#include "pch.h"
#include "core/mod.h"
#include "core/logger.h"
#include <process.h>
#include <cameraunlock/os/game_window.h>

static HANDLE g_initThreadHandle = nullptr;

namespace {

void CenterGameWindow() {
    for (int i = 0; i < 100; ++i) {
        if (cameraunlock::os::FindGameWindow()) break;
        Sleep(100);
    }

    cameraunlock::os::CenterGameWindowOnce([](cameraunlock::os::WindowLogLevel level, const char* message) {
        auto& logger = SkyrimHT::Logger::Instance();
        if (level == cameraunlock::os::WindowLogLevel::Warning) {
            logger.Warning("%s", message);
        } else {
            logger.Info("%s", message);
        }
    });
}

} // namespace

unsigned __stdcall InitThread(void* lpParam) {
    (void)lpParam;

    // Wait for game executable to be loaded
    int waitAttempts = 0;
    constexpr int maxWaitAttempts = 100; // 10 seconds max
    while (!GetModuleHandleA(SkyrimHT::GAME_EXE)) {
        Sleep(100);
        waitAttempts++;
        if (waitAttempts >= maxWaitAttempts) {
            SkyrimHT::Logger::Instance().Initialize();
            SkyrimHT::Logger::Instance().Error(
                "%s was not loaded after %d ms - this process is not Skyrim SE, or the "
                "executable has been renamed. Head tracking will not start.",
                SkyrimHT::GAME_EXE, maxWaitAttempts * 100);
            SkyrimHT::Logger::Instance().Shutdown();
            return 1;
        }
    }

    // Game module found - start logging
    SkyrimHT::Logger::Instance().Initialize();
    SkyrimHT::Logger::Instance().Info("Skyrim SE Head Tracking v%s attached to game process", SkyrimHT::VERSION);

    // Additional delay for game initialization
    Sleep(2000);

    CenterGameWindow();

    // Initialize the mod
    if (!SkyrimHT::Mod::Instance().Initialize()) {
        SkyrimHT::Logger::Instance().Error("Mod initialization failed");
        return 1;
    }

    SkyrimHT::Logger::Instance().Info("Skyrim SE Head Tracking v%s loaded successfully", SkyrimHT::VERSION);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)lpReserved;

    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            g_initThreadHandle = (HANDLE)_beginthreadex(nullptr, 0, InitThread, nullptr, 0, nullptr);
            break;

        case DLL_PROCESS_DETACH:
            // We're holding the loader lock here. Joining threads (init thread,
            // input polling thread) or running MinHook teardown under the lock
            // can deadlock if any of them touches LoadLibrary/GetModuleHandle,
            // and is pointless on process teardown because the OS will reclaim
            // everything. Only close the init-thread handle (non-blocking) and
            // flush the log so it lands on disk.
            if (g_initThreadHandle) {
                CloseHandle(g_initThreadHandle);
                g_initThreadHandle = nullptr;
            }
            SkyrimHT::Logger::Instance().Shutdown();
            break;
    }
    return TRUE;
}
