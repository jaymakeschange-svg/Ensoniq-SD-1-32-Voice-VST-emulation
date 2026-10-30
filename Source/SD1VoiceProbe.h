/*
  ==============================================================================

    Ensoniq SD-1 MAME VST Emulation - Modern UI layer
    Open Source GPLv2/v3

    Read-only view of the ES5505 (OTIS) voice registers for the spectrum.
    It locates the registers through MAME's save-state registry (the same
    entries MAME uses for save states) and the wave ROM regions, and only ever
    reads them from the message thread. Nothing in the engine is changed.

    Include after PluginProcessor.h (needs the MAME headers).

  ==============================================================================
*/

#pragma once

#include "ModernView.h"
#include <string_view>

struct SD1VoiceProbe
{
    struct Field
    {
        const uint8_t* base = nullptr;
        uint32_t size = 0, stride = 0, count = 0;

        uint64_t get (int i) const
        {
            if (base == nullptr || (uint32_t) i >= juce::jmax (1u, count)) return 0;
            const uint8_t* p = base + (size_t) i * stride;
            switch (size)
            {
                case 1:  return *p;
                case 2:  { uint16_t v; std::memcpy (&v, p, 2); return v; }
                case 4:  { uint32_t v; std::memcpy (&v, p, 4); return v; }
                case 8:  { uint64_t v; std::memcpy (&v, p, 8); return v; }
                default: return 0;
            }
        }
    };

    running_machine* machine = nullptr;
    bool ready = false;
    Field control, freqcount, start, end, accum, lvol, rvol, k1, k2, sampleRate, activeVoices;
    const uint16_t* rom[2] { nullptr, nullptr };
    uint32_t romWords[2] { 0, 0 };

    void bind (running_machine* m)
    {
        if (m == machine && ready) return;
        machine = m;
        ready = false;
        control = freqcount = start = end = accum = lvol = rvol = k1 = k2 = sampleRate = activeVoices = Field();
        rom[0] = rom[1] = nullptr;
        if (m == nullptr) return;

        auto& sm = m->save();
        for (int i = 0; i < sm.registration_count(); ++i)
        {
            void* base = nullptr;
            u32 valsize = 0, valcount = 0, blockcount = 0, stride = 0;
            const char* nm = sm.indexed_item (i, base, valsize, valcount, blockcount, stride);
            if (nm == nullptr || base == nullptr) continue;

            const std::string_view n (nm);
            if (n.find ("otis") == std::string_view::npos) continue;

            auto endsWith = [&n] (std::string_view s) { return n.size() >= s.size() && n.substr (n.size() - s.size()) == s; };
            auto set = [&] (Field& f)
            {
                f.base = static_cast<const uint8_t*> (base);
                f.size = valsize;
                f.stride = stride != 0 ? stride : valsize;
                f.count = juce::jmax (1u, blockcount) * juce::jmax (1u, valcount);
            };

            if      (endsWith ("/m_voice.control"))   set (control);
            else if (endsWith ("/m_voice.freqcount")) set (freqcount);
            else if (endsWith ("/m_voice.start"))     set (start);
            else if (endsWith ("/m_voice.end"))       set (end);
            else if (endsWith ("/m_voice.accum"))     set (accum);
            else if (endsWith ("/m_voice.lvol"))      set (lvol);
            else if (endsWith ("/m_voice.rvol"))      set (rvol);
            else if (endsWith ("/m_voice.k1"))        set (k1);
            else if (endsWith ("/m_voice.k2"))        set (k2);
            else if (endsWith ("/m_sample_rate"))     set (sampleRate);
            else if (endsWith ("/m_active_voices"))   set (activeVoices);
        }

        const char* regions[2] = { "waverom", "waverom2" };
        for (int b = 0; b < 2; ++b)
            if (auto* r = m->root_device().memregion (regions[b]))
            {
                rom[b] = reinterpret_cast<const uint16_t*> (r->base());
                romWords[b] = (uint32_t) (r->bytes() / 2);
            }

        ready = control.base != nullptr && freqcount.base != nullptr && accum.base != nullptr
             && lvol.base != nullptr && rom[0] != nullptr;
    }

    // OTIS volume: 4-bit exponent, 4-bit mantissa
    static float volumeToLinear (uint32_t v)
    {
        v &= 0xff;
        const float e = (float) (v >> 4), mant = (float) (v & 15);
        return (16.0f + mant) / 31.0f * std::pow (2.0f, e - 15.0f);
    }

    bool read (sd1ui::VoiceFrame& f) const
    {
        if (! ready) return false;
        static constexpr int fracBits = 11;   // ES550x internal address fraction
        f.numVoices  = activeVoices.base != nullptr ? juce::jlimit (1, 32, (int) activeVoices.get (0) + 1) : 32;
        f.sampleRate = sampleRate.base != nullptr ? (double) (int32_t) sampleRate.get (0) : 0.0;

        for (int i = 0; i < f.numVoices; ++i)
        {
            auto& v = f.v[i];
            const uint32_t ctl = (uint32_t) control.get (i);
            v.active = (ctl & 0x0003) == 0;                 // STOP0 | STOP1 clear
            v.step   = (double) freqcount.get (i) / (double) (1 << fracBits);
            v.addr   = (uint32_t) ((accum.get (i) >> fracBits) & 0xFFFFF);
            v.start  = (uint32_t) ((start.get (i) >> fracBits) & 0xFFFFF);
            v.end    = (uint32_t) ((end.get (i)   >> fracBits) & 0xFFFFF);
            v.bank   = (int) ((ctl >> 2) & 1);
            v.amp    = 0.5f * (volumeToLinear ((uint32_t) lvol.get (i)) + volumeToLinear ((uint32_t) rvol.get (i)));
            v.k1     = (float) (((uint32_t) k1.get (i) & 0xffff) >> 4) / 4096.0f;
            v.k2     = (float) (((uint32_t) k2.get (i) & 0xffff) >> 4) / 4096.0f;
            v.lp3    = (ctl & 0x0400) != 0;
            v.lp4    = (ctl & 0x0800) != 0;
        }
        return true;
    }

    // Reads n wave words from the voice position, wrapping inside its loop.
    int readWave (int bank, uint32_t addr, uint32_t loopStart, uint32_t loopEnd, int16_t* dst, int n) const
    {
        if (! ready || bank < 0 || bank > 1 || rom[bank] == nullptr) return 0;
        const uint32_t words = romWords[bank];
        const bool loops = loopEnd > loopStart + 8 && loopEnd < words;
        int got = 0;
        uint32_t a = addr;
        for (; got < n; ++got)
        {
            if (loops && a > loopEnd) a = loopStart + (a - loopEnd - 1) % (loopEnd - loopStart + 1);
            if (a >= words) break;
            dst[got] = (int16_t) rom[bank][a++];
        }
        return got;
    }
};
