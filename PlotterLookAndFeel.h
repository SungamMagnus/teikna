#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace plotter
{
/** Drafting-table palette: warm paper, ink blue-green, no fills except active state. */
namespace theme
{
    const juce::Colour paper      { 0xffede7da };   // panel background
    const juce::Colour sheet      { 0xfffcfcfa };   // the drawing surface
    const juce::Colour ink        { 0xff2c6058 };
    const juce::Colour inkSoft    { 0xff7d9c96 };
    const juce::Colour scaleRow   { 0xffe7f0ed };   // in-scale row tint
    const juce::Colour rootRow    { 0xffd3e3df };
    const juce::Colour line       { 0xffd2ded9 };
    const juce::Colour barLine    { 0xff9fbab4 };
    const juce::Colour readHead   { 0xffb4593c };   // rust, the one warm accent
    const juce::Colour onText     { 0xffede7da };

    inline juce::Font mono (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              height,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }
}

class PlotterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PlotterLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme::paper);
        setColour (juce::ComboBox::backgroundColourId,        theme::paper);
        setColour (juce::ComboBox::textColourId,              theme::ink);
        setColour (juce::ComboBox::outlineColourId,           theme::ink);
        setColour (juce::PopupMenu::backgroundColourId,       theme::paper);
        setColour (juce::PopupMenu::textColourId,             theme::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::ink);
        setColour (juce::PopupMenu::highlightedTextColourId,  theme::onText);
        setColour (juce::Slider::textBoxTextColourId,         theme::ink);
        setColour (juce::Slider::textBoxOutlineColourId,      theme::ink);
        setColour (juce::Slider::textBoxBackgroundColourId,   theme::paper);
    }

    // ---- buttons: outlined, square, filled when active ---------------------
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.75f);
        const bool on = b.getToggleState();

        if (on)
        {
            g.setColour (theme::ink);
            g.fillRect (r);
        }
        else if (highlighted || down)
        {
            g.setColour (theme::ink.withAlpha (0.08f));
            g.fillRect (r);
        }

        g.setColour (theme::ink);

        if (b.getProperties()["dashed"])
        {
            const float dashes[] { 5.0f, 4.0f };
            juce::Path p;
            p.addRectangle (r);
            juce::PathStrokeType (1.5f).createDashedStroke (p, p, dashes, 2);
            g.fillPath (p);
        }
        else
        {
            g.drawRect (r, 1.5f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (theme::mono (12.0f, true).withExtraKerningFactor (0.14f));
        g.setColour (b.getToggleState() ? theme::onText : theme::ink);
        g.drawText (b.getButtonText().toUpperCase(), b.getLocalBounds(),
                    juce::Justification::centred, false);
    }

    // ---- combo boxes: outline plus a small caret ---------------------------
    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int, int, int, int, juce::ComboBox&) override
    {
        const juce::Rectangle<float> r (0.75f, 0.75f, width - 1.5f, height - 1.5f);
        g.setColour (theme::ink);
        g.drawRect (r, 1.5f);

        juce::Path caret;
        const float cx = (float) width - 13.0f, cy = height * 0.5f;
        caret.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.fillPath (caret);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return theme::mono (12.0f).withExtraKerningFactor (0.08f);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 0, box.getWidth() - 24, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, theme::ink);
        label.setJustificationType (juce::Justification::centredLeft);
    }

    juce::Font getPopupMenuFont() override { return theme::mono (12.0f); }

    // ---- toggles: a square box with a cross when on ------------------------
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const float boxSize = 15.0f;
        const auto bounds = b.getLocalBounds().toFloat();
        const juce::Rectangle<float> box (1.0f, (bounds.getHeight() - boxSize) * 0.5f, boxSize, boxSize);

        if (b.getToggleState())
        {
            g.setColour (theme::ink);
            g.fillRect (box);
            g.setColour (theme::onText);
            g.drawLine (box.getX() + 4, box.getY() + 4, box.getRight() - 4, box.getBottom() - 4, 1.6f);
            g.drawLine (box.getRight() - 4, box.getY() + 4, box.getX() + 4, box.getBottom() - 4, 1.6f);
        }
        else
        {
            g.setColour (highlighted ? theme::ink.withAlpha (0.08f) : juce::Colours::transparentBlack);
            g.fillRect (box);
            g.setColour (theme::inkSoft);
            g.drawRect (box, 1.5f);
        }

        g.setFont (theme::mono (12.0f, b.getToggleState()).withExtraKerningFactor (0.12f));
        g.setColour (b.getToggleState() ? theme::ink : theme::inkSoft);
        g.drawText (b.getButtonText().toUpperCase(),
                    b.getLocalBounds().withTrimmedLeft ((int) boxSize + 8),
                    juce::Justification::centredLeft, false);
    }

    // ---- sliders ----------------------------------------------------------
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style == juce::Slider::LinearBar)
        {
            g.setColour (theme::ink);
            g.drawRect (juce::Rectangle<float> ((float) x + 0.75f, (float) y + 0.75f,
                                                width - 1.5f, height - 1.5f), 1.5f);
            return;
        }

        const float cy = y + height * 0.5f;
        g.setColour (theme::inkSoft);
        g.drawLine ((float) x, cy, (float) (x + width), cy, 1.5f);

        g.setColour (theme::ink);
        g.drawLine (sliderPos, cy - 8.0f, sliderPos, cy + 8.0f, 2.0f);

        juce::ignoreUnused (s);
    }

    juce::Font getLabelFont (juce::Label&) override { return theme::mono (12.0f); }
};
} // namespace plotter
