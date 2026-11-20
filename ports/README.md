# JIANT beyond the FM-1

JIANT FM is meant to run whole, on its own, on more than one machine: the M-VAVE FM-1, the M-VAVE SMK-37 Pro / Elite
and the Akai MPC Sample, plus a plugin for the MPC standalones and desktop hosts. Every one of them plays the same
firmware sources (`firmware/src`), so a song sounds the same everywhere.

```
                        firmware/src  (engines, sequencer, songs, generator, FX, UI, 240 x 240 screen)
                                  |
          +-----------------------+------------------------+
          |                                                |
   firmware/hal  (bare metal, JieLi AC79)           ports/core  (one C library, any CPU)
          |                                                |
   +------+-------+                     +------------------+-------------------+
   |              |                     |                  |                   |
 FM-1     SMK-37 Pro / Elite     web/emu (browser)   ports/mpc-plugin    MPC Sample app
 (today)  (same chip: next)      (today)             (this folder)       (next: its own OS)
```

| Port | What it is | State |
| --- | --- | --- |
| [`core/`](core/) | The whole firmware as one C library: boot, audio, MIDI, the panel, the screen, the music saved and restored. The browser emulator's host layer behind a stable API ([`jiant_core.h`](core/jiant_core.h)). | Works, tested |
| [`mpc-plugin/`](mpc-plugin/) | JIANT FM as an instrument plugin: MPC standalones (MPC OS 3.10.1+), and desktop VST2 hosts. | Builds for ARM32, ARM64 and desktop; passes the SDK's host simulator; not yet tried on an MPC |
| MPC Sample app | JIANT as the MPC Sample's whole system, its own screen and controls, started from the microSD card. | Next |
| SMK-37 | JIANT on the SMK-37 Pro / Elite, bare metal like the FM-1 (same AC791N chip, same screen and audio format). | Next |

## The core

```sh
ports/core/build.sh test      # build/core/libjiant_core.a, then its test
```

One device per process (the firmware keeps its state in globals), not thread safe; see the header. The test boots it,
plays a MIDI note, draws the screen, plays a generated song, saves and restores the music, and follows an external
MIDI clock.

## The MPC plugin

```sh
pip install ziglang           # the cross compiler, as inMusic's SDK uses
python3 ports/mpc-plugin/build.py
```

It fetches inMusic's [MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk) (MIT) at a pinned
commit into `build/mpc-plugin-sdk`, builds the core and the plugin for the desktop, ARM32 and ARM64, runs the SDK's
host simulator (the MPC's load sequence and its quirks, at 44.1 kHz and at 48 kHz), and assembles the folder the MPC
installs, checked with the SDK's `validate.py`:

```
build/mpc-plugin/Juan Jiant - VST - JIANT FM/      (and a zip of it)
```

On an MPC: Preferences > Enhancements (Beta) > Enable Enhancements and External Plugin Support; copy the folder to
`VST2 Plugins` on a USB drive or SD card, restart, and load JIANT FM on a plugin track.

What the plugin does:

- **MIDI**: channels 1 to 4 play tracks 1 to 4 (GLO > MIDI IN ROUTE CH1-4).
- **Sync Host** (default): the sequencer follows the MPC's transport and tempo (a 24 PPQN clock made from the host's
  beats). **Internal**: its own tempo; **Play** starts and stops it.
- **GENE**: genre, seed, energy and form; **Generate** makes a song.
- **The music is saved in the MPC project**: the live section, the songs and the user presets.
- **Any sample rate**: 44.1 kHz as is (the MPC's), other rates resampled.

Limits, today:

- **No JIANT screen inside an MPC**: MPC plugins show only the MPC's parameter page. The full JIANT, its screen and
  its UI, is the MPC Sample app's job (next), not the plugin's.
- **One JIANT per project**: a second instance stays silent (the firmware is one device).
- Tried in the SDK's host simulator, not yet on MPC hardware.

## Licences

JIANT is GPL-3.0-only. The SDK's framework is MIT and links into the plugin under the GPL; the plugin folder carries
JIANT's `LICENSE` and where its source is. The SMK-37's own SDK library is proprietary: the SMK-37 port is to run bare
metal on JIANT's own HAL, as on the FM-1, not linked with it.
