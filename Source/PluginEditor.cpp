#include "PluginEditor.h"
#include "Scales.h"

using namespace plotter;

namespace
{
    const juce::Colour bgColour   { 0xffe6e8e3 };
    const juce::Colour inkColour  { 0xff161c26 };
    const juce::Colour blueColour { 0xff1b36a8 };

    void fillCombo (juce::ComboBox& box, const juce::StringArray& items)
    {
        int id = 1;
        for (const auto& item : items)
            box.addItem (item, id++);
    }
}

PlotterEditor::PlotterEditor (PlotterProcessor& p)
    : AudioProcessorEditor (&p), processor (p), canvas (p)
{
    addAndMakeVisible (canvas);

    for (auto* b : { &pencilButton, &eraserButton, &undoButton, &clearButton,
                     &guidesButton, &exportButton, &dragButton })
        addAndMakeVisible (b);

    pencilButton.setClickingTogglesState (false);
    eraserButton.setClickingTogglesState (false);

    pencilButton.onClick = [this] { canvas.setTool (PlotterCanvas::Tool::pencil); refreshToolButtons(); };
    eraserButton.onClick = [this] { canvas.setTool (PlotterCanvas::Tool::eraser); refreshToolButtons(); };
    undoButton.onClick   = [this] { canvas.undo(); };
    clearButton.onClick  = [this] { canvas.clearPicture(); };
    guidesButton.onClick = [this]
    {
        canvas.setGuides (! canvas.getGuides());
        guidesButton.setColour (juce::TextButton::buttonColourId,
                                canvas.getGuides() ? blueColour : bgColour);
    };
    exportButton.onClick = [this] { exportMidi(); };

    dragButton.onStateChange = [this]
    {
        if (! dragButton.isMouseButtonDown() || isDragAndDropActive())
            return;

        dragFile = writeMidiToTempFile();
        if (dragFile.existsAsFile())
            performExternalDragDropOfFiles ({ dragFile.getFullPathName() }, false, this);
    };

    addAndMakeVisible (nibSlider);
    nibSlider.setRange (1.5, 26.0, 0.5);
    nibSlider.setValue (canvas.getNib(), juce::dontSendNotification);
    nibSlider.onValueChange = [this] { canvas.setNib ((float) nibSlider.getValue()); };

    fillCombo (keyBox,    noteNames());
    fillCombo (scaleBox,  scaleNames());
    fillCombo (barsBox,   { "1", "2", "4", "8" });
    fillCombo (resBox,    { "1/4", "1/8", "1/16", "1/32" });
    fillCombo (lowOctBox, { "C0", "C1", "C2", "C3", "C4", "C5" });
    fillCombo (octsBox,   { "2", "3", "4", "5", "6" });
    fillCombo (waveBox,   { "saw", "square", "triangle", "sine" });

    for (auto* b : { &keyBox, &scaleBox, &barsBox, &resBox, &lowOctBox, &octsBox, &waveBox })
        addAndMakeVisible (b);

    for (auto* b : { &snapButton, &sendMidiButton, &hearButton, &freeRunButton })
        addAndMakeVisible (b);

    addAndMakeVisible (gateSlider);

    const std::pair<juce::ComboBox*, const char*> combos[] {
        { &keyBox, "key" }, { &scaleBox, "scale" }, { &barsBox, "bars" }, { &resBox, "res" },
        { &lowOctBox, "lowOct" }, { &octsBox, "octs" }, { &waveBox, "wave" }
    };

    for (auto& [box, id] : combos)
        comboAttachments.push_back (std::make_unique<APVTS::ComboBoxAttachment> (processor.apvts, id, *box));

    const std::pair<juce::Button*, const char*> buttons[] {
        { &snapButton, "snap" }, { &sendMidiButton, "sendMidi" },
        { &hearButton, "hearIt" }, { &freeRunButton, "freeRun" }
    };

    for (auto& [button, id] : buttons)
        buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (processor.apvts, id, *button));

    gateAttachment = std::make_unique<APVTS::SliderAttachment> (processor.apvts, "gate", gateSlider);

    refreshToolButtons();
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (760, 520, 2400, 1500);
    setSize (1080, 700);
}

PlotterEditor::~PlotterEditor()
{
    if (dragFile.existsAsFile())
        dragFile.deleteFile();
}

void PlotterEditor::refreshToolButtons()
{
    const bool pencil = canvas.getTool() == PlotterCanvas::Tool::pencil;
    pencilButton.setColour (juce::TextButton::buttonColourId, pencil ? blueColour : bgColour);
    eraserButton.setColour (juce::TextButton::buttonColourId, pencil ? bgColour : blueColour);
    pencilButton.setColour (juce::TextButton::textColourOffId, pencil ? bgColour : inkColour);
    eraserButton.setColour (juce::TextButton::textColourOffId, pencil ? inkColour : bgColour);
}

bool PlotterEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getTextCharacter() == 'p') { canvas.setTool (PlotterCanvas::Tool::pencil); refreshToolButtons(); return true; }
    if (k.getTextCharacter() == 'e') { canvas.setTool (PlotterCanvas::Tool::eraser); refreshToolButtons(); return true; }
    if (k.getTextCharacter() == '[') { nibSlider.setValue (nibSlider.getValue() - 2.0); return true; }
    if (k.getTextCharacter() == ']') { nibSlider.setValue (nibSlider.getValue() + 2.0); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { canvas.undo(); return true; }

    return false;
}

// ---------------------------------------------------------------------------

void PlotterEditor::paint (juce::Graphics& g)
{
    g.fillAll (bgColour);

    g.setColour (blueColour);
    g.setFont (juce::FontOptions (26.0f).withStyle ("Bold"));
    g.drawText ("TEIKNA", 16, 10, 200, 30, juce::Justification::left);

    g.setColour (inkColour.withAlpha (0.55f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Draw a picture. The host transport plays it left to right.",
                150, 16, 460, 20, juce::Justification::left);
}

void PlotterEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (34);                      // masthead

    auto toolRow = area.removeFromTop (32);
    const auto slot = [&toolRow] (int w) { auto r = toolRow.removeFromLeft (w); toolRow.removeFromLeft (6); return r; };

    pencilButton.setBounds (slot (80));
    eraserButton.setBounds (slot (80));
    nibSlider   .setBounds (slot (120));
    undoButton  .setBounds (slot (70));
    clearButton .setBounds (slot (70));
    guidesButton.setBounds (slot (80));
    dragButton  .setBounds (toolRow.removeFromRight (100));
    toolRow.removeFromRight (6);
    exportButton.setBounds (toolRow.removeFromRight (110));

    area.removeFromTop (8);

    auto paramRow = area.removeFromTop (30);
    const auto pslot = [&paramRow] (int w) { auto r = paramRow.removeFromLeft (w); paramRow.removeFromLeft (6); return r; };

    keyBox        .setBounds (pslot (64));
    scaleBox      .setBounds (pslot (150));
    snapButton    .setBounds (pslot (70));
    lowOctBox     .setBounds (pslot (64));
    octsBox       .setBounds (pslot (58));
    barsBox       .setBounds (pslot (58));
    resBox        .setBounds (pslot (70));
    waveBox       .setBounds (pslot (90));
    hearButton    .setBounds (pslot (80));
    sendMidiButton.setBounds (pslot (90));
    freeRunButton .setBounds (pslot (150));
    gateSlider    .setBounds (paramRow.removeFromRight (70));

    area.removeFromTop (10);

    // keep the paper at 2:1
    auto paper = area;
    const int w = juce::jmin (paper.getWidth(), paper.getHeight() * 2);
    const int h = w / 2;
    canvas.setBounds (juce::Rectangle<int> (w, h).withCentre (paper.getCentre()));
}

// ---------------------------------------------------------------------------

juce::File PlotterEditor::writeMidiToTempFile() const
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("teikna-drag.mid");

    juce::MidiFile midiFile;
    const int ticksPerQuarter = 960;
    midiFile.setTicksPerQuarterNote (ticksPerQuarter);

    const auto rs = processor.readSettings();
    const auto pattern = processor.currentPattern();
    const double ticksPerStep = (double) ticksPerQuarter / rs.stepsPerBeat;

    juce::MidiMessageSequence sequence;
    sequence.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    for (const auto& n : pattern.notes)
    {
        const double on  = n.startStep * ticksPerStep;
        const double off = juce::jmax (on + 2.0, (n.startStep + n.lengthSteps * rs.gate) * ticksPerStep);
        sequence.addEvent (juce::MidiMessage::noteOn (1, n.midiNote, (juce::uint8) n.velocity), on);
        sequence.addEvent (juce::MidiMessage::noteOff (1, n.midiNote), off);
    }

    sequence.updateMatchedPairs();
    sequence.addEvent (juce::MidiMessage::endOfTrack(), pattern.numSteps * ticksPerStep);
    midiFile.addTrack (sequence);

    file.deleteFile();

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        midiFile.writeTo (*stream);
        stream->flush();
    }

    return file;
}

void PlotterEditor::exportMidi()
{
    const auto rs = processor.readSettings();
    const auto name = "teikna_" + noteNames()[rs.root].replace ("#", "s")
                    + "-" + juce::String (scales()[(size_t) rs.scaleIndex].name).removeCharacters (" .#-")
                    + ".mid";

    chooser = std::make_unique<juce::FileChooser> (
        "Export MIDI",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name),
        "*.mid");

    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto target = fc.getResult();
        if (target == juce::File())
            return;

        const auto temp = writeMidiToTempFile();
        if (temp.existsAsFile())
        {
            target.deleteFile();
            temp.copyFileTo (target);
        }
    });
}
