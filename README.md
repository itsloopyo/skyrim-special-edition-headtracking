# Skyrim SE Head Tracking

![Skyrim Special Edition running with this mod](https://raw.githubusercontent.com/itsloopyo/skyrim-special-edition-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Skyrim Special Edition that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view; the game's aim stays on your mouse or controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- [The Elder Scrolls V: Skyrim Special Edition](https://store.steampowered.com/app/489830/) (Steam, Anniversary Edition 1.6.x)
- [OpenTrack](https://github.com/opentrack/opentrack) or a compatible head tracking app (smartphone, webcam, or dedicated hardware)
- Windows 10 / 11 (64-bit)

## Installation

1. Download the latest `SkyrimSEHeadTracking-v<version>-installer.zip` from the [Releases page](https://github.com/itsloopyo/skyrim-special-edition-headtracking/releases)
2. Extract the ZIP anywhere
3. Double-click `install.cmd`
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`
5. Launch the game

The installer finds your Skyrim SE install via Steam registry. If it can't find the game:

- Set the `SKYRIM_SE_PATH` environment variable to your game folder, or
- Run from a command prompt: `install.cmd "D:\Games\Skyrim Special Edition"`

### Manual Installation

For users on Nexus Mods or anyone who prefers to drop files in by hand:

**Step 1: Install Ultimate ASI Loader**

1. Download `Ultimate-ASI-Loader_x64.zip` from [ThirteenAG's releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)
2. Extract `dinput8.dll` into your Skyrim SE directory (the folder containing `SkyrimSE.exe`)

**Step 2: Install the mod**

1. Grab the `-nexus.zip` variant from the Releases page
2. Copy the contents of the `Root/` folder (`SkyrimSEHeadTracking.asi`) into your Skyrim SE directory, next to `SkyrimSE.exe`

`HeadTracking.ini` is written automatically next to `SkyrimSE.exe` on first launch - there's no need to copy it in, and it isn't bundled so updates never overwrite your settings.

**Mod Organizer 2:** install the `-nexus.zip` as a normal mod. The `Root/` folder is handled by the Root Builder plugin, which deploys it to the game root. Without Root Builder, install manually as above - MO2's virtual file system only covers `Data/`, not the game root where `.asi` files must live.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches between horizon-locked yaw (default) and camera-local yaw.

## Configuration

The mod writes `HeadTracking.ini` next to `SkyrimSE.exe` on first launch. Edit it to customize:

```ini
[Network]
UDPPort=4242              ; UDP port for tracker data (1024-65535)

[Sensitivity]
YawMultiplier=1.0         ; Horizontal rotation sensitivity (0.1-5.0)
PitchMultiplier=1.0       ; Vertical rotation sensitivity (0.1-5.0)
RollMultiplier=1.0        ; Head tilt sensitivity (0.0-2.0)
LocalSmoothing=0.0        ; Smoothing for a tracker on this PC (0.0-1.0)
RemoteSmoothing=0.15      ; Smoothing for a tracker on the network (0.0-1.0)

[Position]
SensitivityX=1.0          ; Lateral position sensitivity (0.1-10.0)
SensitivityY=1.0          ; Vertical position sensitivity (0.1-10.0)
SensitivityZ=1.0          ; Depth position sensitivity (0.1-10.0)
LimitX=0.30               ; Max lateral offset in meters
LimitY=0.20                ; Max vertical offset in meters
LimitZ=0.40                ; Max forward offset in meters
LimitZBack=0.10           ; Max backward offset (prevents camera clipping)
InvertX=true              ; Invert lateral axis
InvertY=false             ; Invert vertical axis
; InvertZ is for a tracker that sends depth backwards, not for a lean that
; feels reversed. It is applied before the LimitZ / LimitZBack clamp, so
; turning it on also swaps the travel budgets to 0.10m forward and 0.40m back.
InvertZ=false
Enabled=true              ; Enable 6DOF (set false for rotation-only 3DOF)

[Hotkeys]
ToggleKey=0x23            ; End key (virtual key code in hex)
PositionToggleKey=0x21    ; Page Up key
YawModeKey=0x22           ; Page Down key - toggle world/local yaw

[General]
AutoEnable=true           ; Start tracking when the game launches
ShowNotifications=true    ; Write status messages to HeadTracking.log
WorldSpaceYaw=true        ; true = horizon-locked yaw (default), false = camera-local
```

Delete the file to reset to defaults.

## Troubleshooting

**Mod not loading:**

- Verify `dinput8.dll` (ASI Loader) is in your Skyrim SE directory alongside `SkyrimSE.exe`
- Check that `SkyrimSEHeadTracking.asi` is in the same directory
- Check `HeadTracking.log` in the game folder for error messages. It is rewritten
  on every launch; the previous session is kept as `HeadTracking.prev.log`, which
  is the one to send after a crash

**No tracking response:**

- Ensure your tracker is running and outputting data
- Verify the UDP port matches in both tracker and `HeadTracking.ini`
- Press **End** to enable tracking if `AutoEnable` is off
- Check that your firewall isn't blocking UDP port 4242

**View is off-centre:**

- Centre in your tracker app: OpenTrack's Center bind, the CENTER button in a phone app, or your headset's own centring. The mod applies what the tracker sends and keeps no centre of its own, so the tracker is the only place to set one.

**Jittery or unstable tracking:**

- Increase filtering in your tracker software, or raise `LocalSmoothing` (tracker on this PC) / `RemoteSmoothing` (tracker on the network) in `HeadTracking.ini`
- Reduce sensitivity multipliers in `HeadTracking.ini`
- Improve lighting for webcam-based tracking
- If you're streaming from a phone over WiFi, some jitter is expected; send via a wired hotspot or switch to webcam tracking for the smoothest signal

**Wrong rotation axis (head rotates the view the wrong way):**

- Flip `[Position] InvertX` or `InvertY` if a lateral or vertical lean moves the view the wrong way
- `InvertZ` is for a tracker that sends depth backwards, not for a lean that feels reversed. It is applied before the `LimitZ` / `LimitZBack` clamp, so switching it on also swaps the travel budgets to 0.10m forward and 0.40m back
- For rotation axes, flip the sign on the sensitivity multiplier (e.g. `PitchMultiplier=-1.0`)
- Centre in your tracker app after changing signs so the new orientation is taken as the neutral pose

**Yaw feels wrong when looking up or down at extreme angles:**

- Try toggling between world-locked and camera-local yaw with `Page Down`. World-locked (default) is horizon-stable; camera-local follows the camera's current up-axis.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd` from the release folder. This removes the mod DLLs. Ultimate ASI Loader (`dinput8.dll`) is only removed if the installer originally put it there. To remove it anyway:

```
uninstall.cmd /force
```

To remove manually, delete these files from your Skyrim SE directory:

- `SkyrimSEHeadTracking.asi`
- `HeadTracking.ini`
- `HeadTracking.log` and `HeadTracking.prev.log` (if present)
- `dinput8.dll` (only if you also want to remove the ASI Loader)

## Building from Source

### Prerequisites

- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the C++ desktop development workload
- [CMake 3.20+](https://cmake.org/)
- [pixi](https://pixi.sh) task runner

### Build

```bash
git clone --recurse-submodules https://github.com/itsloopyo/skyrim-special-edition-headtracking.git
cd skyrim-special-edition-headtracking

# Build and install to game
pixi run install

# Build only
pixi run build-release

# Package for release
pixi run package
```

**Manual CMake:**

```bash
cmake -B build -A x64
cmake --build build --config Release
```

Output: `bin/Release/SkyrimSEHeadTracking.asi`

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT. See [LICENSE](LICENSE).

## Credits

- [Bethesda Game Studios](https://bethesdagamestudios.com/) and [Bethesda Softworks](https://bethesda.net/) - developer and publisher of Skyrim Special Edition
- [OpenTrack](https://github.com/opentrack/opentrack) - Head tracking protocol and software
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) - ASI plugin loading
- [MinHook](https://github.com/TsudaKageyu/minhook) - API hooking library
- [inih](https://github.com/benhoyt/inih) - INI file parser
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR) - the published engine notes this mod's offsets were cross-checked against. None of its code is used or linked here; see [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)
- [SKSE](https://skse.silverlock.org/) and the Address Library (meh321) - the wider Skyrim modding effort that makes mods like this possible
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) - shared head tracking pipeline

This is an unofficial, fan-made modification. It is not affiliated with,
endorsed by, or sponsored by Bethesda Game Studios, Bethesda Softworks, or any
other rights holder, and it requires a legitimately purchased copy of the game.
