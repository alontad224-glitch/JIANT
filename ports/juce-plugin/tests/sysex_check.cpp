// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Juan Ignacio Antonucci (Juan Jiant)
// The web editor through the standalone app (the plugin's own processor, made as the standalone wrapper makes it): its
// SysEx in the MIDI that reaches processBlock, the replies in the MIDI it gives back (what the app sends out of the
// MIDI output chosen in its settings). INFO: the device ('D' 3, a computer) and its big CPU ('H' 15 16, SAMPLE and
// GRAIN named); PRESET SAMPLE and DESC 83: SAMPLE's TRIG; a note in is not echoed out.
//   sysex_check
#include "../Source/JiantProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static int fails;
static void check(const char* what, bool ok)
{
    std::printf("juce sysex: %-70s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
    check("the standalone app has a MIDI output (producesMidi)", p->producesMidi());
    p->setRateAndBufferSizeDetails(48000.0, 256);
    p->prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> buf(2, 256);
    auto run = [&](std::initializer_list<uint8_t> sx, bool note = false) {
        juce::MidiBuffer midi;
        std::vector<uint8_t> v(sx);
        if (v.size())
            midi.addEvent(v.data(), int(v.size()), 0);
        if (note)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, uint8_t(100)), 1);
        p->processBlock(buf, midi);
        std::vector<std::vector<uint8_t>> out;
        for (const auto m : midi)
            out.emplace_back(m.data, m.data + m.numBytes);
        return out;
    };
    run({});
    auto info = run({0xF0, 0x7D, 0x46, 0x4C, 1, 0xF7});
    bool ok = info.size() == 1;
    if (ok) {
        const auto& r = info[0];
        size_t n = r.size();
        std::string s(r.begin(), r.end());
        ok = n > 40 && r[0] == 0xF0 && r[4] == 1 && r[n - 1] == 0xF7 && r[n - 4] == 0x59 && r[n - 8] == 0x48 &&
             r[n - 6] == 15 && r[n - 5] == 16 && r[n - 11] == 0x44 && r[n - 9] == 3 &&
             s.find("SAMPLE") != std::string::npos && s.find("GRAIN") != std::string::npos;
    }
    check("INFO in, its reply out: 'D' 3 (a computer), 'H' 15 16, SAMPLE and GRAIN", ok);
    auto pr = run({0xF0, 0x7D, 0x46, 0x4C, 8, 4, 0, 0xF7});
    auto d = run({0xF0, 0x7D, 0x46, 0x4C, 5, 0, 83, 0xF7});
    check("PRESET SAMPLE, DESC 83: TRIG", pr.size() == 1 && pr[0].size() == 8 && d.size() == 1 && d[0].size() > 20 &&
                                           std::string(d[0].begin() + 14, d[0].begin() + 18) == "TRIG");
    auto nt = run({}, true);
    check("a note in is not echoed out", nt.empty());
    std::printf(fails ? "juce sysex: %d FAILED\n" : "juce sysex: all passed\n", fails);
    return fails != 0;
}
