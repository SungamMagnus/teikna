# Plotter — a drawing sequencer as a JUCE plugin

Draw a picture; the host transport reads it left to right. Vertical position is pitch,
horizontal position is time, ink coverage is velocity.

Verified: builds clean against **JUCE 9.0.1** with `juce_recommended_warning_flags`, and
passes **pluginval strictness 5** (editor, state, automation, audio processing at
44.1/48/96 kHz across block sizes 64–1024).

---

## Why it's an instrument, not a MIDI effect

Ableton's own docs say AU plug-ins don't support direct MIDI out, and that you need the
VST version to route MIDI from a plug-in. Meanwhile a `IS_MIDI_EFFECT` build is a
Logic/AU MIDI-FX concept that Live won't load as one at all.

So this is built as `IS_SYNTH TRUE` + `NEEDS_MIDI_OUTPUT TRUE`. It loads as an instrument,
it has a small built-in voice so it makes sound on its own, and it emits MIDI that other
tracks can pick up.

**Routing it in Live:** drop Plotter on a MIDI track. On a second MIDI track, set
*MIDI From* → the Plotter track → *Plotter*, and *Monitor* to **In**. Put your real
instrument there. Turn off Plotter's built-in **Voice** toggle once you're monitoring the
other track. If routing gives you trouble, the **Drag MIDI** button drags a clip of the
current picture straight into the session — often the faster path.

---

## Build

```bash
cd Plotter
cmake -B build -DCMAKE_BUILD_TYPE=Release          # uses ~/JUCE by default
cmake --build build -j8
```

Point it elsewhere with `-DJUCE_PATH=/path/to/JUCE`. `COPY_PLUGIN_AFTER_BUILD` installs the
VST3 and AU to the usual user folders. Formats built: VST3, AU, Standalone.

**On JUCE 7:** one line needs changing. In `PluginEditor.cpp`, `juce::FontOptions` is JUCE 8+;
swap `g.setFont (juce::FontOptions (26.0f).withStyle ("Bold"))` for
`g.setFont (juce::Font (26.0f, juce::Font::bold))` and the same for the 12.0f line.

---

## How the tempo sync works

Everything is derived from the host's musical position, never from a counter of elapsed
samples — so it cannot drift, and it survives tempo automation for free.

Each block:

1. Read `getPlayHead()->getPosition()` → `bpm`, `ppqPosition`, `isPlaying`.
2. Convert to a step position: `startStep = ppqPosition * stepsPerBeat`.
   `stepsPerBeat` is 4 for a 1/16 read, 8 for 1/32, and so on.
3. `stepsPerSample = (bpm / 60) * stepsPerBeat / sampleRate`, giving `endStep` for this block.
4. Every integer step boundary that falls inside `[startStep, endStep)` gets a sample offset
   of `(step - startStep) / stepsPerSample`, so note starts land sample-accurately inside
   the block rather than snapping to block edges.
5. Steps wrap modulo the pattern length, so bar 1 of the host is always column 1 of the picture.

Note-offs are tracked as absolute step positions in `activeNotes` and fired the same way,
which means notes held across a block or loop boundary still end where they should.

If `startStep` doesn't continue from where the last block ended (a locate, a loop jump, a
transport start), everything sounding is flushed with note-offs before the new position is
played. Stopping the transport flushes too — no stuck notes.

With no host transport (standalone, or transport stopped), the **Run when stopped** toggle
free-runs from an internal clock at the *Tempo (no host)* parameter.

---

## Threading

The picture lives on the message thread. The audio thread never touches a `juce::Image`,
never allocates, and never locks.

- You draw → `PlotterCanvas` rasterises into `processor.getPicture()`.
- On mouse-up (or any parameter change, via `AsyncUpdater`) → `pictureChanged()` downsamples
  the image to a pitch × time grid and builds a `Pattern`.
- `PatternBank::publish()` writes the new pattern into one of three slots and flips an
  atomic index. The audio thread reads whichever slot is live. Three slots rather than two
  means the writer can never overwrite the slot the reader is currently on.

The playhead position goes the other way as a single `std::atomic<float>`, which the canvas
polls at 30 Hz.

---

## Files

| File | What's in it |
|---|---|
| `Source/Scales.h` | Ableton's default scale set, `snapToScale()` |
| `Source/Pattern.h` | `Pattern`, the triple-buffered `PatternBank`, and `PictureReader::analyse()` |
| `Source/SynthVoice.h` | The monitor voice: one oscillator, one-pole lowpass, ADSR |
| `Source/PluginProcessor.*` | Parameters, transport sync, MIDI generation, state (drawing saved as PNG in the state tree) |
| `Source/PlotterCanvas.*` | Pencil and eraser, undo, guides, playhead |
| `Source/PluginEditor.*` | Controls, MIDI export, drag-to-DAW |

The drawing is serialised as a base64 PNG inside the APVTS state, so pictures save and
reload with the session and travel with presets.

---

## Things I left for you

- **Snap is destructive at read time only.** The picture keeps its true pixel positions;
  changing key or scale re-reads it. That's deliberate, but it means a drawing can read
  very differently between scales — worth trying on purpose.
- **The eraser clears alpha pixel by pixel** inside the stroke's bounding box. Fine at
  1600×800; if you raise the canvas resolution a lot, move it to a stroke-mask approach.
- **No polyphony limit on MIDI out.** A solid filled blob can throw 40 simultaneous notes at
  your synth. A "max voices per step" parameter, keeping the loudest, would be a sane addition.
- **Undo is 8 full-frame snapshots** (~5 MB each). A stroke-list undo would be lighter if you
  want deeper history.
