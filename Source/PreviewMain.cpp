#include "StepShaperEditor.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace stepshaper
{
class PreviewWindow final : public juce::DocumentWindow, private juce::Timer
{
public:
    PreviewWindow() : juce::DocumentWindow ("Pattern Bank Preview", juce::Colour (0xff090b10),
                                             juce::DocumentWindow::closeButton), engine (model)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        editor = std::make_unique<StepShaperEditor> (model, &engine);
        setContentOwned (editor.release(), true);
        centreWithSize (840, 440);
        setVisible (true);
        startTimerHz (120);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
    void timerCallback() override
    {
        context.playing = true;
        context.samplesPerTick = 256;
        context.songBeat += static_cast<double> (context.samplesPerTick) / context.sampleRate * context.tempoBpm / 60.0;
        engine.tick (context);
    }

    Model model;
    Engine engine;
    TickContext context;
    std::unique_ptr<StepShaperEditor> editor;
};

class PreviewApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Pattern Bank Preview"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.trim().isNotEmpty())
        {
            auto screenshotArgument = commandLine.trim();
            const auto renderSynced = screenshotArgument.containsIgnoreCase ("-sync.png");
            Model model;
            if (! renderSynced)
                model.mutate ([] (ProjectState& state) { state.lanes[state.selectedLane].sync = false; });
            Engine engine (model);
            StepShaperEditor editor (model, &engine);
            editor.setSize (840, 440);
            juce::Image image (juce::Image::RGB, editor.getWidth(), editor.getHeight(), true);
            juce::Graphics graphics (image);
            editor.paintEntireComponent (graphics, true);
            if (auto stream = juce::File (screenshotArgument.unquoted()).createOutputStream())
                juce::PNGImageFormat().writeImageToStream (image, *stream);
            quit();
            return;
        }
        window = std::make_unique<PreviewWindow>();
    }
    void shutdown() override { window.reset(); }
private:
    std::unique_ptr<PreviewWindow> window;
};
} // namespace stepshaper

START_JUCE_APPLICATION (stepshaper::PreviewApplication)
