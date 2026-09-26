// SPDX-License-Identifier: MIT
//
// The config differential test. Every input is read two ways:
//
//   oracle     the published build's reader (oracle/), v0.3.0 at c3a1f7f, with its
//              Mod::LoadConfig
//   import     the frozen reader in src/legacy_config/
//
// Comparison 1, oracle against import, finds what a player updating from the published build
// sees change that the conversion did not cause. Every difference it may find is listed in
// kComparisonOneDifferences with the commit that made it; any other fails the test.
//
// Inputs: no file, an empty file, the HeadTracking.ini each release shipped (v0.1.0, v0.1.1 and
// v0.2.0 shipped one file and v0.3.0 another, in the installer ZIP's plugins\ and as the launcher
// seed from v0.1.1 on; the Nexus ZIPs carry none), the two versions committed since v0.3.0, the
// file v0.3.0 writes at first launch when there is none (extracted once into inputs/ with
// --first-run), core's corpus over the v0.3.0 file, the v0.3.0 file with all three hotkeys on
// each code from 0x01 to 0xFE, the v0.3.0 file with a number that is not finite on each float it
// reads, and a legacy file another program holds open with no sharing.

#include "legacy_config/legacy_config.h"
#include "oracle_adapter.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace legacy = SkyrimHT::legacy;
namespace cfg = cameraunlock::config;
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
// The run
// ---------------------------------------------------------------------------

// Each input is read in folders of its own, so no reader ever meets a file another input left.
struct Folders {
    std::wstring root;
    std::wstring oracle;
    std::wstring import;
};

Folders NextFolders(const std::wstring& root) {
    static int n = 0;
    const std::wstring dir = MakeFolder(root, std::to_wstring(n++).c_str());
    return {dir, MakeFolder(dir, L"oracle"), MakeFolder(dir, L"import")};
}

void RemoveFolders(const Folders& f) {
    for (const std::wstring& dir : {f.oracle, f.import, f.root}) {
        EmptyFolder(dir);
        if (!RemoveDirectoryW(dir.c_str())) throw std::runtime_error("cannot remove a test folder");
    }
}

const wchar_t kIniName[] = L"HeadTracking.ini";

void RunInput(const std::wstring& root, const std::string& name, const std::optional<std::string>& bytes) {
    const Folders f = NextFolders(root);
    OracleRun o;
    {
        const std::wstring path = f.oracle + L"\\" + kIniName;
        if (bytes) WriteBytes(path, *bytes);
        o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
    }

    ImportRun i;
    {
        const std::wstring path = f.import + L"\\" + kIniName;
        if (bytes) {
            WriteBytes(path, *bytes);
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY);
        }
        const auto before = Snapshot(f.import);
        i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        if (Snapshot(f.import) != before) Fail(name, "the import changed the folder it read from");
        if (!bytes && i.status != legacy::ReadStatus::Absent) Fail(name, "the import read a file that is not there");
    }

    CompareOracleWithImport(name, o, i);
    RemoveFolders(f);
}

// A legacy file another program holds open with no sharing. inih cannot open it, so the
// published build ran on its defaults and failed to write over it, and the import reads it as
// absent.
void TestUnopenableFile(const std::wstring& root, const std::string& shipped) {
    const std::string name = "a legacy file another program holds open with no sharing";
    const Folders f = NextFolders(root);
    OracleRun o;
    ImportRun i;
    for (const std::wstring& dir : {f.oracle, f.import}) {
        const std::wstring path = dir + L"\\" + kIniName;
        WriteBytes(path, shipped);
        HANDLE held = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot hold the test file open");
        if (dir == f.oracle) {
            o.status = oracle_api::LoadOrCreate(Narrow(path).c_str(), o.cfg);
        } else {
            i.status = legacy::Read(Narrow(path).c_str(), i.cfg);
        }
        CloseHandle(held);
        if (ReadBytes(path) != shipped) Fail(name, "a build rewrote a file it could not open");
    }
    CompareOracleWithImport(name, o, i);
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
            {"first run, v0.3.0", firstRun},
        };
        for (const auto& [name, bytes] : inputs) RunInput(root, name, bytes);
        TestUnopenableFile(root, shipped);

        const std::vector<IniMutation> corpus = GenerateIniMutations(shipped, legacy::ReadKeys(), CorpusKeys());
        for (const IniMutation& m : corpus) RunInput(root, "corpus: " + m.name, m.bytes);

        const auto codes = EveryHotkeyCode(shipped);
        for (const auto& [name, bytes] : codes) RunInput(root, name, bytes);

        const auto nonFinite = NonFiniteFloats(shipped);
        for (const auto& [name, bytes] : nonFinite) RunInput(root, name, bytes);

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
