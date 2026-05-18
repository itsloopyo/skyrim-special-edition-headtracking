# Skyrim SE Head Tracking

![Mod GIF](https://raw.githubusercontent.com/itsloopyo/skyrim-special-edition-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Skyrim Special Edition. Look around naturally with your head while your mouse and keyboard/controller still handle aim, no VR headset required.

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

1. Download and install [OpenTrack](https://github.com/opentrack/opentrack/releases)
2. Configure your tracker as input
3. Set output to **UDP over network**
4. Host: `127.0.0.1`, Port: `4242`
5. Start tracking before launching the game

### Webcam Setup

No special hardware needed. OpenTrack's built-in **neuralnet tracker** uses any webcam for 6DOF face tracking.

1. In OpenTrack, set the input to **neuralnet tracker**
2. Select your webcam in the tracker settings
3. Set output to **UDP over network** (`127.0.0.1:4242`)
4. Start tracking before launching the game
5. Recenter in OpenTrack via its hotkey, and press **Home** in-game to recenter the mod as needed

### Phone App Setup

This mod includes built-in smoothing for network jitter, so if your tracking app already sends a filtered signal you can point the phone straight at port 4242 without routing through OpenTrack on the PC.

1. Install an OpenTrack-compatible head tracking app from your phone's app store
2. Configure the phone app to send to your PC's IP address on port 4242 (run `ipconfig` on the PC to find it, e.g. `192.168.1.100`)
3. Set the protocol to OpenTrack/UDP
4. Start tracking

**With OpenTrack (optional):** If you want curve mapping or a visual preview, route through OpenTrack. Set OpenTrack's input to "UDP over network" on a different port (e.g. 5252), point the phone app at that port, and set OpenTrack's output to `127.0.0.1:4242`. Make sure your firewall allows incoming UDP on the input port.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Recenter            | `Home`      | `Ctrl+Shift+T`  |
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

[Position]
SensitivityX=1.0          ; Lateral position sensitivity (0.1-10.0)
SensitivityY=1.0          ; Vertical position sensitivity (0.1-10.0)
SensitivityZ=1.0          ; Depth position sensitivity (0.1-10.0)
LimitX=0.30               ; Max lateral offset in meters
LimitY=0.20                ; Max vertical offset in meters
LimitZ=0.40                ; Max forward offset in meters
LimitZBack=0.10           ; Max backward offset (prevents camera clipping)
Smoothing=0.15            ; Position smoothing (0.0-0.99)
InvertX=true              ; Invert lateral axis
InvertY=false             ; Invert vertical axis
InvertZ=true              ; Invert depth axis
Enabled=true              ; Enable 6DOF (set false for rotation-only 3DOF)

[Hotkeys]
ToggleKey=0x23            ; End key (virtual key code in hex)
RecenterKey=0x24          ; Home key
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
- Check `HeadTracking.log` in the game folder for error messages

**No tracking response:**

- Ensure your tracker is running and outputting data
- Verify the UDP port matches in both tracker and `HeadTracking.ini`
- Press **End** to enable tracking if `AutoEnable` is off
- Press **Home** to recenter if the view is offset
- Check that your firewall isn't blocking UDP port 4242

**Jittery or unstable tracking:**

- Increase filtering in your tracker software, or raise `Smoothing` in `HeadTracking.ini`
- Reduce sensitivity multipliers in `HeadTracking.ini`
- Improve lighting for webcam-based tracking
- If you're streaming from a phone over WiFi, some jitter is expected; send via a wired hotspot or switch to webcam tracking for the smoothest signal

**Wrong rotation axis (head rotates the view the wrong way):**

- Invert the position axis in the `[Position]` section (`InvertX`, `InvertY`, `InvertZ`)
- For rotation axes, flip the sign on the sensitivity multiplier (e.g. `PitchMultiplier=-1.0`)
- Press **Home** after changing signs so the new orientation is taken as the neutral pose

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
- `HeadTracking.log` (if present)
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
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `bin/Release/SkyrimSEHeadTracking.asi`

## License

MIT. See [LICENSE](LICENSE).

## Credits

- [Bethesda Game Studios](https://bethesdagamestudios.com/) - Skyrim Special Edition
- [OpenTrack](https://github.com/opentrack/opentrack) - Head tracking protocol and software
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) - ASI plugin loading
- [MinHook](https://github.com/TsudaKageyu/minhook) - API hooking library
- [inih](https://github.com/benhoyt/inih) - INI file parser
