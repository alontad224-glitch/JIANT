// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
#include "jiant/jiant_app.h"
#include "drivers/platform.h"
#include "runtime/screen.h"
#include "surface/leds.h"
#include "surface/pads.h"
#include "jiant_core.h"
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;
using jiant_port::Command;

namespace {
constexpr const char* kFlashFile = "flash.img";
constexpr const char* kNoData = "-";           // a session with no flash file (the tools)
constexpr uint32_t kQuietBlocks = 345;          // ~1 s without a flash write before it is copied out
constexpr size_t kCopyChunk = 64 * 1024;

// the card's folder for JIANT: the first mounted card under /media
std::string card_folder()
{
    std::error_code error;
    if (!fs::is_directory("/media", error))
        return {};
    for (const auto& entry : fs::directory_iterator("/media", error))
        if (entry.is_directory(error) && fs::exists(entry.path() / "JIANT" / kFlashFile, error))
            return (entry.path() / "JIANT").string();
    return {};
}

const char* const kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
std::string key_name(unsigned key)                // key 0 is F3
{
    unsigned midi = 53 + key;
    return std::string(kNotes[midi % 12]) + std::to_string(int(midi / 12) - 1);
}
}  // namespace

JiantApp::JiantApp(std::string data, uint32_t seed) : folder_(std::move(data))
{
    if (folder_.empty())
        folder_ = card_folder();               // (empty still: the service thread finds the card)
    if (folder_ != kNoData && !folder_.empty()) {
        std::vector<uint8_t> image;
        if (load_flash(folder_ + "/" + kFlashFile, image))
            jiant_flash_restore(image.data(), image.size());
    }
    if (!seed)
        seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
    jiant_boot(seed, 1);
    size_t size = 0;
    jiant_flash(&size);
    flashCopy_.resize(size);
    flashWritesSeen_ = flashCopyWrites_ = jiant_flash_writes();
}

JiantApp::~JiantApp()
{
    // the audio thread has stopped: the music as it is into the autosave, then the flash straight from the core
    if (folder_ != kNoData && !folder_.empty())
        jiant_autosave_now();
    if (folder_ != kNoData && !folder_.empty() && jiant_flash_writes() != flashCopyWrites_) {
        size_t size = 0;
        const uint8_t* image = jiant_flash(&size);
        std::memcpy(flashCopy_.data(), image, size);
        save_flash(folder_);
    }
}

bool JiantApp::load_flash(const std::string& file, std::vector<uint8_t>& image)
{
    size_t size = 0;
    jiant_flash(&size);
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f)
        return false;
    image.resize(size);
    bool ok = std::fread(image.data(), 1, size, f) == size && std::fgetc(f) == EOF;
    std::fclose(f);
    return ok;
}

// --------------------------------------------------------------------------------------------- MIDI thread ---

void JiantApp::post(const Command& c)
{
    if (c.kind == Command::None)
        return;
    uint32_t w = qWrite_.load(std::memory_order_relaxed);
    if (w - qRead_.load(std::memory_order_acquire) >= kQueue)
        return;                                 // full (1024 events in 3 ms: dropped)
    queue_[w % kQueue] = c;
    qWrite_.store(w + 1, std::memory_order_release);
}

void JiantApp::surface(const SurfaceEvent& event)
{
    if (event.pad >= 0) {
        unsigned kind = event.midi.status & 0xF0;
        if (kind == 0x90 || kind == 0x80)
            post(panel_.pad(unsigned(event.pad), kind == 0x90 && event.midi.value));
        return;
    }
    const SurfaceInput& in = event.input;
    if (in.kind == SurfaceInput::Kind::Button)
        post(panel_.button(in.index, in.pressed, playing_.load(std::memory_order_relaxed)));
    else if (in.kind == SurfaceInput::Kind::Control)
        post(panel_.control(in.index, in.value, in.delta, event.syncing));
    window_.store(panel_.window(), std::memory_order_relaxed);
    shift_.store(panel_.shift(), std::memory_order_relaxed);
}

void JiantApp::midi(const MidiPortInfo&, const MidiPacket& packet)
{
    unsigned i = 0;
    while (i < packet.size) {
        uint8_t st = packet.bytes[i];
        if (st == 0xF0) {                       // SysEx: not for JIANT here
            while (i < packet.size && packet.bytes[i] != 0xF7)
                ++i;
            ++i;
            continue;
        }
        if (st >= 0xF8) {                       // realtime: the clock, start, continue, stop
            post({Command::Midi, st});
            ++i;
            continue;
        }
        if (st < 0x80 || st >= 0xF0) {          // a stray data byte or system common
            ++i;
            continue;
        }
        unsigned len = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 2 : 3;
        if (i + len > packet.size)
            break;
        uint32_t d1 = packet.bytes[i + 1] & 0x7F, d2 = len == 3 ? packet.bytes[i + 2] & 0x7F : 0;
        post({Command::Midi, st | d1 << 8 | d2 << 16});
        i += len;
    }
}

void JiantApp::midi_lost()
{
    for (uint32_t ch = 0; ch < 16; ++ch)
        post({Command::Midi, (0xB0 | ch) | 123u << 8});   // all notes off
}

void JiantApp::lights(SurfaceLights& lights, Clock::time_point)
{
    uint32_t keys = ledKeys_.load(std::memory_order_relaxed), buttons = ledButtons_.load(std::memory_order_relaxed);
    uint32_t held = session().held.load(std::memory_order_relaxed);
    for (unsigned p = 0; p < jiant_port::kPads; ++p) {
        unsigned key = panel_.padKey(p);
        Rgb c {};
        if (held & (1u << p))
            c = {100, 100, 100};
        else if ((keys >> key) & 1u)
            c = {127, 48, 0};                   // JIANT's orange
        else
            c = {4, 2, 0};
        lights.pad(p, c);
    }
    for (unsigned b = 0; b < surface_bank_lights; ++b)
        lights.bank(b, b == panel_.window());
    for (unsigned fm = 0; fm < 14; ++fm) {
        int m = jiant_port::PanelMap::mpcButtonFor(fm);
        bool on = (buttons >> fm) & 1u;
        if (fm == jiant_port::PLAY)
            on = on || ((buttons >> 14) & 1u);  // PLAY's green
        if (m >= 0)
            lights.button(SurfaceButton(m), on ? ButtonLight::Bright : ButtonLight::Dim);
    }
    lights.button(SurfaceButton::Stop, playing_.load(std::memory_order_relaxed) ? ButtonLight::Bright : ButtonLight::Dim);
    uint8_t m = meter_.load(std::memory_order_relaxed);
    lights.meter(0, m);
    lights.meter(1, m);
}

// -------------------------------------------------------------------------------------------- audio thread ---

void JiantApp::apply(const Command& c)
{
    switch (c.kind) {
    case Command::Keys: jiant_keys(c.value); break;
    case Command::Buttons: jiant_buttons(c.value); break;
    case Command::Encoder: jiant_encoder(static_cast<enum jiant_encoder>(c.encoder), c.steps); break;
    case Command::Master: jiant_master(c.value); break;
    case Command::Midi: jiant_midi(uint8_t(c.value), uint8_t(c.value >> 8), uint8_t(c.value >> 16)); break;
    default: break;
    }
}

void JiantApp::render(const AudioBlock&, AudioBlock& output)
{
    uint32_t r = qRead_.load(std::memory_order_relaxed), w = qWrite_.load(std::memory_order_acquire);
    for (; r != w; ++r)
        apply(queue_[r % kQueue]);
    qRead_.store(r, std::memory_order_release);

    float left[audio_frames], right[audio_frames];
    jiant_render(left, right, audio_frames);
    float peak = 0.0f;
    for (unsigned i = 0; i < audio_frames; ++i) {
        float l = std::clamp(left[i], -1.0f, 1.0f), rr = std::clamp(right[i], -1.0f, 1.0f);
        peak = std::max(peak, std::max(std::fabs(l), std::fabs(rr)));
        output[2 * i] = int32_t(std::lrint(double(l) * 2147483647.0));
        output[2 * i + 1] = int32_t(std::lrint(double(rr) * 2147483647.0));
    }
    level_ = std::max(peak, level_ * 0.97f);
    static constexpr float steps[5] = {0.01f, 0.05f, 0.15f, 0.4f, 0.9f};
    uint8_t mask = 0;
    for (unsigned k = 0; k < 5; ++k)
        if (level_ >= steps[k])
            mask |= uint8_t(1u << k);
    meter_.store(mask, std::memory_order_relaxed);

    playing_.store(jiant_playing() != 0, std::memory_order_relaxed);
    cpu_.store(jiant_cpu_percent(), std::memory_order_relaxed);
    cpuPeak_.store(jiant_cpu_peak_percent(), std::memory_order_relaxed);
    shed_.store(jiant_voices_shed(), std::memory_order_relaxed);
    governed_.store(jiant_load_governed() != 0, std::memory_order_relaxed);
    if (jiant_voices_sounding() > voicesMost_.load(std::memory_order_relaxed))
        voicesMost_.store(jiant_voices_sounding(), std::memory_order_relaxed);
    ledButtons_.store(jiant_led_buttons(), std::memory_order_relaxed);
    ledKeys_.store(jiant_led_keys(), std::memory_order_relaxed);
    publish_screen();
    copy_flash_step();
}

// the screen, at most every 6 blocks (17 ms) and only when the firmware drew
void JiantApp::publish_screen()
{
    if (++blocksSinceScreen_ < 6)
        return;
    uint32_t draws = jiant_screen_draws();
    if (draws == screenDraws_)
        return;
    blocksSinceScreen_ = 0;
    screenDraws_ = draws;
    uint32_t s = screenSeq_.load(std::memory_order_relaxed);
    screenSeq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(screenShared_.data(), jiant_screen(), sizeof screenShared_);
    screenSeq_.store(s + 2, std::memory_order_release);
}

// the flash out for the card: once a second has passed without a write, 64 KiB a block (16 blocks, 46 ms); a write
// in between starts it over. The service thread writes the whole copy and clears flashReady_.
void JiantApp::copy_flash_step()
{
    uint32_t writes = jiant_flash_writes();
    if (writes != flashWritesSeen_) {
        flashWritesSeen_ = writes;
        flashQuietBlocks_ = 0;
        flashCopying_ = false;
        return;
    }
    if (!flashCopying_) {
        if (writes == flashCopyWrites_ || flashReady_.load(std::memory_order_acquire) || ++flashQuietBlocks_ < kQuietBlocks)
            return;
        flashCopying_ = true;
        flashCopyAt_ = 0;
    }
    size_t size = 0;
    const uint8_t* image = jiant_flash(&size);
    size_t n = std::min(kCopyChunk, size - flashCopyAt_);
    std::memcpy(flashCopy_.data() + flashCopyAt_, image + flashCopyAt_, n);
    flashCopyAt_ += uint32_t(n);
    if (flashCopyAt_ >= size) {
        flashCopying_ = false;
        flashCopyWrites_ = writes;
        flashReady_.store(true, std::memory_order_release);
    }
}

// ------------------------------------------------------------------------------------------ service thread ---

std::string JiantApp::data_folder(Hardware* hardware)
{
    if (!folder_.empty() || !hardware)
        return folder_;
    for (const auto& space : hardware->platform.storage())
        if (space.path.rfind("/media/", 0) == 0 && !space.read_only)
            return folder_ = space.path + "/JIANT";
    return {};
}

bool JiantApp::save_flash(const std::string& folder)
{
    std::error_code error;
    fs::create_directories(folder, error);
    std::string file = folder + "/" + kFlashFile, temp = file + ".new";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f)
        return false;
    bool ok = std::fwrite(flashCopy_.data(), 1, flashCopy_.size(), f) == flashCopy_.size() && std::fflush(f) == 0 &&
              fsync(fileno(f)) == 0;
    ok = std::fclose(f) == 0 && ok;
    if (ok)
        fs::rename(temp, file, error);
    ok = ok && !error;
    if (ok)
        flashSaves_.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

void JiantApp::service(Hardware& hardware)
{
    if (!flashReady_.load(std::memory_order_acquire) || folder_ == kNoData)
        return;
    if (session().card_share.load(std::memory_order_relaxed) != 0)
        return;                                 // the card is lent to a computer: later
    std::string folder = data_folder(&hardware);
    if (folder.empty())
        return;                                 // no card yet: kept until there is one
    save_flash(folder);
    flashReady_.store(false, std::memory_order_release);
}

std::string JiantApp::status() const
{
    return "jiant=" + std::string(jiant_version()) + " saves=" + std::to_string(flashSaves_.load()) +
           " window=" + std::to_string(window_.load()) + " jiant_cpu=" + std::to_string(cpu_.load()) +
           " jiant_cpu_peak=" + std::to_string(cpuPeak_.load()) + " jiant_voices_most=" +
           std::to_string(voicesMost_.load()) + " jiant_shed=" + std::to_string(shed_.load()) +
           " jiant_governed=" + std::to_string(int(governed_.load()));
}

std::string JiantApp::command(const std::string& action, const std::string&, Hardware&)
{
    if (action != "jiant-save")
        return {};
    return flashReady_.load() ? "OK saving" : "OK nothing waiting (saved once the firmware is idle a second)";
}

// ------------------------------------------------------------------------------------------- screen thread ---

void JiantApp::draw(juce::Graphics& g) { paint(g); }

void JiantApp::paint(juce::Graphics& g)
{
    for (int tries = 0; tries < 4; ++tries) {
        uint32_t s = screenSeq_.load(std::memory_order_acquire);
        if (s & 1u)
            continue;
        std::memcpy(screenLocal_.data(), screenShared_.data(), sizeof screenLocal_);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (screenSeq_.load(std::memory_order_relaxed) == s)
            break;
    }
    juce::Image image(juce::Image::RGB, 240, 240, false);
    {
        juce::Image::BitmapData px(image, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 240; ++x) {
                uint16_t v = screenLocal_[size_t(y * 240 + x)];
                v = uint16_t(v >> 8 | v << 8);   // the panel's byte order
                uint8_t r = uint8_t((v >> 11) << 3), gg = uint8_t(((v >> 5) & 63) << 2), b = uint8_t((v & 31) << 3);
                reinterpret_cast<juce::PixelRGB*>(px.getPixelPointer(x, y))->setARGB(255, r | r >> 5, gg | gg >> 6, b | b >> 5);
            }
    }
    g.fillAll(juce::Colours::black);
    g.drawImageAt(image, 40, 0);

    if (!font_) {
        try { font_ = load_typeface(); } catch (...) {}
    }
    if (!font_)
        return;
    unsigned w = window_.load(std::memory_order_relaxed);
    unsigned first = jiant_port::kWindowStart[w % jiant_port::kWindows];
    auto text = [&](const juce::String& s, int x, int y, int width, float size, juce::Colour c) {
        g.setColour(c);
        g.setFont(juce::Font(juce::FontOptions(font_).withHeight(size)));
        g.drawText(s, x, y, width, int(size) + 4, juce::Justification::centred, false);
    };
    const juce::Colour orange(0xffff7a00), grey(0xff8a8a8a);
    text("PADS", 0, 8, 40, 11, grey);
    text(juce::String::charToString(juce::juce_wchar('A' + w)), 0, 22, 40, 26, orange);
    text(key_name(first), 0, 58, 40, 11, grey);
    text(key_name(first + 15), 0, 72, 40, 11, grey);
    if (shift_.load(std::memory_order_relaxed))
        text("SHIFT", 0, 210, 40, 10, orange);
    text("JIANT", 280, 8, 40, 11, orange);
    text("CPU", 280, 196, 40, 10, grey);           // the real load of the MPC's processor
    text(juce::String(int(cpu_.load(std::memory_order_relaxed))) + "%", 280, 210, 40, 12,
         governed_.load(std::memory_order_relaxed) ? juce::Colour(0xffff4040) : orange);
}
