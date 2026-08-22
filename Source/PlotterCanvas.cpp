#include "PlotterCanvas.h"
#include "Scales.h"

namespace
{
    const juce::Colour paperColour { 0xfff2f3ef };
    const juce::Colour inkColour   { 0xff1b36a8 };
    const juce::Colour pinkColour  { 0xffff48b0 };

    float distanceToSegment (juce::Point<float> p, juce::Point<float> a, juce::Point<float> b)
    {
        const auto ab = b - a;
        const float lengthSquared = ab.x * ab.x + ab.y * ab.y;

        if (lengthSquared <= 1.0e-6f)
            return p.getDistanceFrom (a);

        const float t = juce::jlimit (0.0f, 1.0f, ((p - a).x * ab.x + (p - a).y * ab.y) / lengthSquared);
        return p.getDistanceFrom (a + ab * t);
    }
}

PlotterCanvas::PlotterCanvas (PlotterProcessor& p) : processor (p)
{
    setOpaque (true);
    setMouseCursor (juce::MouseCursor::NoCursor);
    startTimerHz (30);
}

// ---------------------------------------------------------------------------

juce::Point<float> PlotterCanvas::toPicture (juce::Point<float> local) const
{
    const auto b = getLocalBounds().toFloat();
    return { local.x / juce::jmax (1.0f, b.getWidth())  * (float) PlotterProcessor::canvasWidth,
             local.y / juce::jmax (1.0f, b.getHeight()) * (float) PlotterProcessor::canvasHeight };
}

void PlotterCanvas::pushUndo()
{
    undoStack.push_back (processor.getPicture().createCopy());

    while (undoStack.size() > 8)          // full-size snapshots, so keep the stack short
        undoStack.pop_front();
}

void PlotterCanvas::pencilSegment (juce::Point<float> a, juce::Point<float> b)
{
    juce::Graphics g (processor.getPicture());
    g.setColour (inkColour);
    g.drawLine ({ a, b }, nib * 2.0f);
    g.fillEllipse (juce::Rectangle<float> (nib * 2.0f, nib * 2.0f).withCentre (b));
}

void PlotterCanvas::eraseSegment (juce::Point<float> a, juce::Point<float> b)
{
    // Graphics has no destination-out, so clear the alpha by hand inside the
    // bounding box of the stroke.
    const float radius = nib * 2.2f;
    auto area = juce::Rectangle<float> (a, b).expanded (radius + 1.0f)
                    .getSmallestIntegerContainer()
                    .getIntersection (processor.getPicture().getBounds());

    if (area.isEmpty())
        return;

    const juce::Image::BitmapData data (processor.getPicture(), juce::Image::BitmapData::readWrite);

    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            if (distanceToSegment ({ (float) x + 0.5f, (float) y + 0.5f }, a, b) <= radius)
                data.setPixelColour (x, y, juce::Colours::transparentBlack);
}

// ---------------------------------------------------------------------------

void PlotterCanvas::mouseDown (const juce::MouseEvent& e)
{
    pushUndo();
    dragging = true;
    erasingStroke = (tool == Tool::eraser) || e.mods.isRightButtonDown();
    lastPoint = toPicture (e.position);

    if (erasingStroke) eraseSegment (lastPoint, lastPoint);
    else               pencilSegment (lastPoint, lastPoint);

    repaint();
}

void PlotterCanvas::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto p = toPicture (e.position);
    cursor = e.position;
    cursorValid = true;

    if (erasingStroke) eraseSegment (lastPoint, p);
    else               pencilSegment (lastPoint, p);

    lastPoint = p;
    repaint();
}

void PlotterCanvas::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    processor.pictureChanged();     // re-read the picture once the stroke is finished
    repaint();
}

void PlotterCanvas::mouseMove (const juce::MouseEvent& e)
{
    cursor = e.position;
    cursorValid = true;
    repaint();
}

void PlotterCanvas::mouseExit (const juce::MouseEvent&)
{
    cursorValid = false;
    repaint();
}

void PlotterCanvas::undo()
{
    if (undoStack.empty())
        return;

    processor.getPicture() = undoStack.back();   // snapshots are unique copies
    undoStack.pop_back();

    processor.pictureChanged();
    repaint();
}

void PlotterCanvas::clearPicture()
{
    pushUndo();
    processor.getPicture().clear (processor.getPicture().getBounds());
    processor.pictureChanged();
    repaint();
}

// ---------------------------------------------------------------------------

void PlotterCanvas::timerCallback()
{
    const float now = processor.getPlayheadStep();

    if (std::abs (now - lastPlayhead) > 0.001f)
    {
        lastPlayhead = now;
        repaint();
    }
}

void PlotterCanvas::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.fillAll (paperColour);
    g.drawImage (processor.getPicture(), bounds);

    const auto rs = processor.readSettings();

    if (guides)
    {
        const float rowHeight = bounds.getHeight() / (float) rs.numRows();
        g.setFont (11.0f);

        for (int r = 0; r <= rs.numRows(); ++r)
        {
            const int midiNote = rs.lowMidi() + (rs.numRows() - r);
            if (midiNote % 12 != 0)
                continue;

            const float y = r * rowHeight;
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.drawHorizontalLine ((int) y, 0.0f, bounds.getWidth());
            g.setColour (juce::Colours::black.withAlpha (0.40f));
            g.drawText ("C" + juce::String (midiNote / 12 - 1),
                        4, (int) y - 14, 40, 14, juce::Justification::left);
        }

        const float barWidth = bounds.getWidth() / (float) rs.bars;
        for (int b = 0; b <= rs.bars; ++b)
        {
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.drawVerticalLine ((int) (b * barWidth), 0.0f, bounds.getHeight());

            if (b < rs.bars)
            {
                g.setColour (juce::Colours::black.withAlpha (0.08f));
                for (int q = 1; q < 4; ++q)
                    g.drawVerticalLine ((int) (b * barWidth + q * barWidth / 4.0f), 0.0f, bounds.getHeight());
            }
        }
    }

    const float head = processor.getPlayheadStep();
    if (head >= 0.0f)
    {
        const float x = head / (float) juce::jmax (1, rs.numSteps()) * bounds.getWidth();
        g.setColour (pinkColour.withAlpha (0.13f));
        g.fillRect (0.0f, 0.0f, x, bounds.getHeight());
        g.setColour (pinkColour);
        g.fillRect (x - 1.5f, 0.0f, 3.0f, bounds.getHeight());
    }

    if (cursorValid)
    {
        const float scale = bounds.getWidth() / (float) PlotterProcessor::canvasWidth;
        const float r = juce::jmax (2.5f, nib * (tool == Tool::eraser ? 2.2f : 1.0f) * scale);
        g.setColour (tool == Tool::eraser ? pinkColour : inkColour);
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (cursor), 1.2f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawRect (getLocalBounds(), 1);
}
