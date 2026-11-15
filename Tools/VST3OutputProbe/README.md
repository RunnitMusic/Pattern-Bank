# Pattern Bank VST3 Output Probe

This deliberately small plug-in tests one question: does FL Studio expose a
VST3 processor output parameter as a routable internal controller?

`Probe Output` is declared as a read-only VST3 parameter and emits a 0-1
triangle wave through `ProcessData::outputParameterChanges`. Audio is passed
through unchanged.

## Build on Windows

```powershell
cmake -S . -B build-vst3-probe -DSTEPSHAPER_BUILD_FL_PLUGIN=OFF `
  -DSTEPSHAPER_BUILD_PREVIEW=OFF -DSTEPSHAPER_BUILD_TESTS=OFF `
  -DSTEPSHAPER_BUILD_VST3_OUTPUT_TESTER=ON
cmake --build build-vst3-probe --config Release --target VST3OutputProbe_VST3
```

## FL Studio test

1. Copy `Pattern Bank VST3 Output Probe.vst3` into
   `C:\Program Files\Common Files\VST3`. FL Studio does not enumerate VST3
   bundles from its configurable VST2 search paths, including the newer
   per-user VST3 location. `Install-Windows.ps1` performs this copy when run as
   administrator.
2. Run **Find installed plugins** in FL Studio's Plugin Manager.
3. Load it directly in a Mixer slot. Confirm that its orange bar moves.
4. Open **Link to controller** on another plug-in parameter and inspect the
   **Internal controller** list for `Probe Output`.
5. Load the probe in Patcher and inspect its output nodes for a routable
   parameter/controller output.
6. Also inspect Fruity Wrapper's parameter list. A moving read-only parameter
   without a controller source means FL receives the VST3 output update but
   does not bridge it to the native controller system.

Expected likely result: the value may be visible to the wrapper, but no red
controller output is created. That result is still useful because it cleanly
separates VST3 host communication from FL's native internal-controller API.
