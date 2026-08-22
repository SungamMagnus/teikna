#pragma once
#include <juce_graphics/juce_graphics.h>
#include <atomic>
#include <map>
#include <vector>
#include "Scales.h"

namespace plotter
{
struct PlotterNote
{
    int midiNote   = 60;
    int startStep  = 0;
    int lengthSteps = 1;
    int velocity   = 100;
};

struct Pattern
{
    int numSteps = 64;                 // length of one loop, in steps
    std::vector<PlotterNote> notes;    // sorted by startStep
};

/** How the picture should be read. Everything here comes from plugin parameters. */
struct ReadSettings
{
    int   root        = 0;   // 0 = C
    int   scaleIndex  = 1;   // Minor
    bool  snap        = true;
    int   bars        = 4;
    int   stepsPerBeat = 4;  // 4 = 1/16 notes
    int   lowOctave   = 2;   // bottom of the canvas, C2
    int   octaves     = 4;
    float gate        = 0.95f;

    int numSteps() const { return bars * 4 * stepsPerBeat; }
    int numRows()  const { return octaves * 12; }
    int lowMidi()  const { return 12 + lowOctave * 12; }
};

/**
    Three-slot handoff between the message thread (which analyses the drawing)
    and the audio thread (which plays it). The writer never touches the slot the
    reader is on, and the reader never blocks, allocates or frees.
*/
class PatternBank
{
public:
    PatternBank() = default;

    /** Message thread only. */
    void publish (Pattern newPattern)
    {
        const int liveNow = live.load (std::memory_order_acquire);
        int target = 0;
        while (target == liveNow || target == lastWritten)
            ++target;

        slots[(size_t) target] = std::move (newPattern);
        lastWritten = target;
        live.store (target, std::memory_order_release);
    }

    /** Audio thread. The reference stays valid until this thread reads again. */
    const Pattern& read() const noexcept
    {
        return slots[(size_t) live.load (std::memory_order_acquire)];
    }

private:
    Pattern slots[3];
    std::atomic<int> live { 0 };
    int lastWritten = 0;
};

/**
    Reads a drawing into notes.

    Vertical position is pitch (top of the canvas is the highest note), horizontal
    position is time. Ink coverage after downsampling becomes velocity, so a light
    scribble plays quieter than a solid dragged mark.
*/
namespace PictureReader
{
    inline constexpr float threshold = 0.09f;

    /** Halve repeatedly before the final resize, so thin strokes survive a big downscale. */
    inline juce::Image downsample (const juce::Image& source, int cols, int rows)
    {
        juce::Image img = source;

        while (img.getWidth() > cols * 2 && img.getHeight() > rows * 2)
            img = img.rescaled (juce::jmax (cols, img.getWidth() / 2),
                                juce::jmax (rows, img.getHeight() / 2),
                                juce::Graphics::highResamplingQuality);

        return img.rescaled (cols, rows, juce::Graphics::highResamplingQuality);
    }

    inline Pattern analyse (const juce::Image& picture, const ReadSettings& rs)
    {
        Pattern pattern;
        pattern.numSteps = rs.numSteps();

        if (! picture.isValid())
            return pattern;

        const int cols = rs.numSteps();
        const int rows = rs.numRows();
        const auto small = downsample (picture, cols, rows);

        // midi note -> per-column ink coverage
        std::map<int, std::vector<float>> ink;
        {
            const juce::Image::BitmapData data (small, juce::Image::BitmapData::readOnly);

            for (int r = 0; r < rows; ++r)
            {
                const int rawNote = rs.lowMidi() + (rows - 1 - r);
                const int midiNote = rs.snap ? snapToScale (rawNote, rs.root, rs.scaleIndex) : rawNote;

                for (int c = 0; c < cols; ++c)
                {
                    const float a = data.getPixelColour (c, r).getFloatAlpha();
                    if (a < threshold)
                        continue;

                    auto& row = ink.try_emplace (midiNote, std::vector<float> ((size_t) cols, 0.0f)).first->second;
                    row[(size_t) c] = juce::jmax (row[(size_t) c], a);
                }
            }
        }

        for (auto& [midiNote, row] : ink)
        {
            int c = 0;
            while (c < cols)
            {
                if (row[(size_t) c] <= 0.0f) { ++c; continue; }

                const int start = c;
                float peak = 0.0f;
                while (c < cols && row[(size_t) c] > 0.0f)
                {
                    peak = juce::jmax (peak, row[(size_t) c]);
                    ++c;
                }

                PlotterNote n;
                n.midiNote    = juce::jlimit (0, 127, midiNote);
                n.startStep   = start;
                n.lengthSteps = c - start;
                n.velocity    = juce::jlimit (28, 127, juce::roundToInt (38.0f + peak * 95.0f));
                pattern.notes.push_back (n);
            }
        }

        std::sort (pattern.notes.begin(), pattern.notes.end(),
                   [] (const PlotterNote& a, const PlotterNote& b) { return a.startStep < b.startStep; });

        return pattern;
    }
} // namespace PictureReader
} // namespace plotter
