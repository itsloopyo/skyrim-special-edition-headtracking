# Changelog

## [0.3.0] - 2026-08-20

### Added

- split smoothing into local/remote and drop mod-side recentring

## [Unreleased]

### Changed

- Settings move to `CameraUnlock.ini`, next to `SkyrimSE.exe`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - Reticle settings, and a key that toggled the reticle.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] PositionToggleKey` is now `CycleTrackingModeKey`, and the chords, fixed in code before, can be changed or removed like any other key.
- The tracking mode (`Page Up`) and the yaw mode (`Page Down`) are saved to `CameraUnlock.ini` the moment you change them, so the next launch starts with the same choice. They lasted for the session only before. `End` still changes the session only.
- `[General] AutoEnable` is now `EnableOnStartup`, `[Network] UDPPort` is now `UdpPort`, `[Sensitivity] LocalSmoothing` and `RemoteSmoothing` move to `[Smoothing]`, and `[Position] LimitX`, `LimitY`, `LimitZ` and `LimitZBack` are now `PositionLimitX`, `PositionLimitY`, `PositionLimitZ` and `PositionLimitZBack`. `LimitY` bounded leaning down as well as up; the import writes its value into both `PositionLimitY` and the new `PositionLimitYDown`. `[Position] Enabled` chose the tracking mode the game started in and is now that mode, as `RotationEnabled` and `PositionEnabled`.
- The installer no longer copies a config into the game folder. Every earlier installer copied its `HeadTracking.ini` over yours on each install, and `uninstall.cmd` deleted it; now the mod creates `CameraUnlock.ini` itself and the uninstall leaves `CameraUnlock.ini` and `HeadTracking.ini` in place.
- Leaning down is bounded by `LimitY`, as leaning up is (6d25db0). 0.3.0 held the downward lean at 0.20 m whatever `LimitY` said, so with `LimitY` changed from 0.20 the downward travel changes to match.
- With no `HeadTracking.ini`, or one with no `InvertZ` line, depth now runs the fixed way described under Fixed (b285051). 0.3.0 took `InvertZ=true` there.
- Recentring is gone entirely: the `Home` / `Ctrl+Shift+T` hotkey, the
  `RecenterKey` ini entry, the "View Recentered" notification, and the mod's own
  centre. Your tracker owns the centre now. Set it there, with OpenTrack's Center
  bind, the CENTER button in a phone app, or your headset's own centring, and the
  mod applies what the tracker sends. Two centres in series was the problem: when
  the view was off you could not tell which side was wrong, and switching trackers
  meant centring in both.
- the log records the first RAW tracker sample the receiver accepts, ahead of the enable and gameplay gates, so a "no head tracking" report can be told apart from a tracker that never reached the receiver. Previously the log ended at "Initialization complete" either way.
- `HeadTracking.log` keeps one previous generation as `HeadTracking.prev.log`. It was already rewritten per launch, so a crash report was destroyed by the relaunch that came before the user sent it.

- smoothing is now two user-configurable keys in `[Sensitivity]`: `LocalSmoothing` (default 0.0, tracker running on this PC) and `RemoteSmoothing` (default 0.15, tracker on a remote network device). The value is picked per connection from the packet source address and covers both rotation and position.
- removed `[Sensitivity] RotationSmoothing` and `[Position] Smoothing`.
- removed the hidden 0.15 baseline smoothing floor, so a local tracker now gets zero-latency, unsmoothed tracking by default.

### Removed

- The sensitivity, scale, deadzone, response curve and axis inversion settings: `[Sensitivity] YawMultiplier`, `PitchMultiplier` and `RollMultiplier`, and `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`, `InvertX`, `InvertY` and `InvertZ`. Set these in your tracker app instead. The x inversion every earlier version shipped switched on (`InvertX=true`) is now part of how the mod converts the tracker's axes to the game's, so leaning left and right goes the same way it did.
- Every file 0.1.0 to 0.3.0 shipped or wrote set `InvertZ=true`, which is what made depth run backwards. The other inversions are built into the mod at the values those versions shipped, but `InvertZ=true` is dropped even though it was the shipped value, so after updating depth runs the fixed way described under Fixed, whatever your old file said. If you had set `InvertZ=false` yourself, depth already ran that way and does not change. The other sensitivity and inversion settings shipped at the same defaults in every copy (installer, launcher seed and first-run file), and with those at their shipped defaults the camera moves as it did before.
- `[Crosshair] Show`. The game's crosshair now always moves to follow your aim while head tracking is on. `Show=false` in an old file is not carried over.

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.
- build profile for SkyrimSE.exe 1.7.104.0, the Steam build published on
  2026-08-30. The crosshair override and the projectile lean offset were dormant
  on it ("unsupported game build 1.7.104.0" in the log) because their two RVAs
  had only ever been pinned for 1.6.1170. The 1.6.1170 profile is untouched and
  still matches, so a player who stays on that build keeps both features.

### Fixed

- 6DOF depth ran backwards: leaning in pushed the camera out and pulling back
  pulled it in. The depth sign is flipped at the engine boundary, so the
  asymmetric budget stays attached to the physical direction - 0.40m of travel
  leaning in, 0.10m pulling back to stop the camera clipping through the player.
  Arrow launch origins follow the same offset, so they move with it.

## [0.1.0] - 2026-05-18

## [0.2.0] - 2026-08-03

### Fixed

- show full control set in pixi install via shared -Controls

## [0.1.1] - 2026-06-08

### Added

- add HeadTrackingSession and expand C++ core with RE Engine, Unreal, and tracking-session modules
- aim projection, reframework/unreal hooks, input/logging hardening, games
- add Mass Effect Legendary Edition to games catalog
- expand games catalog, fix unicode games.json read, stage launcher manifest
- add Pacific Drive to games catalog
- add Homeworld: Remastered Collection to games catalog
- add manifest-mode installer validator and ASI loader subdir support
- authenticate GitHub API requests via env token when present
- add R.E.P.O. detection data

### Fixed

- fail fast in ASI dev-deploy when the game is running
- restore il2cpp camera position by undoing applied local delta
- set SO_REUSEADDR so the receiver reclaims its port on relaunch

### Other

- protocol: reject finite-but-out-of-float-range packet values
- data: add Subnautica 2 to games registry
- detection: add installer-registry game path lookup (Black & White GameDir)
- protocol: reorder tracking data member in udp_receiver
- data: fix Subnautica 2 Steam app id (3367150 -> 1962700)
- data: add Ni no Kuni Remastered and Yakuza 0; switch find-game output to UTF-8
- detection: add Xbox/GDK build support for Subnautica 2 (and any future GDK title)
- find-game: escape `&` in GAME_DISPLAY_NAME so echo doesn't split
- templates: add uninstall.ps1; data: add Deus Ex Mankind Divided
- powershell: add NightlyRelease module for Patreon-gated nightly builds
- protocol: disable SIO_UDP_CONNRESET and add one-shot receiver diagnostics; powershell: write nightly manifest.json without UTF-8 BOM; data: add Mixtape
- powershell: stop redirecting git stderr in Update-CameraUnlockCoreToRemoteTip
- powershell: publish dev builds as GitHub pre-releases
- protocol: disable SIO_UDP_CONNRESET and add one-shot receiver diagnostics
- data: add Mixtape
- powershell: stop redirecting git stderr in Update-CameraUnlockCoreToRemoteTip
- powershell: run gh under Continue so its stderr doesn't abort the dev-release publish
- reframework: strip VR runtime DLLs on install for flatscreen mode
- reframework: cache GetValue method and avoid per-call heap in ArrayGetValue; data: add BioShock Infinite
- uninstall: remove reframework_revision.txt marker dropped at game root
- install: render MOD_CONTROLS multi-line via percent expansion
- Add YAPYAP to games.json
- powershell: write state file BOM-less so Lopari JSON parser accepts it
- Move CI build logic into pixi, add launcher manifest, bump ASI loader to v9.7.2
- powershell: stop redirecting git stderr in Invoke-VersionCommit

### Other

- Hello world
