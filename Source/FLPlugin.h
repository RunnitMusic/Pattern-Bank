#pragma once

#include "Core.h"
#include "StepShaperEditor.h"
#include "fp_plugclass.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_set>

namespace stepshaper
{
class FLPlugin final : public TFruityPlug
{
public:
    FLPlugin (TFruityPlugHost* hostToUse, TPluginTag tagToUse);
    ~FLPlugin() override;

    intptr_t _stdcall Dispatcher (intptr_t ID, intptr_t Index, intptr_t Value) override;
    void _stdcall Idle_Public() override;
    void _stdcall SaveRestoreState (IStream* stream, BOOL save) override;
    void _stdcall GetName (int section, int index, int value, char* name) override;
    int _stdcall ProcessEvent (int eventID, int eventValue, int flags) override;
    int _stdcall ProcessParam (int index, intptr_t value, int recFlags) override;
    void _stdcall Eff_Render (PWAV32FS source, PWAV32FS destination, int length) override;
    void _stdcall Gen_Render (PWAV32FS destination, int& length) override;
    TVoiceHandle _stdcall TriggerVoice (PVoiceParams voiceParams, intptr_t setTag) override;
    void _stdcall Voice_Release (TVoiceHandle handle) override;
    void _stdcall Voice_Kill (TVoiceHandle handle) override;
    int _stdcall Voice_ProcessEvent (TVoiceHandle handle, int eventID, int eventValue, int flags) override;
    int _stdcall Voice_Render (TVoiceHandle handle, PWAV32FS destination, int& length) override;
    void _stdcall NewTick() override;
    void _stdcall MIDITick() override;
    void _stdcall MIDIIn (int& message) override;
    void _stdcall MsgIn (intptr_t message) override;
    int _stdcall OutputVoice_ProcessEvent (TOutVoiceHandle handle, int eventID, int eventValue, int flags) override;
    void _stdcall OutputVoice_Kill (TVoiceHandle handle) override;

    static PFruityPlugInfo pluginInfo() noexcept;

private:
    void showEditor (HWND parent);
    void hideEditor();
    void editorParameterChanged (int lane, Parameter parameter);
    void editorRetriggerChanged (int lane, bool isDown);
    void showParameterMenu (int lane, Parameter parameter, juce::Point<int> screenPosition);
    void handleMidiMessage (intptr_t message);
    void releaseVoice (TVoiceHandle handle);
    static Parameter parameterFromIndex (int index) noexcept;
    static int laneFromParameterIndex (int index) noexcept;
    static void copyName (char* destination, const std::string& source) noexcept;

    TFruityPlugHost* host = nullptr;
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    Model model;
    Engine engine;
    std::unique_ptr<StepShaperEditor> editor;
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<double> tempo { 120.0 };
    std::atomic<int> samplesPerTick { 64 };
    std::atomic<bool> playing { false };
    std::array<std::uint32_t, maxLanes> lastOutputs {};
    std::atomic<bool> hostedInPatcher { false };
    bool sentInitialOutputs = false;
    bool sentUsingPatcherEncoding = false;
    int modelListenerToken = 0;
    std::mutex midiMutex;
    std::unordered_set<TVoiceHandle> activeVoices;
    std::array<bool, 16 * 128> activeMidiNotes {};
    std::array<std::atomic<bool>, maxLanes> retriggerParameterDown {};
    TVoiceHandle nextVoiceHandle = 1;
    bool usesVoiceCallbacks = false;
};
} // namespace stepshaper
