# Changelog

## [0.3.0] - 2026-08-20

### Added

- split smoothing into local/remote and drop mod-side recentring

## [Unreleased]

### Changed

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
