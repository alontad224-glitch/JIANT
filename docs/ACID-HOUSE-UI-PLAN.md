# ACID HOUSE UI — implementation audit and plan

Target repository: `alontad224-glitch/JIANT`
Working branch: `acid-house-ui`
Visual source of truth: the user's four-panel FM-1 ACID HOUSE reference image (SYNTH / SEQ / DRUM-X / MOD-FX).

## Verified repository facts

- The FM-1 LCD is 240 × 240 pixels (`firmware/src/lcd.c`).
- The firmware UI is rendered in C; the main drawing path is split across `ui.c`, `ui_draw.c`, and `ui_graph.c`.
- The existing sequencer is in `firmware/src/seq.c`; `firmware/src/core.h` defines `NSTEP 64`.
- The core currently defines six parts/tracks (`NPART 6`, `NTRK NPART`) and eight shared hardware voices (`NVOICE 8`). The sequencer source's opening comment still says four tracks, so comments/docs must not be treated as authoritative over current definitions.
- The Analog engine is `firmware/src/eng_analog.c`. It already has two oscillators, several oscillator modes, noise, drive, and LP / HP / BP / COMB filter modes. The acid-style sequencer should therefore drive this existing engine rather than create a second synth engine.
- The drum engine is `firmware/src/eng_drum.c`, with synthesis in `firmware/src/drumx_voice.c`. It has eight lanes: BD, SD, CP, CH, OH, TM, RS, CB; the engine comments and `docs/TONIC-DRUMX.md` describe the A/B patch and morph design.
- The existing UI already has a drum step grid and DRUM-X sound-edit pages. Relevant files include `firmware/src/ui_graph.c`, `firmware/src/ui_dx.c`, and `firmware/src/ui_input.c`.
- The build instructions are in `BUILDING.md`. The native FM-1 build requires the JieLi pi32v2 toolchain and AC79 SDK; the browser emulator is optional and requires Emscripten. `tests/run_tests.sh` contains UI render/layout, sequencer, engine and regression tests.

## Important correction to stale project notes

Some older JIANT planning documents describe a four-track configuration and a different dark/thermal visual concept. The current `core.h` definitions and the user's latest ACID HOUSE reference take precedence for this project. Do not change track count, engine storage indices, project formats, or sequencer timing as part of a UI-only adaptation without a separately reviewed design.

## Product target

Replace the existing interface presentation with four functional destinations:

1. **SYNTH** — Analog engine controls, waveform display, filter, envelope, acid accent/drive and glide.
2. **SEQ** — melodic step editor with pitch, accent, slide, tie, pattern length, tempo/swing, direction, scale/octave, legato and a visible playhead.
3. **DRUM-X** — eight-lane drum grid and selected-lane sound controls, retaining the actual DRUM-X engine and current lane/kit data.
4. **MOD-FX** — existing modulation, envelopes and effects controls presented in the new visual system.

The user's image is a 1536 × 1024 desktop-style concept. The actual FM-1 display is 240 × 240, so the implementation must adapt the four sections to compact pages and hardware navigation; it must not attempt to draw the desktop layout at native size.

## Design language

- Warm cream / parchment background.
- Bright signal yellow for selected controls and active steps.
- Violet / purple for oscillator and modulation accents.
- Mint / seafoam for envelopes and secondary modulation.
- Dark charcoal controls and high-contrast text.
- Geometric, Bauhaus-inspired shapes; clear hierarchy and restrained decoration.
- All graphics are drawn by the existing C UI renderer. Avoid full-screen bitmaps and large new RAM allocations.

## Sequencer behaviour requirements

- Keep the existing 64-step data/playback model unless code inspection proves a smaller native pattern is already required for a specific view.
- A 16-step page may show a selected 16-step bank/window; it must not silently truncate longer patterns.
- Pitch, accent, slide and tie must be stored in the existing pattern/step representation or an explicitly reviewed compatible extension.
- The displayed playhead must follow actual sequencer playback, not a UI-only animation.
- Drum-X must continue to trigger the existing eight synthesized lanes; do not substitute samples or create a second drum playback path.
- Preserve project/preset compatibility and existing MIDI/clock behaviour.

## Safe implementation sequence

### Stage 0 — audit (this commit)
Record verified architecture, display limits, build/test path and the proposed scope. No DSP or persisted data layout changes.

### Stage 1 — visual foundation
Implement the cream/yellow/violet/mint palette and reusable compact UI drawing primitives. Keep all existing controls functional. Add/update UI render tests and compare all palettes/layouts.

### Stage 2 — Acid sequencer view
Map the existing step events and input gestures. Add the melodic pitch/accent/slide/tie presentation and hardware navigation. Test real playback, pattern lengths, MIDI and save/load compatibility.

### Stage 3 — DRUM-X view
Redesign the eight-lane grid and selected-lane controls using existing drum state and trigger paths. Test lane mutes, playback, recording and persistence.

### Stage 4 — MOD-FX and integration
Restyle the modulation, envelope and effect pages. Run the full host tests, UI render/layout checks, browser emulator tests and target build when the toolchain is available.

## Build and test commands

```sh
./build.sh
tests/run_tests.sh
web/emu/build.sh
```

The target build cannot be claimed successful until the JieLi toolchain and AC79 SDK are available and the commands have actually run. Test in the browser emulator before installing on hardware; back up device flash before the first firmware installation.

## Current status

- Source audit: started; key engine, UI, sequencer, display and build files inspected.
- Code changes: none yet.
- Firmware build: not run in this environment.
- Hardware installation: not attempted.
