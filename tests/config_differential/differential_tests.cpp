// SPDX-License-Identifier: MIT
//
// The config differential test. Every input is read three ways:
//
//   oracle     the published build's reader (oracle/), v0.3.0 at c3a1f7f, with its
//              Mod::LoadConfig
//   import     the frozen reader in src/legacy_config/
//   migration  the config owner importing the input, as HeadTracking.ini, into a new
//              CameraUnlock.ini beside it, then the canonical reader and table on the result,
//              and the startup code of this build
//
// Comparison 1, oracle against import, finds what a player updating from the published build
// sees change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Comparison 2, import against migration, is the proof for the migration: no difference but
// the approved ones, each of which the import must record as dropped. A sensitivity or an
// axis inversion the player set away from what the build shipped is dropped (pose_shaping);
// the shipped InvertX=true is the x negation at the engine boundary now, which
// lean_direction_tests holds bit for bit. [Crosshair] Show=false is dropped (reticle): the
// game's crosshair always follows the aim now. A hotkey code outside 0x01-0xFE imports as
// unbound (N1), and a float the reader let through as NaN imports as the row's default (N2).
// The frozen reader clamps every other number into a range the canonical rows hold, so no
// input is deferred for a value.
//
// Comparison 2 runs over two Defaults.ini files, one at the built-in values and one a player
// changed, since the migration writes default exactly where the imported value equals what
// Defaults.ini gives. After every load HeadTracking.ini keeps its bytes, its write time and its
// attribute, Defaults.ini is never written, and the folder holds the legacy file and
// CameraUnlock.ini and nothing else. The next load reads CameraUnlock.ini, imports nothing and
// writes nothing, and a read-only legacy file imports as a writable one does. The distinct
// migrated files are written beside the executable under migrated\, for lint-migrated.mjs to
// run core's canonical config lint over.
//
// Inputs: no file, an empty file, the HeadTracking.ini each release shipped (v0.1.0, v0.1.1 and
// v0.2.0 shipped one file and v0.3.0 another, in the installer ZIP's plugins\ and as the launcher
// seed from v0.1.1 on; the Nexus ZIPs carry none), the two versions committed since v0.3.0, the
// file v0.3.0 writes at first launch when there is none (extracted once into inputs/ with
// --first-run), the file v0.1.0, v0.1.1 and v0.2.0 write at first launch (one writer, extracted
// once from v0.1.0's sources, as frozen.tsv records), core's corpus over the v0.3.0 file, the
// v0.3.0 file with all three hotkeys on each code from 0x01 to 0xFE, the v0.3.0 file with a
// number that is not finite on each float it reads, and a legacy file another program holds open
// with no sharing.

#include "core/config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = SkyrimHT::legacy;
namespace cfg = cameraunlock::config;
using SkyrimHT::Config;
using cameraunlock::TrackingMode;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;
using cameraunlock::input::KeyModifiers;

int g_failures = 0;

void Fail(const std::string& input, const std::string& what) {
    if (g_failures < 50) std::printf("FAIL [%s]: %s\n", input.c_str(), what.c_str());
    ++g_failures;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

std::string Narrow(const std::wstring& path) {
    const int size = WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), 'x');
    WideCharToMultiByte(CP_ACP, 0, path.c_str(), -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<size_t>(size) - 1);
    return out;
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + Narrow(path));
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + Narrow(path));
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Every file in the folder, name and bytes, for "the import changed nothing".
std::map<std::wstring, std::string> Snapshot(const std::wstring& dir) {
    std::map<std::wstring, std::string> files;
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot list the test folder");
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files[data.cFileName] = ReadBytes(dir + L"\\" + data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return files;
}

void EmptyFolder(const std::wstring& dir) {
    for (const auto& [name, bytes] : Snapshot(dir)) {
        const std::wstring path = dir + L"\\" + name;
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(path.c_str())) throw std::runtime_error("cannot empty the test folder");
    }
}

std::wstring MakeFolder(const std::wstring& parent, const wchar_t* name) {
    const std::wstring dir = parent + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        throw std::runtime_error("cannot create the test folder");
    }
    EmptyFolder(dir);
    return dir;
}

// ---------------------------------------------------------------------------
// Hotkeys: what each build puts on its poller
// ---------------------------------------------------------------------------

enum class Action { Toggle, CycleMode, YawMode };

const char* ActionName(Action a) {
    switch (a) {
        case Action::Toggle: return "toggle";
        case Action::CycleMode: return "cycle mode";
        case Action::YawMode: return "yaw mode";
    }
    throw std::logic_error("action");
}

// One key the poller watches for an action, and the modifiers it fires with: kNav is
// NavGuarded (not while Ctrl and Shift are both held), kChord is ChordGuarded (while both are
// held).
struct Registration {
    Action action;
    int vk;
    unsigned modifiers;

    bool operator<(const Registration& o) const {
        return std::tie(action, vk, modifiers) < std::tie(o.action, o.vk, o.modifiers);
    }
    bool operator==(const Registration& o) const {
        return action == o.action && vk == o.vk && modifiers == o.modifiers;
    }
    bool operator!=(const Registration& o) const { return !(*this == o); }
};

constexpr unsigned kNav = static_cast<unsigned>(KeyModifiers::kNone);
constexpr unsigned kChord = static_cast<unsigned>(KeyModifiers::kCtrl | KeyModifiers::kShift);

std::string Describe(const std::vector<Registration>& regs) {
    std::string out;
    for (const Registration& r : regs) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%s%s:%s0x%02X", out.empty() ? "" : " ", ActionName(r.action),
                      r.modifiers == kChord ? "Ctrl+Shift+" : "", static_cast<unsigned>(r.vk));
        out += buf;
    }
    return out;
}

// RegisterBindings in src/hooks/input_hook.cpp, the same at v0.3.0 and at the frozen commit
// once its diagnostic keys are compiled out of a release build, with the poller calls recorded.
// The chords were registered unconditionally.
template <class C>
std::vector<Registration> PublishedHotkeys(const C& c) {
    std::vector<Registration> regs = {
        {Action::Toggle, c.toggleKey, kNav},
        {Action::CycleMode, c.positionToggleKey, kNav},
        {Action::YawMode, c.yawModeKey, kNav},
        {Action::Toggle, 'Y', kChord},
        {Action::CycleMode, 'G', kChord},
        {Action::YawMode, 'H', kChord},
    };
    std::sort(regs.begin(), regs.end());
    return regs;
}

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// Every field of the two Configs, floats bit for bit.
template <class A, class B>
std::vector<std::string> FieldDifferences(const A& a, const B& b) {
    std::vector<std::string> out;
    const auto check = [&out](bool same, const char* name) {
        if (!same) out.push_back(name);
    };
#define SAME(f) check(a.f == b.f, #f)
#define SAME_BITS(f) check(Bits(a.f) == Bits(b.f), #f)
    SAME(udpPort);
    SAME_BITS(yawMultiplier);
    SAME_BITS(pitchMultiplier);
    SAME_BITS(rollMultiplier);
    SAME_BITS(localSmoothing);
    SAME_BITS(remoteSmoothing);
    SAME(toggleKey);
    SAME(positionToggleKey);
    SAME(yawModeKey);
    SAME_BITS(positionSensitivityX);
    SAME_BITS(positionSensitivityY);
    SAME_BITS(positionSensitivityZ);
    SAME_BITS(positionLimitX);
    SAME_BITS(positionLimitY);
    SAME_BITS(positionLimitZ);
    SAME_BITS(positionLimitZBack);
    SAME(positionInvertX);
    SAME(positionInvertY);
    SAME(positionInvertZ);
    SAME(positionEnabled);
    SAME(autoEnable);
    SAME(showNotifications);
    SAME(worldSpaceYaw);
    SAME(showCrosshair);
#undef SAME
#undef SAME_BITS
    return out;
}

// ---------------------------------------------------------------------------
// Startup: what Mod::Initialize runs on
// ---------------------------------------------------------------------------

// Everything Mod::Initialize takes from the config: the port, whether tracking starts on, the
// tracking mode, the yaw mode, the rotation sensitivity, the position settings it hands the
// processor, the smoothing pair, whether notifications are logged, and whether the crosshair
// overlay is installed.
struct Startup {
    int port = 0;
    bool enabled = false;
    TrackingMode mode = TrackingMode::RotationAndPosition;
    bool world_yaw = false;
    bool notifications = false;
    bool crosshair = false;
    uint32_t sens_yaw = 0;
    uint32_t sens_pitch = 0;
    uint32_t sens_roll = 0;
    uint32_t pos_sens_x = 0;
    uint32_t pos_sens_y = 0;
    uint32_t pos_sens_z = 0;
    bool invert_x = false;
    bool invert_y = false;
    bool invert_z = false;
    uint32_t local_smoothing = 0;
    uint32_t remote_smoothing = 0;
    uint32_t limit_x = 0;
    uint32_t limit_y = 0;
    uint32_t limit_y_down = 0;
    uint32_t limit_z = 0;
    uint32_t limit_z_back = 0;
};

// cameraunlock::PositionSettings::limit_y_down's default at v0.3.0's core pin 3465659, which
// that build's Mod::Initialize never set.
constexpr float kPublishedLimitYDown = 0.20f;

template <class C>
Startup StartupOf(const C& c) {
    Startup s;
    s.port = c.udpPort;
    s.enabled = c.autoEnable;
    s.mode = c.positionEnabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly;
    s.world_yaw = c.worldSpaceYaw;
    s.notifications = c.showNotifications;
    s.crosshair = c.showCrosshair;
    s.sens_yaw = Bits(c.yawMultiplier);
    s.sens_pitch = Bits(c.pitchMultiplier);
    s.sens_roll = Bits(c.rollMultiplier);
    s.pos_sens_x = Bits(c.positionSensitivityX);
    s.pos_sens_y = Bits(c.positionSensitivityY);
    s.pos_sens_z = Bits(c.positionSensitivityZ);
    s.invert_x = c.positionInvertX;
    s.invert_y = c.positionInvertY;
    s.invert_z = c.positionInvertZ;
    s.local_smoothing = Bits(c.localSmoothing);
    s.remote_smoothing = Bits(c.remoteSmoothing);
    s.limit_x = Bits(c.positionLimitX);
    s.limit_y = Bits(c.positionLimitY);
    s.limit_z = Bits(c.positionLimitZ);
    s.limit_z_back = Bits(c.positionLimitZBack);
    return s;
}

// Mod::Initialize at v0.3.0: the down limit stayed at PositionSettings' default.
Startup PublishedStartup(const oracle_api::Config& c) {
    Startup s = StartupOf(c);
    s.limit_y_down = Bits(kPublishedLimitYDown);
    return s;
}

// Mod::Initialize at the frozen commit: LimitY bounds both directions.
Startup FrozenStartup(const legacy::Config& c) {
    Startup s = StartupOf(c);
    s.limit_y_down = Bits(c.positionLimitY);
    return s;
}

std::vector<std::string> StartupDifferences(const Startup& a, const Startup& b) {
    std::vector<std::string> out;
#define SAME(f) \
    if (a.f != b.f) out.push_back(#f)
    SAME(port);
    SAME(enabled);
    SAME(mode);
    SAME(world_yaw);
    SAME(notifications);
    SAME(crosshair);
    SAME(sens_yaw);
    SAME(sens_pitch);
    SAME(sens_roll);
    SAME(pos_sens_x);
    SAME(pos_sens_y);
    SAME(pos_sens_z);
    SAME(invert_x);
    SAME(invert_y);
    SAME(invert_z);
    SAME(local_smoothing);
    SAME(remote_smoothing);
    SAME(limit_x);
    SAME(limit_y);
    SAME(limit_y_down);
    SAME(limit_z);
    SAME(limit_z_back);
#undef SAME
    return out;
}

// ---------------------------------------------------------------------------
// Comparison 1: the published build against the frozen reader
// ---------------------------------------------------------------------------

// What a player updating from the published build sees change, and the commit that made each
// change. The changelog carries the same list.
struct ListedDifference {
    const char* id;
    const char* commit;
    const char* what;
    int seen = 0;
};

ListedDifference kComparisonOneDifferences[] = {
    {"InvertZ default", "b285051",
     "[Position] InvertZ is false where no file or no InvertZ line sets it; v0.3.0 took true, and wrote "
     "true into the file it created. Every file a release shipped or wrote sets the key"},
    {"lower lean limit", "6d25db0",
     "the lean down is bounded by [Position] LimitY, as the lean up is; v0.3.0 held it at 0.20 m "
     "whatever LimitY said"},
};

ListedDifference& Listed(const char* id) {
    for (ListedDifference& d : kComparisonOneDifferences) {
        if (std::strcmp(d.id, id) == 0) return d;
    }
    throw std::logic_error("no listed difference " + std::string(id));
}

struct OracleRun {
    oracle_api::LoadStatus status = oracle_api::LoadStatus::Read;
    oracle_api::Config cfg;
};

struct ImportRun {
    legacy::ReadStatus status = legacy::ReadStatus::Read;
    legacy::Config cfg;
};

bool SameStatus(oracle_api::LoadStatus o, legacy::ReadStatus i) {
    switch (o) {
        case oracle_api::LoadStatus::Read: return i == legacy::ReadStatus::Read;
        case oracle_api::LoadStatus::Created: return i == legacy::ReadStatus::Absent;
    }
    throw std::logic_error("status");
}

void CompareOracleWithImport(const std::string& name, const OracleRun& o, const ImportRun& i) {
    if (!SameStatus(o.status, i.status)) {
        Fail(name, "comparison 1: the published build and the import do not agree on whether the file was read");
        return;
    }
    // Both builds read the key where the file sets it, so a difference in InvertZ means neither
    // read one and each holds its own default.
    const bool invertZDefault = o.cfg.positionInvertZ == oracle_api::Defaults().positionInvertZ &&
                                i.cfg.positionInvertZ == legacy::kDefaultPositionInvertZ;
    bool invertZSeen = false;
    for (const std::string& field : FieldDifferences(o.cfg, i.cfg)) {
        if (field == "positionInvertZ" && invertZDefault) {
            invertZSeen = true;
            continue;
        }
        Fail(name, "comparison 1: " + field + " differs from the published build with no listed reason");
    }
    if (invertZSeen) ++Listed("InvertZ default").seen;

    bool limitSeen = false;
    for (const std::string& d : StartupDifferences(PublishedStartup(o.cfg), FrozenStartup(i.cfg))) {
        if (d == "invert_z" && invertZSeen) continue;
        if (d == "limit_y_down") {
            limitSeen = true;
            continue;
        }
        Fail(name, "comparison 1: the start differs in " + d + " with no listed reason");
    }
    if (limitSeen) ++Listed("lower lean limit").seen;

    const std::vector<Registration> expected = PublishedHotkeys(o.cfg);
    const std::vector<Registration> actual = PublishedHotkeys(i.cfg);
    if (expected != actual) {
        Fail(name, "comparison 1: hotkeys " + Describe(actual) + " against the published build's " + Describe(expected));
    }
}

// ---------------------------------------------------------------------------
// The keys the frozen reader reads, and the corpus descriptors
// ---------------------------------------------------------------------------

// One valid value other than the shipped one, and one value past each bound the reader clamps
// or refuses, for every key legacy::ReadKeys lists.
std::vector<MutationKey> CorpusKeys() {
    const auto boolean = [](const char* s, const char* k, const char* alternate) {
        return MutationKey{s, k, alternate, {}, false, {}};
    };
    const auto multiplier = [](const char* k) { return MutationKey{"Sensitivity", k, "0.5", {"0.05", "6"}, false, {}}; };
    const auto smooth = [](const char* k) { return MutationKey{"Sensitivity", k, "0.3", {"-0.5", "1.5"}, false, {}}; };
    const auto sens = [](const char* k) { return MutationKey{"Position", k, "0.5", {"0.05", "11"}, false, {}}; };
    const auto limit = [](const char* k) { return MutationKey{"Position", k, "0.25", {"0.001", "3"}, false, {}}; };
    const auto hotkey = [](const char* k, const char* alt) {
        return MutationKey{"Hotkeys", k, alt, {"0xFF", "0x100", "-1"}, true, {}};
    };
    return {
        MutationKey{"Network", "UDPPort", "5000", {"1023", "65536"}, false, {}},
        multiplier("YawMultiplier"),
        multiplier("PitchMultiplier"),
        MutationKey{"Sensitivity", "RollMultiplier", "0.5", {"-1", "3"}, false, {}},
        smooth("LocalSmoothing"),
        smooth("RemoteSmoothing"),
        hotkey("ToggleKey", "0x70"),
        hotkey("PositionToggleKey", "0x71"),
        hotkey("YawModeKey", "0x72"),
        sens("SensitivityX"),
        sens("SensitivityY"),
        sens("SensitivityZ"),
        limit("LimitX"),
        limit("LimitY"),
        limit("LimitZ"),
        limit("LimitZBack"),
        boolean("Position", "InvertX", "false"),
        boolean("Position", "InvertY", "true"),
        boolean("Position", "InvertZ", "true"),
        boolean("Position", "Enabled", "false"),
        boolean("General", "AutoEnable", "false"),
        boolean("General", "ShowNotifications", "false"),
        boolean("General", "WorldSpaceYaw", "false"),
        boolean("Crosshair", "Show", "false"),
    };
}

// ---------------------------------------------------------------------------
// Comparison 2: the frozen reader against the migration
// ---------------------------------------------------------------------------

// What the mod starts with. Pose shaping is not here: the migrated build applies none, which
// CheckNoShaping holds it to, and CheckPoseShaping holds the import to listing every value it
// leaves out. Nor is the crosshair: the migrated build always moves it, and CheckDropRules holds
// the import to recording a [Crosshair] Show=false it leaves out.
struct Start {
    int port = 0;
    bool enabled = false;
    TrackingMode mode = TrackingMode::RotationAndPosition;
    bool world_yaw = false;
    bool notifications = false;
    uint32_t local_smoothing = 0;
    uint32_t remote_smoothing = 0;
    uint32_t limit_x = 0;
    uint32_t limit_y = 0;
    uint32_t limit_y_down = 0;
    uint32_t limit_z = 0;
    uint32_t limit_z_back = 0;
    std::vector<Registration> hotkeys;
};

const cfg::DroppedValue* FindDrop(const std::vector<cfg::DroppedValue>& dropped, cfg::DropRule rule,
                                  const char* section, const char* key) {
    for (const cfg::DroppedValue& d : dropped) {
        if (d.rule == rule && d.section == section && d.key == key) return &d;
    }
    return nullptr;
}

// A hotkey code outside 0x01-0xFE imports as unbound (N1), and only then is it dropped. Code 0
// never fired on the published build's poller and imports as unbound, unrecorded.
bool KeyKept(const std::string& name, int vk, const char* key, const std::vector<cfg::DroppedValue>& dropped) {
    const bool outOfRange = vk != 0 && (vk < 0x01 || vk > 0xFE);
    if (outOfRange != (FindDrop(dropped, cfg::DropRule::KeyCodeOutOfRange, "Hotkeys", key) != nullptr)) {
        Fail(name, std::string("[Hotkeys] ") + key + " dropped as out of range does not match its code");
    }
    return vk != 0 && !outOfRange;
}

// A float the frozen reader left NaN imports as the row's default (N2), and only then is it
// dropped.
uint32_t FiniteOrDefault(const std::string& name, float value, float rowDefault, const char* section, const char* key,
                         const std::vector<cfg::DroppedValue>& dropped) {
    const bool finite = std::isfinite(value);
    if (finite == (FindDrop(dropped, cfg::DropRule::NonFiniteNumber, section, key) != nullptr)) {
        Fail(name, std::string("[") + section + "] " + key + " dropped as not finite does not match its value");
    }
    return Bits(finite ? value : rowDefault);
}

// Mod::Initialize and RegisterBindings as commit A ran them, with the approved changes applied:
// a NaN the reader let through is the row's default (N2), and a code N1 unbinds is not registered.
Start FromImport(const std::string& name, const legacy::Config& c, const std::vector<cfg::DroppedValue>& dropped) {
    const Config defaults = SkyrimHT::MakeConfigTable().defaults();
    Start s;
    s.port = c.udpPort;
    s.enabled = c.autoEnable;
    s.mode = c.positionEnabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly;
    s.world_yaw = c.worldSpaceYaw;
    s.notifications = c.showNotifications;
    s.local_smoothing =
        FiniteOrDefault(name, c.localSmoothing, defaults.local_smoothing, "Sensitivity", "LocalSmoothing", dropped);
    s.remote_smoothing =
        FiniteOrDefault(name, c.remoteSmoothing, defaults.remote_smoothing, "Sensitivity", "RemoteSmoothing", dropped);
    s.limit_x = FiniteOrDefault(name, c.positionLimitX, defaults.position.limit_x, "Position", "LimitX", dropped);
    s.limit_y = FiniteOrDefault(name, c.positionLimitY, defaults.position.limit_y, "Position", "LimitY", dropped);
    s.limit_y_down = Bits(std::isfinite(c.positionLimitY) ? c.positionLimitY : defaults.position.limit_y_down);
    s.limit_z = FiniteOrDefault(name, c.positionLimitZ, defaults.position.limit_z, "Position", "LimitZ", dropped);
    s.limit_z_back =
        FiniteOrDefault(name, c.positionLimitZBack, defaults.position.limit_z_back, "Position", "LimitZBack", dropped);
    const bool toggleKept = KeyKept(name, c.toggleKey, "ToggleKey", dropped);
    const bool cycleKept = KeyKept(name, c.positionToggleKey, "PositionToggleKey", dropped);
    const bool yawKept = KeyKept(name, c.yawModeKey, "YawModeKey", dropped);
    for (const Registration& r : PublishedHotkeys(c)) {
        const bool kept = r.modifiers == kChord || (r.action == Action::Toggle && toggleKept) ||
                          (r.action == Action::CycleMode && cycleKept) || (r.action == Action::YawMode && yawKept);
        if (kept) s.hotkeys.push_back(r);
    }
    return s;
}

// This build: Mod::Initialize, and the lists RegisterBindings registers.
Start FromMigration(const Config& c) {
    Start s;
    s.port = c.udp_port;
    s.enabled = c.enable_on_startup;
    s.mode = cameraunlock::DecodeTrackingMode(c.rotation_enabled, c.position_enabled).value();
    s.world_yaw = c.world_space_yaw;
    s.notifications = c.show_notifications;
    s.local_smoothing = Bits(c.local_smoothing);
    s.remote_smoothing = Bits(c.remote_smoothing);
    s.limit_x = Bits(c.position.limit_x);
    s.limit_y = Bits(c.position.limit_y);
    s.limit_y_down = Bits(c.position.limit_y_down);
    s.limit_z = Bits(c.position.limit_z);
    s.limit_z_back = Bits(c.position.limit_z_back);
    const std::pair<Action, const std::string*> lists[] = {
        {Action::Toggle, &c.toggle_key_name},
        {Action::CycleMode, &c.cycle_tracking_mode_key_name},
        {Action::YawMode, &c.yaw_mode_key_name},
    };
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        if (!parsed.ok()) throw std::logic_error("a migrated hotkey list does not parse: " + *list);
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            s.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    std::sort(s.hotkeys.begin(), s.hotkeys.end());
    return s;
}

std::vector<std::string> StartDifferences(const Start& a, const Start& b) {
    std::vector<std::string> out;
#define SAME(f) \
    if (a.f != b.f) out.push_back(#f)
    SAME(port);
    SAME(enabled);
    SAME(mode);
    SAME(world_yaw);
    SAME(notifications);
    SAME(local_smoothing);
    SAME(remote_smoothing);
    SAME(limit_x);
    SAME(limit_y);
    SAME(limit_y_down);
    SAME(limit_z);
    SAME(limit_z_back);
#undef SAME
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys " + Describe(a.hotkeys) + " against " + Describe(b.hotkeys));
    return out;
}

// The runtime config applies no pose shaping of its own: the position processor gets it
// whole, and the rotation processor's sensitivity is never set, so both must be identity, and
// the position smoothing must be the smoothing rows' values.
void CheckNoShaping(const std::string& name, const Config& c) {
    const cameraunlock::PositionSettings& p = c.position;
    if (p.sensitivity_x != 1.0f || p.sensitivity_y != 1.0f || p.sensitivity_z != 1.0f || p.invert_x || p.invert_y ||
        p.invert_z) {
        Fail(name, "the migrated config shapes the position");
    }
    if (Bits(p.local_smoothing) != Bits(c.local_smoothing) || Bits(p.remote_smoothing) != Bits(c.remote_smoothing)) {
        Fail(name, "the position smoothing is not the smoothing rows' values");
    }
}

// Every sensitivity and inversion the frozen reader read is listed in its place, folded where
// it holds what the build shipped and dropped as PoseShaping where it does not. Returns how
// many were dropped.
int CheckPoseShaping(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    struct Read {
        const char* section;
        const char* key;
        bool shipped;
    };
    const Read reads[] = {
        {"Sensitivity", "YawMultiplier", c.yawMultiplier == legacy::kDefaultMultiplier},
        {"Sensitivity", "PitchMultiplier", c.pitchMultiplier == legacy::kDefaultMultiplier},
        {"Sensitivity", "RollMultiplier", c.rollMultiplier == legacy::kDefaultMultiplier},
        {"Position", "SensitivityX", c.positionSensitivityX == legacy::kDefaultPositionSensitivity},
        {"Position", "SensitivityY", c.positionSensitivityY == legacy::kDefaultPositionSensitivity},
        {"Position", "SensitivityZ", c.positionSensitivityZ == legacy::kDefaultPositionSensitivity},
        {"Position", "InvertX", c.positionInvertX == legacy::kDefaultPositionInvertX},
        {"Position", "InvertY", c.positionInvertY == legacy::kDefaultPositionInvertY},
        {"Position", "InvertZ", c.positionInvertZ == legacy::kDefaultPositionInvertZ},
    };
    if (result.pose_shaping.size() != std::size(reads)) {
        Fail(name, "the import lists " + std::to_string(result.pose_shaping.size()) + " pose-shaping values, not 9");
        return 0;
    }
    int dropped = 0;
    for (size_t k = 0; k < std::size(reads); ++k) {
        const cfg::PoseShapingValue& v = result.pose_shaping[k];
        const std::string label = std::string("[") + reads[k].section + "] " + reads[k].key;
        if (v.section != reads[k].section || v.key != reads[k].key) Fail(name, label + " is not listed in its place");
        if (v.folded != reads[k].shipped) Fail(name, label + " is " + (v.folded ? "folded" : "dropped") + " wrongly");
        const bool listed = FindDrop(result.dropped, cfg::DropRule::PoseShaping, reads[k].section, reads[k].key) != nullptr;
        if (listed == reads[k].shipped) {
            Fail(name, label + (listed ? " is dropped at its shipped value" : " is changed and not dropped"));
        }
        if (!reads[k].shipped) ++dropped;
    }
    return dropped;
}

// Every drop the import recorded is by one of the approved rules this map applies, and
// [Crosshair] Show is dropped exactly where it was false.
void CheckDropRules(const std::string& name, const legacy::Config& c, const cfg::ImportResult& result) {
    for (const cfg::DroppedValue& d : result.dropped) {
        const bool approved = d.rule == cfg::DropRule::PoseShaping || d.rule == cfg::DropRule::KeyCodeOutOfRange ||
                              d.rule == cfg::DropRule::NonFiniteNumber || d.rule == cfg::DropRule::Reticle;
        if (!approved) Fail(name, "the import drops [" + d.section + "] " + d.key + " by a rule this map never applies");
    }
    const bool reticle = FindDrop(result.dropped, cfg::DropRule::Reticle, "Crosshair", "Show") != nullptr;
    if (reticle != !c.showCrosshair) Fail(name, "[Crosshair] Show dropped does not match its value");
}

struct MigrationTally {
    std::string committed;
    std::wstring builtin_defaults;
    std::wstring altered_defaults;
    std::set<std::string> migrated;
    int created = 0;
    int converted = 0;
    int with_default_rows = 0;
    int with_values = 0;
    int with_pose_shaping_dropped = 0;
    int with_n1 = 0;
    int with_n2 = 0;
    int with_reticle = 0;
};

cfg::ConfigOwnerOptions<Config> Options(const std::wstring& dir, const std::wstring& defaults) {
    return SkyrimHT::MakeConfigOwnerOptions(dir + L"\\", cfg::DefaultsFile::At(defaults));
}

FILETIME WriteTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        throw std::runtime_error("cannot read a test file's attributes");
    }
    return data.ftLastWriteTime;
}

bool SameTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    return std::any_of(lines.begin(), lines.end(),
                       [&text](const std::string& line) { return line.find(text) != std::string::npos; });
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

// Each input is read in folders of its own, so no reader ever meets a file another input left.
struct Folders {
    std::wstring root;
    std::wstring oracle;
    std::wstring import;
    std::wstring migration;
    std::wstring read_only;
};

Folders NextFolders(const std::wstring& root) {
    static int n = 0;
    const std::wstring dir = MakeFolder(root, std::to_wstring(n++).c_str());
    return {dir, MakeFolder(dir, L"oracle"), MakeFolder(dir, L"import"), MakeFolder(dir, L"migration"),
            MakeFolder(dir, L"read-only")};
}

void RemoveFolders(const Folders& f) {
    for (const std::wstring& dir : {f.oracle, f.import, f.migration, f.read_only, f.root}) {
        EmptyFolder(dir);
        if (!RemoveDirectoryW(dir.c_str())) throw std::runtime_error("cannot remove a test folder");
    }
}

const wchar_t kIniName[] = L"HeadTracking.ini";

// The owner's load on the legacy file `bytes` in `dir`, read-only when asked, with everything
// the migration must leave as it was checked afterwards: the legacy file's bytes, write time and
// attribute, Defaults.ini's bytes, and a folder holding the legacy file and CameraUnlock.ini
// and nothing else.
struct Migration {
    cfg::ConfigLoadResult<Config> loaded;
    std::optional<std::string> bytes;
};

Migration Migrate(const std::string& name, const std::wstring& dir, const std::optional<std::string>& legacyBytes,
                  bool readOnly, const std::wstring& defaults) {
    EmptyFolder(dir);
    const std::wstring legacyPath = dir + L"\\" + kIniName;
    const std::wstring path = dir + L"\\" + SkyrimHT::kConfigFileName;
    FILETIME before{};
    if (legacyBytes) {
        WriteBytes(legacyPath, *legacyBytes);
        if (readOnly) SetFileAttributesW(legacyPath.c_str(), FILE_ATTRIBUTE_READONLY);
        before = WriteTime(legacyPath);
    }
    const std::string defaultsBefore = ReadBytes(defaults);

    Migration m{};
    {
        cfg::ConfigOwner<Config> owner(Options(dir, defaults));
        m.loaded = owner.Load();
    }

    std::map<std::wstring, std::string> expected;
    if (legacyBytes) {
        expected[kIniName] = *legacyBytes;
        if (!SameTime(WriteTime(legacyPath), before)) Fail(name, "the legacy file's write time changed");
        const DWORD attributes = GetFileAttributesW(legacyPath.c_str());
        if (((attributes & FILE_ATTRIBUTE_READONLY) != 0) != readOnly) Fail(name, "the legacy file's read-only attribute changed");
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        m.bytes = ReadBytes(path);
        expected[SkyrimHT::kConfigFileName] = *m.bytes;
    }
    if (Snapshot(dir) != expected) Fail(name, "the folder holds files other than the legacy file and CameraUnlock.ini");
    if (ReadBytes(defaults) != defaultsBefore) Fail(name, "the load wrote Defaults.ini");
    return m;
}

// The migrated file loaded again over the same Defaults.ini: it reads as canonical with
// nothing to report, gives the same start, imports nothing and changes neither file.
void CheckSecondLoad(const std::string& name, const std::wstring& dir, const Migration& first,
                     const std::optional<std::string>& legacyBytes, const std::wstring& defaults) {
    const auto before = Snapshot(dir);
    const std::string defaultsBefore = ReadBytes(defaults);
    cfg::ConfigOwner<Config> owner(Options(dir, defaults));
    const cfg::ConfigLoadResult<Config> again = owner.Load();
    if (again.status != cfg::ConfigLoadStatus::Canonical) Fail(name, "the second load is not Canonical");
    if (!again.diagnostics.empty()) Fail(name, "the migrated file draws diagnostics");
    if (!StartDifferences(FromMigration(first.loaded.config), FromMigration(again.config)).empty()) {
        Fail(name, "the second load starts differently from the first");
    }
    if (Contains(again.log, "created from")) Fail(name, "the second load imports again");
    if (Snapshot(dir) != before || ReadBytes(defaults) != defaultsBefore) Fail(name, "the second load changed a file");
    if (legacyBytes.has_value() != Contains(again.log, "is left as it was and is not read")) {
        Fail(name, "the second load's log does not say whether the legacy file was left unread");
    }
}

// Comparison 2 over one Defaults.ini. The session must start as the import does in every case
// but a fresh install over a changed Defaults.ini, which follows that file.
void MigrateInput(const Folders& f, const std::string& name, const std::optional<std::string>& bytes,
                  const ImportRun& i, const cfg::ImportResult* result, const std::wstring& defaults,
                  MigrationTally& tally) {
    using cfg::ConfigLoadStatus;
    const bool builtin = defaults == tally.builtin_defaults;
    const std::string label = name + (builtin ? "" : ", Defaults.ini changed");
    const Migration m = Migrate(label, f.migration, bytes, false, defaults);
    CheckNoShaping(label, m.loaded.config);

    if (!bytes) {
        if (m.loaded.status != ConfigLoadStatus::Created) Fail(label, "no file is not Created");
        if (m.bytes != tally.committed) Fail(label, "the created file is not HeadTracking.ini as committed");
        if (builtin) {
            ++tally.created;
            for (const std::string& d : StartDifferences(FromImport(label, i.cfg, {}), FromMigration(m.loaded.config))) {
                Fail(label, "comparison 2: " + d);
            }
        }
        CheckSecondLoad(label, f.migration, m, bytes, defaults);
        return;
    }

    if (builtin) {
        if (CheckPoseShaping(label, i.cfg, *result) > 0) ++tally.with_pose_shaping_dropped;
        CheckDropRules(label, i.cfg, *result);
        const auto has = [result](cfg::DropRule rule) {
            return std::any_of(result->dropped.begin(), result->dropped.end(),
                               [rule](const cfg::DroppedValue& d) { return d.rule == rule; });
        };
        if (has(cfg::DropRule::KeyCodeOutOfRange)) ++tally.with_n1;
        if (has(cfg::DropRule::NonFiniteNumber)) ++tally.with_n2;
        if (has(cfg::DropRule::Reticle)) ++tally.with_reticle;
    }

    for (const std::string& d : StartDifferences(FromImport(label, i.cfg, result->dropped), FromMigration(m.loaded.config))) {
        Fail(label, "comparison 2: " + d);
    }

    if (m.loaded.status != ConfigLoadStatus::Migrated) {
        Fail(label, std::string("the migration is ") + cfg::ConfigLoadStatusName(m.loaded.status) + ": " + m.loaded.reason);
        return;
    }
    if (!Contains(m.loaded.log, "created from")) Fail(label, "the log does not say where CameraUnlock.ini came from");
    ++tally.converted;
    tally.migrated.insert(*m.bytes);
    if (m.bytes->find("=default\r\n") != std::string::npos) ++tally.with_default_rows;
    if (*m.bytes != tally.committed) ++tally.with_values;

    if (builtin) {
        const Migration ro = Migrate(label, f.read_only, bytes, true, defaults);
        if (ro.loaded.status != ConfigLoadStatus::Migrated || ro.bytes != m.bytes) {
            Fail(label, "a read-only legacy file does not import as a writable one does");
        }
    }

    CheckSecondLoad(label, f.migration, m, bytes, defaults);
}

void RunInput(const std::wstring& root, const std::string& name, const std::optional<std::string>& bytes,
              MigrationTally& tally) {
    const Folders f = NextFolders(root);
    OracleRun o;
    {
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
    }

    ImportRun i;
    std::optional<cfg::ImportResult> result;
    {
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (bytes) {
            Config mapped = SkyrimHT::MakeConfigTable().defaults();
            result = SkyrimHT::MakeLegacyImport().run(cfg::LegacyInput{path, Narrow(path), false}, mapped);
            if (result->status != cfg::ImportStatus::Imported) Fail(name, "the mapped import is not Imported");
        }
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
    for (const std::wstring& defaults : {tally.builtin_defaults, tally.altered_defaults}) {
        MigrateInput(f, name, bytes, i, result ? &*result : nullptr, defaults, tally);
    }
    RemoveFolders(f);
}

// A legacy file another program holds open with no sharing. inih cannot open it, so the
// published build ran on its defaults and failed to write over it, and the import reads it as
// absent. The owner defers the import on its own defaults, which start the same, creates
// nothing and saves nothing that session.
void TestUnopenableFile(const std::wstring& root, const std::string& shipped, const MigrationTally& tally) {
    const std::string name = "a legacy file another program holds open with no sharing";
    const Folders f = NextFolders(root);
    OracleRun o;
    ImportRun i;
    std::optional<cfg::ConfigLoadResult<Config>> loaded;
    for (const std::wstring& dir : {f.oracle, f.import, f.migration}) {
        const std::wstring path = dir + L"\\" + kIniName;
        WriteBytes(path, shipped);
        HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot hold the test file open");
        if (dir == f.oracle) {
            o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
        } else if (dir == f.import) {
            i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        } else {
            cfg::ConfigOwner<Config> owner(Options(dir, tally.builtin_defaults));
            loaded.emplace(owner.Load());
            if (owner.Save([](Config& c) { c.world_space_yaw = false; }).status != cfg::ConfigSaveStatus::NotSaved) {
                Fail(name, "a deferred session saved");
            }
        }
        CloseHandle(held);
        if (ReadBytes(path) != shipped) Fail(name, "a build rewrote a file it could not open");
    }
    CompareOracleWithImport(name, o, i);
    if (loaded->status != cfg::ConfigLoadStatus::Deferred) {
        Fail(name, std::string("the owner's load is ") + cfg::ConfigLoadStatusName(loaded->status) + ", not Deferred");
    }
    for (const std::string& d : StartDifferences(FromImport(name, i.cfg, {}), FromMigration(loaded->config))) {
        Fail(name, "comparison 2: " + d);
    }
    if (Snapshot(f.migration) != std::map<std::wstring, std::string>{{kIniName, shipped}}) {
        Fail(name, "a deferred import created a file or changed the legacy one");
    }
    RemoveFolders(f);
}

std::string ReadInput(const std::string& file) {
    return ReadBytes(Widen(std::string(SKYRIMHT_DIFFERENTIAL_INPUTS) + "/" + file));
}

// `text` with the value of its one `key=` line replaced, the rest of that line included.
std::string WithValue(const std::string& text, const std::string& key, const std::string& value) {
    const size_t at = text.find("\n" + key + "=");
    if (at == std::string::npos || text.find("\n" + key + "=", at + 1) != std::string::npos) {
        throw std::logic_error("the shipped file does not hold exactly one " + key + " line");
    }
    const size_t start = at + 1 + key.size() + 1;
    const size_t end = text.find_first_of("\r\n", start);
    return text.substr(0, start) + value + text.substr(end);
}

// Every hotkey code the frozen reader can hand the poller in range, 0x01 to 0xFE, on all three
// hotkeys at once. The corpus tries one alternate code per hotkey; this is where the codes the
// key table has no name for, or names only as a modifier, are covered.
std::vector<std::pair<std::string, std::string>> EveryHotkeyCode(const std::string& shipped) {
    std::vector<std::pair<std::string, std::string>> inputs;
    for (int vk = 0x01; vk <= 0xFE; ++vk) {
        char code[8];
        std::snprintf(code, sizeof(code), "0x%02X", static_cast<unsigned>(vk));
        std::string bytes = shipped;
        for (const char* key : {"ToggleKey", "PositionToggleKey", "YawModeKey"}) bytes = WithValue(bytes, key, code);
        inputs.emplace_back(std::string("every hotkey ") + code, bytes);
    }
    return inputs;
}

// The reader parses floats with atof and clamps with std::clamp, which hands a NaN back as it
// is, so each float it reads is tried with nan, inf and -inf.
std::vector<std::pair<std::string, std::string>> NonFiniteFloats(const std::string& shipped) {
    std::vector<std::pair<std::string, std::string>> inputs;
    for (const char* key : {"YawMultiplier", "PitchMultiplier", "RollMultiplier", "LocalSmoothing", "RemoteSmoothing",
                            "SensitivityX", "SensitivityY", "SensitivityZ", "LimitX", "LimitY", "LimitZ",
                            "LimitZBack"}) {
        for (const char* value : {"nan", "inf", "-inf"}) {
            inputs.emplace_back(std::string(key) + "=" + value, WithValue(shipped, key, value));
        }
    }
    return inputs;
}

void TestFrozenDefaults() {
    oracle_api::Config o = oracle_api::Defaults();
    const legacy::Config l;
    // The one default b285051 moved, which kComparisonOneDifferences lists.
    if (!o.positionInvertZ || l.positionInvertZ) Fail("defaults", "InvertZ is not the one default b285051 moved");
    o.positionInvertZ = l.positionInvertZ;
    for (const std::string& field : FieldDifferences(o, l)) {
        Fail("defaults", field + ": the frozen default differs from the published build's");
    }
}

// Registration compares the two builds by key and modifiers, which holds only while a binding
// with no modifiers fires as the old build's NavGuarded did (not while Ctrl and Shift are both
// held) and a Ctrl+Shift binding as its ChordGuarded did (while both are held). Alt changes
// neither.
void TestRegistrationModel() {
    using cameraunlock::input::detail::BindingFires;
    for (unsigned held = 0; held < 8; ++held) {
        const auto mods = static_cast<KeyModifiers>(held);
        const bool chordHeld = cameraunlock::input::HasModifiers(mods, KeyModifiers::kCtrl | KeyModifiers::kShift);
        if (BindingFires(KeyModifiers::kNone, mods) != !chordHeld) {
            Fail("registration", "a key with no modifiers does not fire as NavGuarded did, held " + std::to_string(held));
        }
        if (BindingFires(KeyModifiers::kCtrl | KeyModifiers::kShift, mods) != chordHeld) {
            Fail("registration", "a Ctrl+Shift key does not fire as ChordGuarded did, held " + std::to_string(held));
        }
    }
}

// The published build's first-run output, which inputs/first-run-v0.3.0.ini holds.
std::string OracleFirstRun(const std::wstring& root) {
    const Folders f = NextFolders(root);
    const std::wstring path = f.oracle + L"\\" + kIniName;
    oracle_api::Config created;
    if (oracle_api::LoadOrCreate(Narrow(path).c_str(), created) != oracle_api::LoadStatus::Created) {
        throw std::logic_error("the oracle read a file in an empty folder");
    }
    std::string bytes = ReadBytes(path);
    RemoveFolders(f);
    return bytes;
}

// Defaults.ini as a player may have changed it, from the one the owner created: every value this
// game takes from it differs from the built-in one, each set to the corpus's alternate for the
// legacy key it comes from, so a corpus input holding that alternate migrates as default.
void WriteAlteredDefaults(const MigrationTally& tally) {
    std::string text = ReadBytes(tally.builtin_defaults);
    const std::pair<const char*, const char*> changes[] = {
        {"UdpPort=4242", "UdpPort=5000"},
        {"EnableOnStartup=true", "EnableOnStartup=false"},
        {"WorldSpaceYaw=true", "WorldSpaceYaw=false"},
        {"PositionEnabled=true", "PositionEnabled=false"},
        {"LocalSmoothing=0.0", "LocalSmoothing=0.3"},
        {"RemoteSmoothing=0.15", "RemoteSmoothing=0.3"},
        {"PositionLimitX=0.3", "PositionLimitX=0.25"},
        {"PositionLimitY=0.2", "PositionLimitY=0.25"},
        {"PositionLimitYDown=0.2", "PositionLimitYDown=0.25"},
        {"PositionLimitZ=0.4", "PositionLimitZ=0.25"},
        {"PositionLimitZBack=0.1", "PositionLimitZBack=0.25"},
        {"ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=F1, Ctrl+Shift+Y"},
        {"CycleTrackingModeKey=PageUp, Ctrl+Shift+G", "CycleTrackingModeKey=F2, Ctrl+Shift+G"},
        {"YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=F3, Ctrl+Shift+H"},
    };
    for (const auto& [from, to] : changes) {
        const std::string line = std::string("\r\n") + from + "\r\n";
        const size_t at = text.find(line);
        if (at == std::string::npos) throw std::runtime_error(std::string("the created Defaults.ini has no line ") + from);
        text.replace(at + 2, std::strlen(from), to);
    }
    WriteBytes(tally.altered_defaults, text);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const std::wstring root = std::wstring(temp) + L"skyrimht-config-differential-" +
                                  std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(root.c_str(), nullptr);

        // `--first-run <path>` writes the published build's first-run output to <path> and runs
        // nothing else: how inputs/first-run-v0.3.0.ini was extracted.
        if (argc == 3 && std::strcmp(argv[1], "--first-run") == 0) {
            WriteBytes(Widen(argv[2]), OracleFirstRun(root));
            RemoveDirectoryW(root.c_str());
            return 0;
        }

        // Each Defaults.ini sits outside the game folder, in a user folder of its own whose
        // parent exists, as the owner requires before it creates the file.
        MigrationTally tally;
        tally.committed = ReadBytes(Widen(SKYRIMHT_COMMITTED_CONFIG));
        const std::wstring builtinUser = MakeFolder(root, L"user-builtin");
        const std::wstring alteredUser = MakeFolder(root, L"user-altered");
        tally.builtin_defaults = builtinUser + L"\\Defaults.ini";
        tally.altered_defaults = alteredUser + L"\\Defaults.ini";
        {
            const Folders f = NextFolders(root);
            cfg::ConfigOwner<Config> owner(Options(f.migration, tally.builtin_defaults));
            if (owner.Load().status != cfg::ConfigLoadStatus::Created) Fail("first load", "the first load is not Created");
            RemoveFolders(f);
        }
        WriteAlteredDefaults(tally);

        TestFrozenDefaults();
        TestRegistrationModel();

        const std::string shipped = ReadInput("shipped-v0.3.0.ini");
        const std::string firstRun = ReadInput("first-run-v0.3.0.ini");
        if (OracleFirstRun(root) != firstRun) {
            Fail("first run", "the oracle's first-run output is not inputs/first-run-v0.3.0.ini");
        }

        const std::vector<std::pair<std::string, std::optional<std::string>>> inputs = {
            {"no file", std::nullopt},
            {"empty file", std::string()},
            {"shipped, v0.1.0 to v0.2.0", ReadInput("shipped-v0.1.0.ini")},
            {"shipped, v0.3.0", shipped},
            {"committed, b285051", ReadInput("committed-b285051.ini")},
            {"committed, 3040cc0", ReadInput("committed-3040cc0.ini")},
            {"first run, v0.1.0 to v0.2.0", ReadInput("first-run-v0.1.0.ini")},
            {"first run, v0.3.0", firstRun},
        };
        for (const auto& [name, bytes] : inputs) RunInput(root, name, bytes, tally);
        TestUnopenableFile(root, shipped, tally);

        // Fresh equals upgrade: every file a release shipped, each version committed since and
        // the ones every release wrote at first launch convert, over Defaults.ini at the built-in
        // values, to the committed file, as no file is created as it.
        for (const auto& [name, bytes] : inputs) {
            if (!bytes || bytes->empty()) continue;
            const Folders f = NextFolders(root);
            const Migration m = Migrate(name, f.migration, bytes, false, tally.builtin_defaults);
            if (m.bytes != tally.committed) {
                Fail("fresh equals upgrade", name + " does not convert to the committed file");
            }
            RemoveFolders(f);
        }

        const std::vector<IniMutation> corpus = GenerateIniMutations(shipped, legacy::ReadKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(root, "corpus: " + m.name, m.bytes, tally);

        const auto codes = EveryHotkeyCode(shipped);
        for (const auto& [name, bytes] : codes) RunInput(root, name, bytes, tally);

        const auto nonFinite = NonFiniteFloats(shipped);
        for (const auto& [name, bytes] : nonFinite) RunInput(root, name, bytes, tally);

        std::printf("%zu inputs, %zu of them from the corpus, %zu with every hotkey on one code and %zu with a "
                    "float that is not finite\n",
                    inputs.size() + 1 + corpus.size() + codes.size() + nonFinite.size(), corpus.size(), codes.size(),
                    nonFinite.size());
        std::printf("comparison 1, the published build (v0.3.0) against the frozen reader: %zu listed differences\n",
                    std::size(kComparisonOneDifferences));
        for (const ListedDifference& d : kComparisonOneDifferences) {
            std::printf("  %s (%s): %d inputs\n    %s\n", d.id, d.commit, d.seen, d.what);
            if (d.seen == 0) Fail(d.id, "a listed difference no input shows");
        }
        std::printf("comparison 2, the frozen reader against the migration, over Defaults.ini at the built-in values "
                    "and changed: %d created, %d converted (%d holding a default row, %d a value), %zu distinct files\n",
                    tally.created, tally.converted, tally.with_default_rows, tally.with_values, tally.migrated.size());
        std::printf("  %d with a changed sensitivity or inversion dropped (pose_shaping)\n",
                    tally.with_pose_shaping_dropped);
        std::printf("  %d with [Crosshair] Show=false dropped (reticle)\n", tally.with_reticle);
        std::printf("  %d with a hotkey code outside 0x01-0xFE unbound (N1)\n", tally.with_n1);
        std::printf("  %d with a float that is not finite at the row's default (N2)\n", tally.with_n2);
        if (tally.with_pose_shaping_dropped == 0) Fail("pose shaping", "no input drops a changed value");
        if (tally.with_reticle == 0) Fail("reticle", "no input drops [Crosshair] Show=false");
        if (tally.with_n1 == 0) Fail("N1", "no input unbinds an out-of-range code");
        if (tally.with_n2 == 0) Fail("N2", "no input imports a float that is not finite as the default");
        if (tally.with_default_rows == 0) Fail("default rows", "no import writes default");
        if (tally.with_values == 0) Fail("values", "no import writes a value");
        if (tally.migrated.count(tally.committed) == 0) Fail("first run", "no input migrated to the committed file");

        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring lintDir(exe);
        lintDir = lintDir.substr(0, lintDir.find_last_of(L'\\'));
        lintDir = MakeFolder(lintDir, L"migrated");
        int n = 0;
        for (const std::string& file : tally.migrated) {
            WriteBytes(lintDir + L"\\" + std::to_wstring(n++) + L".ini", file);
        }

        for (const std::wstring& user : {builtinUser, alteredUser}) {
            EmptyFolder(user);
            RemoveDirectoryW(user.c_str());
        }
        RemoveDirectoryW(root.c_str());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("config differential: all passed\n");
        return 0;
    }
    std::printf("config differential: %d failure(s)\n", g_failures);
    return 1;
}
