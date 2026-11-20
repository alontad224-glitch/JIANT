/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* The core's API against the real firmware (ports/core/build.sh test): it boots, sounds on a MIDI note and on PLAY,
 * follows an external clock, draws its screen, and its state goes out and comes back the same. */
#include "jiant_core.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails;
#define CHECK(c, ...) do { if (c) printf("  ok    " __VA_ARGS__); else { printf("  FAIL  " __VA_ARGS__); fails++; } \
                           printf("\n"); } while (0)

static float bl[44100], br[44100];
static double rms(uint32_t n)                  /* render n frames, their level */
{
    double s = 0;
    uint32_t i;
    jiant_render(bl, br, n);
    for (i = 0; i < n; i++)
        s += (double)bl[i] * bl[i] + (double)br[i] * br[i];
    return sqrt(s / (2.0 * n));
}

int main(void)
{
    uint8_t *a, *b;
    size_t na, nb, nc;
    uint32_t i, lit = 0;
    double quiet, note, after;

    printf("JIANT core %s\n", jiant_version());
    {
        size_t fs = 0;
        static uint8_t img[1u << 20];
        jiant_flash(&fs);
        memset(img, 0xFF, sizeof img);
        CHECK(fs == sizeof img && jiant_flash_restore(img, 5) == 1 && jiant_flash_restore(img, fs) == 0,
              "a flash image is taken before boot (%zu bytes)", fs);
    }
    CHECK(jiant_boot(1, 1) == 0 && jiant_booted(), "boots (splash skipped)");
    CHECK(jiant_boot(1, 1) == 1, "a second boot is refused");
    CHECK(jiant_flash_restore(jiant_flash(NULL), 1u << 20) == 1, "and a flash image after it");

    quiet = rms(4410);
    jiant_midi(0x90, 60, 100);
    note = rms(22050);
    jiant_midi(0x80, 60, 0);
    rms(44100);
    after = rms(4410);
    CHECK(note > 0.005 && note > 10 * (quiet + 1e-6), "a MIDI note sounds (rms %.4f, before %.6f)", note, quiet);
    CHECK(after < note / 4, "and stops after its note-off (rms %.5f)", after);

    for (i = 0; i < JIANT_SCREEN_W * JIANT_SCREEN_H; i++)
        lit += jiant_screen()[i] != jiant_screen()[0];
    CHECK(jiant_screen_draws() > 0 && lit > 1000, "the screen is drawn (%u px differ from the corner)", lit);

    jiant_generate(0, 1234, 70, 0);
    jiant_play(1);
    note = rms(44100);
    CHECK(jiant_playing() && note > 0.005, "a generated song plays (rms %.4f, %u BPM)", note, jiant_bpm());
    jiant_play(0);
    rms(4410);
    CHECK(!jiant_playing(), "and stops");

    na = jiant_state_save(NULL, 0);
    a = malloc(na);
    CHECK(jiant_state_save(a, na) == na && na > 16, "the state: %zu bytes", na);
    jiant_generate(0, 4321, 30, 0);
    rms(4410);
    nb = jiant_state_save(NULL, 0);
    b = malloc(nb > na ? nb : na);
    jiant_state_save(b, nb);
    CHECK(nb != na || memcmp(a, b, na), "another song changes it");
    {
        uint32_t w = jiant_flash_writes();
        CHECK(jiant_state_load(a, na) == 0, "the first loads back");
        CHECK(jiant_flash_writes() != w, "into the flash (%u writes)", jiant_flash_writes() - w);
    }
    rms(4410);
    nc = jiant_state_save(b, nb > na ? nb : na);
    CHECK(nc == na && !memcmp(a, b, na), "and saves the same bytes");
    a[na / 2] ^= 0x55;
    CHECK(jiant_state_load(a, na) == 2, "a damaged state is refused");
    CHECK(jiant_state_load((const uint8_t *)"nope", 4) == 1, "and one that is not a state");

    {                                          /* a computer's CPU: every part its own voices (core.h FELUCCA_HOST_POWER) */
        uint32_t most = 0;
        struct timespec t0, t1;
        double secs;
        jiant_test_heavy();
        jiant_play(1);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (i = 0; i < 40u; i++) {
            rms(4410);
            if (jiant_voices_sounding() > most)
                most = jiant_voices_sounding();
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        secs = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        CHECK(most > 8u, "the heavy song sounds %u voices at once (the FM-1: 8 between all parts)", most);
        CHECK(!jiant_voices_shed() && !jiant_load_governed(),
              "on this CPU nothing given way: %.0fx real time, load %u %% (worst half %u %%)", 4.0 / secs,
              jiant_cpu_percent(), jiant_cpu_peak_percent());
        jiant_play(0);
        rms(4410);
    }

    jiant_clock_external(1);
    rms(128);
    jiant_midi(0xFA, 0, 0);
    for (i = 0; i < 96u; i++) {                /* 120 BPM: a pulse every 20.8 ms, 918.75 frames */
        jiant_midi(0xF8, 0, 0);
        rms(919);
    }
    CHECK(jiant_playing(), "the external clock starts it (%u BPM)", jiant_bpm());
    jiant_midi(0xFC, 0, 0);
    rms(1024);
    CHECK(!jiant_playing(), "and stops it");
    jiant_clock_external(0);

    printf(fails ? "core: %d FAILED\n" : "core: all passed\n", fails);
    return fails != 0;
}
