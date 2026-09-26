// SPDX-License-Identifier: MIT
//
// CameraUnlock.ini against the table: the committed HeadTracking.ini is the table's fresh
// render byte for byte, the owner creates exactly those bytes, the defaults every published
// build ran on map to the table's defaults, each toggle's save changes the lines of its own
// rows and no other byte and leaves Defaults.ini and HeadTracking.ini alone, End's row cannot
// be saved, and a legacy file in a folder the ANSI code page cannot name imports as the
// published build read it. `--render-config <path>` writes the fresh render to <path> instead
// and runs nothing else (pixi run render-config).
//
// Every owner here reads and creates a scratch Defaults.ini, never the player's own.

#include "core/config.h"
#include "legacy_config/legacy_config.h"

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace cfg = cameraunlock::config;
using SkyrimHT::Config;

namespace {

int g_failures = 0;

void Check(bool cond, const std::string& what) {
    if (!cond) {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

std::string ReadBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read a test file");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const std::wstring& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write a test file");
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool Exists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::string FreshRender() {
    return cfg::RenderCanonicalFresh(SkyrimHT::MakeConfigTable(), cfg::RenderHeader{SkyrimHT::kConfigDisplayName});
}

std::string AllValues(const Config& c) {
    return cfg::RenderCanonical(SkyrimHT::MakeConfigTable(), c, cfg::RenderHeader{SkyrimHT::kConfigDisplayName});
}

// A fresh game folder in %TEMP%, ending in a separator, with Defaults.ini in a user folder of
// its own beside it.
struct Scratch {
    std::wstring folder;
    std::wstring defaults;
};

std::wstring ScratchRoot() {
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    return std::wstring(temp) + L"skyrimht-config-tests-" + std::to_wstring(GetCurrentProcessId());
}

Scratch MakeScratch(const std::wstring& name) {
    const std::wstring root = ScratchRoot();
    CreateDirectoryW(root.c_str(), nullptr);
    const std::wstring dir = root + L"\\" + name;
    if (!CreateDirectoryW(dir.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    const std::wstring user = dir + L"\\user";
    if (!CreateDirectoryW(user.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    const std::wstring game = dir + L"\\game";
    if (!CreateDirectoryW(game.c_str(), nullptr)) throw std::runtime_error("cannot create a scratch folder");
    return {game + L"\\", user + L"\\CameraUnlock\\Defaults.ini"};
}

// The scratch tree, read-only files included.
void RemoveTree(const std::wstring& dir) {
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        const std::wstring path = dir + L"\\" + name;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            RemoveTree(path);
        } else {
            SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(path.c_str());
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    RemoveDirectoryW(dir.c_str());
}

cfg::ConfigOwnerOptions<Config> Options(const Scratch& s) {
    return SkyrimHT::MakeConfigOwnerOptions(s.folder, cfg::DefaultsFile::At(s.defaults));
}

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < bytes.size()) {
        const size_t end = bytes.find("\r\n", start);
        if (end == std::string::npos) {
            lines.push_back(bytes.substr(start));
            break;
        }
        lines.push_back(bytes.substr(start, end - start));
        start = end + 2;
    }
    return lines;
}

// The lines of `after` that differ from `before`, which must have as many.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before);
    const std::vector<std::string> b = Lines(after);
    if (a.size() != b.size()) return {"a line was added or removed"};
    std::vector<std::string> changed;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

void CommittedFileIsTheFreshRender() {
    std::printf("HeadTracking.ini, the committed file, is the table's fresh render\n");
    Check(ReadBytes(SKYRIMHT_COMMITTED_CONFIG) == FreshRender(), "run pixi run render-config after changing a row");
}

void EveryHotkeyDefaultParses() {
    std::printf("every hotkey list the table defaults to parses\n");
    const Config defaults = SkyrimHT::MakeConfigTable().defaults();
    for (const std::string* list :
         {&defaults.toggle_key_name, &defaults.cycle_tracking_mode_key_name, &defaults.yaw_mode_key_name}) {
        Check(cameraunlock::input::ParseKeyBindings(*list).ok(), *list);
    }
    Check(defaults.toggle_key_name == "End, Ctrl+Shift+Y", "ToggleKey is the fleet's default");
    Check(defaults.cycle_tracking_mode_key_name == "PageUp, Ctrl+Shift+G", "CycleTrackingModeKey is the fleet's default");
    Check(defaults.yaw_mode_key_name == "PageDown, Ctrl+Shift+H", "YawModeKey is the fleet's default");
    Check(defaults.world_space_yaw, "WorldSpaceYaw starts true");
}

// A fresh install and an update from a build that ran on its defaults start the same: the
// frozen defaults map to the table's defaults, every pose-shaping value is the shipped one,
// which the lean boundary now applies itself, and nothing is dropped.
void LegacyDefaultsMapToTheDefaults() {
    std::printf("the defaults the published builds ran on map to the table's defaults\n");
    const std::wstring missing = ScratchRoot() + L"\\no-such-folder\\HeadTracking.ini";
    const cfg::ConfigTable<Config> table = SkyrimHT::MakeConfigTable();
    Config mapped = table.defaults();
    std::string ansi;
    for (const wchar_t ch : missing) ansi.push_back(static_cast<char>(ch));
    const cfg::ImportResult result = SkyrimHT::MakeLegacyImport().run(cfg::LegacyInput{missing, ansi, false}, mapped);
    Check(result.status == cfg::ImportStatus::Absent, "no file imports as Absent");
    Check(result.dropped.empty(), "the old defaults drop nothing");
    Check(result.pose_shaping.size() == 9, "every sensitivity and inversion is recorded");
    for (const cfg::PoseShapingValue& value : result.pose_shaping) {
        Check(value.folded, "[" + value.section + "] " + value.key + " at its shipped value is folded");
    }
    Check(AllValues(mapped) == AllValues(table.defaults()), "the old defaults map to the defaults");
    Check(!mapped.position.invert_x && !mapped.position.invert_y && !mapped.position.invert_z,
          "the processor gets no inversion: the shipped InvertX is the boundary's x negation");
}

void FirstLaunchCreatesTheCommittedFile() {
    std::printf("the first launch with no file creates the committed bytes\n");
    const Scratch s = MakeScratch(L"created");
    cfg::ConfigOwner<Config> owner(Options(s));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Created, "the load is Created");
    Check(ReadBytes(s.folder + SkyrimHT::kConfigFileName) == FreshRender(), "the created file is the fresh render");
    Check(!Exists(s.folder + SkyrimHT::kLegacyConfigFileName), "no HeadTracking.ini is written");
    Check(Exists(s.defaults), "the first launch creates Defaults.ini");
    Check(AllValues(loaded.config) == AllValues(SkyrimHT::MakeConfigTable().defaults()),
          "the first launch runs on the built-in values");
}

// A save changes the lines of its rows and no other byte, writes a value over default, and
// touches neither Defaults.ini nor HeadTracking.ini; the yaw mode and the tracking mode
// persist, and End's row cannot be saved at all.
void TogglesSaveTheirRowsOnly() {
    std::printf("each toggle's save writes its own rows and no other byte\n");
    const Scratch s = MakeScratch(L"saves");
    const std::wstring path = s.folder + SkyrimHT::kConfigFileName;
    const std::wstring legacyPath = s.folder + SkyrimHT::kLegacyConfigFileName;
    const std::string legacyBytes = "[General]\r\nAutoEnable=false\r\n";
    WriteBytes(path, FreshRender());
    WriteBytes(legacyPath, legacyBytes);

    cfg::ConfigOwner<Config> owner(Options(s));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Canonical, "the committed file loads as canonical");
    Check(loaded.config.enable_on_startup, "HeadTracking.ini is not read while CameraUnlock.ini exists");
    Check(Contains(loaded.log, "is left as it was and is not read"),
          "the log says HeadTracking.ini is not read while CameraUnlock.ini exists");
    const std::string fresh = ReadBytes(path);
    const std::string defaultsBefore = ReadBytes(s.defaults);

    const cfg::ConfigSaveResult yaw = owner.Save([](Config& c) { c.world_space_yaw = false; });
    Check(yaw.status == cfg::ConfigSaveStatus::Saved, "the yaw save is Saved");
    Check(Contains(yaw.log, "WorldSpaceYaw=false is now set for this game, and no longer follows Defaults.ini"),
          "the log says WorldSpaceYaw no longer follows Defaults.ini");
    const std::string afterYaw = ReadBytes(path);
    Check(ChangedLines(fresh, afterYaw) == std::vector<std::string>{"WorldSpaceYaw=false"},
          "only WorldSpaceYaw=default became false");

    const auto rotationOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
    const cfg::ConfigSaveResult mode = owner.Save([rotationOnly](Config& c) {
        c.rotation_enabled = rotationOnly.rotation_enabled;
        c.position_enabled = rotationOnly.position_enabled;
    });
    Check(mode.status == cfg::ConfigSaveStatus::Saved, "the mode save is Saved");
    const std::string afterRotationOnly = ReadBytes(path);
    Check(ChangedLines(afterYaw, afterRotationOnly) ==
              std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=false"},
          "a mode change writes both rows of the pair and nothing else");

    const auto positionOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::PositionOnly);
    Check(owner.Save([positionOnly](Config& c) {
              c.rotation_enabled = positionOnly.rotation_enabled;
              c.position_enabled = positionOnly.position_enabled;
          }).status == cfg::ConfigSaveStatus::Saved,
          "the third tracking mode saves");
    const std::string afterPositionOnly = ReadBytes(path);
    Check(ChangedLines(afterRotationOnly, afterPositionOnly) ==
              std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"},
          "saving position only changes the mode pair and nothing else");

    bool threw = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::logic_error&) {
        threw = true;
    }
    Check(threw, "EnableOnStartup is not Writable: End never persists");
    Check(ReadBytes(path) == afterPositionOnly, "a refused save writes nothing");
    Check(ReadBytes(s.defaults) == defaultsBefore, "saving leaves Defaults.ini as it was");
    Check(ReadBytes(legacyPath) == legacyBytes, "saving leaves HeadTracking.ini as it was");

    cfg::ConfigOwner<Config> again(Options(s));
    const cfg::ConfigLoadResult<Config> reread = again.Load();
    Check(reread.status == cfg::ConfigLoadStatus::Canonical && reread.diagnostics.empty(),
          "the saved file reads back as canonical");
    Check(!reread.config.world_space_yaw, "the yaw choice survives a restart");
    Check(!reread.config.rotation_enabled && reread.config.position_enabled, "the mode survives a restart");
    Check(reread.config.enable_on_startup, "EnableOnStartup is as the file says");
}

// The published builds read HeadTracking.ini by the ANSI path GetModuleFileNameA gave them. In a
// folder the ANSI code page cannot name, that path names no file, so they ran on their defaults.
// The import opens it the same way, so such a player starts as before.
void AFolderTheCodePageCannotNameImportsAsThePublishedBuildReadIt() {
    std::printf("a legacy file in a folder the ANSI code page cannot name imports as it was read\n");
    const Scratch s = MakeScratch(L"skyrim-\x4E2D");
    const std::wstring legacyPath = s.folder + SkyrimHT::kLegacyConfigFileName;
    WriteBytes(legacyPath, "[Network]\r\nUDPPort=5000\r\n");
    BOOL usedDefault = FALSE;
    char narrow[MAX_PATH];
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, legacyPath.c_str(), -1, narrow, MAX_PATH, nullptr, &usedDefault);

    cfg::ConfigOwner<Config> owner(Options(s));
    const cfg::ConfigLoadResult<Config> loaded = owner.Load();
    Check(ReadBytes(legacyPath) == "[Network]\r\nUDPPort=5000\r\n", "the legacy file keeps its bytes");
    if (usedDefault) {
        Check(loaded.config.udp_port == 4242, "the port is the default the published build ran on");
    } else {
        std::printf("  note: this system's ANSI code page names the folder, so the file is read\n");
        Check(loaded.status == cfg::ConfigLoadStatus::Migrated && loaded.config.udp_port == 5000,
              "the file is read through its ANSI path");
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
            const std::string path = argv[2];
            WriteBytes(std::wstring(path.begin(), path.end()), FreshRender());
            std::printf("wrote %s\n", argv[2]);
            return 0;
        }

        std::printf("SkyrimSEHeadTracking config tests\n=================================\n");
        RemoveTree(ScratchRoot());
        CommittedFileIsTheFreshRender();
        EveryHotkeyDefaultParses();
        LegacyDefaultsMapToTheDefaults();
        FirstLaunchCreatesTheCommittedFile();
        TogglesSaveTheirRowsOnly();
        AFolderTheCodePageCannotNameImportsAsThePublishedBuildReadIt();
        RemoveTree(ScratchRoot());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    if (g_failures == 0) {
        std::printf("All tests passed!\n");
        return 0;
    }
    std::printf("%d test(s) FAILED\n", g_failures);
    return 1;
}
