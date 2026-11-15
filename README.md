# Pattern Bank for FL Studio

Pattern Bank is a native FL Studio internal-controller plug-in by Runnit for drawing banks of LFO shapes and exposing them as red Patcher controller outputs.

Beta users can start with the [Pattern Bank Quick How-To](PATTERN_BANK_HOW_TO.md).

Each LFO also includes two automatable Mod macros. Drag the small arrow on a Mod knob to a spline point
(choose its X or Y badge) or a tension handle. Right-click the Mod knob to drag assignment depths,
use the red `X` beside an assignment to remove it, or open FL Studio's host parameter menu.

## Current design

- Eight stable Patcher outputs are declared so saved links and automation never move.
- A new instance starts with one enabled LFO; the `+` control activates up to eight.
- Every LFO owns 128 shape slots and its own speed, sync, timing feel, and pattern-switch mode.
- Each lane has FL-style `L` loop-start and MIDI-only `S` sustain/end flags. The LFO free-runs while transport is stopped and returns to `L` when Play begins unless Position Sync is enabled.
- Pattern automation supports phase-preserving `Off`, immediate `Restart`, and song-grid latches of 1/8, 1/4, 1/2, 1, or 2 beats.
- The editing row includes draggable or inline-editable 1–48 X/Y grids, Edit/Ramp Up/Ramp Down/Steps/Eraser brushes, mixed and focused randomizers, Shift drawing, Ctrl selection/group movement, Alt grid snapping, Delete, and Ctrl+Z.
- Each LFO has an automatable MIDI Trigger switch. The native plug-in advertises note input to Patcher, and incoming note-ons restart enabled lanes from their Start flags.
- Patcher controller outputs use the public SDK's 64-bit fixed-point bridge. Automatable plug-in parameters separately use FL Studio's current 30-bit range.
- Copy/paste uses a versioned text format on the system clipboard, so it works across instances.
- The small arrow below the grid opens the Pattern State menu. Individual patterns can be loaded or saved as versioned `.patternbank` files in `Documents\Image-Line\Pattern Bank\Data`; the files preserve the name, spline, curve types, Mod routes, speed, Sync, Base, Amount, trigger, and position settings.
- `Smooth abrupt changes` rounds vertical transitions only by replacing each hard edge with control points immediately before, at, and after the transition (with a capacity-safe two-point fallback for full 64-point patterns). `Smooth up...` previews a low-pass-style Smooth control and linear-trace Decimation live. Decimation ranges from a dense 64-point trace at zero to one start/end segment at maximum; Accept commits one undoable edit and closing the window cancels the preview.
- The controller tick performs no shape generation, drawing, locking, or dynamic allocation. Randomization and curve editing happen on the GUI thread.

## Build

From a Visual Studio developer PowerShell:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

On this workspace, the equivalent one-command build is:

```powershell
.\Build.ps1 -Configuration Release
```

On Windows, `Pattern Bank_x64.dll` is the FL Studio native plug-in binary and `Pattern Bank Preview.exe` is a standalone UI harness for development.

The install-ready native package is written to `build-release/package`. Close FL Studio, then double-click `Install-PatternBank-Windows.cmd`. The installer requests administrator access, stores one version-independent master payload under `C:\ProgramData\Runnit\Pattern Bank`, detects every installed FL Studio version, and creates a Pattern Bank-only junction inside each native Effects folder. Rerunning the installer repairs a newly installed FL Studio major version without duplicating the plug-in binary.

For a developer build, an elevated PowerShell can still target one version explicitly:

```powershell
.\Install-Local.ps1 -FLVersion 2026
```

Run `Diagnose-PatternBank-Windows.ps1` to verify the master payload and every default FL installation. The Windows release target statically links the Microsoft C/C++ runtime and uses JUCE's Direct2D renderer. The FL mixer tick on both platforms does not draw, lock, generate shapes, copy banks, or allocate memory, and unchanged controller values are not re-sent to the host. Controller output is encoded automatically for either normal Mixer-slot 16-bit internal-controller links or Patcher's wide fixed-point ports.

### macOS native build

The macOS target uses the same FL Studio native-controller interface and produces a universal Intel/Apple Silicon `Pattern Bank_x64.dylib`. A Mac with Xcode and CMake is required; Apple binaries cannot be produced or validated from the Windows build machine.

```zsh
chmod +x Build-Mac.sh Install-Local-Mac.sh
./Build-Mac.sh
./Install-Local-Mac.sh
```

The build script targets macOS 11 or newer, writes to `build-macos-universal`, runs the core tests, verifies both arm64 and x86_64 slices, rejects accidental Homebrew or build-machine library dependencies, clears quarantine, and applies an ad-hoc signature. The installer finds all FL Studio application bundles in `/Applications` and `~/Applications`, preserves the previous Pattern Bank copy, installs into each native Effects directory, clears `com.apple.quarantine`, re-signs the installed dylib, and verifies its architectures and signature. Specific application bundles can be passed as arguments when needed. `Diagnose-PatternBank-macOS.command` performs the same non-mutating checks after installation.

FL Studio replaces its application bundle during major updates, so rerun the Mac installer after installing a new FL Studio application. A Developer ID signature and notarization can further reduce public-install friction, but the beta package is deliberately ad-hoc signed after installation because its final location is inside FL Studio's bundle.

## Continuous integration and beta testing

GitHub Actions builds and tests Windows x64, a universal Mac package on an Apple Silicon macOS 27/Xcode 27 runner, and a separate Intel macOS smoke build. Successful runs publish complete install-ready packages with installers and diagnostic scripts. The macOS artifact is ad-hoc signed for beta testing; it is not a notarized public release.

Mac testers should use the macOS beta issue form and report their Mac processor, macOS version, FL Studio version and whether FL is running natively or under Rosetta. Do not use an important project for the first load test.

## SDK provenance

The public MIT-licensed [tonikasoft/fpsdk](https://github.com/tonikasoft/fpsdk) repository is vendored under `third_party/fpsdk`. Pattern Bank directly uses the C++ FL SDK interface contained in that repository; it is not a VST wrapper.
