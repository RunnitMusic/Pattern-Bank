# Pattern Bank Bottom Row — Design QA

- Source visual truth: `C:\Users\nicho\.codex\generated_images\01a00b83-7b7c-7f50-ba00-36415ff581b2\exec-4deba961-e4a6-48e4-a5bc-ebe15bf23f27.png`
- Source pixels: 2167 × 725, generated concept crop.
- Implementation evidence (Hz): `C:\Users\nicho\OneDrive\Documents\VST Building\StepShaperFL\implementation-bottom-row-hz.png`
- Implementation evidence (Sync): `C:\Users\nicho\OneDrive\Documents\VST Building\StepShaperFL\implementation-bottom-row-sync.png`
- Implementation pixels / native viewport: 840 × 440 at 1× density.
- State: default lane and pattern; Hz/open-lock state plus tempo-synced/closed-lock state.
- Normalization: the source is a wide concept crop rather than a full 840 × 440 plugin frame. Comparison used the bottom control strip as the common content region and retained the production plugin's existing 73 px strip height and full-frame chrome.

## Full-view comparison evidence

- The production UI preserves the existing grid, editor toolbar, palette, typography, and top toolbar.
- The lower strip is now divided into the four approved groups with consistent full-height separator lines.
- Speed/Sync, Retrigger/MIDI, Base/Mode/Amount, and Mod 1/Mod 2 retain clear intra-group proximity and larger inter-group spacing.
- The existing right-aligned Runnit credit remains visible; this was omitted from the concept crop but is intentional product content.

## Focused control comparison evidence

- Speed is a vertically draggable value field with a connected right-side lock segment. Hz displays an open neutral lock; tempo sync displays a closed orange lock and musical division text.
- The retrigger button uses the same square-cornered blue-gray gradient, outline, and pressed-state language as the top-row controls. The supplied arrow-into-circle SVG is rotated upward toward the spline grid.
- Base, Mode, and Amount are closer together without overlaps. Both Mod knobs retain their cyan indicators and adjacent four-arrow assignment handles.
- The supplied retrigger SVG remains vector artwork. Lock icons use Lucide vector assets and are credited in `THIRD_PARTY_NOTICES.md`.

## Comparison history

### Pass 1

- [P2] Hz value text ellipsized at the native 840 px width.
  - Fix: widened the Speed value segment by 10 px and reclaimed unused horizontal space from the Output group.

### Pass 2

- Post-fix evidence: `implementation-bottom-row-hz.png` displays `1.00 Hz` in full, while `implementation-bottom-row-sync.png` displays `1 bar` in full.
- No actionable P0, P1, or P2 visual differences remain.

## Required fidelity surfaces

- Fonts and typography: existing JUCE product fonts, weights, uppercase labels, and hierarchy preserved; no clipping or truncation remains.
- Spacing and layout rhythm: four groups align to one baseline with 13 px divider gutters and tighter spacing inside each group.
- Colors and visual tokens: existing panel, border, muted text, orange active, and cyan modulation colors reused.
- Image quality and asset fidelity: vector icons render sharply at native density; no placeholders or raster substitutions.
- Copy and content: approved labels and existing product credit retained; values correctly reflect Hz and synced-division modes.

## Follow-up polish

- [P3] The production strip is slightly denser vertically than the generated concept. This intentionally preserves the plugin's current 840 × 440 footprint and leaves more space for the spline grid.

final result: passed
