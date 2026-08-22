#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace plotter
{
struct PlotterSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override    { return true; }
    bool appliesToChannel (int) override { return true; }
};

/** One oscillator, one-pole lowpass, ADSR. Deliberately plain — it is a monitor
    voice, the point of the plugin is the MIDI it sends out. */
class PlotterVoice : public juce::SynthesiserVoice
{
public:
    enum Waveform { saw = 0, square, triangle, sine };

    void setWaveform (int w) noexcept { waveform = w; }

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<PlotterSound*> (s) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override
    {
        phase = 0.0;
        level = velocity;
        increment = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber) / getSampleRate();
        cutoff = (float) std::exp (-2.0 * juce::MathConstants<double>::pi
                                   * juce::jmin (0.45, increment * 7.0));
        lastOut = 0.0f;

        adsr.setSampleRate (getSampleRate());
        adsr.setParameters ({ 0.005f, 0.12f, 0.75f, 0.08f });
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            adsr.noteOff();
        }
        else
        {
            adsr.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (! adsr.isActive())
            return;

        while (--numSamples >= 0)
        {
            const float raw = oscillator();
            lastOut = raw + cutoff * (lastOut - raw);          // gentle one-pole lowpass
            const float s = lastOut * level * 0.25f * adsr.getNextSample();

            for (int ch = out.getNumChannels(); --ch >= 0;)
                out.addSample (ch, startSample, s);

            ++startSample;

            phase += increment;
            if (phase >= 1.0)
                phase -= 1.0;

            if (! adsr.isActive())
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    float oscillator() const noexcept
    {
        const auto p = (float) phase;
        switch (waveform)
        {
            case square:   return p < 0.5f ? 1.0f : -1.0f;
            case triangle: return 4.0f * std::abs (p - 0.5f) - 1.0f;
            case sine:     return std::sin (p * juce::MathConstants<float>::twoPi);
            case saw:
            default:       return 2.0f * p - 1.0f;
        }
    }

    juce::ADSR adsr;
    double phase = 0.0, increment = 0.0;
    float level = 0.0f, cutoff = 0.5f, lastOut = 0.0f;
    int waveform = saw;
};
} // namespace plotter
