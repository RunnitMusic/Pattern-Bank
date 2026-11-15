#include "PluginProcessor.h"

#include <cmath>

namespace
{
class ProbeEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit ProbeEditor (VST3OutputProbeProcessor& processorIn)
        : AudioProcessorEditor (processorIn), processor (processorIn)
    {
        setSize (440, 210);
        startTimerHz (30);
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.fillAll (juce::Colour::fromRGB (31, 39, 50));

        graphics.setColour (juce::Colour::fromRGB (225, 230, 236));
        graphics.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        graphics.drawText ("VST3 Output Parameter Probe", 24, 20, 392, 30,
                           juce::Justification::centredLeft);

        graphics.setFont (juce::FontOptions (14.0f));
        graphics.setColour (juce::Colour::fromRGB (176, 187, 201));
        graphics.drawFittedText (
            "The moving value is emitted from process() as a read-only VST3 output "
            "parameter. Check whether FL exposes it as a controller source or Patcher output.",
            24, 58, 392, 52, juce::Justification::topLeft, 3);

        const auto bar = juce::Rectangle<float> (24.0f, 126.0f, 392.0f, 24.0f);
        graphics.setColour (juce::Colour::fromRGB (19, 25, 33));
        graphics.fillRoundedRectangle (bar, 3.0f);
        graphics.setColour (juce::Colour::fromRGB (255, 166, 24));
        graphics.fillRoundedRectangle (bar.withWidth (bar.getWidth() * value), 3.0f);

        graphics.setColour (juce::Colour::fromRGB (225, 230, 236));
        graphics.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        graphics.drawText (juce::String (value, 3), 24, 160, 392, 26,
                           juce::Justification::centred);
    }

private:
    void timerCallback() override
    {
        const auto next = processor.getDisplayedValue();
        if (std::abs (next - value) > 0.0001f)
        {
            value = next;
            repaint();
        }
    }

    VST3OutputProbeProcessor& processor;
    float value = 0.0f;
};
}

VST3OutputProbeProcessor::VST3OutputProbeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    auto parameter = std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "probe_output", 1 },
        "Probe Output",
        juce::NormalisableRange<float> { 0.0f, 1.0f },
        0.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("normalized")
            .withCategory (juce::AudioProcessorParameter::outputMeter)
            .withAutomatable (false));

    outputParameter = parameter.get();
    addParameter (parameter.release());
}

void VST3OutputProbeProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    phase = 0.0;
}

bool VST3OutputProbeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet()
        && (layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
            || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
}

template <typename Sample>
void VST3OutputProbeProcessor::process (juce::AudioBuffer<Sample>& buffer)
{
    // A half-Hz triangle wave makes routing failures easy to distinguish from a static value.
    phase += 0.5 * static_cast<double> (buffer.getNumSamples()) / currentSampleRate;
    phase -= std::floor (phase);

    const auto triangle = static_cast<float> (1.0 - std::abs (2.0 * phase - 1.0));
    displayedValue.store (triangle);

    // outputMeter maps to VST3's kIsReadOnly flag. JUCE collects this parameter change
    // during process() and writes it to ProcessData::outputParameterChanges.
    outputParameter->setValueNotifyingHost (triangle);
}

void VST3OutputProbeProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&)
{
    process (buffer);
}

void VST3OutputProbeProcessor::processBlock (juce::AudioBuffer<double>& buffer,
                                              juce::MidiBuffer&)
{
    process (buffer);
}

juce::AudioProcessorEditor* VST3OutputProbeProcessor::createEditor()
{
    return new ProbeEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VST3OutputProbeProcessor();
}

