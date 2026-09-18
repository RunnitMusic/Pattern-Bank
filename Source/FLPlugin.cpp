#include "FLPlugin.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace stepshaper
{
namespace
{
char longName[] = "Pattern Bank";
char shortName[] = "PatternBank";

TFruityPlugInfo info = []
{
    TFruityPlugInfo result {};
    result.SDKVersion = CurrentSDKVersion;
    result.LongName = longName;
    result.ShortName = shortName;
    result.Flags = FPF_Type_Visual | FPF_GetNoteInput | FPF_WantNewTick | FPF_CantSmartDisable;
    result.NumParams = parameterCount;
    result.DefPoly = 0;
    result.NumOutCtrls = maxLanes;
    result.NumOutVoices = 0;
    return result;
}();

float floatFromBits (intptr_t value) noexcept
{
    return std::bit_cast<float> (static_cast<std::uint32_t> (value));
}

bool isNoteOnMessage (intptr_t message) noexcept
{
    const auto packed = static_cast<std::uint32_t> (message);
    const auto status = packed & 0xffu;
    const auto velocity = (packed >> 16u) & 0xffu;
    return (status & 0xf0u) == 0x90u && velocity != 0u;
}

bool isNoteOffMessage (intptr_t message) noexcept
{
    const auto packed = static_cast<std::uint32_t> (message);
    const auto status = packed & 0xffu;
    const auto velocity = (packed >> 16u) & 0xffu;
    return (status & 0xf0u) == 0x80u || ((status & 0xf0u) == 0x90u && velocity == 0u);
}

bool streamCallSucceeded (HRESULT result) noexcept
{
    return result >= 0;
}

std::string parameterName (int lane, Parameter parameter)
{
    const auto prefix = "LFO " + std::to_string (lane + 1) + " ";
    switch (parameter)
    {
        case Parameter::pattern: return prefix + "Pattern";
        case Parameter::speed: return prefix + "Free Rate";
        case Parameter::sync: return prefix + "Sync";
        case Parameter::division: return prefix + "Sync Length";
        case Parameter::timingFeel: return prefix + "Timing";
        case Parameter::changeMode: return prefix + "Trig Sync";
        case Parameter::enabled: return prefix + "Enabled";
        case Parameter::startPosition: return prefix + "Loop Start Position";
        case Parameter::midiTrigger: return prefix + "MIDI Trigger";
        case Parameter::baseValue: return prefix + "Base Value";
        case Parameter::patternMix: return prefix + "Amount";
        case Parameter::sustainPosition: return prefix + "Sustain Position";
        case Parameter::mod1: return prefix + "Mod 1";
        case Parameter::mod2: return prefix + "Mod 2";
        case Parameter::positionSync: return prefix + "Position Sync";
        case Parameter::retrigger: return prefix + "Retrigger";
    }
    return prefix;
}
} // namespace

PFruityPlugInfo FLPlugin::pluginInfo() noexcept { return &info; }

FLPlugin::FLPlugin (TFruityPlugHost* hostToUse, TPluginTag tagToUse)
    : host (hostToUse), engine (model)
{
    HostTag = tagToUse;
    Info = pluginInfo();
    EditorHandle = 0;
    lastOutputs.fill (std::numeric_limits<std::uint32_t>::max());
    modelListenerToken = model.addListener ([this]
    {
        if (host != nullptr)
            host->Dispatcher (HostTag, FHD_SetDirty, 0, 1);
    });
    if (host != nullptr)
    {
        host->Dispatcher (HostTag, FHD_SetNumPresets, 0, static_cast<int> (FactoryBank::count));
    }
}

FLPlugin::~FLPlugin()
{
    hideEditor();
    model.removeListener (modelListenerToken);
}

intptr_t _stdcall FLPlugin::Dispatcher (intptr_t ID, intptr_t Index, intptr_t Value)
{
    switch (ID)
    {
        case FPD_ShowEditor:
            if (Value != 0)
            {
#if defined(_WIN32)
                showEditor (reinterpret_cast<HWND> (Value));
#else
                showEditor (static_cast<HWND> (Value));
#endif
            }
            else hideEditor();
#if defined(_WIN32)
            return reinterpret_cast<intptr_t> (EditorHandle);
#else
            return static_cast<intptr_t> (EditorHandle);
#endif
        case FPD_SetSampleRate:
            sampleRate.store (std::max<intptr_t> (1, Value), std::memory_order_relaxed);
            return 0;
        case FPD_SetPlaying:
            playing.store (Value != 0, std::memory_order_relaxed);
            return 0;
        case FPD_SetSamplesPerTick:
            samplesPerTick.store (std::max (1, static_cast<int> (std::lround (floatFromBits (Value)))), std::memory_order_relaxed);
            return 0;
        case FPD_MIDIIn:
            handleMidiMessage (Value);
            return 0;
        case FPD_SetPreset:
        {
            const auto state = model.snapshot();
            const auto lane = state->selectedLane;
            const auto preset = static_cast<FactoryBank> (std::clamp (static_cast<int> (Index), 0,
                                                                      static_cast<int> (FactoryBank::count) - 1));
            model.loadFactoryBank (lane, preset);
            return 0;
        }
        case FPD_GetParamInfo:
        {
            const auto parameter = parameterFromIndex (static_cast<int> (Index));
            return parameter == Parameter::speed || parameter == Parameter::baseValue
                || parameter == Parameter::patternMix ? 0 : PI_CantInterpolate;
        }
        case FPD_PreferredNumIO:
            // The SDK documents this query as Patcher-specific. Its controller
            // ports use a different fixed-point scale from normal Mixer-slot
            // "Link to controller" connections.
            hostedInPatcher.store (true, std::memory_order_release);
            return -1;
        default:
            return 0;
    }
}

void _stdcall FLPlugin::Idle_Public()
{
    model.synchroniseRealtimeParameters();
}

void _stdcall FLPlugin::SaveRestoreState (IStream* stream, BOOL save)
{
    if (stream == nullptr) return;
    if (save)
    {
        model.synchroniseRealtimeParameters();
        const auto bytes = model.serialize();
        const auto size = static_cast<std::uint32_t> (bytes.size());
        ULONG written = 0;
        if (streamCallSucceeded (stream->Write (&size, sizeof (size), &written)) && written == sizeof (size) && size > 0)
            stream->Write (bytes.data(), size, &written);
        return;
    }

    std::uint32_t size = 0;
    ULONG read = 0;
    if (! streamCallSucceeded (stream->Read (&size, sizeof (size), &read)) || read != sizeof (size)
        || size == 0 || size > 4 * 1024 * 1024)
        return;
    std::vector<std::uint8_t> bytes (size);
    if (streamCallSucceeded (stream->Read (bytes.data(), size, &read)) && read == size)
        model.deserialize (bytes.data(), bytes.size());
}

void FLPlugin::copyName (char* destination, const std::string& source) noexcept
{
    if (destination == nullptr) return;
#if defined(_MSC_VER)
    strncpy_s (destination, 256, source.c_str(), _TRUNCATE);
#else
    std::strncpy (destination, source.c_str(), 255);
    destination[255] = 0;
#endif
}

void _stdcall FLPlugin::GetName (int section, int index, int value, char* name)
{
    if (name == nullptr) return;
    if (section == FPN_OutCtrl)
    {
        copyName (name, index >= 0 && index < maxLanes ? "LFO " + std::to_string (index + 1) : "LFO");
        return;
    }
    if (section == FPN_Preset)
    {
        const auto state = model.snapshot();
        (void) state;
        copyName (name, factoryBankName (static_cast<FactoryBank> (std::clamp (index, 0,
                                                   static_cast<int> (FactoryBank::count) - 1))));
        return;
    }
    if (index < 0 || index >= parameterCount) { copyName (name, ""); return; }
    const auto laneIndex = laneFromParameterIndex (index);
    const auto parameter = parameterFromIndex (index);
    if (section == FPN_Param)
    {
        copyName (name, parameterName (laneIndex, parameter));
        return;
    }
    if (section != FPN_ParamValue) { copyName (name, ""); return; }

    auto lane = model.snapshot()->lanes[laneIndex];
    setParameterFromHost (lane, parameter, decodeModernFLParameterValue (value));
    char buffer[64] {};
    switch (parameter)
    {
        case Parameter::pattern:
        {
            const auto& bank = patternBank (lane);
            std::snprintf (buffer, sizeof (buffer), "%02d %s", lane.selectedPattern + 1,
                           bank.names[lane.selectedPattern].c_str());
            break;
        }
        case Parameter::speed: std::snprintf (buffer, sizeof (buffer), "%.3f Hz", lane.speedHz); break;
        case Parameter::sync: std::snprintf (buffer, sizeof (buffer), "%s", lane.sync ? "On" : "Off"); break;
        case Parameter::division: std::snprintf (buffer, sizeof (buffer), "%s", divisionName (lane.division)); break;
        case Parameter::timingFeel: std::snprintf (buffer, sizeof (buffer), "%s", timingFeelName (lane.timingFeel)); break;
        case Parameter::changeMode: std::snprintf (buffer, sizeof (buffer), "%s", changeModeName (lane.changeMode)); break;
        case Parameter::enabled: std::snprintf (buffer, sizeof (buffer), "%s", lane.enabled ? "On" : "Off"); break;
        case Parameter::startPosition: std::snprintf (buffer, sizeof (buffer), "%.1f%%", lane.startPosition * 100.0f); break;
        case Parameter::midiTrigger: std::snprintf (buffer, sizeof (buffer), "%s", lane.midiTrigger ? "On" : "Off"); break;
        case Parameter::baseValue: std::snprintf (buffer, sizeof (buffer), "%.1f%%", lane.baseValue * 100.0f); break;
        case Parameter::patternMix: std::snprintf (buffer, sizeof (buffer), "%+.1f%%", lane.patternMix * 100.0f); break;
        case Parameter::sustainPosition: std::snprintf (buffer, sizeof (buffer), "%.1f%%", lane.sustainPosition * 100.0f); break;
        case Parameter::mod1: std::snprintf (buffer, sizeof (buffer), "%.1f%%", lane.mod1 * 100.0f); break;
        case Parameter::mod2: std::snprintf (buffer, sizeof (buffer), "%.1f%%", lane.mod2 * 100.0f); break;
        case Parameter::positionSync: std::snprintf (buffer, sizeof (buffer), "%s", lane.positionSync ? "On" : "Off"); break;
        case Parameter::retrigger: std::snprintf (buffer, sizeof (buffer), "%s",
                                                  decodeModernFLParameterValue (value) >= 32768
                                                      ? "Pressed" : "Released"); break;
    }
    copyName (name, buffer);
}

int _stdcall FLPlugin::ProcessEvent (int eventID, int eventValue, int flags)
{
    if (eventID == FPE_Tempo)
    {
        tempo.store (std::max (1.0f, floatFromBits (eventValue)), std::memory_order_relaxed);
        if (flags > 0) samplesPerTick.store (flags, std::memory_order_relaxed);
    }
    return 0;
}

Parameter FLPlugin::parameterFromIndex (int index) noexcept
{
    if (index >= mod2ParameterOffset) return Parameter::mod2;
    if (index >= mod1ParameterOffset) return Parameter::mod1;
    if (index >= patternMixParameterOffset) return Parameter::patternMix;
    if (index >= baseValueParameterOffset) return Parameter::baseValue;
    if (index >= sustainPositionParameterOffset) return Parameter::sustainPosition;
    if (index >= startPositionParameterOffset) return Parameter::startPosition;
    if (index >= retriggerParameterOffset) return Parameter::retrigger;
    if (index >= midiTriggerParameterOffset) return Parameter::midiTrigger;
    if (index >= positionSyncParameterOffset) return Parameter::positionSync;
    if (index >= changeModeParameterOffset) return Parameter::changeMode;
    if (index >= timingFeelParameterOffset) return Parameter::timingFeel;
    if (index >= divisionParameterOffset) return Parameter::division;
    if (index >= syncParameterOffset) return Parameter::sync;
    if (index >= speedParameterOffset) return Parameter::speed;
    if (index >= patternParameterOffset) return Parameter::pattern;
    return Parameter::enabled;
}

int FLPlugin::laneFromParameterIndex (int index) noexcept
{
    return std::clamp (index % maxLanes, 0, maxLanes - 1);
}

int _stdcall FLPlugin::ProcessParam (int index, intptr_t value, int recFlags)
{
    if (index < 0 || index >= parameterCount) return 0;
    const auto lane = laneFromParameterIndex (index);
    const auto parameter = parameterFromIndex (index);
    const auto fromLegacyController = (recFlags & (REC_FromMIDI | REC_InternalCtrl)) != 0;
    if (parameter == Parameter::retrigger)
    {
        if ((recFlags & REC_UpdateValue) != 0)
        {
            const auto isDown = decodeModernFLParameterValue (value) >= 32768;
            const auto wasDown = retriggerParameterDown[lane].exchange (isDown, std::memory_order_acq_rel);
            if (isDown != wasDown)
            {
                if (isDown) engine.requestManualTrigger (lane);
                else engine.requestManualRelease (lane);
            }
        }
        return encodeFLParameterReturnValue (
            retriggerParameterDown[lane].load (std::memory_order_relaxed) ? 65536 : 0,
            fromLegacyController);
    }
    // REC_FromMIDI describes the value's origin/range; it is not a write request by itself.
    // Patcher can set it on queries, so mutating without REC_UpdateValue can zero our state.
    if ((recFlags & REC_UpdateValue) != 0)
        model.setParameterRealtime (lane, parameter, decodeModernFLParameterValue (value));
    return encodeFLParameterReturnValue (model.realtimeParameterValue (lane, parameter),
                                         fromLegacyController);
}

void _stdcall FLPlugin::NewTick()
{
    // Voice_Release is called while FL owns its voice lock. Defer the host call
    // until this mixer-thread callback, where FL controller/voice APIs are safe.
    retireReleasedHostVoices();

    TFPTime time {};
    TFPTime runningTime {};
    if (host != nullptr)
    {
        host->Dispatcher (HostTag, FHD_GetMixingTime, 0, reinterpret_cast<intptr_t> (&time));
        host->Dispatcher (HostTag, FHD_GetMixingTime, 3, reinterpret_cast<intptr_t> (&runningTime));
    }
    TickContext context;
    context.songBeat = time.t;
    context.runningMilliseconds = runningTime.t;
    context.tempoBpm = tempo.load (std::memory_order_relaxed);
    context.sampleRate = sampleRate.load (std::memory_order_relaxed);
    context.samplesPerTick = samplesPerTick.load (std::memory_order_relaxed);
    context.playing = playing.load (std::memory_order_relaxed);
    engine.tick (context);

    if (host == nullptr) return;
    const auto usePatcherEncoding = hostedInPatcher.load (std::memory_order_acquire);
    const auto encodingChanged = sentInitialOutputs && usePatcherEncoding != sentUsingPatcherEncoding;
    for (int lane = 0; lane < maxLanes; ++lane)
    {
        const auto output = static_cast<std::uint32_t> (std::lround (std::clamp (engine.output (lane), 0.0f, 1.0f) * 65536.0f));
        // Patcher only needs a notification when the 16-bit controller value has
        // actually changed. Re-sending every enabled lane on every mixer tick adds
        // avoidable host/UI work, particularly with several plug-in instances.
        if (! sentInitialOutputs || encodingChanged || output != lastOutputs[lane])
        {
            lastOutputs[lane] = output;
            host->OnControllerChanged (HostTag, lane,
                                       encodeFLControllerValue (engine.output (lane), usePatcherEncoding));
        }
    }
    sentInitialOutputs = true;
    sentUsingPatcherEncoding = usePatcherEncoding;
}

void FLPlugin::showEditor (HWND parent)
{
    if (editor != nullptr) { editor->setVisible (true); return; }
    editor = std::make_unique<StepShaperEditor> (model, &engine,
        [this] (int lane, Parameter parameter) { editorParameterChanged (lane, parameter); },
        [this] (const std::string& hint)
        {
            if (host == nullptr) return;
            std::array<char, 256> hintText {};
            copyName (hintText.data(), hint);
            host->OnHint (HostTag, hintText.data());
        },
        [this] (int lane, bool isDown) { editorRetriggerChanged (lane, isDown); });
    editor->setParameterMenuCallback ([this] (int lane, Parameter parameter, juce::Point<int> position)
    {
        showParameterMenu (lane, parameter, position);
    });
    editor->addToDesktop (0,
#if defined(_WIN32)
                          parent
#else
                          reinterpret_cast<void*> (parent)
#endif
    );
    editor->setBounds (0, 0, 840, 440);
    editor->setVisible (true);
    if (editor->getPeer() != nullptr)
    {
#if defined(_WIN32)
        EditorHandle = static_cast<HWND> (editor->getPeer()->getNativeHandle());
#else
        EditorHandle = reinterpret_cast<HWND> (editor->getPeer()->getNativeHandle());
#endif
    }
    else EditorHandle = 0;
    if (host != nullptr)
    {
        host->Dispatcher (HostTag, FHD_WantIdle, 0, 1);
        host->Dispatcher (HostTag, FHD_EditorResized, 0, 0);
    }
}

void FLPlugin::showParameterMenu (int lane, Parameter parameter, juce::Point<int> screenPosition)
{
    if (host == nullptr) return;
    const auto index = parameterIndex (lane, parameter);
    juce::PopupMenu menu;
    std::vector<int> hostItems;
    for (int item = 0; item < 256; ++item)
    {
        auto* entry = reinterpret_cast<PParamMenuEntry> (
            host->Dispatcher (HostTag, FHD_GetParamMenuEntry, index, item));
        if (entry == nullptr) break;
        const auto name = entry->Name != nullptr ? juce::String::fromUTF8 (entry->Name) : juce::String();
        if (name == "-") menu.addSeparator();
        else
        {
            menu.addItem (item + 1, name, (entry->Flags & FHP_Disabled) == 0,
                          (entry->Flags & FHP_Checked) != 0);
            hostItems.push_back (item);
        }
    }
    if (hostItems.empty()) return;
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                            { screenPosition.x, screenPosition.y, 1, 1 }),
                        [this, index] (int result)
                        {
                            if (result > 0 && host != nullptr)
                                host->Dispatcher (HostTag, FHD_ParamMenu, index, result - 1);
                        });
}

void FLPlugin::hideEditor()
{
    if (editor == nullptr) return;
    editor->setVisible (false);
    editor.reset();
    EditorHandle = 0;
    if (host != nullptr) host->Dispatcher (HostTag, FHD_WantIdle, 0, 0);
}

void FLPlugin::editorParameterChanged (int lane, Parameter parameter)
{
    if (host == nullptr) return;
    const auto value = static_cast<intptr_t> (model.realtimeParameterValue (lane, parameter)) * modernFLValueScale;
    host->OnParamChanged (HostTag, parameterIndex (lane, parameter), static_cast<int> (value));
    if (parameter == Parameter::speed || parameter == Parameter::division
        || parameter == Parameter::baseValue || parameter == Parameter::patternMix)
    {
        char valueName[256] {};
        GetName (FPN_ParamValue, parameterIndex (lane, parameter), static_cast<int> (value), valueName);
        const auto hint = parameterName (lane, parameter) + ": " + valueName;
        std::array<char, 256> hintText {};
        copyName (hintText.data(), hint);
        host->OnHint (HostTag, hintText.data());
    }
    if (parameter == Parameter::pattern)
        host->Dispatcher (HostTag, FHD_NamesChanged, 0, FPN_Preset);
}

void FLPlugin::editorRetriggerChanged (int lane, bool isDown)
{
    lane = std::clamp (lane, 0, maxLanes - 1);
    retriggerParameterDown[lane].store (isDown, std::memory_order_release);
    if (host != nullptr)
        host->OnParamChanged (HostTag, parameterIndex (lane, Parameter::retrigger),
                              isDown ? static_cast<int> (65536LL * modernFLValueScale) : 0);
}

void _stdcall FLPlugin::Eff_Render (PWAV32FS, PWAV32FS, int) {}
void _stdcall FLPlugin::Gen_Render (PWAV32FS, int& length) { length = 0; }
TVoiceHandle _stdcall FLPlugin::TriggerVoice (PVoiceParams, intptr_t setTag)
{
    std::lock_guard lock (midiMutex);
    // FL may report the same note through both raw MIDI and voice callbacks.
    // Once voices are available they are authoritative because their handles
    // provide the only reliable pairing between note-on and note-off.
    usesVoiceCallbacks = true;
    activeMidiNotes.fill (false);
    const auto overlapping = ! activeVoices.empty();
    const auto handle = nextVoiceHandle++;
    if (nextVoiceHandle == FVH_Null || nextVoiceHandle == 0) nextVoiceHandle = 1;
    activeVoices.emplace (handle, setTag);
    engine.requestMidiTrigger (overlapping);
    return handle;
}
void _stdcall FLPlugin::Voice_Release (TVoiceHandle handle)
{
    // FL calls voice functions while holding its plug-in lock. Calling back to
    // host->Voice_Kill here can deadlock when a Patcher wire or this instance is
    // being removed, so queue the host-side retirement for NewTick.
    releaseVoice (handle, true);
}
void _stdcall FLPlugin::Voice_Kill (TVoiceHandle handle) { releaseVoice (handle, false); }
int _stdcall FLPlugin::Voice_ProcessEvent (TVoiceHandle, int, int, int) { return 0; }
int _stdcall FLPlugin::Voice_Render (TVoiceHandle, PWAV32FS, int& length) { length = 0; return FVR_Ok; }
void _stdcall FLPlugin::MIDITick() {}
void _stdcall FLPlugin::MIDIIn (int& message)
{
    handleMidiMessage (message);
}

void FLPlugin::handleMidiMessage (intptr_t message)
{
    if (! isNoteOnMessage (message) && ! isNoteOffMessage (message)) return;
    const auto packed = static_cast<std::uint32_t> (message);
    const auto channel = static_cast<int> (packed & 0x0fu);
    const auto note = static_cast<int> ((packed >> 8u) & 0x7fu);
    const auto index = static_cast<std::size_t> (channel * 128 + note);
    std::lock_guard lock (midiMutex);
    if (usesVoiceCallbacks) return;
    if (isNoteOnMessage (message))
    {
        if (activeMidiNotes[index]) return; // FPD_MIDIIn and MIDIIn may report the same event.
        const auto overlapping = ! activeVoices.empty()
            || std::any_of (activeMidiNotes.begin(), activeMidiNotes.end(), [] (bool held) { return held; });
        activeMidiNotes[index] = true;
        engine.requestMidiTrigger (overlapping);
        return;
    }
    if (! activeMidiNotes[index]) return;
    activeMidiNotes[index] = false;
    const auto anyRawNotes = std::any_of (activeMidiNotes.begin(), activeMidiNotes.end(), [] (bool held) { return held; });
    if (! anyRawNotes) engine.requestMidiRelease();
}

void FLPlugin::releaseVoice (TVoiceHandle handle, bool queueHostRetirement)
{
    std::lock_guard lock (midiMutex);
    if (! queueHostRetirement)
        pendingHostVoiceKills.erase (handle); // FL is already destroying it during teardown/polyphony limiting.
    const auto active = activeVoices.find (handle);
    if (active == activeVoices.end()) return; // Release and Kill can both arrive for one voice.
    if (queueHostRetirement)
        pendingHostVoiceKills.insert_or_assign (handle, active->second);
    activeVoices.erase (active);
    if (activeVoices.empty()) engine.requestMidiRelease();
}

void FLPlugin::retireReleasedHostVoices()
{
    std::vector<intptr_t> hostVoiceTags;
    {
        std::lock_guard lock (midiMutex);
        hostVoiceTags.reserve (pendingHostVoiceKills.size());
        for (const auto& [handle, hostVoiceTag] : pendingHostVoiceKills)
        {
            static_cast<void> (handle);
            hostVoiceTags.push_back (hostVoiceTag);
        }
        pendingHostVoiceKills.clear();
    }

    // Sender is FL's SetTag from TriggerVoice, not Pattern Bank's returned
    // TVoiceHandle. KillHandle=false says our local handle is already gone.
    if (host != nullptr)
        for (const auto hostVoiceTag : hostVoiceTags)
            host->Voice_Kill (hostVoiceTag, false);
}
void _stdcall FLPlugin::MsgIn (intptr_t) {}
int _stdcall FLPlugin::OutputVoice_ProcessEvent (TOutVoiceHandle, int, int, int) { return 0; }
void _stdcall FLPlugin::OutputVoice_Kill (TVoiceHandle) {}
} // namespace stepshaper
