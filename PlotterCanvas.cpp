#include "PlotterCanvas.h"
#include "PlotterLookAndFeel.h"
#include "Scales.h"

using namespace plotter;

namespace
{
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

juce::Rectangle<int> PlotterCanvas::getSheetArea() const
{
    return getLocalBounds().withTrimmedLeft (gutterWidth);
}

// ---------------------------------------------------------------------------

juce::Point<float> PlotterCanvas::toPicture (juce::Point<float> local) const
{
    const auto sheet = getSheetArea().toFloat();
    return { (local.x - sheet.getX()) / juce::jmax (1.0f, sheet.getWidth())  * (float) PlotterProcessor::canvasWidth,
             (local.y - sheet.getY()) / juce::jmax (1.0f, sheet.getHeight()) * (float) PlotterProcessor::canvasHeight };
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
    g.setColour (theme::ink);
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
    if (! getSheetArea().contains (e.getPosition()))
        return;                                     // the gutter is a ruler, not a surface

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
    cursor = e.position;
    cursorValid = true;

    if (! dragging)
        return;

    const auto p = toPicture (e.position);

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
    cursorValid = getSheetArea().contains (e.getPosition());
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
    const auto rs = processor.readSettings();
    const auto sheet = getSheetArea().toFloat();
    const auto gutter = getLocalBounds().toFloat().withWidth ((float) gutterWidth);

    const int rows = rs.numRows();
    const float rowHeight = sheet.getHeight() / (float) rows;
    const bool labelEveryRow = rowHeight >= 13.0f;
    const auto names = noteNames();

    g.fillAll (theme::paper);
    g.setColour (theme::sheet);
    g.fillRect (sheet);

    // ---- pitch rows: in-scale tinted, root stronger -------------------------
    for (int r = 0; r < rows; ++r)
    {
        const int midiNote = rs.lowMidi() + (rows - 1 - r);
        const int degree = ((midiNote - rs.root) % 12 + 12) % 12;
        const bool inScale = isInScale (midiNote, rs.root, rs.scaleIndex);
        const bool isRoot = inScale && degree == 0;
        const float y = sheet.getY() + r * rowHeight;

        if (guides && inScale)
        {
            g.setColour (isRoot ? theme::rootRow : theme::scaleRow);
            g.fillRect (sheet.getX(), y, sheet.getWidth(), rowHeight);
        }

        if (! (labelEveryRow || midiNote % 12 == 0))
            continue;

        const juce::Rectangle<float> labelRow (gutter.getX(), y, gutter.getWidth(), rowHeight);
        const auto name = names[midiNote % 12] + juce::String (midiNote / 12 - 1);

        if (isRoot)
        {
            g.setColour (theme::ink);
            g.fillRect (labelRow.reduced (1.0f, 0.5f));
            g.setColour (theme::onText);
        }
        else
        {
            if (inScale)
            {
                g.setColour (theme::scaleRow);
                g.fillRect (labelRow.reduced (1.0f, 0.5f));
            }
            g.setColour (inScale ? theme::ink : theme::inkSoft.withAlpha (0.75f));
        }

        g.setFont (theme::mono (juce::jmin (12.0f, juce::jmax (7.0f, rowHeight - 2.0f)), inScale));
        g.drawText (name, labelRow.withTrimmedLeft (10.0f), juce::Justification::centredLeft, false);
    }

    // ---- grid ---------------------------------------------------------------
    if (guides)
    {
        g.setColour (theme::line);
        for (int r = 0; r <= rows; ++r)
            g.drawHorizontalLine ((int) (sheet.getY() + r * rowHeight), sheet.getX(), sheet.getRight());

        const float stepWidth = sheet.getWidth() / (float) juce::jmax (1, rs.numSteps());

        for (int s = 0; s <= rs.numSteps(); ++s)
        {
            const bool bar  = s % (rs.stepsPerBeat * 4) == 0;
            const bool beat = s % rs.stepsPerBeat == 0;

            if (! beat && stepWidth < 5.0f)
                continue;

            g.setColour (bar ? theme::barLine
                             : beat ? theme::line.darker (0.15f)
                                    : theme::line.withAlpha (0.6f));
            g.drawVerticalLine ((int) (sheet.getX() + s * stepWidth), sheet.getY(), sheet.getBottom());
        }
    }

    // ---- the drawing --------------------------------------------------------
    g.drawImage (processor.getPicture(), sheet);

    // ---- read head ----------------------------------------------------------
    const float head = processor.getPlayheadStep();
    if (head >= 0.0f)
    {
        const float x = sheet.getX() + head / (float) juce::jmax (1, rs.numSteps()) * sheet.getWidth();
        g.setColour (theme::readHead.withAlpha (0.10f));
        g.fillRect (sheet.getX(), sheet.getY(), x - sheet.getX(), sheet.getHeight());
        g.setColour (theme::readHead);
        g.fillRect (x - 1.5f, sheet.getY(), 3.0f, sheet.getHeight());
    }

    // ---- what you are looking at -------------------------------------------
    {
        const auto text = names[rs.root].toUpperCase() + " "
                        + juce::String (scales()[(size_t) rs.scaleIndex].name).toUpperCase()
                        + (rs.snap ? juce::String ("  |  SNAP ON") : juce::String ("  |  SNAP OFF"));

        const auto font = theme::mono (11.0f).withExtraKerningFactor (0.1f);
        g.setFont (font);

        const float width = juce::jmax (160.0f, (float) juce::GlyphArrangement::getStringWidthInt (font, text) + 24.0f);
        const juce::Rectangle<float> box (sheet.getRight() - width - 6.0f, sheet.getY() + 6.0f, width, 22.0f);

        g.setColour (theme::sheet.withAlpha (0.85f));
        g.fillRect (box);
        g.setColour (theme::inkSoft);
        g.drawRect (box, 1.0f);
        g.setColour (theme::ink);
        g.drawText (text, box, juce::Justification::centred, false);
    }

    // ---- nib cursor ---------------------------------------------------------
    if (cursorValid)
    {
        const float scale = sheet.getWidth() / (float) PlotterProcessor::canvasWidth;
        const float r = juce::jmax (2.5f, nib * (tool == Tool::eraser ? 2.2f : 1.0f) * scale);
        g.setColour (tool == Tool::eraser ? theme::readHead : theme::ink);
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (cursor), 1.2f);
    }

    g.setColour (theme::ink);
    g.drawRect (getLocalBounds(), 1);
    g.drawVerticalLine (gutterWidth, 0.0f, (float) getHeight());
}
