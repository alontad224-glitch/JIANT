# JIANT FM on the Akai MPC Sample

JIANT FM as the MPC Sample's whole instrument, independent of any other app: the firmware's core
([`ports/core`](../core)) at the MPC's 44.1 kHz in 128-frame blocks (the FM-1's own block), JIANT's 240 x 240 screen in
the middle of the MPC's 320 x 240, the MPC's pads, buttons and knobs as the FM-1's panel, MIDI in from the DIN and USB
ports, and the device's flash kept on the card. It runs in place of the stock app, started from the microSD card, on
[sample-base](https://github.com/keremimo/sample-base) by keremimo (the runtime Melodee uses: the hardware, the
threads, the SD boot and the way back to the stock app).

| | |
| --- | --- |
| [`src/jiant/jiant_app.cpp`](src/jiant/jiant_app.cpp) | the app: audio, screen, lights, MIDI, the flash on the card |
| [`src/jiant/panel_map.h`](src/jiant/panel_map.h) | the MPC's surface as the FM-1's panel (the table is at its top) |
| [`src/main.cpp`](src/main.cpp) | the runtime, and the tools: `--self-test`, `--preview`, `--persist-write` / `--persist-read` |
| [`release/README.txt`](release/README.txt) | what the user reads: install, the controls, where the music is kept |

## Build

```sh
ports/mpc-sample/build.sh test      # here (Linux): the app, its tests, build/preview.png
ports/mpc-sample/build.sh docker    # the MPC's ARM64 build in sample-base's image (Docker or Podman)
```

`build.sh` fetches sample-base at a pinned commit into `build/sample-base` and JUCE into `build/deps`. On an ARM64
Linux (or an ARM64 chroot under qemu-user) `build.sh` alone makes the MPC's binary; it must run on glibc 2.36 or older
(Debian bookworm, Ubuntu 22.04).

The tests: the control map (`tests/panel_map_test.cpp`), a note through the whole firmware (`--self-test`, which the
SD boot also runs before it starts JIANT), the screen (`--preview`), and the music saved on the card coming back in a
new process (`--persist-write`, `--persist-read`).

## Release

```sh
python3 ports/mpc-sample/tools/make_release.py v10.7.64-mpc1 --binary <the ARM64 build>
```

writes `ports/mpc-sample/dist/JIANT-FM-MPC-Sample-<version>.zip`: an `autorun` folder for the top of the card (signed as
the MPC wants it), the README and the licences.

## Licences

JIANT is GPL-3.0-only; sample-base and JUCE are AGPL-3.0. The MPC Sample program combines them, as GPLv3 section 13
allows, and is conveyed as a whole under the AGPLv3; JIANT's own files keep the GPL.
