/*
  ==============================================================================

    Ensoniq SD-1 MAME VST Emulation - Modern UI layer
    Open Source GPLv2/v3

    A full-window layer drawn over the original MAME panel. It does not touch
    the engine: everything goes through the same paths the panel already uses
    (panel-button parameters, the data entry slider parameter, the VFD text and
    the preset/file manager). The PANEL tab simply hides this layer.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ModernTheme.h"

namespace sd1ui
{
    //==========================================================================
    // Snapshot of the ES5505 (OTIS) voices, read from the running emulation.
    struct VoiceState
    {
        bool     active = false;
        double   step   = 0.0;    // wave words consumed per output sample
        uint32_t addr = 0, start = 0, end = 0;
        int      bank   = 0;
        float    amp    = 0.0f;   // linear 0..1
        float    k1 = 1.0f, k2 = 1.0f; // one-pole coefficients 0..1
        bool     lp3 = true, lp4 = true;
    };

    struct VoiceFrame
    {
        int        numVoices  = 0;
        double     sampleRate = 0.0;
        VoiceState v[32];
    };

    //==========================================================================
    // What the modern layer needs from the plugin. Implemented by the editor
    // (and by a mock in the UI test harness).
    class Host
    {
    public:
        virtual ~Host() = default;

        virtual bool         engineReady() = 0;
        virtual juce::String vfdText() = 0;               // 80 chars, 2 x 40
        virtual bool         vfdUnderlined (int index) = 0;

        virtual void  setButton (const juce::String& paramID, bool down) = 0;
        virtual void  beginDataEntry() = 0;
        virtual void  setDataEntry (float v) = 0;
        virtual void  endDataEntry() = 0;
        virtual float getDataEntry() = 0;

        virtual bool  readVoices (VoiceFrame&) = 0;
        virtual int   readWave (int bank, uint32_t addr, uint32_t loopStart, uint32_t loopEnd,
                                int16_t* dst, int n) = 0;

        virtual void         stepPreset (int delta) = 0;
        virtual juce::String presetName() = 0;
        virtual juce::String presetSource() = 0;

        virtual void openLibrary() = 0;
        virtual void openMedia (juce::Component* anchor) = 0;
        virtual void openSettings() = 0;
        virtual void showPanel() = 0;
    };

    //==========================================================================
    // Live 3D spectrum of the voices that are sounding (waves x pitch x filter x volume).
    class SpectrumView : public juce::Component
    {
    public:
        explicit SpectrumView (Host& h);
        void update();                       // called ~30 Hz by the owner
        void paint (juce::Graphics&) override;

        static constexpr int numBins = 112;
        static constexpr int numRows = 38;

    private:
        Host& host;
        std::vector<float> history;          // numRows x numBins, row 0 = newest
        int   activeVoices = 0;
        float peakHz = 0.0f;
        double lastSampleRate = 29761.9;

        float binToHz (float b) const;
        void  computeFrame (const VoiceFrame&, float* out);
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
    };

    //==========================================================================
    class ModernView : public juce::Component,
                       private juce::Timer
    {
    public:
        explicit ModernView (Host& h);
        ~ModernView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown  (const juce::MouseEvent&) override;
        void mouseDrag  (const juce::MouseEvent&) override;
        void mouseUp    (const juce::MouseEvent&) override;
        void mouseMove  (const juce::MouseEvent&) override;
        void mouseExit  (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // One soft-button zone of the VFD, parsed for display
        struct Zone
        {
            juce::String label, value, raw;
            bool  selected = false, numeric = false;
            float norm = 0.0f;               // bar position 0..1
            bool  bipolar = false;
        };

    private:
        void timerCallback() override;

        // --- layout (design units: 1200 wide, height follows the window aspect) ---
        float scale = 1.0f, designH = 542.0f;
        juce::Rectangle<float> topBar, presetBox, prevBtn, nextBtn, tabRow, subRow,
                               specArea, vfdArea, footer;
        juce::Rectangle<float> zoneRects[6];
        struct Hit { juce::Rectangle<float> r; juce::String id; juce::String text; int kind; };
        std::vector<Hit> hits;               // buttons/tabs rebuilt in resized()
        juce::String hoverId;
        float bankCaptionX = 0.0f;

        juce::Point<float> toDesign (juce::Point<float> p) const { return p / scale; }
        const Hit* hitAt (juce::Point<float> designPos) const;
        int zoneAt (juce::Point<float> designPos) const;
        void layoutHits();
        void clickHit (const Hit&);

        // --- panel key queue: one press/release per two ticks, like a real finger ---
        juce::StringArray keyQueue;
        juce::String keyDown;
        int keyHoldTicks = 0;
        void queueKey (const juce::String& id) { keyQueue.add (id); }
        void serviceKeys();

        // --- state ---
        Host& host;
        SpectrumView spectrum;
        juce::String vfdChars;
        Zone zones[6];
        juce::String pageTitle, activeTab;
        int   dragZone = -1;
        float dragStartValue = 0.0f;
        bool  dragging = false;

        void parseVfd();
        void drawTopBar (juce::Graphics&);
        void drawTabs (juce::Graphics&);
        void drawVfd (juce::Graphics&, juce::Rectangle<float>);
        void drawZone (juce::Graphics&, int z);
        void drawKey (juce::Graphics&, const Hit&, bool active, bool lightKey);

        LookAndFeel lnf;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModernView)
    };
}
