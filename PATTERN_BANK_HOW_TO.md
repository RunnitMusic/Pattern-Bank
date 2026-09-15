# Pattern Bank — Quick How-To

Pattern Bank is an internal automation controller for FL Studio. Draw or load a repeating pattern, then use one of its LFO outputs to control a knob, slider, or Patcher parameter. One instance can provide up to eight independent controller outputs.

This quick guide follows the general organization of Image-Line's [Fruity Envelope Controller manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/Fruity%20Envelope%20Controller.htm), adapted for Pattern Bank.

## Quick setup

### Link to an FL Studio or plug-in control

1. Load **Pattern Bank** in the Channel Rack.
2. Choose a factory bank and pattern.
3. Right-click the control you want to automate and select **Link to controller**.
4. Open **Internal controller** and choose **Pattern Bank — LFO 1** (or another enabled LFO).
5. Move the **Amount** and **Base** knobs in Pattern Bank to set the output range.

Give each Pattern Bank instance a recognizable Channel Rack name when a project uses several instances. This makes the correct controller easier to find in FL Studio's link menu.

### Use it inside Patcher

1. Add Pattern Bank to the Patcher map.
2. Connect one of its red **LFO 1–8** controller outputs to an exposed parameter input.
3. Use the **+** button at the top of Pattern Bank when you need another independent LFO output.

## LFO outputs

The numbered buttons select the LFO being edited. Each LFO has its own pattern bank, timing, output controls, MIDI behavior, and Mod assignments.

- **1–8** — Select an LFO output.
- **+** — Enable the next LFO output, up to eight.
- **Pattern** — Select, rename, reorder, or delete a pattern slot.
- **+ inside the Pattern menu** — Create a new pattern slot without closing the list.
- **New** — Create and select a new pattern.
- **Del** — Delete the selected pattern.
- **Dice P** — Select a random preset from the current bank.
- **Dice A** — Randomize the pattern and its related controls.
- **Copy / Paste** — Copy a complete pattern between slots or separate Pattern Bank instances.
- **Undo / Redo** — Step backward or forward through edits.

Pattern slots remember their spline and related settings, including Speed, Sync, Base, Amount, trigger options, markers, Mod values, and Mod assignments.

## Factory banks

Use the preset selector in FL Studio's title bar to load one of four 100-pattern banks:

- **Filter Motion** — Low-pass, band-pass, phaser, rhythmic sweeps, and evolving motion.
- **Volume and Gate** — Loop chopping, stabs, techno/rave gates, and drum-and-bass-inspired rhythms.
- **Melodic −12 to +12** — Pitch-shifter patterns spanning one octave in either direction.
- **Melodic −24 to +24** — Wider arpeggios and coarse-pitch patterns spanning two octaves in either direction.

Loading a factory bank replaces the current LFO's bank, so copy or save custom patterns first.

## Pattern editor

The horizontal axis is time. The vertical axis is the controller value sent to the linked target.

### Point and curve editing

- **Double-click empty space or a segment** — Add a point.
- **Double-click a point** — Remove it.
- **Drag a point** — Change its time and value.
- **Alt-drag a point** — Snap it to the current grid.
- **Drag a midpoint handle** — Change that segment's tension.
- **Right-click a midpoint handle** — Choose its curve family.
- **Ctrl-drag empty space** — Marquee-select points.
- **Ctrl-drag selected points** — Move them together.
- **Shift-drag** — Draw freely across the grid.
- **Delete** — Remove selected points.
- **Ctrl+Z** — Undo.

Available segment families include Hold, Single Curve, Double Curve, Half Sine, Stairs, Smooth Stairs, Pulse, Sine, Triangle, and Saw.

### Drawing and grid controls

- **Grid X** — Sets the timing divisions used for horizontal snapping and drawing.
- **Grid Y** — Sets the value or pitch divisions used for vertical snapping.
- **Edit** — Standard point editing.
- **Ramp Up / Ramp Down** — Paint rising or falling cells.
- **Steps** — Paint stepped values.
- **Eraser** — Remove painted sections.
- **Randomize** — Generate a new shape. Its menu includes lines, ramp-ups, ramp-downs, steps, minor melodies, and major melodies.

Grid values range from 1 to 48. For pitch work, Grid Y 24 suits the −12/+12 bank and Grid Y 48 suits the −24/+24 bank.

## Timing and playback

- **Speed** — Sets the free-running rate in Hz or the musical length when Sync is enabled.
- **Sync** — Locks the pattern duration to FL Studio's tempo. Triplet, straight, and dotted divisions are available.
- **Trig Sync** — Controls when an automated pattern change takes effect: immediately without restarting, restart immediately, or latch to a musical boundary.
- **Pos Sync** — While MIDI Trigger is off, aligns the pattern with FL Studio's song position instead of restarting it when playback begins.

Sync uses the project tempo and runs at the same rate whether FL Studio is playing or stopped.

## Output controls

- **Base** — The output value used when Amount is zero.
- **Amount** — Sets the pattern depth around Base from −100% through 0% to +100%. Negative values invert the motion.

A common starting point is **Base = 0** and **Amount = +100%**. Reduce Amount when the linked control needs subtler movement.

## MIDI Trigger, loop, sustain, and release

Enable **MIDI Trigger** when notes should articulate the pattern. Pattern Bank responds to note-on and note-off; pitch and velocity do not change the controller value.

- **Legato off** — Every new note retriggers the pattern.
- **Legato on** — Overlapping notes do not retrigger. A new note retriggers only after all previous notes have ended.

Two flags appear above the editor in MIDI Trigger mode:

- **Start / Loop-Back flag** — Drag to set its position. Hold Alt while dragging to snap it to Grid X. Right-click to choose:
  - **Start Position** — A note begins at this flag.
  - **Loop-Back Position** — A note begins at the start of the pattern, then returns to this flag after reaching the Loop-End marker.
- **Sustain / Loop-End flag** — Drag to set the end of the loop or sustain section. Hold Alt while dragging to snap. Right-click to toggle:
  - **Loop Forever** — Repeat the marked section. Enabling this disables Sustain.
  - **Sustain** — Hold the value at this marker. Enabling this disables Loop Forever.
  - **Release** — On note-off, play from the marker through the remaining tail of the pattern.

With **Sustain on** and **Release off**, the Sustain value remains held until a note-on that qualifies under the current Legato setting.

## Mod 1 and Mod 2

The two Mod knobs can transform a pattern without replacing its original points.

1. Drag the four-arrow symbol beside a Mod knob.
2. Drop it on a point to assign its X or Y position, on a midpoint handle to assign tension, or on Speed to assign rate.
3. Turn the Mod knob to move all of its assigned targets together.
4. Right-click the Mod knob to view assignments, adjust their positive or negative depth, or remove one with its red **X**.

Use Mod 1 for one musical variation and Mod 2 for another—for example, Mod 1 can lengthen gate releases while Mod 2 changes timing. A pitch pattern can morph into a related melody by assigning several point values and note positions.

## Pattern State menu and files

Click the small arrow at the lower-left of the editor to open Pattern State and spline tools.

- **Open state file / Save state file** — Load or save the current slot as a `.patternbank` file.
- **Copy state / Paste state** — Transfer the current pattern, including its settings and Mod assignments.
- **Flip Vertically / Flip Horizontally** — Mirror the pattern.
- **Normalize Levels** — Expand the pattern to the available vertical range.
- **Smooth Up** — Preview low-pass-style smoothing and point decimation; Accept commits the result.
- **Smooth Abrupt Changes** — Round vertical transitions by adding points around sharp corners.
- **Turn All Points Smooth** — Apply smooth segment behavior throughout the shape.

Pattern files are stored by default in:

`Documents\Image-Line\Pattern Bank\Data`

## Three quick workflows

### Chop a loop into rave stabs

1. Load **Volume and Gate**.
2. Link LFO 1 to a volume control.
3. Choose a stab or gate pattern and enable Sync.
4. Set Base to 0 and Amount to +100%.
5. Use Mod 1 on the lower point after each falling edge to lengthen or shorten the releases.

### Animate a filter

1. Load **Filter Motion**.
2. Link LFO 1 to filter cutoff or phaser frequency.
3. Reduce Amount until the motion stays inside a useful tonal range.
4. Assign Mod 1 to selected point values and Mod 2 to timing or tension for performance variations.

### Arpeggiate or warp pitch

1. Choose the melodic bank that matches the target range.
2. Link the output to a pitch shifter's −12/+12 control or 3xOsc's −24/+24 coarse control.
3. Match Grid Y to 24 or 48.
4. Enable Sync and choose a musically useful duration.
5. Use the Mod knobs to change notes, timing, vibrato depth, or Speed.

## Beta-testing checklist

When reporting a problem, include:

- FL Studio version and Windows version.
- Whether Pattern Bank was loaded in the Channel Rack or Patcher.
- Factory bank, pattern number, and Sync division.
- Whether MIDI Trigger, Legato, Pos Sync, Loop Forever, Sustain, or Release were enabled.
- A saved `.patternbank` file when the issue depends on a specific shape.
- Exact steps that reproduce the behavior.
