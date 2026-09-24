#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace stepshaper
{
constexpr int maxLanes = 8;
constexpr int patternsPerLane = 128;
constexpr int maxPoints = 64;
constexpr int parameterKinds = 15;
// Host parameters are deliberately type-major: every LFO's Enabled parameter,
// then every Pattern parameter, and so on. Keep this order stable after beta.
constexpr int enabledParameterOffset = 0 * maxLanes;
constexpr int patternParameterOffset = 1 * maxLanes;
constexpr int speedParameterOffset = 2 * maxLanes;
constexpr int syncParameterOffset = 3 * maxLanes;
constexpr int divisionParameterOffset = 4 * maxLanes;
constexpr int timingFeelParameterOffset = 5 * maxLanes;
constexpr int changeModeParameterOffset = 6 * maxLanes;
constexpr int positionSyncParameterOffset = 7 * maxLanes;
constexpr int midiTriggerParameterOffset = 8 * maxLanes;
constexpr int retriggerParameterOffset = 9 * maxLanes;
constexpr int startPositionParameterOffset = 10 * maxLanes;
constexpr int sustainPositionParameterOffset = 11 * maxLanes;
constexpr int baseValueParameterOffset = 12 * maxLanes;
constexpr int patternMixParameterOffset = 13 * maxLanes;
constexpr int mod1ParameterOffset = 14 * maxLanes;
constexpr int mod2ParameterOffset = 15 * maxLanes;
constexpr int parameterCount = 16 * maxLanes;
constexpr std::int64_t modernFLValueScale = 16384LL; // parameter automation: 65536 legacy steps to FL's 30-bit range
constexpr std::int64_t patcherControllerScale = 268435456LL; // controller outputs: public-SDK 64-bit Patcher fixed point

enum class ChangeMode : std::uint8_t
{
    off,
    restart,
    latchEighthBeat,
    latchQuarterBeat,
    latchHalfBeat,
    latchOneBeat,
    latchTwoBeats,
    count
};

enum class TimingFeel : std::uint8_t { straight, dotted, triplet };
enum class DrawMode : std::uint8_t { edit, rampUp, rampDown, steps, eraser };
enum class FactoryBank : std::uint8_t { filters, volumeGates, melodic12, melodic24, count };
enum class MidiStartMode : std::uint8_t { startPosition, loopBack };
enum class MidiReleaseMode : std::uint8_t { loopForever, release };
enum class CurveType : std::uint8_t
{
    hold,
    singleCurve,
    doubleCurve,
    halfSine,
    stairs,
    smoothStairs,
    pulse,
    sine,
    triangle,
    saw,
    count
};
enum class Parameter : std::uint8_t
{
    pattern,
    speed,
    sync,
    division,
    timingFeel,
    changeMode,
    enabled,
    startPosition,
    midiTrigger,
    baseValue,
    patternMix,
    sustainPosition,
    mod1,
    mod2,
    positionSync,
    retrigger
};

struct Point
{
    float x = 0.0f;
    float y = 0.0f;
    float tension = 0.0f;
    CurveType curve = CurveType::singleCurve;
    std::array<float, 2> modX {};
    std::array<float, 2> modY {};
    std::array<float, 2> modTension {};
};

struct Pattern
{
    std::array<Point, maxPoints> points {};
    std::uint8_t count = 0;

    float valueAt (float phase) const noexcept;
    float valueAt (float phase, float mod1, float mod2) const noexcept;
    Pattern withModulation (float mod1, float mod2) const noexcept;
    std::optional<Pattern> doubled() const noexcept;
    void normalise() noexcept;
    static Pattern factory (int index);
};

struct PatternSettings
{
    bool sync = true;
    float speedHz = 1.0f;
    int division = 19;
    TimingFeel timingFeel = TimingFeel::straight;
    ChangeMode changeMode = ChangeMode::off;
    float startPosition = 0.0f;
    float sustainPosition = 1.0f;
    bool midiTrigger = false;
    float baseValue = 0.0f;
    float amount = 1.0f;
    float mod1 = 0.0f;
    float mod2 = 0.0f;
    std::array<float, 2> modSpeed {};
    bool positionSync = false;
    MidiStartMode midiStartMode = MidiStartMode::startPosition;
    MidiReleaseMode midiReleaseMode = MidiReleaseMode::loopForever;
    bool legato = false;
    bool midiLoopForever = true;
    bool midiSustain = false;
    bool amountBipolar = false;
    bool operator== (const PatternSettings&) const = default;
};

struct PatternBank
{
    std::array<Pattern, patternsPerLane> patterns {};
    std::array<bool, patternsPerLane> occupied {};
    std::array<std::string, patternsPerLane> names {};
    std::array<PatternSettings, patternsPerLane> settings {};
};

struct LaneState
{
    bool enabled = false;
    int selectedPattern = 0;
    bool sync = true;
    float speedHz = 1.0f;
    int division = 19;
    TimingFeel timingFeel = TimingFeel::straight;
    ChangeMode changeMode = ChangeMode::off;
    float startPosition = 0.0f;
    float sustainPosition = 1.0f;
    bool midiTrigger = false;
    float baseValue = 0.0f;
    float patternMix = 1.0f;
    float mod1 = 0.0f;
    float mod2 = 0.0f;
    std::array<float, 2> modSpeed {};
    bool positionSync = false;
    MidiStartMode midiStartMode = MidiStartMode::startPosition;
    MidiReleaseMode midiReleaseMode = MidiReleaseMode::loopForever;
    bool legato = false;
    bool midiLoopForever = true;
    bool midiSustain = false;
    bool amountBipolar = false;
    std::shared_ptr<const PatternBank> bank;
};

const PatternBank& patternBank (const LaneState& lane) noexcept;
PatternBank& editPatternBank (LaneState& lane);

struct ProjectState
{
    std::uint32_t version = 17;
    std::uint64_t revision = 1;
    int activeLaneCount = 1;
    int selectedLane = 0;
    int editorGridX = 8;
    int editorGridY = 8;
    DrawMode editorDrawMode = DrawMode::edit;
    std::array<LaneState, maxLanes> lanes {};

    static ProjectState defaults();
    static void initialiseDefaults (ProjectState& destination);
    void validate() noexcept;
};

class Model
{
public:
    using Listener = std::function<void()>;

    Model();

    std::shared_ptr<const ProjectState> snapshot() const noexcept;
    void mutate (const std::function<void (ProjectState&)>& edit, bool createUndo = true);
    void beginGesture();
    void endGesture();
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;

    int addLane();
    void selectLane (int lane);
    void selectPattern (int lane, int pattern, bool createUndo = true);
    void randomPattern (int lane);
    void randomizeAll (int lane);
    int newPattern (int lane);
    bool deletePattern (int lane);
    void renamePattern (int lane, int slot, std::string name);
    void reorderPattern (int lane, int from, int to);
    void loadFactoryBank (int lane, FactoryBank bank);

    std::string copyPatternText (int lane, int slot) const;
    bool pastePatternText (int lane, int slot, const std::string& text);

    std::vector<std::uint8_t> serialize() const;
    bool deserialize (const void* data, std::size_t size);
    std::vector<std::uint8_t> serializeLaneBank (int lane) const;
    bool deserializeLaneBank (int lane, const void* data, std::size_t size);

    void setParameterRealtime (int lane, Parameter parameter, int hostValue) noexcept;
    int realtimeParameterValue (int lane, Parameter parameter) const noexcept;
    LaneState realtimeLane (int lane, const ProjectState& shapeState) const noexcept;
    bool synchroniseRealtimeParameters();

    int addListener (Listener listener);
    void removeListener (int token);

private:
    void publishLocked (bool updateRealtime = true);
    void pushUndoLocked();
    void writeRealtimeFromStateLocked() noexcept;
    void notifyListeners();

    mutable std::mutex mutex;
    std::unique_ptr<ProjectState> state;
    // Use the shared_ptr atomic free functions instead of atomic<shared_ptr>.
    // The latter is a C++20 specialization that older Apple libc++ SDKs do not
    // expose consistently when Xcode builds a universal target.
    std::shared_ptr<const ProjectState> published;
    std::vector<ProjectState> undoStack;
    std::vector<ProjectState> redoStack;
    bool gestureOpen = false;
    bool gestureChanged = false;
    std::mt19937 random { std::random_device {}() };
    int nextListenerToken = 1;
    std::vector<std::pair<int, Listener>> listeners;
    std::array<std::array<std::atomic<int>, parameterKinds>, maxLanes> realtimeParameters {};
    std::atomic<std::uint64_t> realtimeRevision { 1 };
    std::uint64_t syncedRealtimeRevision = 1;
};

struct TickContext
{
    double songBeat = 0.0;
    double runningMilliseconds = -1.0;
    double tempoBpm = 120.0;
    double sampleRate = 44100.0;
    int samplesPerTick = 64;
    bool playing = false;
};

class Engine
{
public:
    explicit Engine (const Model& source);
    void requestMidiTrigger (bool overlapping = false) noexcept;
    void requestMidiRelease() noexcept;
    void requestManualTrigger (int lane) noexcept;
    void requestManualRelease (int lane) noexcept;
    void tick (const TickContext& context) noexcept;
    float output (int lane) const noexcept;
    float phase (int lane) const noexcept;
    int activePattern (int lane) const noexcept;
    int pendingPattern (int lane) const noexcept;

private:
    struct RuntimeLane
    {
        float phase = 0.0f;
        float output = 0.0f;
        int requested = 0;
        int active = 0;
        int pending = -1;
        double boundary = 0.0;
        bool initialized = false;
        bool midiHeld = false;
        bool midiEnvelopeActivated = false;
        bool releaseTail = false;
        bool stoppedAfterRelease = false;
        bool refreshPending = false;
        double refreshBoundary = 0.0;
    };

    static double latchBeats (ChangeMode mode) noexcept;
    static double periodBeats (const LaneState& lane) noexcept;
    void handleRequest (RuntimeLane& runtime, const LaneState& lane,
                        const TickContext& context) noexcept;

    const Model& model;
    std::array<RuntimeLane, maxLanes> runtime {};
    std::array<std::atomic<float>, maxLanes> phaseDisplay {};
    std::array<std::atomic<float>, maxLanes> outputDisplay {};
    std::array<std::atomic<int>, maxLanes> activeDisplay {};
    std::array<std::atomic<int>, maxLanes> pendingDisplay {};
    std::atomic<bool> midiTriggerRequested { false };
    std::atomic<bool> midiOverlapTriggerRequested { false };
    std::atomic<bool> midiReleaseRequested { false };
    std::array<std::atomic<bool>, maxLanes> manualTriggerRequested {};
    std::array<std::atomic<bool>, maxLanes> manualReleaseRequested {};
    double lastBeat = 0.0;
    double lastRunningMilliseconds = 0.0;
    bool previousPlaying = false;
    bool hasTicked = false;
    bool hasRunningClock = false;
};

int parameterIndex (int lane, Parameter parameter) noexcept;
int parameterToHost (const LaneState& lane, Parameter parameter) noexcept;
void setParameterFromHost (LaneState& lane, Parameter parameter, int hostValue) noexcept;
const char* changeModeName (ChangeMode mode) noexcept;
const char* timingFeelName (TimingFeel feel) noexcept;
const char* divisionName (int division) noexcept;
double divisionBeats (int division) noexcept;
const char* curveTypeName (CurveType type) noexcept;
const char* factoryBankName (FactoryBank bank) noexcept;
float curveSegment (float start, float end, float phase, float tension,
                    CurveType type = CurveType::singleCurve) noexcept;
intptr_t encodeFLControllerValue (float normalized, bool forPatcher) noexcept;
int decodeModernFLParameterValue (intptr_t value) noexcept;
int decodeFLParameterValue (intptr_t value, bool legacyRange) noexcept;
int encodeFLParameterReturnValue (int hostValue) noexcept;
int decodeFLFloatParameterValue (intptr_t value) noexcept;
int encodeFLFloatParameterValue (int hostValue) noexcept;
} // namespace stepshaper
