# V10.7.46: system audio overload

Source: `f44f81ce0ebbf6a94f37abbbc99125163e69585f`.
Baseline: published V10.7.45, `6ebc1b73521b831a8cbfdfcbbe272f2c88d8d63b`.

## Corrections

- Voice admission, guard/recovery, half-rate pressure handling and UI throttling use the total audio-half deadline, including nested input/USB interrupts. Rendering-only diagnostics are retained.
- Core 1 refuses a new mailbox submission while its previous USB resampling job is pending. The existing serial mixer fallback handles that block without corrupting job ownership.
- Generator recipes, voice caps and DSP sounds are unchanged. An experimental CLOUD arithmetic rewrite was rejected because it did not demonstrate lower target cost.

## Evidence

- Simulated 60% rendering plus 30% nested work now engages and retains overload protection and prevents additional voice admission.
- Production mailbox regression covers a pending USB job, completion, fault and counter wrap.
- ASan/UBSan overload/recovery stress: 201 presets, four tracks. No memory errors; legacy signed-shift and signed-overflow checks excluded.
- Serial, deferred, threaded and chaotic scheduling agree on all 11 multicore scenes.
- All 219 rendered audio cases match V45. The golden suite's two stale reference mismatches are identical on V45 and V46; references were not rewritten.
- Target loop instruction-cost metrics are unchanged from V45. All 60 host instruction-count sections remain within budget; CPU TEST is 2831 instructions/sample, six-track CPU TEST 2925, unchanged to rounding.
- EXPLORE seed 197, six tracks, COMPLEX 100, 20 seconds: worst four-second host estimate 2759 on V45, 2757 on V46. This is effectively unchanged; it is not evidence of lower hardware CPU use.
- The target package builds with strict layout checks: image 582976/584336 bytes, RAM data+bss 98164/98304, LOWBUF 32660/32768, RAM code 32604/34816. CRC, flash layout, unchanged update loader and exact linked segment pairing pass.
- Local package SHA-256: `511aa43a71842212d59d5bbc9d85ff46e14e9d26553262359051628f564087dc`.

The repository-wide legacy shell suite is not wholly green on this toolchain: MICRO's old save-byte expectation and the final fairness steals expectation also fail on unmodified V45; the full UI test has a pre-existing GCC 14 incompatible-pointer diagnostic. The host wall/thread-time GEN budget varies with execution conditions and failed on V45 too. Deterministic instruction counts and the target build were compared directly instead; no budgets were raised to hide these results.

No physical FM-1 was available. This release fixes demonstrated gaps in overload protection and job ownership; it does not establish a lower baseline CPU percentage or prove that every reported glitch is eliminated. Validate the affected projects on hardware with and without USB, including active performance FX.
