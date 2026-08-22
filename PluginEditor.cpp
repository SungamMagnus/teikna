#include "PluginEditor.h"
#include "PlotterLookAndFeel.h"
#include "Scales.h"

using namespace plotter;

namespace
{
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
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (canvas);
    canvas.setGuides (true);

    for (auto* b : { &pencilButton, &eraserButton, &undoButton, &clearButton,
                     &guidesButton, &exportButton, &dragButton })
        addAndMakeVisible (b);

    pencilButton.setClickingTogglesState (true);
    eraserButton.setClickingTogglesState (true);
    guidesButton.setClickingTogglesState (true);

    pencilButton.setToggleState (true, juce::dontSendNotification);
    guidesButton.setToggleState (true, juce::dontSendNotification);
    dragButton.getProperties().set ("dashed", true);

    pencilButton.onClick = [this] { canvas.setTool (PlotterCanvas::Tool::pencil); refreshToolButtons(); };
    eraserButton.onClick = [this] { canvas.setTool (PlotterCanvas::Tool::eraser); refreshToolButtons(); };
    undoButton.onClick   = [this] { canvas.undo(); };
    clearButton.onClick  = [this] { canvas.clearPicture(); };
    guidesButton.onClick = [this] { canvas.setGuides (guidesButton.getToggleState()); };
    exportButton.onClick = [this] { exportMidi(); };

    exportButton.setToggleState (true, juce::dontSendNotification);   // filled, it is the primary action

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
    nibSlider.onValueChange = [this]
    {
        canvas.setNib ((float) nibSlider.getValue());
        nibValue.setText (juce::String ((int) nibSlider.getValue()).paddedLeft ('0', 2),
                          juce::dontSendNotification);
    };

    addAndMakeVisible (nibValue);
    nibValue.setText ("06", juce::dontSendNotification);
    nibValue.setColour (juce::Label::textColourId, theme::ink);
    nibValue.setJustificationType (juce::Justification::centredLeft);

    fillCombo (keyBox,    noteNames());
    fillCombo (scaleBox,  scaleNames());
    fillCombo (barsBox,   { "1 BAR", "2 BARS", "4 BARS", "8 BARS" });
    fillCombo (resBox,    { "1/4", "1/8", "1/16", "1/32" });
    fillCombo (lowOctBox, { "C0", "C1", "C2", "C3", "C4", "C5" });
    fillCombo (octsBox,   { "2 OCT", "3 OCT", "4 OCT", "5 OCT", "6 OCT" });
    fillCombo (waveBox,   { "SAW", "SQUARE", "TRIANGLE", "SINE" });

    for (auto* b : { &keyBox, &scaleBox, &barsBox, &resBox, &lowOctBox, &octsBox, &waveBox })
        addAndMakeVisible (b);

    snapButton.setButtonText ("Snap to scale");
    sendMidiButton.setButtonText ("MIDI out");
    hearButton.setButtonText ("Voice");
    freeRunButton.setButtonText ("Run when stopped");

    for (auto* b : { &snapButton, &sendMidiButton, &hearButton, &freeRunButton })
        addAndMakeVisible (b);

    addAndMakeVisible (gateSlider);
    gateSlider.setSliderStyle (juce::Slider::LinearBar);
    gateSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 24);
    gateSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    gateSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

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

    // Repaint the sheet when key, scale or snap change so the shading follows.
    for (auto* box : { &keyBox, &scaleBox, &lowOctBox, &octsBox, &barsBox, &resBox })
        box->onChange = [this] { canvas.repaint(); };

    snapButton.onStateChange = [this] { canvas.repaint(); };

    refreshToolButtons();
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (860, 540, 2400, 1500);
    setSize (1180, 720);
}

PlotterEditor::~PlotterEditor()
{
    setLookAndFeel (nullptr);

    if (dragFile.existsAsFile())
        dragFile.deleteFile();
}

void PlotterEditor::refreshToolButtons()
{
    const bool pencil = canvas.getTool() == PlotterCanvas::Tool::pencil;
    pencilButton.setToggleState (pencil, juce::dontSendNotification);
    eraserButton.setToggleState (! pencil, juce::dontSendNotification);
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
    g.fillAll (theme::paper);

    g.setColour (theme::ink);
    g.setFont (theme::mono (28.0f, true).withExtraKerningFactor (0.22f));
    g.drawText ("PLOTTER", 22, 16, 320, 34, juce::Justification::left);

    g.setFont (theme::mono (13.0f));
    g.setColour (theme::ink.withAlpha (0.75f));
    g.drawText ("draw a picture. the host transport plays it left to right.",
                300, 20, 620, 26, juce::Justification::left);

    g.setColour (theme::ink.withAlpha (0.35f));
    g.drawHorizontalLine (58, 22.0f, (float) getWidth() - 22.0f);

    g.setColour (theme::ink);
    g.drawRect (getLocalBounds().reduced (8), 2);
}

void PlotterEditor::resized()
{
    auto area = getLocalBounds().reduced (22, 18);
    area.removeFromTop (50);                      // masthead

    auto toolRow = area.removeFromTop (40);
    const auto slot = [&toolRow] (int w, int gap = 8)
    {
        auto r = toolRow.removeFromLeft (w);
        toolRow.removeFromLeft (gap);
        return r;
    };

    pencilButton.setBounds (slot (110, 0));
    eraserButton.setBounds (slot (110, 26));
    nibSlider   .setBounds (slot (150, 8));
    nibValue    .setBounds (slot (34, 26));
    undoButton  .setBounds (slot (96, 0));
    clearButton .setBounds (slot (96, 0));
    guidesButton.setBounds (slot (100));

    dragButton  .setBounds (toolRow.removeFromRight (150));
    toolRow.removeFromRight (10);
    exportButton.setBounds (toolRow.removeFromRight (170));

    area.removeFromTop (14);

    auto paramRow = area.removeFromTop (32);
    const auto pslot = [&paramRow] (int w, int gap = 10)
    {
        auto r = paramRow.removeFromLeft (w);
        paramRow.removeFromLeft (gap);
        return r;
    };

    keyBox        .setBounds (pslot (74));
    scaleBox      .setBounds (pslot (160));
    snapButton    .setBounds (pslot (150, 20));
    lowOctBox     .setBounds (pslot (74));
    octsBox       .setBounds (pslot (86));
    barsBox       .setBounds (pslot (96));
    resBox        .setBounds (pslot (82));
    waveBox       .setBounds (pslot (110, 20));
    hearButton    .setBounds (pslot (90));
    sendMidiButton.setBounds (pslot (110));
    freeRunButton .setBounds (pslot (190));

    gateSlider    .setBounds (paramRow.removeFromRight (76));

    area.removeFromTop (14);
    canvas.setBounds (area);
}

// ---------------------------------------------------------------------------

juce::File PlotterEditor::writeMidiToTempFile() const
{
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("plotter-drag.mid");

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
    const auto name = "plotter_" + noteNames()[rs.root].replace ("#", "s")
                    + "-" + juce::String (scales()[(size_t) rs.scaleIndex].name).removeCharacters (" .#-")
                    + ".mid";

    chooser = std::make_unique<juce::FileChooser> (
        "Export MIDI",
        juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name),
        "*.mid");

    const auto chooserFlags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    chooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& fc)
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
