#include "pch.h"
#include "core/mod.h"
#include "core/logger.h"
#include <process.h>

static HANDLE g_initThreadHandle = nullptr;

namespace {

struct EnumCtx {
    DWORD pid;
    HWND  hwnd;
};

BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    RECT r;
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    if ((r.right - r.left) < 200 || (r.bottom - r.top) < 200) return TRUE;
    ctx->hwnd = hwnd;
    return FALSE;
}

void CenterGameWindow() {
    EnumCtx ctx{ GetCurrentProcessId(), nullptr };

    for (int i = 0; i < 100 && !ctx.hwnd; ++i) {
        EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.hwnd) break;
        Sleep(100);
    }

    if (!ctx.hwnd) {
        SkyrimHT::Logger::Instance().Warning("CenterGameWindow: game window not found");
        return;
    }

    LONG style = GetWindowLongA(ctx.hwnd, GWL_STYLE);
    // Fullscreen-borderless / exclusive fullscreen: no caption, no thick frame.
    // Leave those alone; only re-center true windowed mode.
    const bool isWindowed = (style & WS_CAPTION) != 0;

    if (isWindowed) {
        RECT wr;
        if (GetWindowRect(ctx.hwnd, &wr)) {
            int w = wr.right - wr.left;
            int h = wr.bottom - wr.top;

            HMONITOR mon = MonitorFromWindow(ctx.hwnd, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi{ sizeof(mi) };
            if (GetMonitorInfoA(mon, &mi)) {
                int sw = mi.rcWork.right - mi.rcWork.left;
                int sh = mi.rcWork.bottom - mi.rcWork.top;
                int x = mi.rcWork.left + (sw - w) / 2;
                int y = mi.rcWork.top  + (sh - h) / 2;

                SetWindowPos(ctx.hwnd, nullptr, x, y, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                SkyrimHT::Logger::Instance().Info("CenterGameWindow: moved window to %d,%d (size %dx%d)", x, y, w, h);
            }
        }
    } else {
        SkyrimHT::Logger::Instance().Info("CenterGameWindow: window is borderless/fullscreen, leaving position as-is");
    }

    if (IsIconic(ctx.hwnd)) {
        ShowWindow(ctx.hwnd, SW_RESTORE);
    }
    SetWindowPos(ctx.hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
    SetForegroundWindow(ctx.hwnd);
    SkyrimHT::Logger::Instance().Info("CenterGameWindow: brought game window to front");
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
            // Not the game process - exit silently
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
