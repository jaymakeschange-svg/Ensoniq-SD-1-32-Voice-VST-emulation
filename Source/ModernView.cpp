/*
  ==============================================================================

    Ensoniq SD-1 MAME VST Emulation - Modern UI layer
    Open Source GPLv2/v3

  ==============================================================================
*/

#include "ModernView.h"
#include <complex>

namespace sd1ui
{
    using namespace Colours;

    namespace
    {
        const char* softIds[6] = { "btn_soft_tl", "btn_soft_tc", "btn_soft_tr",
                                   "btn_soft_bl", "btn_soft_bm", "btn_soft_br" };

        struct KeyDef { const char* id; const char* text; };

        const KeyDef programmingTabs[] = {
            { "btn_select",    "SELECT VOICE" }, { "btn_wave",      "WAVE" },
            { "btn_mod_mixer", "MOD MIXER" },    { "btn_prog_ctrl", "PROGRAM CTRL" },
            { "btn_effects2",  "EFFECTS" },      { "btn_pitch",     "PITCH" },
            { "btn_pitch_mod", "PITCH MOD" },    { "btn_filters",   "FILTERS" },
            { "btn_output",    "OUTPUT" },       { "btn_lfo",       "LFO" },
            { "btn_env1",      "ENV 1" },        { "btn_env2",      "ENV 2" },
            { "btn_env3",      "ENV 3" } };

        const KeyDef modeKeys[] = {
            { "btn_sounds", "SOUNDS" }, { "btn_presets", "PRESETS" },
            { "btn_cartbankset", "CART" }, { "btn_seq_mode", "SEQ" } };

        const KeyDef perfMenu[] = {
            { "btn_volume", "Volume" }, { "btn_pan", "Pan" }, { "btn_timbre", "Timbre" },
            { "btn_key_zone", "Key Zone" }, { "btn_transpose", "Transpose" }, { "btn_release", "Release" },
            { "btn_patch_menu", "Patch Select" }, { "btn_midi", "MIDI" }, { "btn_effects1", "Effects" },
            { "btn_replace", "Replace Program" } };

        const KeyDef sysMenu[] = {
            { "btn_master", "Master" }, { "btn_storage", "Storage" }, { "btn_midi_ctrl", "MIDI Control" } };

        const KeyDef seqMenu[] = {
            { "btn_song", "Song" }, { "btn_seq_track", "Edit Seq" }, { "btn_track", "Track" },
            { "btn_click", "Click" }, { "btn_seq_ctrl", "Seq Control" }, { "btn_locate", "Locate" },
            { "btn_record", "Record" }, { "btn_stop_cont", "Stop / Cont" }, { "btn_play", "Play" },
            { "btn_track_1_6", "Tracks 1-6" }, { "btn_track_7_12", "Tracks 7-12" } };

        enum Kind { kKey = 0, kAction, kMenu, kTab, kLightKey };

        // ---- tiny radix-2 FFT (juce_dsp is not part of this project) ----
        void fft (std::vector<std::complex<float>>& a)
        {
            const size_t n = a.size();
            for (size_t i = 1, j = 0; i < n; ++i)
            {
                size_t bit = n >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap (a[i], a[j]);
            }
            for (size_t len = 2; len <= n; len <<= 1)
            {
                const float ang = -2.0f * juce::MathConstants<float>::pi / (float) len;
                const std::complex<float> wl (std::cos (ang), std::sin (ang));
                for (size_t i = 0; i < n; i += len)
                {
                    std::complex<float> w (1.0f, 0.0f);
                    for (size_t k = 0; k < len / 2; ++k)
                    {
                        auto u = a[i + k], v = a[i + k + len / 2] * w;
                        a[i + k] = u + v;
                        a[i + k + len / 2] = u - v;
                        w *= wl;
                    }
                }
            }
        }

        // magnitude of the OTIS one-pole stage  y += k (x - y)
        float onePoleLP (float k, float w)
        {
            k = juce::jlimit (0.0005f, 1.0f, k);
            const float b = 1.0f - k;
            return k / std::sqrt (juce::jmax (1.0e-9f, 1.0f - 2.0f * b * std::cos (w) + b * b));
        }

        bool isNumberToken (const juce::String& s)
        {
            if (s.isEmpty()) return false;
            int i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
            if (i >= s.length()) return false;
            bool digit = false;
            for (; i < s.length(); ++i)
            {
                auto c = s[i];
                if (juce::CharacterFunctions::isDigit (c)) digit = true;
                else if (c != '.' && c != '%') return false;
            }
            return digit;
        }
    }

    //==========================================================================
    // SpectrumView
    //==========================================================================
    SpectrumView::SpectrumView (Host& h) : host (h), history ((size_t) (numRows * numBins), 0.0f)
    {
        setOpaque (true);
    }

    float SpectrumView::binToHz (float b) const
    {
        const float lo = 30.0f, hi = 15000.0f;
        return lo * std::pow (hi / lo, b / (float) (numBins - 1));
    }

    void SpectrumView::computeFrame (const VoiceFrame& fr, float* out)
    {
        constexpr int N = 512;
        std::vector<float> acc ((size_t) numBins, 0.0f);
        std::vector<std::complex<float>> buf ((size_t) N);
        int16_t wave[N];

        const double sr = fr.sampleRate > 1000.0 ? fr.sampleRate : 29761.9;
        lastSampleRate = sr;
        const float lo = 30.0f, hi = 15000.0f, logSpan = std::log (hi / lo);
        activeVoices = 0;

        for (int i = 0; i < fr.numVoices; ++i)
        {
            const auto& v = fr.v[i];
            if (! v.active || v.amp < 1.0e-4f || v.step <= 0.0) continue;
            ++activeVoices;

            const int got = host.readWave (v.bank, v.addr, v.start, v.end, wave, N);
            if (got < 32) continue;

            for (int n = 0; n < N; ++n)
            {
                const float s = n < got ? (float) wave[n] / 32768.0f : 0.0f;
                const float win = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) n / (float) (N - 1));
                buf[(size_t) n] = { s * win, 0.0f };
            }
            fft (buf);

            for (int k = 1; k < N / 2; ++k)
            {
                const float hz = (float) ((double) k * sr * v.step / (double) N);
                if (hz < lo || hz > juce::jmin (hi, (float) (sr * 0.5))) continue;

                const float w  = 2.0f * juce::MathConstants<float>::pi * hz / (float) sr;
                const float l1 = onePoleLP (v.k1, w);
                const float l2 = onePoleLP (v.k2, w);
                const float h2 = std::sqrt (juce::jmax (0.0f, 1.0f - l2 * l2));
                float g = l1 * l1 * (v.lp3 ? l2 : h2) * (v.lp4 ? l2 : h2);

                const float mag = std::abs (buf[(size_t) k]) / (float) (N / 4);
                const float p = mag * mag * g * g * v.amp * v.amp;
                const int b = juce::jlimit (0, numBins - 1,
                                            (int) std::lround (std::log (hz / lo) / logSpan * (float) (numBins - 1)));
                acc[(size_t) b] += p;
            }
        }

        // soften single-bin needles into peaks
        std::vector<float> sm ((size_t) numBins, 0.0f);
        for (int b = 0; b < numBins; ++b)
            for (int d = -2; d <= 2; ++d)
            {
                const int j = juce::jlimit (0, numBins - 1, b + d);
                static constexpr float kern[5] = { 0.08f, 0.24f, 0.36f, 0.24f, 0.08f };
                sm[(size_t) b] += acc[(size_t) j] * kern[d + 2];
            }
        acc.swap (sm);

        float best = 0.0f; int bestBin = -1;
        for (int b = 0; b < numBins; ++b)
        {
            const float db = 10.0f * std::log10 (acc[(size_t) b] + 1.0e-12f);
            const float v = juce::jlimit (0.0f, 1.0f, (db + 84.0f) / 84.0f);
            out[b] = v;
            if (v > best) { best = v; bestBin = b; }
        }
        peakHz = (bestBin >= 0 && best > 0.05f) ? binToHz ((float) bestBin) : 0.0f;
    }

    void SpectrumView::update()
    {
        VoiceFrame fr;
        std::vector<float> row ((size_t) numBins, 0.0f);
        if (host.engineReady() && host.readVoices (fr))
            computeFrame (fr, row.data());
        else
            activeVoices = 0;

        // light smoothing on the newest row so releases glide instead of blinking
        for (int b = 0; b < numBins; ++b)
            row[(size_t) b] = juce::jmax (row[(size_t) b], history[(size_t) b] - 0.035f);

        std::memmove (history.data() + numBins, history.data(), sizeof (float) * (size_t) (numBins * (numRows - 1)));
        std::memcpy (history.data(), row.data(), sizeof (float) * (size_t) numBins);
        repaint();
    }

    void SpectrumView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        const float u = r.getHeight() / 220.0f;            // local unit

        g.setGradientFill (juce::ColourGradient (panelDark, 0, 0, bezel, 0, r.getBottom(), false));
        g.fillAll();

        g.setColour (labelDim);
        g.setFont (labelFont (11.0f * juce::jmax (0.8f, u)));
        g.drawText ("THE SOUND YOU'RE PLAYING: 32 OTIS VOICES OVER TIME",
                    r.reduced (12 * u, 8 * u).removeFromTop (14 * u), juce::Justification::topLeft);

        // readouts, right side
        {
            auto info = r.reduced (14 * u, 10 * u).removeFromRight (150 * u).removeFromTop (60 * u);
            g.setFont (labelFont (11.0f * juce::jmax (0.8f, u)));
            auto line = [&] (const juce::String& k, const juce::String& v)
            {
                auto row = info.removeFromTop (16 * u);
                g.setColour (labelDim); g.drawText (k, row, juce::Justification::centredLeft);
                g.setColour (label);    g.drawText (v, row, juce::Justification::centredRight);
            };
            line ("VOICES", juce::String (activeVoices) + " / 32");
            line ("PEAK", peakHz > 0 ? (peakHz >= 1000 ? juce::String (peakHz / 1000.0f, 2) + " kHz"
                                                       : juce::String ((int) peakHz) + " Hz") : "-");
            line ("RATE", juce::String (lastSampleRate / 1000.0, 1) + " kHz");
        }

        // 3D waterfall: newest row at the front, older rows recede up and to the right
        auto plot = r.reduced (16 * u, 10 * u).withTrimmedTop (18 * u).withTrimmedBottom (14 * u);
        const float frontW = plot.getWidth() * 0.74f;
        const float depthX = plot.getWidth() * 0.20f;
        const float depthY = plot.getHeight() * 0.42f;
        const float amp    = plot.getHeight() * 0.56f;

        auto rowGeom = [&] (int i, float& x0, float& y0, float& w, float& a)
        {
            const float t = (float) i / (float) (numRows - 1);
            x0 = plot.getX() + depthX * t;
            y0 = plot.getBottom() - depthY * t;
            w  = frontW * (1.0f - 0.18f * t);
            a  = amp * (1.0f - 0.35f * t);
        };

        // floor guides at 100 Hz / 1 kHz / 10 kHz
        const float lo = 30.0f, hi = 15000.0f;
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float fx = std::log (hz / lo) / std::log (hi / lo);
            float x0, y0, w, a, x1, y1, w1, a1;
            rowGeom (0, x0, y0, w, a);
            rowGeom (numRows - 1, x1, y1, w1, a1);
            g.setColour (vfd.withAlpha (0.10f));
            g.drawLine (x0 + w * fx, y0, x1 + w1 * fx, y1, 1.0f);
            g.setColour (labelDim.withAlpha (0.8f));
            g.setFont (labelFont (10.0f * juce::jmax (0.8f, u), false));
            g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + " kHz" : juce::String ((int) hz) + " Hz",
                        juce::Rectangle<float> (x0 + w * fx - 30, y0 + 2, 60, 12 * u), juce::Justification::centredTop);
        }

        for (int i = numRows - 1; i >= 0; --i)
        {
            float x0, y0, w, a;
            rowGeom (i, x0, y0, w, a);
            const float* h = history.data() + (size_t) i * numBins;

            juce::Path line, fill;
            for (int b = 0; b < numBins; ++b)
            {
                const float x = x0 + w * (float) b / (float) (numBins - 1);
                const float y = y0 - h[b] * a;
                if (b == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, y0); fill.lineTo (x, y); }
                else        { line.lineTo (x, y); fill.lineTo (x, y); }
            }
            fill.lineTo (x0 + w, y0);
            fill.closeSubPath();

            const float t = (float) i / (float) (numRows - 1);
            g.setColour (bezel.withAlpha (0.86f));                 // hidden-line occlusion
            g.fillPath (fill);

            if (i == 0)
            {
                g.setGradientFill (juce::ColourGradient (salmon.withAlpha (0.28f), 0, y0 - a,
                                                         salmon.withAlpha (0.02f), 0, y0, false));
                g.fillPath (fill);
                g.setColour (salmon.withAlpha (0.35f));
                g.strokePath (line, juce::PathStrokeType (4.0f * juce::jmax (0.7f, u)));
                g.setColour (salmon);
                g.strokePath (line, juce::PathStrokeType (1.8f * juce::jmax (0.7f, u)));
            }
            else
            {
                g.setColour (vfd.withAlpha (0.08f + 0.55f * (1.0f - t)));
                g.strokePath (line, juce::PathStrokeType (1.0f));
            }
        }

        g.setColour (bezel);
        g.drawRect (getLocalBounds(), 1);
    }

    //==========================================================================
    // ModernView
    //==========================================================================
    ModernView::ModernView (Host& h) : host (h), spectrum (h)
    {
        setOpaque (true);
        setLookAndFeel (&lnf);
        addAndMakeVisible (spectrum);
        spectrum.setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }

    ModernView::~ModernView()
    {
        setLookAndFeel (nullptr);
    }

    void ModernView::timerCallback()
    {
        if (! isVisible()) return;
        serviceKeys();
        parseVfd();
        if (spectrum.isVisible()) spectrum.update();
        repaint();
    }

    // ---- panel key queue -------------------------------------------------------
    void ModernView::serviceKeys()
    {
        if (keyDown.isNotEmpty())
        {
            if (--keyHoldTicks > 0) return;
            host.setButton (keyDown, false);
            keyDown.clear();
            keyHoldTicks = 1;                      // one tick gap between presses
            return;
        }
        if (keyHoldTicks > 0) { --keyHoldTicks; return; }
        if (keyQueue.isEmpty()) return;

        keyDown = keyQueue[0];
        keyQueue.remove (0);
        host.setButton (keyDown, true);
        keyHoldTicks = 2;
    }

    // ---- VFD -> six soft-button zones ------------------------------------------
    void ModernView::parseVfd()
    {
        vfdChars = host.vfdText().paddedRight (' ', 80).substring (0, 80);
        for (auto& z : zones) z = Zone();

        for (int line = 0; line < 2; ++line)
        {
            int col = 0;
            while (col < 40)
            {
                while (col < 40 && vfdChars[line * 40 + col] == ' ') ++col;
                if (col >= 40) break;
                const int startCol = col;
                // a token ends at two spaces in a row (single spaces stay inside: "WAVE CLASS")
                while (col < 40 && ! (vfdChars[line * 40 + col] == ' ' && (col + 1 >= 40 || vfdChars[line * 40 + col + 1] == ' ')))
                    ++col;
                const int endCol = col;               // exclusive
                const float centre = 0.5f * (float) (startCol + endCol - 1);
                const int zi = line * 3 + (centre < 13.0f ? 0 : centre < 26.5f ? 1 : 2);

                auto& z = zones[zi];
                const auto tok = vfdChars.substring (line * 40 + startCol, line * 40 + endCol).trim();
                z.raw = z.raw.isEmpty() ? tok : z.raw + " " + tok;
                for (int c = startCol; c < endCol; ++c)
                    if (host.vfdUnderlined (line * 40 + c)) z.selected = true;
            }
        }

        // page name lives in the top-left corner
        pageTitle = vfdChars.substring (0, 40).trim().upToFirstOccurrenceOf (" ", false, false);

        // the page name sits in the top-left zone; keep it out of that zone's label
        if (pageTitle.isNotEmpty() && zones[0].raw.startsWith (pageTitle + " "))
            zones[0].raw = zones[0].raw.fromFirstOccurrenceOf (pageTitle + " ", false, false).trim();

        for (auto& z : zones)
        {
            if (z.raw.isEmpty()) continue;
            if (z.raw.containsChar ('='))
            {
                z.label = z.raw.upToLastOccurrenceOf ("=", false, false).trim();
                z.value = z.raw.fromLastOccurrenceOf ("=", false, false).trim();
            }
            else
            {
                auto words = juce::StringArray::fromTokens (z.raw, " ", "");
                if (words.size() > 1 && isNumberToken (words[words.size() - 1]))
                {
                    z.value = words[words.size() - 1];
                    words.remove (words.size() - 1);
                    z.label = words.joinIntoString (" ");
                }
                else
                {
                    z.value = z.raw;
                }
            }

            auto numText = z.value.upToFirstOccurrenceOf (" ", false, false);
            if (isNumberToken (numText))
            {
                const float n = numText.retainCharacters ("+-0123456789.").getFloatValue();
                z.numeric = true;
                z.bipolar = numText.startsWithChar ('+') || numText.startsWithChar ('-');
                const float span = std::abs (n) > 127 ? 255.0f : std::abs (n) > 99 ? 127.0f : 99.0f;
                z.norm = z.bipolar ? juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * n / span)
                                   : juce::jlimit (0.0f, 1.0f, n / span);
            }
        }
    }

    // ---- layout ------------------------------------------------------------------
    void ModernView::resized()
    {
        scale   = juce::jmax (0.1f, (float) getWidth() / 1200.0f);
        designH = (float) getHeight() / scale;
        const float W = 1200.0f, m = 14.0f;

        topBar    = { 0, 0, W, 58 };
        presetBox = { 232, 9, 404, 40 };
        prevBtn   = presetBox.withWidth (38).reduced (4);
        nextBtn   = presetBox.withX (presetBox.getRight() - 38).withWidth (38).reduced (4);
        tabRow    = { m, 66, W - 2 * m, 30 };
        subRow    = { m, 102, W - 2 * m, 24 };

        const bool compact = designH < 380.0f;
        const float zoneH = compact ? 44.0f : 54.0f, vfdH = compact ? 38.0f : 46.0f, gap = 6.0f;
        const float footerH = compact ? 0.0f : 18.0f;
        const float blockH = zoneH * 2 + vfdH + gap * 2;
        const float blockTop = designH - footerH - 8.0f - blockH;
        const float specTop = subRow.getBottom() + 10.0f;

        specArea = { m, specTop, W - 2 * m, juce::jmax (0.0f, blockTop - 10.0f - specTop) };
        vfdArea  = { m + 44, blockTop + zoneH + gap, W - 2 * m - 88, vfdH };
        footer   = { m, designH - footerH - 4.0f, W - 2 * m, footerH };

        const float zw = (vfdArea.getWidth() - 2 * gap) / 3.0f;
        for (int i = 0; i < 3; ++i)
        {
            zoneRects[i]     = { vfdArea.getX() + (float) i * (zw + gap), blockTop, zw, zoneH };
            zoneRects[i + 3] = { vfdArea.getX() + (float) i * (zw + gap), vfdArea.getBottom() + gap, zw, zoneH };
        }

        spectrum.setVisible (specArea.getHeight() > 70.0f);
        spectrum.setBounds ((specArea * scale).toNearestInt());
        layoutHits();
    }

    void ModernView::layoutHits()
    {
        hits.clear();

        // top-right actions
        struct A { const char* id; const char* t; int kind; float w; };
        const A acts[] = { { "lib", "LIBRARY", kAction, 86 }, { "media", "MEDIA", kAction, 70 },
                           { "btn_compare", "COMPARE", kKey, 84 }, { "btn_copy", "COPY", kKey, 60 },
                           { "btn_write", "WRITE", kKey, 66 }, { "panel", "PANEL", kAction, 66 },
                           { "settings", "SETUP", kAction, 62 } };
        float x = 1200.0f - 14.0f;
        for (int i = (int) std::size (acts) - 1; i >= 0; --i)
        {
            x -= acts[i].w;
            hits.push_back ({ { x, 13, acts[i].w, 32 }, acts[i].id, acts[i].t, acts[i].kind });
            x -= 6;
        }

        hits.push_back ({ prevBtn, "prev", "<", kAction });
        hits.push_back ({ nextBtn, "next", ">", kAction });

        // programming page tabs
        const int nTabs = (int) std::size (programmingTabs);
        const float tw = (tabRow.getWidth() - (float) (nTabs - 1) * 4.0f) / (float) nTabs;
        for (int i = 0; i < nTabs; ++i)
            hits.push_back ({ { tabRow.getX() + (float) i * (tw + 4.0f), tabRow.getY(), tw, tabRow.getHeight() },
                              programmingTabs[i].id, programmingTabs[i].text, kTab });

        // mode keys, bank keys, menus
        float sx = subRow.getX() + 46.0f;
        for (auto& k : modeKeys)
        {
            hits.push_back ({ { sx, subRow.getY(), 74, subRow.getHeight() }, k.id, k.text, kLightKey });
            sx += 78;
        }
        bankCaptionX = sx + 4;
        sx += 44;
        for (int b = 0; b < 10; ++b)
        {
            hits.push_back ({ { sx, subRow.getY(), 32, subRow.getHeight() }, "btn_bank" + juce::String (b),
                              juce::String (b), kLightKey });
            sx += 36;
        }
        sx += 10;
        const A menus[] = { { "menu_perf", "PERFORMANCE", kMenu, 108 }, { "menu_sys", "SYSTEM", kMenu, 74 },
                            { "menu_seq", "SEQUENCER", kMenu, 94 } };
        for (auto& mnu : menus)
        {
            hits.push_back ({ { sx, subRow.getY(), mnu.w, subRow.getHeight() }, mnu.id, mnu.t, mnu.kind });
            sx += mnu.w + 6;
        }
        hits.push_back ({ { sx, subRow.getY(), 44, subRow.getHeight() }, "btn_up", "UP", kKey });
        sx += 48;
        hits.push_back ({ { sx, subRow.getY(), 54, subRow.getHeight() }, "btn_down", "DOWN", kKey });
    }

    const ModernView::Hit* ModernView::hitAt (juce::Point<float> p) const
    {
        for (auto& h : hits)
            if (h.r.contains (p)) return &h;
        return nullptr;
    }

    int ModernView::zoneAt (juce::Point<float> p) const
    {
        for (int i = 0; i < 6; ++i)
            if (zoneRects[i].contains (p)) return i;
        return -1;
    }

    // ---- input -------------------------------------------------------------------
    void ModernView::clickHit (const Hit& h)
    {
        if (h.kind == kKey || h.kind == kLightKey)
        {
            if (keyQueue.size() < 16) queueKey (h.id);
            if (h.kind == kLightKey) activeTab = h.id;
            return;
        }
        if (h.kind == kTab)
        {
            queueKey (h.id);                         // pressing again cycles the sub-pages
            activeTab = h.id;
            return;
        }
        if (h.kind == kMenu)
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel (&lnf);
            const KeyDef* defs = h.id == "menu_perf" ? perfMenu : h.id == "menu_sys" ? sysMenu : seqMenu;
            const int n = h.id == "menu_perf" ? (int) std::size (perfMenu)
                        : h.id == "menu_sys"  ? (int) std::size (sysMenu) : (int) std::size (seqMenu);
            for (int i = 0; i < n; ++i) menu.addItem (i + 1, defs[i].text);

            juce::Component::SafePointer<ModernView> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal ((h.r * scale).toNearestInt())),
                                [safe, defs, n] (int r)
                                {
                                    if (safe != nullptr && r > 0 && r <= n)
                                    {
                                        safe->queueKey (defs[r - 1].id);
                                        safe->activeTab = defs[r - 1].id;
                                    }
                                });
            return;
        }

        if      (h.id == "prev")     host.stepPreset (-1);
        else if (h.id == "next")     host.stepPreset (+1);
        else if (h.id == "lib")      host.openLibrary();
        else if (h.id == "panel")    host.showPanel();
        else if (h.id == "settings") host.openSettings();
        else if (h.id == "media")    host.openMedia (this);
    }

    void ModernView::mouseDown (const juce::MouseEvent& e)
    {
        const auto p = toDesign (e.position);
        dragging = false;
        dragZone = -1;

        if (auto* h = hitAt (p)) { clickHit (*h); return; }

        if (presetBox.contains (p)) { host.openLibrary(); return; }

        const int z = zoneAt (p);
        if (z >= 0)
        {
            dragZone = z;
            dragStartValue = host.getDataEntry();
            if (! zones[z].selected) queueKey (softIds[z]);   // select the parameter, like the soft button
        }
    }

    void ModernView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragZone < 0) return;
        const float dy = (float) e.getDistanceFromDragStartY() / scale;
        if (! dragging && std::abs (dy) < 3.0f) return;
        if (! dragging) { dragging = true; host.beginDataEntry(); }
        host.setDataEntry (juce::jlimit (0.0f, 1.0f, dragStartValue - dy / 180.0f));
    }

    void ModernView::mouseUp (const juce::MouseEvent&)
    {
        if (dragging) host.endDataEntry();
        dragging = false;
        dragZone = -1;
    }

    void ModernView::mouseMove (const juce::MouseEvent& e)
    {
        juce::String id;
        if (auto* h = hitAt (toDesign (e.position))) id = h->id;
        else if (int z = zoneAt (toDesign (e.position)); z >= 0) id = "zone" + juce::String (z);
        if (id != hoverId) { hoverId = id; repaint(); }
    }

    void ModernView::mouseExit (const juce::MouseEvent&)
    {
        hoverId.clear();
        repaint();
    }

    void ModernView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        const auto p = toDesign (e.position);
        const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
        if (std::abs (d) < 0.01f) return;

        if (presetBox.contains (p)) { host.stepPreset (d > 0 ? -1 : +1); return; }

        const int z = zoneAt (p);
        if (z < 0 || keyQueue.size() > 12) return;
        if (! zones[z].selected && ! keyQueue.contains (softIds[z])) queueKey (softIds[z]);
        queueKey (d > 0 ? "btn_up" : "btn_down");
    }

    //==========================================================================
    // painting
    //==========================================================================
    void ModernView::drawKey (juce::Graphics& g, const Hit& h, bool active, bool lightKey)
    {
        auto r = h.r;
        const bool hover = hoverId == h.id;

        if (lightKey)
        {
            g.setColour (keyGrey.brighter (hover ? 0.12f : 0.0f));
            g.fillRoundedRectangle (r, 2.5f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawRoundedRectangle (r, 2.5f, 1.0f);
            auto ledR = juce::Rectangle<float> (r.getX() + 6, r.getCentreY() - 3, 9, 6);
            g.setColour (active ? led : ledOff);
            g.fillRect (ledR);
            if (active) { g.setColour (led.withAlpha (0.35f)); g.fillRect (ledR.expanded (2)); }
            g.setColour (bezel);
            g.setFont (labelFont (12.0f));
            g.drawText (h.text, r.withTrimmedLeft (17), juce::Justification::centred);
            return;
        }

        g.setColour (active ? bezel.brighter (0.12f) : bezel.brighter (hover ? 0.08f : 0.0f));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (active ? salmon : panelLight);
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, active ? 1.5f : 1.0f);
        if (active)
        {
            g.setColour (salmon);
            g.fillRect (r.getX() + 6, r.getBottom() - 3, r.getWidth() - 12, 2.0f);
        }
        g.setColour (h.kind == kMenu ? label : (active ? label : label.withAlpha (0.92f)));
        g.setFont (labelFont (h.r.getHeight() > 28 ? 13.5f : 12.0f));
        g.drawText (h.kind == kMenu ? h.text + juce::String::fromUTF8 (" \xe2\x96\xbe") : h.text,
                    r, juce::Justification::centred);
    }

    void ModernView::drawTopBar (juce::Graphics& g)
    {
        g.setColour (panelLight);
        g.fillRect (topBar);
        g.setColour (salmon);
        g.fillRect (topBar.getX(), topBar.getBottom() - 2, topBar.getWidth(), 2.0f);

        // SD-1 wordmark with the stacked "32"
        g.setColour (salmon);
        g.setFont (labelFont (36.0f).withHorizontalScale (0.95f));
        g.drawText ("SD-1", juce::Rectangle<float> (16, 7, 92, 44), juce::Justification::centredLeft);
        g.setFont (labelFont (14.0f));
        g.drawText ("3", juce::Rectangle<float> (100, 12, 14, 16), juce::Justification::centred);
        g.drawText ("2", juce::Rectangle<float> (100, 28, 14, 16), juce::Justification::centred);
        g.setColour (labelDim);
        g.setFont (labelFont (11.0f));
        g.drawText ("VOICE EDITOR", juce::Rectangle<float> (122, 16, 100, 14), juce::Justification::centredLeft);
        g.setColour (label.withAlpha (0.55f));
        g.setFont (labelFont (10.0f, false));
        g.drawText (host.engineReady() ? "ENGINE RUNNING" : "ENGINE OFFLINE",
                    juce::Rectangle<float> (122, 30, 100, 12), juce::Justification::centredLeft);

        // preset display: VFD glass
        g.setColour (bezel);
        g.fillRoundedRectangle (presetBox.expanded (2), 5.0f);
        g.setColour (glass);
        g.fillRoundedRectangle (presetBox, 4.0f);

        auto text = presetBox.reduced (44, 3);
        auto src = host.presetSource().toUpperCase();
        auto name = host.presetName();
        if (name.isEmpty())
        {
            // fall back to the program the firmware has underlined (SOUNDS / PRESETS mode)
            for (auto& z : zones) if (z.selected) { name = z.raw; break; }
        }
        if (name.isEmpty()) name = "NO PRESET SELECTED";

        g.setFont (labelFont (9.5f));
        g.setColour (vfd.withAlpha (0.55f));
        g.drawText (src.isNotEmpty() ? src : "PRESET", text.removeFromTop (12), juce::Justification::centredLeft);
        g.setFont (vfdFont (19.0f));
        g.setColour (vfd.withAlpha (0.22f));
        g.drawText (name.toUpperCase(), text.translated (0, 0.5f).expanded (1, 0), juce::Justification::centredLeft, true);
        g.setColour (vfd);
        g.drawText (name.toUpperCase(), text, juce::Justification::centredLeft, true);

        for (auto* b : { &prevBtn, &nextBtn })
        {
            const bool hov = hoverId == (b == &prevBtn ? "prev" : "next");
            g.setColour (bezel.brighter (hov ? 0.18f : 0.08f));
            g.fillRoundedRectangle (*b, 3.0f);
            juce::Path tri;
            auto c = b->getCentre();
            if (b == &prevBtn) tri.addTriangle (c.x + 4, c.y - 6, c.x + 4, c.y + 6, c.x - 5, c.y);
            else               tri.addTriangle (c.x - 4, c.y - 6, c.x - 4, c.y + 6, c.x + 5, c.y);
            g.setColour (hov ? vfd : label);
            g.fillPath (tri);
        }
    }

    void ModernView::drawTabs (juce::Graphics& g)
    {
        for (auto& h : hits)
        {
            if (h.kind == kTab)
            {
                // active = last tab pressed, or the page whose name the firmware shows
                bool active = (activeTab == h.id);
                if (! active && pageTitle.isNotEmpty())
                {
                    auto t = h.text.removeCharacters (" ").toUpperCase();
                    active = activeTab.isEmpty() && t.startsWith (pageTitle.removeCharacters (" ").toUpperCase());
                }
                drawKey (g, h, active, false);
            }
            else if (h.kind == kLightKey)
                drawKey (g, h, activeTab == h.id, true);
            else if (h.r.getY() > topBar.getBottom())
                drawKey (g, h, activeTab == h.id && h.kind == kMenu, false);
            else if (h.id != "prev" && h.id != "next")
                drawKey (g, h, h.id == "lib", false);
        }

        // SD-1 style section lines + captions under the rows
        g.setColour (salmon);
        g.fillRect (tabRow.getX(), tabRow.getBottom() + 2, tabRow.getWidth(), 1.5f);
        g.setFont (labelFont (10.5f));
        g.setColour (label);
        g.drawText ("MODE", juce::Rectangle<float> (subRow.getX(), subRow.getY(), 44, subRow.getHeight()),
                    juce::Justification::centredLeft);
        g.drawText ("BANK", juce::Rectangle<float> (bankCaptionX, subRow.getY(), 44, subRow.getHeight()),
                    juce::Justification::centredLeft);
    }

    void ModernView::drawVfd (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (bezel);
        g.fillRoundedRectangle (r.expanded (3), 5.0f);
        g.setColour (glass);
        g.fillRoundedRectangle (r, 4.0f);

        // zone guides (where the soft buttons look)
        g.setColour (vfd.withAlpha (0.06f));
        for (int i = 1; i < 3; ++i)
            g.fillRect (r.getX() + r.getWidth() * (float) i / 3.0f, r.getY() + 4, 1.0f, r.getHeight() - 8);

        auto inner = r.reduced (10, 4);
        const float cw = inner.getWidth() / 40.0f, lh = inner.getHeight() / 2.0f;
        g.setFont (vfdFont (juce::jmin (lh * 0.86f, cw * 1.55f)));
        for (int i = 0; i < 80; ++i)
        {
            const auto ch = vfdChars[i];
            const juce::Rectangle<float> cell (inner.getX() + (float) (i % 40) * cw,
                                               inner.getY() + (float) (i / 40) * lh, cw, lh);
            if (ch != ' ')
            {
                g.setColour (vfd.withAlpha (0.25f));
                g.drawText (juce::String::charToString (ch), cell.translated (0.6f, 0.4f), juce::Justification::centred, false);
                g.setColour (vfd);
                g.drawText (juce::String::charToString (ch), cell, juce::Justification::centred, false);
            }
            else
            {
                g.setColour (vfdDim.withAlpha (0.35f));
                g.fillRect (cell.getCentreX() - 0.75f, cell.getCentreY() - 0.75f, 1.5f, 1.5f);
            }
            if (host.vfdUnderlined (i))
            {
                g.setColour (vfd);
                g.fillRect (cell.getX() + 1, cell.getBottom() - 2.0f, cw - 2, 1.5f);
            }
        }
    }

    void ModernView::drawZone (juce::Graphics& g, int zi)
    {
        const auto& z = zones[zi];
        auto r = zoneRects[zi];
        const bool top = zi < 3, hover = hoverId == "zone" + juce::String (zi);
        const bool empty = z.raw.isEmpty();

        // the soft button itself, on the outer edge, like the hardware
        auto key = juce::Rectangle<float> (0, 0, 40, 7).withCentre ({ r.getCentreX(), top ? r.getY() - 4.0f : r.getBottom() + 4.0f });
        g.setColour (z.selected ? salmon : bezel.brighter (hover ? 0.25f : 0.1f));
        g.fillRoundedRectangle (key, 2.0f);

        g.setColour (empty ? panelDark : bezel);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (z.selected ? salmon : (hover ? keyGrey.withAlpha (0.5f) : panelLight));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, z.selected ? 2.0f : 1.0f);
        if (empty) return;

        auto in = r.reduced (12, 6);
        auto bar = in.removeFromBottom (5);
        in.removeFromBottom (3);

        if (z.label.isNotEmpty())
        {
            g.setColour (z.selected ? salmon : labelDim);
            g.setFont (labelFont (11.5f));
            g.drawText (z.label.toUpperCase(), in.removeFromTop (14), juce::Justification::topLeft, true);
            g.setColour (label);
            g.setFont (labelFont (in.getHeight() * 0.95f, true));
            g.drawText (z.value, in, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (z.selected ? label : label.withAlpha (0.85f));
            g.setFont (labelFont (juce::jmin (22.0f, in.getHeight() * 0.8f)));
            g.drawText (z.value, in, juce::Justification::centredLeft, true);
        }

        if (z.selected)
        {
            g.setColour (salmon);
            g.setFont (labelFont (10.0f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb2\xe2\x96\xbc"), r.reduced (10, 6), juce::Justification::topRight);
        }

        if (z.numeric)
        {
            g.setColour (panelDark);
            g.fillRoundedRectangle (bar, 2.0f);
            auto c = z.selected ? salmon : vfd.withAlpha (0.85f);
            g.setColour (c);
            if (z.bipolar)
            {
                const float mid = bar.getCentreX(), x = bar.getX() + bar.getWidth() * z.norm;
                g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (mid, x), bar.getY(), std::abs (x - mid) + 1.5f, bar.getHeight()), 2.0f);
                g.setColour (labelDim);
                g.fillRect (mid - 0.5f, bar.getY() - 2, 1.0f, bar.getHeight() + 4);
            }
            else
                g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * z.norm)), 2.0f);
        }
    }

    void ModernView::paint (juce::Graphics& g)
    {
        g.fillAll (panel);
        g.addTransform (juce::AffineTransform::scale (scale));

        drawTopBar (g);
        drawTabs (g);

        if (! spectrum.isVisible() && specArea.getHeight() > 8)
        {
            g.setColour (panelDark);
            g.fillRoundedRectangle (specArea, 4.0f);
        }

        // page header strip just above the display block
        for (int i = 0; i < 6; ++i) drawZone (g, i);
        drawVfd (g, vfdArea);

        // side captions, like the panel lettering beside the display
        g.setColour (labelDim);
        g.setFont (labelFont (10.0f));
        g.drawText ("SOFT", juce::Rectangle<float> (14, zoneRects[0].getY(), 40, zoneRects[0].getHeight()), juce::Justification::centredLeft);
        g.drawText ("SOFT", juce::Rectangle<float> (14, zoneRects[3].getY(), 40, zoneRects[3].getHeight()), juce::Justification::centredLeft);
        g.setColour (salmon);
        g.drawText (pageTitle.isNotEmpty() ? pageTitle : "VFD", juce::Rectangle<float> (14, vfdArea.getY(), 44, vfdArea.getHeight()),
                    juce::Justification::centredLeft, true);

        if (footer.getHeight() > 0)
        {
            g.setFont (labelFont (10.0f, false));
            g.setColour (labelDim);
            g.drawText ("CHANGES GO TO THE FIRMWARE AS PANEL PRESSES. PRESS A PAGE TAB AGAIN FOR ITS SUB-PAGES.",
                        footer, juce::Justification::centredLeft);
            g.drawText (juce::String::fromUTF8 ("CLICK A ZONE = SOFT BUTTON   WHEEL = \xe2\x96\xb2/\xe2\x96\xbc   DRAG = DATA ENTRY"),
                        footer, juce::Justification::centredRight);
        }
    }
}
