#include "pch.h"
#include "marker_hook.h"

#include "core/logger.h"
#include "core/sse_addresses.h"
#include "hooks/gfx_value.h"

#include <MinHook.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace SkyrimHT {

namespace {

typedef bool (__fastcall *ScaleformInvoke_t)(
    void* movieRoot,
    void* movieView,
    uint64_t unk,
    const char* method,
    GFxValue* args,
    uint64_t argCount,
    bool allowPathLookup);

constexpr int kMaxLoggedMethods = 256;

ScaleformInvoke_t g_originalInvoke = nullptr;
void* g_hookedAddress = nullptr;
// Cache of every method-name hash we've already classified, interesting or
// not. The Scaleform invoke hook fires on every ActionScript call, so a steady
// state of unique method names lets the hot path skip the substring scan
// entirely - we only do it once per never-before-seen method.
std::atomic<uint32_t> g_seenMethodHashes[kMaxLoggedMethods] = {};
std::atomic<bool> g_seenCacheSaturated{false};

char ToLowerAscii(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c + ('a' - 'A'));
    return c;
}

bool ContainsInsensitive(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return false;

    for (const char* h = haystack; *h; ++h) {
        const char* hp = h;
        const char* np = needle;
        while (*hp && *np && ToLowerAscii(*hp) == ToLowerAscii(*np)) {
            ++hp;
            ++np;
        }
        if (!*np) return true;
    }

    return false;
}

bool IsInterestingMethod(const char* method) {
    return ContainsInsensitive(method, "compass") ||
           ContainsInsensitive(method, "marker") ||
           ContainsInsensitive(method, "quest") ||
           ContainsInsensitive(method, "target") ||
           ContainsInsensitive(method, "location") ||
           ContainsInsensitive(method, "floating") ||
           ContainsInsensitive(method, "objective");
}

uint32_t HashMethod(const char* method) {
    uint32_t hash = 2166136261u;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(method); *p; ++p) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

// Returns:
//   1  - hash inserted (first time we've seen this method)
//   0  - hash already in cache (steady state - cheap)
//  -1  - cache full and hash not present (saturated; treat as "skip work")
int LookupOrInsertMethodHash(uint32_t hash) {
    int firstFree = -1;
    for (int i = 0; i < kMaxLoggedMethods; ++i) {
        const uint32_t v = g_seenMethodHashes[i].load(std::memory_order_acquire);
        if (v == hash) return 0;
        if (v == 0 && firstFree < 0) firstFree = i;
    }
    if (firstFree < 0) {
        g_seenCacheSaturated.store(true, std::memory_order_release);
        return -1;
    }
    uint32_t expected = 0;
    if (g_seenMethodHashes[firstFree].compare_exchange_strong(
            expected, hash, std::memory_order_acq_rel)) {
        return 1;
    }
    // Lost the race - another thread filled this slot. Treat as "saw it" so we
    // don't risk double-logging; this hook is a diagnostic, not a critical path.
    return (expected == hash) ? 0 : -1;
}

void AppendText(char*& out, size_t& remaining, const char* text) {
    if (remaining == 0) return;
    const int written = std::snprintf(out, remaining, "%s", text);
    if (written <= 0) return;
    const size_t used = static_cast<size_t>(written) < remaining
        ? static_cast<size_t>(written)
        : remaining - 1;
    out += used;
    remaining -= used;
}

void AppendFormattedArg(char*& out, size_t& remaining, const GFxValue& arg) {
    if (remaining == 0) return;

    int written = 0;
    switch (arg.type) {
        case GFxValueType::kNumber:
            written = std::snprintf(out, remaining, "num=%.2f", arg.value.number);
            break;
        case GFxValueType::kBoolean:
            written = std::snprintf(out, remaining, "bool=%d", arg.value.boolean ? 1 : 0);
            break;
        case GFxValueType::kString:
            written = std::snprintf(out, remaining, "string");
            break;
        case GFxValueType::kStringW:
            written = std::snprintf(out, remaining, "wstring");
            break;
        case GFxValueType::kObject:
            written = std::snprintf(out, remaining, "object");
            break;
        case GFxValueType::kArray:
            written = std::snprintf(out, remaining, "array");
            break;
        case GFxValueType::kDisplayObject:
            written = std::snprintf(out, remaining, "display");
            break;
        case GFxValueType::kNull:
            written = std::snprintf(out, remaining, "null");
            break;
        default:
            written = std::snprintf(out, remaining, "type=%u", static_cast<uint32_t>(arg.type));
            break;
    }

    if (written <= 0) return;
    const size_t used = static_cast<size_t>(written) < remaining
        ? static_cast<size_t>(written)
        : remaining - 1;
    out += used;
    remaining -= used;
}

void FormatArgSummary(const GFxValue* args, uint64_t argCount, char* buffer, size_t bufferSize) {
    if (bufferSize == 0) return;
    buffer[0] = '\0';
    if (!args || argCount == 0) return;

    char* out = buffer;
    size_t remaining = bufferSize;
    const uint64_t limit = argCount < 4 ? argCount : 4;
    for (uint64_t i = 0; i < limit; ++i) {
        if (i != 0) AppendText(out, remaining, ", ");
        AppendFormattedArg(out, remaining, args[i]);
    }
    if (argCount > limit) AppendText(out, remaining, ", ...");
}

void LogInterestingMethod(const char* method, GFxValue* args, uint64_t argCount) {
    if (!method) return;
    // Fast saturation path: once the cache is full, this hook is a no-op for
    // the rest of the session. Scaleform invoke fires on every AS function
    // call, so skipping the substring scan + atomic loop here is the dominant
    // steady-state win.
    if (g_seenCacheSaturated.load(std::memory_order_relaxed)) return;

    const uint32_t hash = HashMethod(method);
    const int status = LookupOrInsertMethodHash(hash);
    if (status != 1) return;  // already seen, or cache saturated this call

    if (!IsInterestingMethod(method)) return;  // cached as "skip" - won't re-test next time

    char argSummary[192] = {};
    FormatArgSummary(args, argCount, argSummary, sizeof(argSummary));
    Logger::Instance().Info("MarkerHook: Scaleform invoke method='%s' argc=%llu args=[%s]",
                            method,
                            static_cast<unsigned long long>(argCount),
                            argSummary);
}
bool __fastcall ScaleformInvokeHook(
    void* movieRoot,
    void* movieView,
    uint64_t unk,
    const char* method,
    GFxValue* args,
    uint64_t argCount,
    bool allowPathLookup) {
    LogInterestingMethod(method, args, argCount);
    return g_originalInvoke(movieRoot, movieView, unk, method, args, argCount, allowPathLookup);
}

} // namespace

bool InstallMarkerHook() {
    if (g_hookedAddress) return true;
    if (!SSEAddresses::Initialize()) {
        Logger::Instance().Error("MarkerHook: SSE address resolution failed");
        return false;
    }

    const uintptr_t addr = SSEAddresses::ScaleformInvoke_Function();
    if (addr == 0) {
        Logger::Instance().Error("MarkerHook: Scaleform invoke RVA not resolved");
        return false;
    }

    g_hookedAddress = reinterpret_cast<void*>(addr);
    MH_STATUS status = MH_CreateHook(
        g_hookedAddress,
        reinterpret_cast<LPVOID>(&ScaleformInvokeHook),
        reinterpret_cast<LPVOID*>(&g_originalInvoke));
    if (status != MH_OK) {
        Logger::Instance().Error("MarkerHook: MH_CreateHook failed: %d",
                                  static_cast<int>(status));
        g_hookedAddress = nullptr;
        return false;
    }

    Logger::Instance().Info("MarkerHook: installed on Scaleform invoke at 0x%llX",
                            (unsigned long long)addr);
    return true;
}

void RemoveMarkerHook() {
    if (g_hookedAddress) {
        MH_DisableHook(g_hookedAddress);
        MH_RemoveHook(g_hookedAddress);
        g_hookedAddress = nullptr;
        g_originalInvoke = nullptr;
        Logger::Instance().Info("MarkerHook: removed");
    }
}

} // namespace SkyrimHT
