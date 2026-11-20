/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant) */
/* JIANT's core outside the FM-1 (jiant_core.h): the browser emulator's host layer, built natively, behind the
 * stable API. Nothing of the firmware or of the emulator changes for this file.
 *   cc -O2 -ffp-contract=off -std=gnu99 -w -Ibuild/gen -Ifirmware/src -c ports/core/jiant_core.c
 * (after the generated headers: python3 -c "import sys; sys.path.insert(0, 'tools'); import build; build.generate()") */
#include "../../web/emu/felucca_web.c"
#include "jiant_core.h"

#if defined(__GNUC__)
#define JC_API __attribute__((visibility("default")))
#else
#define JC_API
#endif

JC_API const char *jiant_version(void) { return FELUCCA_VERSION; }

static int jc_restored;
JC_API int jiant_flash_restore(const uint8_t *image, size_t size)
{
    if (web_booted || !image || size != sizeof nor)
        return 1;
    memcpy(nor, image, sizeof nor);
    jc_restored = 1;
    return 0;
}
JC_API const uint8_t *jiant_flash(size_t *size)
{
    if (size)
        *size = sizeof nor;
    return nor;
}
JC_API uint32_t jiant_flash_writes(void) { return web_flash_writes; }

JC_API int jiant_autosave_now(void)
{
    uint32_t sig;
    if (!web_booted || !flash_ok || !(ui_prefs & PREF_RESTORE_ON))
        return 0;
    if (song.playing)
        seq_stop();
    sig = autosave_sig();
    if (sig == as.saved)
        return 0;
    project_capture(&proj_scratch);                       /* (project.c autosave_poll's write) */
    proj_wire_gen++;
    as.last = fm1_ms;
    as.wrote = 1;
    as.err = !proj_pack(&proj_wire, &proj_scratch) || st_save(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire) != 0;
    if (as.err)
        return 0;
    as.saved = as.seen = sig;
    as.writes++;
    return 1;
}

JC_API int jiant_boot(uint32_t seed, int skip_splash)
{
    if (web_booted)
        return 1;
    if (!jc_restored)
        web_nor_erase();
    web_seed(seed ? seed : 1u);
    web_boot();
    if (skip_splash) {                    /* the splash and the boot's 30 ms, rendered and dropped */
        while (web_boot_ms)
            web_render(128);
        ring_r = ring_w;
    }
    return 0;
}
JC_API int jiant_booted(void) { return (int)web_booted; }

JC_API void jiant_render(float *l, float *r, uint32_t n)
{
    while (n) {
        uint32_t m = n < 1024u ? n : 1024u;
        web_render(m);
        if (l) {
            memcpy(l, out_l, m * sizeof *l);
            l += m;
        }
        if (r) {
            memcpy(r, out_r, m * sizeof *r);
            r += m;
        }
        n -= m;
    }
}

JC_API int jiant_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (status == 0xF8u || status == 0xFAu || status == 0xFBu || status == 0xFCu)   /* usb.c midi_in_event */
        return midi_enqueue(0xFu | (uint32_t)status << 8, 1u);
    return (int)web_midi(status, d1, d2);
}
JC_API void jiant_clock_external(int on) { song.g[G_CLOCK] = (uint8_t)(on ? 1u : 0u); }   /* 1: USB's clock */
JC_API void jiant_play(int on) { web_play((uint32_t)on); }
JC_API int jiant_playing(void) { return (int)song.playing; }
JC_API uint32_t jiant_bpm(void) { return (uint32_t)song.g[G_BPM]; }

JC_API void jiant_buttons(uint32_t held) { web_buttons(held); }
JC_API void jiant_keys(uint32_t held) { web_keys(held); }
JC_API void jiant_encoder(enum jiant_encoder role, int32_t steps) { web_enc((uint32_t)role, steps); }
JC_API void jiant_master(uint32_t level) { web_master(level); }

JC_API const uint16_t *jiant_screen(void) { return web_fb; }
JC_API uint32_t jiant_screen_draws(void) { return web_draws; }
JC_API uint32_t jiant_led_buttons(void) { return web_lit_buttons(); }
JC_API uint32_t jiant_led_keys(void) { return web_lit_keys(); }

/* ------------------------------------------------------------------ the state ---
 * "JNTS", version, count, then per object: id, length, its bytes; a CRC-32 of all before it last. Little endian.
 * The objects are the backup's (web/fm1backup.js; web_bk_len / web_bk_put): written in the order stems.js restores
 * them, the song index first, the songs' sections, the user presets and their FM6 patches, the live section last. */
#define JS_MAGIC 0x53544E4Au                /* "JNTS" */
#define JS_VERSION 1u
static uint32_t js_ids(uint32_t *ids)
{
    uint32_t n = 0, i;
    ids[n++] = 10u;
    for (i = 0; i < NSONG * 4u; i++)
        ids[n++] = 40u + i;
    ids[n++] = 6u;
    ids[n++] = 7u;
    ids[n++] = 9u;
    ids[n++] = 0u;
    return n;
}
static uint32_t js_crc(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c >> 1 ^ (c & 1u ? 0xEDB88320u : 0u);
    }
    return ~c;
}
static void js_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t js_get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
/* (web_bk_len: id 6, 7 and 9 have no reader there; the user presets and their FM6 patches are read here as
 * editor_backup.c reads them) */
static uint32_t js_len(uint32_t id)
{
    uint8_t *raw = proj_wire_u.raw;
    if (id == 6u || id == 7u) {                          /* (editor_backup.c ed_bk_src: the RAM bank) */
        if (up_bank[id - 6u].magic != UP_BANK_MAGIC)
            return 0;
        memcpy(raw, &up_bank[id - 6u], sizeof up_bank[0]);
        return (uint32_t)sizeof up_bank[0];
    }
    if (id == 9u) {
        if (!upf_valid(&upf))
            return 0;
        memcpy(raw, &upf, sizeof upf);
        return (uint32_t)sizeof upf;
    }
    return web_bk_len(id);
}

/* an object into the device: web_bk_put's, and the user presets into RAM too (editor_backup.c ed_bk_commit) */
static uint32_t js_put(uint32_t id, uint32_t len)
{
    if (id == 6u || id == 7u) {
        const up_bank_t *p = (const up_bank_t *)proj_wire_u.raw;
        uint32_t b = id - 6u;
        if (len && (len != sizeof *p || p->magic != UP_BANK_MAGIC || p->rsize != sizeof(up_rec_t) ||
                    p->nslot != UP_PER_BANK))
            return 2;
        if (len && st_save(OBJ_UPRESET0 + b, proj_wire_u.raw, len))
            return 4;
        memset(&up_bank[b], 0, sizeof up_bank[b]);
        if (len)
            memcpy(&up_bank[b], proj_wire_u.raw, len);
        up_bank_check(b, (int)len);
        up_gen++;
        return 0;
    }
    return web_bk_put(id, len);
}

JC_API size_t jiant_state_save(uint8_t *buf, size_t cap)
{
    uint32_t ids[64], n = js_ids(ids), i, count = 0;
    size_t at = 12u;
    for (i = 0; i < n; i++) {
        uint32_t len = js_len(ids[i]);
        if (!len)
            continue;
        if (buf && at + 8u + len <= cap) {
            js_put32(buf + at, ids[i]);
            js_put32(buf + at + 4u, len);
            memcpy(buf + at + 8u, proj_wire_u.raw, len);
        }
        at += 8u + len;
        count++;
    }
    if (!buf || at + 4u > cap)
        return at + 4u;
    js_put32(buf, JS_MAGIC);
    js_put32(buf + 4u, JS_VERSION);
    js_put32(buf + 8u, count);
    js_put32(buf + at, js_crc(buf, at));
    return at + 4u;
}

JC_API size_t jiant_state_max(void)
{
    uint32_t ids[64];
    return 16u + (size_t)js_ids(ids) * (8u + sizeof proj_wire_u.raw);
}

JC_API int jiant_state_load(const uint8_t *buf, size_t len)
{
    uint32_t count, i;
    size_t at = 12u;
    int rc = 0;
    if (!buf || len < 16u || js_get32(buf) != JS_MAGIC || js_get32(buf + 4u) != JS_VERSION)
        return 1;
    if (js_get32(buf + len - 4u) != js_crc(buf, len - 4u))
        return 2;
    count = js_get32(buf + 8u);
    for (i = 0; i < count; i++) {                         /* every object whole before anything is written */
        uint32_t olen;
        if (at + 8u > len - 4u)
            return 2;
        olen = js_get32(buf + at + 4u);
        if (olen > sizeof proj_wire_u.raw || olen > len - 4u - at - 8u)
            return 2;
        at += 8u + olen;
    }
    if (song.playing)
        seq_stop();
    for (i = 0, at = 12u; i < count; i++) {
        uint32_t id = js_get32(buf + at), olen = js_get32(buf + at + 4u);
        memcpy(proj_wire_u.raw, buf + at + 8u, olen);
        if (js_put(id, olen))
            rc = 3;
        at += 8u + olen;
    }
    ui.force = 1;
    return rc;
}

JC_API void jiant_generate(uint32_t genre, uint32_t seed, uint32_t energy, uint32_t form)
{
    web_gen(genre, seed, energy, form);
}
JC_API uint32_t jiant_genre_count(void) { return web_gen_count(); }
JC_API const char *jiant_genre_name(uint32_t i) { return web_gen_name(i); }
JC_API int jiant_song_select(uint32_t s) { return (int)web_song_select(s); }
