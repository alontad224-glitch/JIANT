/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* JIANT's core outside the FM-1: the whole firmware (engines, sequencer, songs, generator, FX, the UI and its
 * 240 x 240 screen, projects in a simulated flash) as one C library, for every port that is not the FM-1 itself:
 * the MPC plugin (ports/mpc-plugin), the MPC Sample app and the desktop builds. It is the browser emulator's host
 * layer (web/emu/felucca_web.c) behind a stable API: the same sources, so a port plays what the FM-1 and the
 * emulator play, sample for sample.
 *
 * One device per process: the firmware keeps its state in globals. Not thread safe: call everything from one
 * thread at a time (a plugin calls it from its audio thread and hands state requests over; ports/mpc-plugin).
 *
 *   boot:    jiant_boot(seed, 1)            (1: the power-on splash runs here, not in the first seconds of audio)
 *   audio:   jiant_render(l, r, n)          any n, at JIANT_RATE (44.1 kHz stereo float)
 *   play:    jiant_midi(...), jiant_buttons(...), jiant_keys(...), jiant_encoder(...)
 *   see:     jiant_screen() (RGB565), jiant_screen_draws()
 *   keep:    jiant_state_save(...), jiant_state_load(...) */
#ifndef JIANT_CORE_H
#define JIANT_CORE_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JIANT_RATE 44100u
#define JIANT_SCREEN_W 240u
#define JIANT_SCREEN_H 240u
#define JIANT_KEYS 27u                     /* F3 .. G5, bit k = key k */

/* the FM-1's buttons by their printed labels (bit b of jiant_buttons; firmware/src/panel.c) */
enum jiant_button {
    JIANT_B_FX, JIANT_B_SCL, JIANT_B_ENV, JIANT_B_LFO, JIANT_B_EDIT, JIANT_B_GLO, JIANT_B_HOME, JIANT_B_SAVE,
    JIANT_B_ARP, JIANT_B_SEQ, JIANT_B_PLAY, JIANT_B_REC, JIANT_B_OCTDN, JIANT_B_OCTUP, JIANT_NBUTTONS
};
/* the encoders by role (firmware/src/panel.c EN_*) */
enum jiant_encoder {
    JIANT_E_SELECT, JIANT_E_ALGO, JIANT_E_PRESET, JIANT_E_K1, JIANT_E_K2, JIANT_E_K3, JIANT_E_K4, JIANT_NENCODERS
};

const char *jiant_version(void);

/* power on: an erased flash (the power-on parts), this session's seed (the macros' dice); skip_splash 1 runs the
 * splash's ~1.7 s here so the first rendered audio already answers the keys. Once per process. 0 = booted. */
int jiant_boot(uint32_t seed, int skip_splash);
/* the device's own flash (1 MiB NOR: settings, songs, projects, user presets), for a port that keeps it in a file:
 * jiant_flash_restore before jiant_boot gives back a saved image (it boots from it, as the FM-1 does from its flash;
 * 0 done, 1 too late or the wrong size); jiant_flash is the image now, jiant_flash_writes counts what the firmware
 * wrote to it (a change: time to save it, once the firmware is done, as the browser emulator does) */
int jiant_flash_restore(const uint8_t *image, size_t size);
const uint8_t *jiant_flash(size_t *size);
uint32_t jiant_flash_writes(void);
/* the autosave (project.c, MENU > SYSTEM > RESTORE LAST) written now when the music changed since the last one, not
 * after its 10 s of quiet: for a port about to close (the transport is stopped first). 1 written, 0 nothing to do. */
int jiant_autosave_now(void);
int jiant_booted(void);

/* n frames at JIANT_RATE into l and r (any n, either pointer may be 0 to drop the channel) */
void jiant_render(float *l, float *r, uint32_t n);

/* a MIDI message as USB MIDI brings it: channel messages (0x80..0xEF) and the realtime clock (0xF8 clock, 0xFA
 * start, 0xFB continue, 0xFC stop: they count only with jiant_clock_external(1)). 0 = the input ring was full. */
int jiant_midi(uint8_t status, uint8_t d1, uint8_t d2);
/* 1: the sequencer follows the MIDI clock given to jiant_midi (GLO > CLOCK USB); 0: its own tempo */
void jiant_clock_external(int on);
void jiant_play(int on);                   /* PLAY / STOP, as the button does */
int jiant_playing(void);
uint32_t jiant_bpm(void);                  /* the tempo, BPM (its own, or the last the clock gave) */

/* the load, measured on this computer's CPU in real time (the firmware's own meter: a half's render against its
 * 2.9 ms): now (averaged over ~93 ms) and the worst half since boot, in %; the voices the overload guard faded and
 * whether the load governor has the sound lighter now (fewer voices shared, or LOAD LITE): 0 on a CPU with room */
uint32_t jiant_cpu_percent(void);
uint32_t jiant_cpu_peak_percent(void);
uint32_t jiant_voices_shed(void);
int jiant_load_governed(void);
uint32_t jiant_voices_sounding(void);
/* the emulator's bench song into the tracks (web/emu felucca_web.c web_test_heavy): FM6 PAD, TRIO and VOICE CHOIR
 * holding 4-note chords on every step, and a drum track; PLAY is the caller's. For the tests and --bench */
void jiant_test_heavy(void);

/* the panel: the buttons held now (bits by enum jiant_button), the keys held now (bit k = key k), detents turned */
void jiant_buttons(uint32_t held);
void jiant_keys(uint32_t held);
void jiant_encoder(enum jiant_encoder role, int32_t steps);
void jiant_master(uint32_t level);         /* the MASTER knob, 0..1023 */

/* the screen: 240 x 240 RGB565, high byte first as the panel takes it; draws counts the changes */
const uint16_t *jiant_screen(void);
uint32_t jiant_screen_draws(void);
/* LEDs: bit b = button b lit (bit 14: PLAY's green), keys bit k */
uint32_t jiant_led_buttons(void);
uint32_t jiant_led_keys(void);

/* the music (the live section, the songs, their index, the user presets and their FM6 patches) as one block, the
 * objects the editor's backup carries: jiant_state_save writes it to buf (cap bytes) and returns its length, or the
 * length it needs when cap is short (buf may be 0). jiant_state_load stops the transport and restores it: 0 done,
 * 1 not a JIANT state, 2 damaged, 3 an object refused (the others restored). */
size_t jiant_state_save(uint8_t *buf, size_t cap);
size_t jiant_state_max(void);              /* the most jiant_state_save can need */
int jiant_state_load(const uint8_t *buf, size_t len);

/* the song generator (SONG > GENE): genre 0 RANDOM, 1.. a genre, seed 0..9999, energy 0..100, form 0 LOOP 1 PARTS
 * 2 SONG; its name after */
void jiant_generate(uint32_t genre, uint32_t seed, uint32_t energy, uint32_t form);
uint32_t jiant_genre_count(void);
const char *jiant_genre_name(uint32_t i);
int jiant_song_select(uint32_t song);      /* 0..: 0 = done (stopped first) */

#ifdef __cplusplus
}
#endif
#endif
