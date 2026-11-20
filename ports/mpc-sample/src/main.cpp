// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
//
// JIANT FM on the Akai MPC Sample: sample-base's runtime (keremimo/sample-base) hosting JiantApp, plus the tools the
// SD boot and the build run without the hardware:
//   jiant --self-test FILE.wav     a note through the whole firmware, written out; 0 when it sounded and stopped
//   jiant --preview FILE.png       the screen after a few seconds of a generated song
#include "runtime/app.h"
#include "runtime/control_socket.h"
#include "runtime/inventory.h"
#include "runtime/options.h"
#include "runtime/run_app.h"
#include "runtime/screen.h"
#include "jiant/jiant_app.h"
#include "jiant_core.h"
#include <sys/stat.h>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <iterator>
#include <string>
#include <vector>

namespace {
using jiant_port::Command;

void little(std::ofstream& f, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        f.put(char(v >> (8 * i)));
}

// seconds of the app's render() into a WAV (16-bit stereo); peak and rms of the whole, rms of the last half second
struct Take { double peak = 0, rms = 0, tail = 0; bool finite = true; };
Take render_take(JiantApp& app, double seconds, const char* wav, double note_off_at)
{
    unsigned blocks = unsigned(seconds * audio_rate / audio_frames);
    std::ofstream f;
    if (wav) {
        f.open(wav, std::ios::binary);
        uint32_t bytes = blocks * audio_frames * 4;
        f.write("RIFF", 4); little(f, 36 + bytes, 4); f.write("WAVEfmt ", 8);
        little(f, 16, 4); little(f, 1, 2); little(f, 2, 2); little(f, audio_rate, 4);
        little(f, audio_rate * 4, 4); little(f, 4, 2); little(f, 16, 2);
        f.write("data", 4); little(f, bytes, 4);
    }
    AudioBlock in {}, out {};
    Take t;
    double energy = 0, tail = 0;
    unsigned tailFrom = blocks - unsigned(0.5 * audio_rate / audio_frames);
    for (unsigned b = 0; b < blocks; ++b) {
        if (note_off_at >= 0 && b == unsigned(note_off_at * audio_rate / audio_frames))
            app.post({Command::Midi, 0x80 | 60u << 8});
        app.render(in, out);
        for (int32_t s : out) {
            double v = s / 2147483648.0;
            t.finite &= std::isfinite(v);
            t.peak = std::max(t.peak, std::fabs(v));
            energy += v * v;
            if (b >= tailFrom)
                tail += v * v;
            if (wav)
                little(f, uint32_t(uint16_t(int16_t(s >> 16))), 2);
        }
    }
    t.rms = std::sqrt(energy / (blocks * audio_frames * 2.0));
    t.tail = std::sqrt(tail / ((blocks - tailFrom) * audio_frames * 2.0));
    if (wav && !f.good())
        throw std::runtime_error("Write the self-test audio");
    return t;
}

int self_test(const char* wav)
{
    auto app = std::make_unique<JiantApp>("-", 1);
    app->post({Command::Master, 800});
    app->post({Command::Midi, 0x90 | 60u << 8 | 100u << 16});
    Take t = render_take(*app, 3.0, wav, 0.6);
    std::cout << "SELF_TEST jiant=" << jiant_version() << " peak=" << t.peak << " rms=" << t.rms << " tail_rms=" << t.tail
              << '\n';
    if (!t.finite || t.peak < 0.001 || t.rms < 0.0001 || t.tail > 0.002)
        throw std::runtime_error("JIANT note/release self-test failed");
    return 0;
}

int preview(const char* png)
{
    auto app = std::make_unique<JiantApp>("-", 1);
    jiant_generate(0, 1234, 70, 0);
    jiant_play(1);
    render_take(*app, 3.0, nullptr, -1);
    juce::Image image(juce::Image::RGB, 320, 240, true);
    {
        juce::Graphics g(image);
        app->paint(g);
    }
    save_png(image, png);
    std::cout << "PREVIEW " << png << '\n';
    return 0;
}

// the flash kept across sessions: --persist-write DIR makes a song and leaves (the app writes DIR/flash.img on its
// way out, as on the MPC when the wheel is pushed); --persist-read DIR, a new process, starts from that file and
// must hold the same music
// the music saved in the device (the songs, their index, the user presets) without the live section: that comes back
// at power-on only with MENU > SYSTEM > RESTORE LAST ON, which JIANT leaves OFF (as on the FM-1)
std::vector<uint8_t> music()
{
    std::vector<uint8_t> state(jiant_state_max()), kept;
    state.resize(jiant_state_save(state.data(), state.size()));
    auto u32 = [&](size_t at) { return uint32_t(state[at]) | uint32_t(state[at + 1]) << 8 | uint32_t(state[at + 2]) << 16 |
                                       uint32_t(state[at + 3]) << 24; };
    for (size_t at = 12, n = u32(8); n--; ) {
        uint32_t id = u32(at), len = u32(at + 4);
        if (id != 0)
            kept.insert(kept.end(), state.begin() + long(at), state.begin() + long(at + 8 + len));
        at += 8 + len;
    }
    return kept;
}
int persist(const char* dir, bool write)
{
    std::string expect = std::string(dir) + "/expect.bin";
    auto app = std::make_unique<JiantApp>(dir, 1);
    if (write) {
        jiant_generate(0, 777, 80, 2);
        render_take(*app, 4.0, nullptr, -1);
        int saved = jiant_autosave_now();      // (what leaving does; the music after it, the transport stopped)
        auto m = music();
        std::ofstream(expect, std::ios::binary).write(reinterpret_cast<const char*>(m.data()), std::streamsize(m.size()));
        std::cout << "PERSIST wrote " << m.size() << " bytes of music, " << jiant_flash_writes() << " flash writes, autosave "
                  << saved << '\n';
        return 0;
    }
    render_take(*app, 0.5, nullptr, -1);
    std::ifstream f(expect, std::ios::binary);
    std::vector<uint8_t> want((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()), got = music();
    std::cout << "PERSIST read " << got.size() << " bytes of music, expected " << want.size() << '\n';
    if (want.empty() || got != want) {
        std::ofstream(std::string(dir) + "/got.bin", std::ios::binary)
            .write(reinterpret_cast<const char*>(got.data()), std::streamsize(got.size()));
        throw std::runtime_error("the music did not come back from the card (got.bin beside expect.bin)");
    }
    return 0;
}

std::optional<int> run_tool(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--persist-write") == 0)
        return persist(argv[2], true);
    if (argc == 3 && std::strcmp(argv[1], "--persist-read") == 0)
        return persist(argv[2], false);
    if (argc == 3 && std::strcmp(argv[1], "--self-test") == 0)
        return self_test(argv[2]);
    if (argc == 3 && std::strcmp(argv[1], "--preview") == 0)
        return preview(argv[2]);
    return std::nullopt;
}
}  // namespace

int main(int argc, char** argv)
{
    umask(0077);
    try {
        if (auto code = run_tool(argc, argv))
            return *code;
        auto options = parse_options(argc, argv, "jiant");
        switch (options.mode) {
        case Options::Mode::Run: {
            if (options.app != "jiant")
                throw std::runtime_error("This build runs only JIANT, not " + options.app);
            auto app = std::make_unique<JiantApp>(std::getenv("JIANT_DATA") ? std::getenv("JIANT_DATA") : "");
            return run_app(*app, options);
        }
        case Options::Mode::Control: return send_control_command(options.runtime, options.command);
        case Options::Mode::ControlStream: return stream_control_commands(options.runtime);
        case Options::Mode::Hardware: return print_hardware();
        }
    } catch (const std::exception& error) {
        std::cerr << "jiant: " << error.what() << '\n';
    }
    return 1;
}
