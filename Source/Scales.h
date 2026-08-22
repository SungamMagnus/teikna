#pragma once
#include <juce_core/juce_core.h>
#include <vector>

/** The default scale set from Ableton Live, so the plugin agrees with the host. */
namespace plotter
{
struct Scale
{
    const char* name;
    std::vector<int> degrees;
};

inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> table {
        { "Major",             { 0,2,4,5,7,9,11 } },
        { "Minor",             { 0,2,3,5,7,8,10 } },
        { "Dorian",            { 0,2,3,5,7,9,10 } },
        { "Mixolydian",        { 0,2,4,5,7,9,10 } },
        { "Lydian",            { 0,2,4,6,7,9,11 } },
        { "Phrygian",          { 0,1,3,5,7,8,10 } },
        { "Locrian",           { 0,1,3,5,6,8,10 } },
        { "Whole Tone",        { 0,2,4,6,8,10 } },
        { "Half-whole Dim.",   { 0,1,3,4,6,7,9,10 } },
        { "Whole-half Dim.",   { 0,2,3,5,6,8,9,11 } },
        { "Minor Blues",       { 0,3,5,6,7,10 } },
        { "Minor Pentatonic",  { 0,3,5,7,10 } },
        { "Major Pentatonic",  { 0,2,4,7,9 } },
        { "Harmonic Minor",    { 0,2,3,5,7,8,11 } },
        { "Harmonic Major",    { 0,2,4,5,7,8,11 } },
        { "Dorian #4",         { 0,2,3,6,7,9,10 } },
        { "Phrygian Dominant", { 0,1,4,5,7,8,10 } },
        { "Melodic Minor",     { 0,2,3,5,7,9,11 } },
        { "Lydian Augmented",  { 0,2,4,6,8,9,11 } },
        { "Lydian Dominant",   { 0,2,4,6,7,9,10 } },
        { "Super Locrian",     { 0,1,3,4,6,8,10 } },
        { "8-Tone Spanish",    { 0,1,3,4,5,6,8,10 } },
        { "Bhairav",           { 0,1,4,5,7,8,11 } },
        { "Hungarian Minor",   { 0,2,3,6,7,8,11 } },
        { "Hirajoshi",         { 0,2,3,7,8 } },
        { "In-Sen",            { 0,1,5,7,10 } },
        { "Iwato",             { 0,1,5,6,10 } },
        { "Kumoi",             { 0,2,3,7,9 } },
        { "Pelog Selisir",     { 0,1,3,7,8 } },
        { "Pelog Tembung",     { 0,1,5,7,8 } },
        { "Messiaen 1",        { 0,2,4,6,8,10 } },
        { "Messiaen 2",        { 0,1,3,4,6,7,9,10 } },
        { "Messiaen 3",        { 0,2,3,4,6,7,8,10,11 } },
        { "Messiaen 4",        { 0,1,2,5,6,7,8,11 } },
        { "Messiaen 5",        { 0,1,5,6,7,11 } },
        { "Messiaen 6",        { 0,2,4,5,6,8,10,11 } },
        { "Messiaen 7",        { 0,1,2,3,5,6,7,8,9,11 } },
        { "Chromatic",         { 0,1,2,3,4,5,6,7,8,9,10,11 } }
    };
    return table;
}

inline juce::StringArray scaleNames()
{
    juce::StringArray names;
    for (auto& s : scales())
        names.add (s.name);
    return names;
}

inline juce::StringArray noteNames()
{
    return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
}

inline bool isInScale (int midiNote, int root, int scaleIndex)
{
    const auto& degrees = scales()[(size_t) juce::jlimit (0, (int) scales().size() - 1, scaleIndex)].degrees;
    const int pc = ((midiNote - root) % 12 + 12) % 12;
    for (int d : degrees)
        if (d == pc)
            return true;
    return false;
}

/** Nearest in-scale note. Ties resolve downwards, which reads as more musical. */
inline int snapToScale (int midiNote, int root, int scaleIndex)
{
    if (isInScale (midiNote, root, scaleIndex))
        return midiNote;

    for (int d = 1; d <= 6; ++d)
    {
        if (isInScale (midiNote - d, root, scaleIndex)) return midiNote - d;
        if (isInScale (midiNote + d, root, scaleIndex)) return midiNote + d;
    }
    return midiNote;
}
} // namespace plotter
