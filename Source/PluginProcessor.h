#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Pattern.h"
#include "SynthVoice.h"

class PlotterProcessor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    PlotterProcessor();
    ~PlotterProcessor() override;

    // ---- AudioProcessor -------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Plotter"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- the drawing ----------------------------------------------------
    static constexpr int canvasWidth  = 1600;
    static constexpr int canvasHeight = 800;

    /** Message thread only. The editor draws straight into this. */
    juce::Image& getPicture() noexcept { return picture; }

    /** Call after every stroke, or any change to the drawing. Message thread only. */
    void pictureChanged();

    /** Current read settings, built from the parameters. */
    plotter::ReadSettings readSettings() const;

    /** Where the read head is, in steps, for the editor to draw. -1 when stopped. */
    float getPlayheadStep() const noexcept { return playheadStep.load (std::memory_order_relaxed); }

    /** A snapshot of the notes, for MIDI export from the editor. Message thread. */
    plotter::Pattern currentPattern() const { return lastAnalysed; }

    juce::AudioProcessorValueTreeState apvts;

private:
    void parameterChanged (const juce::String&, float) override;
    void handleAsyncUpdate() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void flushActiveNotes (juce::MidiBuffer& out, int sampleOffset);

    juce::Image picture { juce::Image::ARGB, canvasWidth, canvasHeight, true };
    plotter::PatternBank bank;
    plotter::Pattern lastAnalysed;
    std::atomic<bool> patternDirty { false };

    // transport
    struct ActiveNote { int midiNote; double endStepGlobal; };
    std::vector<ActiveNote> activeNotes;
    double expectedStartStep = -1.0e9;
    double freeRunStep = 0.0;
    bool wasRunning = false;

    juce::Synthesiser synth;
    std::atomic<float> playheadStep { -1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlotterProcessor)
};
