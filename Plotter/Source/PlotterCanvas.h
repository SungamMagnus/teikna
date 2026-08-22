#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <deque>
#include "PluginProcessor.h"

/** Blank paper, a pencil and an eraser. Nothing else. */
class PlotterCanvas : public juce::Component,
                      private juce::Timer
{
public:
    enum class Tool { pencil, eraser };

    explicit PlotterCanvas (PlotterProcessor& p);

    void setTool (Tool t)       { tool = t; repaint(); }
    Tool getTool() const        { return tool; }
    void setNib (float radius)  { nib = radius; }
    float getNib() const        { return nib; }
    void setGuides (bool on)    { guides = on; repaint(); }
    bool getGuides() const      { return guides; }

    void undo();
    void clearPicture();

    /** Width of the pitch gutter down the left-hand side, in component pixels. */
    static constexpr int gutterWidth = 96;
    juce::Rectangle<int> getSheetArea() const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    juce::Point<float> toPicture (juce::Point<float> local) const;
    void pencilSegment (juce::Point<float> a, juce::Point<float> b);
    void eraseSegment (juce::Point<float> a, juce::Point<float> b);
    void pushUndo();

    PlotterProcessor& processor;
    Tool tool = Tool::pencil;
    float nib = 6.0f;
    bool guides = false;

    bool dragging = false;
    bool erasingStroke = false;
    juce::Point<float> lastPoint;
    juce::Point<float> cursor;
    bool cursorValid = false;

    std::deque<juce::Image> undoStack;
    float lastPlayhead = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlotterCanvas)
};
