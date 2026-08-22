#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace plotter;

namespace ids
{
    static const juce::String key { "key" }, scale { "scale" }, snap { "snap" };
    static const juce::String bars { "bars" }, res { "res" };
    static const juce::String lowOct { "lowOct" }, octs { "octs" };
    static const juce::String gate { "gate" }, wave { "wave" };
    static const juce::String sendMidi { "sendMidi" }, hearIt { "hearIt" };
    static const juce::String freeBpm { "freeBpm" }, freeRun { "freeRun" };
}

static const juce::StringArray resChoices  { "1/4", "1/8", "1/16", "1/32" };
static const int               resValues[] { 1, 2, 4, 8 };
static const juce::StringArray barChoices  { "1", "2", "4", "8" };
static const int               barValues[] { 1, 2, 4, 8 };

juce::AudioProcessorValueTreeState::ParameterLayout PlotterProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::key, 1 },   "Key",   noteNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::scale, 1 }, "Scale", scaleNames(), 1));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ids::snap, 1 },  "Snap to scale", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::bars, 1 },  "Bars", barChoices, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::res, 1 },   "Read at", resChoices, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::lowOct, 1 },"Low C",
                                                        StringArray { "C0","C1","C2","C3","C4","C5" }, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::octs, 1 },  "Octaves",
                                                        StringArray { "2","3","4","5","6" }, 2));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ids::gate, 1 },  "Gate",
                                                        NormalisableRange<float> (0.1f, 1.0f, 0.01f), 0.95f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ids::wave, 1 },  "Voice",
                                                        StringArray { "saw","square","triangle","sine" }, 0));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ids::sendMidi, 1 }, "Send MIDI out", true));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ids::hearIt, 1 },   "Built-in voice", true));
    layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { ids::freeBpm, 1 },  "Tempo (no host)",
                                                        NormalisableRange<float> (20.0f, 300.0f, 0.1f), 140.0f));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { ids::freeRun, 1 },  "Run when stopped", false));

    return layout;
}

PlotterProcessor::PlotterProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PLOTTER", createLayout())
{
    for (auto* id : { &ids::key, &ids::scale, &ids::snap, &ids::bars, &ids::res,
                      &ids::lowOct, &ids::octs, &ids::gate, &ids::wave })
        apvts.addParameterListener (*id, this);

    for (int i = 0; i < 16; ++i)
        synth.addVoice (new PlotterVoice());

    synth.addSound (new PlotterSound());
    synth.setNoteStealingEnabled (true);

    activeNotes.reserve (256);
    pictureChanged();
}

PlotterProcessor::~PlotterProcessor()
{
    for (auto* id : { &ids::key, &ids::scale, &ids::snap, &ids::bars, &ids::res,
                      &ids::lowOct, &ids::octs, &ids::gate, &ids::wave })
        apvts.removeParameterListener (*id, this);
}

// ---------------------------------------------------------------------------

ReadSettings PlotterProcessor::readSettings() const
{
    const auto raw = [this] (const juce::String& id) -> float
    {
        if (auto* p = apvts.getRawParameterValue (id))
            return p->load();
        return 0.0f;
    };

    ReadSettings rs;
    rs.root         = (int) raw (ids::key);
    rs.scaleIndex   = (int) raw (ids::scale);
    rs.snap         = raw (ids::snap) > 0.5f;
    rs.bars         = barValues[juce::jlimit (0, 3, (int) raw (ids::bars))];
    rs.stepsPerBeat = resValues[juce::jlimit (0, 3, (int) raw (ids::res))];
    rs.lowOctave    = juce::jlimit (0, 5, (int) raw (ids::lowOct));
    rs.octaves      = 2 + juce::jlimit (0, 4, (int) raw (ids::octs));
    rs.gate         = raw (ids::gate);
    return rs;
}

void PlotterProcessor::pictureChanged()
{
    lastAnalysed = PictureReader::analyse (picture, readSettings());
    bank.publish (lastAnalysed);
}

void PlotterProcessor::parameterChanged (const juce::String&, float)
{
    // Re-reading the picture allocates, so bounce it to the message thread.
    triggerAsyncUpdate();
}

void PlotterProcessor::handleAsyncUpdate()
{
    pictureChanged();
}

// ---------------------------------------------------------------------------

void PlotterProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    activeNotes.clear();
    expectedStartStep = -1.0e9;
    freeRunStep = 0.0;
    wasRunning = false;
    playheadStep.store (-1.0f, std::memory_order_relaxed);
}

bool PlotterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void PlotterProcessor::flushActiveNotes (juce::MidiBuffer& out, int sampleOffset)
{
    for (const auto& a : activeNotes)
        out.addEvent (juce::MidiMessage::noteOff (1, a.midiNote), sampleOffset);

    activeNotes.clear();
}

void PlotterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const double sr = getSampleRate();
    if (numSamples <= 0 || sr <= 0.0)
        return;

    const auto& pattern = bank.read();

    const auto* waveParam = apvts.getRawParameterValue (ids::wave);
    const int waveform = waveParam != nullptr ? (int) waveParam->load() : 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<PlotterVoice*> (synth.getVoice (i)))
            v->setWaveform (waveform);

    const auto* gateParam = apvts.getRawParameterValue (ids::gate);
    const auto* resParam  = apvts.getRawParameterValue (ids::res);
    const float gate = gateParam != nullptr ? gateParam->load() : 0.95f;
    const int stepsPerBeat = resValues[juce::jlimit (0, 3, resParam != nullptr ? (int) resParam->load() : 2)];

    // ---- where are we on the host timeline? --------------------------------
    double bpm = 120.0;
    bool running = false;
    double startStep = 0.0;

    if (const auto* freeBpmParam = apvts.getRawParameterValue (ids::freeBpm))
        bpm = (double) freeBpmParam->load();

    const auto* freeRunParam = apvts.getRawParameterValue (ids::freeRun);
    const bool freeRun = freeRunParam != nullptr && freeRunParam->load() > 0.5f;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto hostBpm = pos->getBpm())
                if (*hostBpm > 0.0)
                    bpm = *hostBpm;

            if (pos->getIsPlaying())
            {
                if (auto ppq = pos->getPpqPosition())
                {
                    running = true;
                    startStep = *ppq * stepsPerBeat;   // musical position, so we never drift
                }
            }
        }
    }

    if (! running)
    {
        if (freeRun)
        {
            running = true;
            startStep = freeRunStep;
        }
        else
        {
            if (wasRunning)
                flushActiveNotes (midiMessages, 0);

            wasRunning = false;
            playheadStep.store (-1.0f, std::memory_order_relaxed);

            if (apvts.getRawParameterValue (ids::hearIt)->load() > 0.5f)
                synth.renderNextBlock (buffer, midiMessages, 0, numSamples);

            return;
        }
    }

    const double stepsPerSample = (bpm / 60.0) * stepsPerBeat / sr;
    const double endStep = startStep + numSamples * stepsPerSample;

    // A locate, a loop jump or a fresh start: drop anything still sounding.
    if (! wasRunning || std::abs (startStep - expectedStartStep) > 0.5)
    {
        flushActiveNotes (midiMessages, 0);
        activeNotes.clear();
    }

    juce::MidiBuffer generated;

    const auto offsetFor = [&] (double stepPos)
    {
        return juce::jlimit (0, numSamples - 1,
                             (int) std::floor ((stepPos - startStep) / stepsPerSample));
    };

    // ---- note offs due in this block ---------------------------------------
    for (int i = (int) activeNotes.size(); --i >= 0;)
    {
        if (activeNotes[(size_t) i].endStepGlobal <= endStep)
        {
            generated.addEvent (juce::MidiMessage::noteOff (1, activeNotes[(size_t) i].midiNote),
                                offsetFor (activeNotes[(size_t) i].endStepGlobal));
            activeNotes.erase (activeNotes.begin() + i);
        }
    }

    // ---- note ons on every step boundary inside this block ------------------
    const int loopLength = juce::jmax (1, pattern.numSteps);

    for (double s = std::ceil (startStep - 1.0e-9); s < endStep; s += 1.0)
    {
        const long long absolute = (long long) s;
        int local = (int) (absolute % loopLength);
        if (local < 0)
            local += loopLength;

        for (const auto& n : pattern.notes)
        {
            if (n.startStep < local) continue;
            if (n.startStep > local) break;          // notes are sorted by startStep

            const int offset = offsetFor (s);
            generated.addEvent (juce::MidiMessage::noteOn (1, n.midiNote, (juce::uint8) n.velocity), offset);

            const double end = s + juce::jmax (0.05, n.lengthSteps * (double) gate);
            if (activeNotes.size() < activeNotes.capacity())
                activeNotes.push_back ({ n.midiNote, end });
            else
                generated.addEvent (juce::MidiMessage::noteOff (1, n.midiNote),
                                    juce::jmin (numSamples - 1, offset + 1));
        }
    }

    expectedStartStep = endStep;
    wasRunning = true;
    freeRunStep = endStep;

    {
        const double pos = startStep - std::floor (startStep / loopLength) * loopLength;
        playheadStep.store ((float) pos, std::memory_order_relaxed);
    }

    if (apvts.getRawParameterValue (ids::hearIt)->load() > 0.5f)
        synth.renderNextBlock (buffer, generated, 0, numSamples);

    if (apvts.getRawParameterValue (ids::sendMidi)->load() > 0.5f)
        midiMessages.swapWith (generated);
    else
        midiMessages.clear();
}

// ---------------------------------------------------------------------------

juce::AudioProcessorEditor* PlotterProcessor::createEditor()
{
    return new PlotterEditor (*this);
}

void PlotterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // The drawing travels with the session, as a PNG in the state tree.
    juce::MemoryOutputStream png;
    juce::PNGImageFormat().writeImageToStream (picture, png);

    juce::ValueTree pic ("PICTURE");
    pic.setProperty ("png", juce::Base64::toBase64 (png.getData(), png.getDataSize()), nullptr);
    state.appendChild (pic, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PlotterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;

    auto pic = state.getChildWithName ("PICTURE");
    if (pic.isValid())
    {
        juce::MemoryOutputStream raw;
        if (juce::Base64::convertFromBase64 (raw, pic.getProperty ("png").toString()))
        {
            juce::MemoryInputStream in (raw.getData(), raw.getDataSize(), false);
            auto loaded = juce::PNGImageFormat().decodeImage (in);
            if (loaded.isValid())
            {
                picture = juce::Image (juce::Image::ARGB, canvasWidth, canvasHeight, true);
                juce::Graphics g (picture);
                g.drawImage (loaded, picture.getBounds().toFloat());
            }
        }
        state.removeChild (pic, nullptr);
    }

    apvts.replaceState (state);
    pictureChanged();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PlotterProcessor();
}
