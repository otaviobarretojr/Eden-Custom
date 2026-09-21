# Eden Custom — Performance Validation Plan

Branch: `performance-v0.2`

The performance branch never replaces the stable build based on theoretical gains alone.

## Build pair

Every comparison uses the same source commit:

- **Stable** — normal MSVC Release build.
- **Zen3-AVX2** — same MSVC Release build plus `/arch:AVX2`.

The AVX2 build is experimental until runtime testing shows equal or better stability.

## Test rules

Use identical conditions for both builds:

1. Same Windows/NVIDIA driver and power plan.
2. Same game dump, update, DLC, mods and save.
3. Same Eden configuration.
4. Same resolution scale and graphics settings.
5. Same test location/path in the game.
6. Test shader-cache behavior separately:
   - cold-cache run;
   - warm-cache run.
7. Repeat a run if an external event invalidates it.

## Metrics

Eden already maintains internal performance statistics and can record frame-time history.
For comparisons collect:

- average game FPS;
- average emulation frametime;
- frame-time CSV;
- 1% low derived from frame-time samples;
- 0.1% low derived from frame-time samples;
- 95th/99th-percentile frametime;
- shader-building events;
- crash/hang count;
- visual regressions.

## Promotion rule

The Zen3/AVX2 build becomes a preferred profile only when it:

- has no new crash, hang or graphical regression;
- does not materially worsen frame-time consistency;
- shows a repeatable benefit in at least one relevant workload.

If results are neutral or inconsistent, Stable remains the default.

## Initial game workflow

For each title, use a reproducible save/location and run the same route for each build.
Do not compare different areas or different shader-cache states as if they were equivalent.
