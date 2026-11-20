// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
//
// JIANT FM as the Akai MPC Sample's whole instrument, on sample-base's runtime (keremimo/sample-base, AGPL-3.0):
// the firmware's core (ports/core) plays at the MPC's 44.1 kHz in 128-frame blocks, the FM-1's very block, its
// 240 x 240 screen in the middle of the MPC's 320 x 240, the surface as the FM-1's panel (panel_map.h), MIDI from
// the DIN and USB ports, and the device's flash kept on the card (JIANT/flash.img).
//
// Threads (sample-base's): the core belongs to the audio thread. The MIDI thread turns the surface and the MIDI
// ports into commands through a lock-free queue; the audio thread hands back the screen (a seqlock), the lights
// (atomics) and a copy of the flash for the service thread to write to the card.
#pragma once
#include "runtime/app.h"
#include "jiant/panel_map.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class JiantApp : public App {
public:
    // data: the folder of the device's flash (empty: the card's JIANT folder, found at start and on the service
    // thread). seed: this session's dice (0: from the clock).
    explicit JiantApp(std::string data = {}, uint32_t seed = 0);
    ~JiantApp() override;

    void render(const AudioBlock& input, AudioBlock& output) override;   // audio
    void surface(const SurfaceEvent& event) override;                    // MIDI
    void midi(const MidiPortInfo& port, const MidiPacket& packet) override;
    void midi_lost() override;
    void lights(SurfaceLights& lights, Clock::time_point now) override;  // MIDI
    void draw(juce::Graphics& g) override;                               // screen
    void service(Hardware& hardware) override;                           // service
    std::string status() const override;
    std::string command(const std::string& action, const std::string& argument, Hardware& hardware) override;
    std::string command_names() const override { return "jiant-save"; }

    // for the tools (--self-test, --preview): the core driven straight, no runtime
    void post(const jiant_port::Command& c);
    void paint(juce::Graphics& g);                 // what draw() paints, without the session
    static bool load_flash(const std::string& file, std::vector<uint8_t>& image);

private:
    void apply(const jiant_port::Command& c);      // audio thread
    void publish_screen();                         // audio thread
    void copy_flash_step();                        // audio thread
    bool save_flash(const std::string& folder);    // service thread
    std::string data_folder(Hardware* hardware);   // service thread

    // MIDI thread -> audio thread: one producer, one consumer
    static constexpr unsigned kQueue = 1024;
    std::array<jiant_port::Command, kQueue> queue_ {};
    std::atomic<uint32_t> qWrite_ {0}, qRead_ {0};
    jiant_port::PanelMap panel_;                   // MIDI thread
    std::atomic<bool> playing_ {false};            // audio -> MIDI (STOP)
    std::atomic<unsigned> window_ {0};             // MIDI -> screen
    std::atomic<bool> shift_ {false};

    // audio -> screen: the 240 x 240 frame under a sequence count (odd while it is written)
    std::atomic<uint32_t> screenSeq_ {0};
    std::array<uint16_t, 240 * 240> screenShared_ {};
    std::array<uint16_t, 240 * 240> screenLocal_ {};   // screen thread's copy
    uint32_t screenDraws_ = ~0u, blocksSinceScreen_ = 0;

    // audio -> MIDI: the lights
    std::atomic<uint32_t> ledButtons_ {0}, ledKeys_ {0};

    // audio -> service: the flash, copied 64 KiB a block once the firmware stopped writing for a while
    std::vector<uint8_t> flashCopy_;
    uint32_t flashWritesSeen_ = 0, flashQuietBlocks_ = 0, flashCopyAt_ = 0, flashCopyWrites_ = 0;
    bool flashCopying_ = false;
    std::atomic<bool> flashReady_ {false};         // a whole copy waits for the service thread
    std::atomic<uint32_t> flashSaves_ {0};
    std::string folder_;                           // service thread (and the constructor)

    // audio -> screen, status: the real load on the MPC's CPU (the firmware's meter), the most voices sounding, and
    // whether anything gave way (voices faded by the guard, the governor's lighter sound)
    std::atomic<uint32_t> cpu_ {0}, cpuPeak_ {0}, voicesMost_ {0}, shed_ {0};
    std::atomic<bool> governed_ {false};

    float level_ = 0.0f;                           // audio: for the meters
    std::atomic<uint8_t> meter_ {0};
    juce::Typeface::Ptr font_;
};
