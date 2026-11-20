// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
// The MPC Sample's surface as the FM-1's panel (src/jiant/panel_map.h), without hardware.
#include "jiant/panel_map.h"
#include <cstdio>

using namespace jiant_port;
static int fails;
#define CHECK(c, what) do { std::printf("  %s  %s\n", (c) ? "ok  " : "FAIL", what); fails += !(c); } while (0)

int main()
{
    PanelMap p;
    Command c = p.pad(0, true);
    CHECK(c.kind == Command::Keys && c.value == 1u, "pad 1 is F3 (key 0) in bank A");
    c = p.pad(15, true);
    CHECK(c.value == (1u | 1u << 15), "pad 16 is G#4 (key 15), both held");
    p.button(PadBank, true, false);
    p.button(PadBank, false, false);
    CHECK(p.window() == 1 && p.padKey(0) == 4, "PAD BANK: bank B starts at key 4");
    c = p.pad(0, false);
    CHECK(c.value == 1u << 15, "a pad lets go of the key it pressed, not the bank's new one");
    c = p.pad(15, false);
    CHECK(c.value == 0, "all keys up");
    p.button(Shift, true, false);
    p.button(PadBank, true, false);
    p.button(Shift, false, false);
    CHECK(p.window() == 0, "SHIFT + PAD BANK goes back");
    for (int i = 0; i < 3; ++i) p.button(PadBank, true, false);
    CHECK(p.window() == 3 && p.padKey(15) == 26, "bank D ends on the last key, G5");

    c = p.button(PlayB, true, false);
    CHECK(c.kind == Command::Buttons && c.value == 1u << PLAY, "PLAY is PLAY");
    c = p.button(PlayB, false, false);
    CHECK(c.value == 0, "and lets go");
    c = p.button(Stop, true, false);
    CHECK(c.kind == Command::None, "STOP does nothing while stopped");
    CHECK(p.button(Stop, false, false).kind == Command::None, "nor on its release");
    c = p.button(Stop, true, true);
    CHECK(c.value == 1u << PLAY, "STOP presses PLAY while playing");
    c = p.button(Stop, false, false);
    CHECK(c.value == 0, "and lets it go");
    c = p.button(TopLeft, true, false);
    Command d = p.button(SampleSelect, true, false);
    CHECK(c.value == 1u << HOME && d.value == (1u << HOME | 1u << SAVE), "HOME held, then SAVE: both (a combination)");
    p.button(TopLeft, false, false);
    p.button(SampleSelect, false, false);
    CHECK(p.button(Erase, true, false).kind == Command::None, "an unmapped button does nothing");

    CHECK(p.control(Knob1, 64, 0, true).kind == Command::None, "a knob's start position is a reference only");
    c = p.control(Knob1, 67, 0, false);
    CHECK(c.kind == Command::Encoder && c.encoder == K1 && c.steps == 3, "K1 moved +3: KNOB 1 three steps");
    c = p.control(Fader, 10, 0, false);
    CHECK(c.kind == Command::None, "the fader's first move only sets its reference");
    c = p.control(Fader, 5, 0, false);
    CHECK(c.encoder == K4 && c.steps == -5, "then FADER is KNOB 4");
    p.button(Shift, true, false);
    c = p.control(Knob1, 66, 0, false);
    CHECK(c.encoder == ALGO && c.steps == -1, "SHIFT + K1 is ALGORITHM");
    c = p.control(Wheel, 0, 2, false);
    CHECK(c.encoder == PRESET && c.steps == 2, "SHIFT + wheel is PRESETS");
    p.button(Shift, false, false);
    c = p.control(Wheel, 0, -1, false);
    CHECK(c.encoder == SELECT && c.steps == -1, "the wheel is SELECT");
    c = p.control(MainVolume, 127, 0, false);
    CHECK(c.kind == Command::Master && c.value == 1023, "MAIN VOLUME is MASTER");
    std::printf(fails ? "panel map: %d FAILED\n" : "panel map: all passed\n", fails);
    return fails != 0;
}
