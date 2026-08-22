#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PlotterCanvas.h"
#include "PlotterLookAndFeel.h"

class PlotterEditor : public juce::AudioProcessorEditor,
                      public juce::DragAndDropContainer
{
public:
    explicit PlotterEditor (PlotterProcessor&);
    ~PlotterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void exportMidi();
    juce::File writeMidiToTempFile() const;
    void refreshToolButtons();

    plotter::PlotterLookAndFeel lookAndFeel;
    PlotterProcessor& processor;
    PlotterCanvas canvas;

    juce::TextButton pencilButton { "Pencil" }, eraserButton { "Eraser" };
    juce::TextButton undoButton { "Undo" }, clearButton { "Clear" };
    juce::TextButton guidesButton { "Guides" }, exportButton { "Export MIDI" };
    juce::TextButton dragButton { "Drag MIDI" };
    juce::Slider nibSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label nibValue;

    juce::ComboBox keyBox, scaleBox, barsBox, resBox, lowOctBox, octsBox, waveBox;
    juce::ToggleButton snapButton { "Snap" }, sendMidiButton { "MIDI out" },
                       hearButton { "Voice" }, freeRunButton { "Run when stopped" };
    juce::Slider gateSlider;

    std::vector<std::unique_ptr<APVTS::ComboBoxAttachment>> comboAttachments;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<APVTS::SliderAttachment> gateAttachment;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File dragFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlotterEditor)
};
