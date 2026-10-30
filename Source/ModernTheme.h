/*
  ==============================================================================

    Ensoniq SD-1 MAME VST Emulation - Modern UI layer
    Open Source GPLv2/v3

    Classic SD-1 palette, sampled from the hardware panel:
    charcoal panel, near-black keys/bezels, salmon-red logo + section lines,
    light-grey bank keys, cream keybed, green LEDs and the blue-green VFD.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace sd1ui
{
    namespace Colours
    {
        const juce::Colour panel      { 0xff313131 };  // main panel charcoal
        const juce::Colour panelLight { 0xff3b3b3b };  // raised strips
        const juce::Colour panelDark  { 0xff262626 };  // recessed areas
        const juce::Colour bezel      { 0xff0e0e0e };  // dark page keys / display bezel
        const juce::Colour glass      { 0xff050707 };  // VFD glass
        const juce::Colour salmon     { 0xffd85e69 };  // SD-1 logo + section lines
        const juce::Colour salmonDim  { 0xffa94f4e };
        const juce::Colour keyGrey    { 0xffb8b8b8 };  // light bank/mode keys
        const juce::Colour label      { 0xfffdfdfd };  // panel lettering
        const juce::Colour labelDim   { 0xffa4a4a0 };
        const juce::Colour cream      { 0xffefefe1 };  // keybed
        const juce::Colour led        { 0xff1afd1f };  // bank LEDs
        const juce::Colour ledOff     { 0xff0f3a10 };
        const juce::Colour vfd        { 0xff6ff2e2 };  // blue-green vacuum fluorescent display
        const juce::Colour vfdDim     { 0xff1f4a46 };
    }

    inline juce::Font labelFont (float h, bool bold = true)
    {
        auto f = juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), h,
                                                bold ? juce::Font::bold : juce::Font::plain));
        return f.withHorizontalScale (0.86f); // condensed, like the panel lettering
    }

    inline juce::Font vfdFont (float h)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::bold));
    }

    //==========================================================================
    // Look & feel used by the modern layer and the restyled preset/file manager
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            using namespace Colours;
            setColour (juce::ResizableWindow::backgroundColourId, panel);
            setColour (juce::TextButton::buttonColourId,   bezel);
            setColour (juce::TextButton::buttonOnColourId, salmon);
            setColour (juce::TextButton::textColourOffId,  label);
            setColour (juce::TextButton::textColourOnId,   label);
            setColour (juce::ComboBox::backgroundColourId, bezel);
            setColour (juce::ComboBox::textColourId,       label);
            setColour (juce::ComboBox::outlineColourId,    panelLight);
            setColour (juce::ComboBox::arrowColourId,      salmon);
            setColour (juce::PopupMenu::backgroundColourId, panelDark);
            setColour (juce::PopupMenu::textColourId,       label);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, salmon);
            setColour (juce::PopupMenu::highlightedTextColourId, label);
            setColour (juce::ListBox::backgroundColourId,   panelDark);
            setColour (juce::ListBox::outlineColourId,      bezel);
            setColour (juce::ListBox::textColourId,         label);
            setColour (juce::TreeView::backgroundColourId,  panelDark);
            setColour (juce::TreeView::selectedItemBackgroundColourId, salmon.withAlpha (0.35f));
            setColour (juce::TextEditor::backgroundColourId, glass);
            setColour (juce::TextEditor::textColourId,       vfd);
            setColour (juce::TextEditor::outlineColourId,    bezel);
            setColour (juce::Label::textColourId,            label);
            setColour (juce::GroupComponent::outlineColourId, salmon);
            setColour (juce::GroupComponent::textColourId,    label);
            setColour (juce::ScrollBar::thumbColourId,        keyGrey.withAlpha (0.45f));
            setColour (juce::ProgressBar::backgroundColourId, bezel);
            setColour (juce::ProgressBar::foregroundColourId, salmon);
            setColour (juce::HyperlinkButton::textColourId,   vfd);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                   bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (0.5f);
            auto c = b.getToggleState() ? Colours::salmon : bg;
            if (down)      c = c.brighter (0.15f);
            else if (over) c = c.brighter (0.08f);
            g.setColour (c);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Colours::panelLight);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int h) override
        {
            return labelFont (juce::jmin (15.0f, (float) h * 0.52f));
        }

        void drawGroupComponentOutline (juce::Graphics& g, int w, int h, const juce::String& text,
                                        const juce::Justification&, juce::GroupComponent&) override
        {
            g.setColour (Colours::salmon);
            g.fillRect (0, h - 2, w, 2);              // SD-1 style section line under the group
            g.setColour (Colours::label);
            g.setFont (labelFont (13.0f));
            g.drawText (text.toUpperCase(), 4, 0, w - 8, 16, juce::Justification::centredLeft);
        }
    };
}
