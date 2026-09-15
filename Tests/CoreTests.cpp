#include "Core.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void expect (bool condition, const std::string& message)
{
    if (! condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

bool samePattern (const stepshaper::Pattern& a, const stepshaper::Pattern& b)
{
    if (a.count != b.count) return false;
    for (int point = 0; point < a.count; ++point)
    {
        const auto& x = a.points[point];
        const auto& y = b.points[point];
        if (std::abs (x.x - y.x) > 0.00001f || std::abs (x.y - y.y) > 0.00001f
            || std::abs (x.tension - y.tension) > 0.00001f || x.curve != y.curve
            || x.modX != y.modX || x.modY != y.modY || x.modTension != y.modTension) return false;
    }
    return true;
}

bool hasDuplicate (const stepshaper::PatternBank& bank, int count)
{
    for (int left = 0; left < count; ++left)
        for (int right = left + 1; right < count; ++right)
            if (samePattern (bank.patterns[left], bank.patterns[right]))
            {
                std::cerr << "Duplicate slots: " << left + 1 << " and " << right + 1 << '\n';
                return true;
            }
    return false;
}
}

int main()
{
    using namespace stepshaper;
    Model model;
    auto state = model.snapshot();
    expect (state->activeLaneCount == 1, "starts with one LFO");
    expect (state->lanes[0].enabled && ! state->lanes[1].enabled, "only first stable output is enabled");
    expect (model.addLane() == 1 && model.snapshot()->activeLaneCount == 2, "plus adds the next stable LFO");

    model.mutate ([] (ProjectState& s) { s.lanes[0].amountBipolar = true; }, false);
    const auto copied = model.copyPatternText (0, 0);
    auto windowsPatternFile = copied;
    for (std::size_t newline = 0; (newline = windowsPatternFile.find ('\n', newline)) != std::string::npos; newline += 2)
        windowsPatternFile.replace (newline, 1, "\r\n");
    expect (model.pastePatternText (1, 5, windowsPatternFile),
            "Windows CRLF .patternbank files parse after being saved by JUCE");
    expect (model.pastePatternText (1, 4, copied), "versioned clipboard pattern parses");
    expect (std::abs (patternBank (model.snapshot()->lanes[1]).patterns[4].valueAt (0.4f) - 0.4f) < 0.01f,
            "paste transfers shape between lanes");
    expect (patternBank (model.snapshot()->lanes[1]).settings[4]
            == patternBank (model.snapshot()->lanes[0]).settings[0],
            "copy and paste transfers the pattern's musical settings");
    expect (patternBank (model.snapshot()->lanes[1]).settings[4].amountBipolar,
            ".patternbank files retain the Amount mode");
    expect (patternBank (model.snapshot()->lanes[1]).names[4]
            == patternBank (model.snapshot()->lanes[0]).names[0],
            ".patternbank state carries the pattern name as well as its shape and settings");
    Model separateInstance;
    expect (separateInstance.pastePatternText (0, 3, copied)
            && samePattern (patternBank (separateInstance.snapshot()->lanes[0]).patterns[3],
                            patternBank (model.snapshot()->lanes[0]).patterns[0]),
            "clipboard text pastes the complete spline into a separate Pattern Bank instance");

    Model bankModel;
    const auto addedPattern = bankModel.newPattern (0);
    bankModel.renamePattern (0, addedPattern, "Chopped Gate");
    expect (addedPattern == 12 && patternBank (bankModel.snapshot()->lanes[0]).names[12] == "Chopped Gate",
            "NEW creates and names the next free pattern slot");
    expect (bankModel.deletePattern (0) && bankModel.snapshot()->lanes[0].selectedPattern == 11,
            "DEL removes the current pattern and selects the nearest remaining slot");
    expect (bankModel.undo() && bankModel.canRedo() && bankModel.redo(), "undo and redo preserve pattern-bank edits");
    bankModel.loadFactoryBank (0, FactoryBank::filters);
    expect (patternBank (bankModel.snapshot()->lanes[0]).names[0] == "Sweep Up"
            && patternBank (bankModel.snapshot()->lanes[0]).names[9] == "Sample and Hold"
            && std::count (patternBank (bankModel.snapshot()->lanes[0]).occupied.begin(),
                           patternBank (bankModel.snapshot()->lanes[0]).occupied.end(), true) == 100,
            "filter factory preset loads a complete named bank");
    const auto& filterMorph = patternBank (bankModel.snapshot()->lanes[0]).patterns[50];
    expect (filterMorph.points[0].curve == CurveType::singleCurve
            && filterMorph.points[0].modTension[1] != 0.0f,
            "expanded filter patterns retain useful Mod variations");
    bool allAdvancedFiltersMove = true;
    for (int slot = 50; slot < 98; ++slot)
    {
        const auto& pattern = patternBank (bankModel.snapshot()->lanes[0]).patterns[slot];
        float low = 1.0f, high = 0.0f;
        for (int point = 0; point < pattern.count; ++point)
        {
            low = std::min (low, pattern.points[point].y);
            high = std::max (high, pattern.points[point].y);
        }
        allAdvancedFiltersMove = allAdvancedFiltersMove && high - low >= 0.50f;
    }
    expect (allAdvancedFiltersMove, "advanced filter presets avoid weak near-flat modulation ranges");
    expect (! hasDuplicate (patternBank (bankModel.snapshot()->lanes[0]), 100),
            "filter bank contains no exact duplicate patterns");
    const auto releaseRoutes = [] (const PatternBank& bank)
    {
        int sharp = 0, routed = 0;
        for (int slot = 0; slot < 100; ++slot)
            for (int point = 0; point + 1 < bank.patterns[slot].count; ++point)
            {
                const auto& upper = bank.patterns[slot].points[point];
                const auto& lower = bank.patterns[slot].points[point + 1];
                if (upper.y - lower.y >= .35f && lower.x - upper.x >= 0.0f
                    && lower.x - upper.x <= .16f && lower.x < .999f)
                {
                    ++sharp;
                    routed += lower.modX[0] >= .249f;
                }
            }
        return std::pair { sharp, routed };
    };
    const auto filterReleaseRoutes = releaseRoutes (patternBank (bankModel.snapshot()->lanes[0]));
    expect (filterReleaseRoutes.first > 10 && filterReleaseRoutes.first == filterReleaseRoutes.second,
            "every sharp filter release maps Mod 1 to the lower release endpoint");
    const auto distinctCurves = [] (const Pattern& pattern)
    {
        std::array<bool, static_cast<int> (CurveType::count)> used {};
        for (int point = 0; point + 1 < pattern.count; ++point)
            used[static_cast<std::size_t> (pattern.points[point].curve)] = true;
        return std::count (used.begin(), used.end(), true);
    };
    expect (distinctCurves (patternBank (bankModel.snapshot()->lanes[0]).patterns[97]) <= 4
            && distinctCurves (patternBank (bankModel.snapshot()->lanes[0]).patterns[98]) >= 6,
            "only the final two filter presets are intentionally multi-tension experiments");
    std::array<bool, maxPoints + 1> filterPointCounts {};
    for (int slot = 58; slot < 98; ++slot)
        filterPointCounts[patternBank (bankModel.snapshot()->lanes[0]).patterns[slot].count] = true;
    expect (std::count (filterPointCounts.begin(), filterPointCounts.end(), true) >= 8,
            "bandpass, rhythmic, and evolving filter families use varied pattern topologies");
    bankModel.loadFactoryBank (0, FactoryBank::volumeGates);
    expect (patternBank (bankModel.snapshot()->lanes[0]).names[4] == "Sidechain Pump"
            && patternBank (bankModel.snapshot()->lanes[0]).names[50] == "Techno Pulse 1"
            && patternBank (bankModel.snapshot()->lanes[0]).names[66] == "DnB Break Gate 1",
            "volume factory preset includes focused pump, rave and DnB gate families");
    const auto& raveStab = patternBank (bankModel.snapshot()->lanes[0]).patterns[20];
    expect (std::any_of (raveStab.points.begin(), raveStab.points.begin() + raveStab.count,
                        [] (const Point& point) { return point.tension < -0.1f; }),
            "factory stab patterns include shaped decay tails rather than only square gates");
    float shortestGateHold = 1.0f, longestGateHold = 0.0f;
    for (int slot = 20; slot < 98; ++slot)
    {
        const auto& pattern = patternBank (bankModel.snapshot()->lanes[0]).patterns[slot];
        for (int point = 0; point + 1 < pattern.count; ++point)
            if (pattern.points[point].y > 0.4f && std::abs (pattern.points[point + 1].y - pattern.points[point].y) < 0.001f)
            {
                const auto hold = pattern.points[point + 1].x - pattern.points[point].x;
                if (hold > 0.0001f) { shortestGateHold = std::min (shortestGateHold, hold); longestGateHold = std::max (longestGateHold, hold); }
            }
    }
    expect (longestGateHold > shortestGateHold * 2.0f,
            "gate bank deliberately mixes short chops with medium and longer stab holds");
    float shortestRelease = 1.0f, longestRelease = 0.0f;
    int curvedReleases = 0, straightReleases = 0;
    for (int slot = 20; slot < 98; ++slot)
    {
        const auto& pattern = patternBank (bankModel.snapshot()->lanes[0]).patterns[slot];
        for (int point = 0; point + 1 < pattern.count; ++point)
            if (pattern.points[point].y > 0.4f && pattern.points[point + 1].y < 0.1f)
            {
                const auto duration = pattern.points[point + 1].x - pattern.points[point].x;
                if (duration > 0.0f) { shortestRelease = std::min (shortestRelease, duration); longestRelease = std::max (longestRelease, duration); }
                if (pattern.points[point].curve == CurveType::singleCurve && std::abs (pattern.points[point].tension) < .01f)
                    ++straightReleases;
                else ++curvedReleases;
            }
    }
    expect (longestRelease > shortestRelease * 4.0f && curvedReleases > 10 && straightReleases > 10,
            "gate bank mixes hard drops, long decay tails, and combined release profiles");
    expect (! hasDuplicate (patternBank (bankModel.snapshot()->lanes[0]), 100),
            "volume and gate bank contains no exact duplicate patterns");
    const auto gateReleaseRoutes = releaseRoutes (patternBank (bankModel.snapshot()->lanes[0]));
    expect (gateReleaseRoutes.first > 40 && gateReleaseRoutes.first == gateReleaseRoutes.second,
            "every sharp gate release maps Mod 1 to the lower release endpoint");
    expect (patternBank (bankModel.snapshot()->lanes[0]).settings[20].division == 19
            && patternBank (bankModel.snapshot()->lanes[0]).settings[40].division == 22
            && patternBank (bankModel.snapshot()->lanes[0]).settings[80].division == 19
            && patternBank (bankModel.snapshot()->lanes[0]).settings[90].division == 25
            && patternBank (bankModel.snapshot()->lanes[0]).settings[20].baseValue == 0.0f,
            "factory slots carry appropriate one-bar, two-bar or four-bar timing and zero Base");
    bankModel.loadFactoryBank (0, FactoryBank::melodic12);
    const auto& octaveUp = patternBank (bankModel.snapshot()->lanes[0]).patterns[0];
    expect (octaveUp.valueAt (0.01f) < 0.01f && octaveUp.valueAt (0.99f) > 0.99f,
            "12-semitone melodic bank maps its routing range from -12 to +12");
    expect (bankModel.snapshot()->editorGridY == 24 && octaveUp.count == 6,
            "12-semitone melodic bank selects a 24-line pitch grid and keeps three defined steps");
    expect (patternBank (bankModel.snapshot()->lanes[0]).names[50] == "Classic Arp 1"
            && patternBank (bankModel.snapshot()->lanes[0]).names[82] == "Vocal Phrase 1"
            && patternBank (bankModel.snapshot()->lanes[0]).names[90] == "Long Vocal Run 1",
            "melodic expansion separates classic arps, loop warps and humanized vocal phrases");
    const auto& majorUp = patternBank (bankModel.snapshot()->lanes[0]).patterns[2];
    expect (majorUp.count == 16, "ascending melodic pattern uses eight equal grid-aligned segments");
    const auto usesMusicalGrid = [] (const Pattern& pattern)
    {
        constexpr std::array divisions { 3, 4, 6, 8, 12, 16, 24, 32, 48 };
        return std::any_of (divisions.begin(), divisions.end(), [&] (int division)
        {
            for (int point = 0; point < pattern.count; ++point)
                if (std::abs (pattern.points[point].x * division
                              - std::round (pattern.points[point].x * division)) > 0.0001f) return false;
            return true;
        });
    };
    expect (std::all_of (patternBank (bankModel.snapshot()->lanes[0]).patterns.begin(),
                         patternBank (bankModel.snapshot()->lanes[0]).patterns.begin() + 100, usesMusicalGrid),
            "all -12/+12 melodic presets use straight, triplet or dotted-compatible segment grids");
    const auto& vocalPhrase = patternBank (bankModel.snapshot()->lanes[0]).patterns[84];
    const auto hasHardPitchStep = [&]
    {
        for (int point = 0; point + 1 < vocalPhrase.count; ++point)
            if (std::abs (vocalPhrase.points[point].x - vocalPhrase.points[point + 1].x) < 0.0001f
                && std::abs (vocalPhrase.points[point].y - vocalPhrase.points[point + 1].y) > 0.01f) return true;
        return false;
    }();
    const auto hasVocalCurve = std::any_of (vocalPhrase.points.begin(), vocalPhrase.points.begin() + vocalPhrase.count,
        [] (const Point& point) { return point.curve == CurveType::halfSine; });
    expect (hasHardPitchStep && hasVocalCurve,
            "vocal phrases balance locked notes with selected slides and vibrato moments");
    expect (! hasDuplicate (patternBank (bankModel.snapshot()->lanes[0]), 100),
            "-12/+12 melodic bank contains no exact duplicate patterns");
    int melodicMacroPatterns = 0;
    for (int slot = 0; slot < 100; ++slot)
    {
        const auto& pattern = patternBank (bankModel.snapshot()->lanes[0]).patterns[slot];
        int pitchRoutes = 0, timingRoutes = 0;
        for (int point = 0; point < pattern.count; ++point)
        {
            pitchRoutes += std::abs (pattern.points[point].modY[0]) > .0001f;
            timingRoutes += std::abs (pattern.points[point].modX[1]) > .0001f;
        }
        melodicMacroPatterns += pitchRoutes >= 2 && timingRoutes >= 2;
    }
    expect (melodicMacroPatterns >= 90,
            "most melodic presets map Mod 1 across pitches and Mod 2 across note timing");
    bankModel.loadFactoryBank (0, FactoryBank::melodic24);
    expect (bankModel.snapshot()->editorGridY == 48
            && std::count (patternBank (bankModel.snapshot()->lanes[0]).occupied.begin(),
                           patternBank (bankModel.snapshot()->lanes[0]).occupied.end(), true) == 100,
            "24-semitone melodic bank supplies 100 presets and selects a 48-line pitch grid");
    expect (std::all_of (patternBank (bankModel.snapshot()->lanes[0]).patterns.begin(),
                         patternBank (bankModel.snapshot()->lanes[0]).patterns.begin() + 100, usesMusicalGrid),
            "all -24/+24 melodic presets use straight, triplet or dotted-compatible segment grids");
    expect (! hasDuplicate (patternBank (bankModel.snapshot()->lanes[0]), 100),
            "-24/+24 melodic bank contains no exact duplicate patterns");

    Model speedModModel;
    speedModModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false;
        s.lanes[0].speedHz = 1.0f;
        s.lanes[0].mod1 = 1.0f;
        s.lanes[0].modSpeed[0] = 0.25f;
    }, false);
    Engine speedModEngine (speedModModel);
    TickContext speedModTick;
    speedModTick.sampleRate = 1000.0;
    speedModTick.samplesPerTick = 10;
    speedModEngine.tick (speedModTick);
    expect (speedModEngine.phase (0) > 0.05f,
            "a Mod assignment on Speed changes the real-time free-running LFO rate");

    model.selectPattern (0, 9);
    expect (model.undo() && model.snapshot()->lanes[0].selectedPattern == 0, "undo restores selection");

    Engine engine (model);
    TickContext tick;
    tick.playing = true;
    tick.songBeat = 0.10;
    engine.tick (tick);
    model.mutate ([] (ProjectState& s)
    {
        s.lanes[0].changeMode = ChangeMode::latchQuarterBeat;
        s.lanes[0].selectedPattern = 6;
    }, false);
    tick.songBeat = 0.11;
    engine.tick (tick);
    expect (engine.activePattern (0) == 0 && engine.pendingPattern (0) == 6, "latch queues the latest pattern");
    model.mutate ([] (ProjectState& s) { s.lanes[0].selectedPattern = 7; }, false);
    tick.songBeat = 0.12;
    engine.tick (tick);
    expect (engine.pendingPattern (0) == 7, "latest automated request wins before the boundary");
    tick.songBeat = 0.249;
    engine.tick (tick);
    expect (engine.activePattern (0) == 0, "latch waits for its song-grid boundary");
    tick.songBeat = 0.25;
    engine.tick (tick);
    expect (engine.activePattern (0) == 7 && engine.pendingPattern (0) == -1, "latch applies and clears at boundary");

    model.mutate ([] (ProjectState& s)
    {
        s.lanes[0].changeMode = ChangeMode::off;
        s.lanes[0].selectedPattern = 2;
    }, false);
    const auto oldPhase = engine.phase (0);
    tick.songBeat = 0.26;
    engine.tick (tick);
    expect (engine.activePattern (0) == 2 && engine.phase (0) >= oldPhase, "Off switches immediately while retaining phase");

    expect (parameterIndex (0, Parameter::enabled) == enabledParameterOffset
            && parameterIndex (7, Parameter::enabled) == patternParameterOffset - 1
            && parameterIndex (0, Parameter::pattern) == patternParameterOffset
            && parameterIndex (7, Parameter::pattern) == speedParameterOffset - 1
            && parameterIndex (0, Parameter::speed) == speedParameterOffset
            && parameterIndex (0, Parameter::sync) == syncParameterOffset
            && parameterIndex (0, Parameter::division) == divisionParameterOffset
            && parameterIndex (0, Parameter::timingFeel) == timingFeelParameterOffset
            && parameterIndex (0, Parameter::changeMode) == changeModeParameterOffset
            && parameterIndex (0, Parameter::positionSync) == positionSyncParameterOffset
            && parameterIndex (0, Parameter::midiTrigger) == midiTriggerParameterOffset
            && parameterIndex (0, Parameter::retrigger) == retriggerParameterOffset
            && parameterIndex (0, Parameter::startPosition) == startPositionParameterOffset
            && parameterIndex (0, Parameter::sustainPosition) == sustainPositionParameterOffset
            && parameterIndex (0, Parameter::baseValue) == baseValueParameterOffset
            && parameterIndex (0, Parameter::patternMix) == patternMixParameterOffset
            && parameterIndex (0, Parameter::mod1) == mod1ParameterOffset
            && parameterIndex (0, Parameter::mod2) == mod2ParameterOffset
            && parameterIndex (7, Parameter::mod2) == parameterCount - 1,
            "new automation parameters append without moving existing parameter indices");
    expect (encodeFLControllerValue (0.0f, false) == 0
            && encodeFLControllerValue (0.25f, false) == 16384
            && encodeFLControllerValue (0.5f, false) == 32768
            && encodeFLControllerValue (1.0f, false) == 65536,
            "standalone internal-controller output maps to FL's classic 16-bit range");
    expect (encodeFLControllerValue (0.0f, true) == 0
            && encodeFLControllerValue (0.5f, true) == (static_cast<intptr_t> (1) << 43)
            && encodeFLControllerValue (1.0f, true) == (static_cast<intptr_t> (1) << 44),
            "Patcher controller output maps to its 64-bit fixed-point range");
    expect (decodeModernFLParameterValue (static_cast<intptr_t> (1) << 29) == 32768,
            "incoming modern FL parameter values decode to the internal range");
    expect (encodeFLParameterReturnValue (65536, true) == 65536
            && encodeFLParameterReturnValue (65536, false) == (1 << 30)
            && encodeFLParameterReturnValue (32768, true) == 32768,
            "internal-controller parameter calls return FL's legacy 0-65536 range");
    auto mappedLane = ProjectState::defaults().lanes[0];
    auto sparseBank = std::make_shared<PatternBank> (patternBank (mappedLane));
    sparseBank->occupied.fill (false);
    sparseBank->occupied[2] = sparseBank->occupied[7] = sparseBank->occupied[20] = true;
    mappedLane.bank = sparseBank;
    mappedLane.selectedPattern = 20;
    expect (parameterToHost (mappedLane, Parameter::pattern) == 65536,
            "the last active pattern always maps to automation value one");
    setParameterFromHost (mappedLane, Parameter::pattern, 0);
    expect (mappedLane.selectedPattern == 2, "automation value zero maps to the first active pattern");
    setParameterFromHost (mappedLane, Parameter::pattern, 32768);
    expect (mappedLane.selectedPattern == 7, "pattern automation is distributed across active slots only");
    expect (curveSegment (0.0f, 1.0f, 0.5f, 0.5f) > 0.5f
            && curveSegment (1.0f, 0.0f, 0.5f, -0.5f) > 0.5f,
            "opposite tension signs bend rising and falling segments visibly upward");
    expect (curveSegment (0.0f, 1.0f, 0.5f, 0.0f, CurveType::hold) == 0.0f
            && curveSegment (0.0f, 1.0f, 0.5f, 0.0f, CurveType::halfSine) > 0.7f,
            "FL-style segment families alter the curve independently of its endpoints");
    expect (std::abs (curveSegment (0.0f, 1.0f, 1.0f / 3.0f, 0.066f, CurveType::sine) - 1.0f) < 0.001f
            && curveSegment (0.0f, 1.0f, 2.0f / 3.0f, 0.066f, CurveType::sine) < 0.001f,
            "sine tension produces continuous alternating waves instead of reset ramps");
    expect (std::string (divisionName (9)) == "1/8 T"
            && divisionBeats (9) < divisionBeats (10) && divisionBeats (10) < divisionBeats (11),
            "sync divisions are ordered from triplet through straight to dotted by duration");
    expect (std::abs (curveSegment (0.0f, 1.0f, 0.04f, 1.0f, CurveType::stairs) - 1.0f / 32.0f) < 0.001f,
            "maximum stair tension resolves 32 steps");
    Pattern modulated = Pattern::factory (0);
    modulated.points[1].modY[0] = -0.5f;
    expect (modulated.valueAt (0.75f, 1.0f, 0.0f) < modulated.valueAt (0.75f),
            "mod macros apply stored point-axis depth without rewriting the base pattern");
    const auto displayedModulation = modulated.withModulation (1.0f, 0.0f);
    expect (displayedModulation.points[1].y < modulated.points[1].y,
            "the editor projection exposes the same modulated point movement");
    Pattern xModulated;
    xModulated.count = 5;
    xModulated.points[0] = { 0.0f, 0.0f, 0.0f };
    xModulated.points[1] = { 0.25f, 1.0f, 0.0f };
    xModulated.points[2] = { 0.5f, 0.0f, 0.0f };
    xModulated.points[3] = { 0.75f, 1.0f, 0.0f };
    xModulated.points[4] = { 1.0f, 0.0f, 0.0f };
    xModulated.points[1].modX[0] = 1.0f;
    xModulated.normalise();
    const auto xProjection = xModulated.withModulation (1.0f, 0.0f);
    expect (std::abs (xProjection.points[1].x - xModulated.points[2].x) < 0.00001f,
            "X modulation stops at the next point");
    expect (std::abs (xProjection.points[2].x - 0.5f) < 0.00001f
            && std::abs (xProjection.points[3].x - 0.75f) < 0.00001f,
            "X modulation never offsets later points");
    auto opposingXModulation = xModulated;
    opposingXModulation.points[1].modX[0] = 0.5f;
    opposingXModulation.points[2].modX[0] = -0.5f;
    const auto opposingProjection = opposingXModulation.withModulation (1.0f, 0.0f);
    expect (opposingProjection.points[1].x <= opposingProjection.points[2].x
            && std::abs (opposingProjection.points[1].x - opposingProjection.points[2].x) < 0.00001f
            && std::abs (opposingProjection.points[3].x - 0.75f) < 0.00001f,
            "opposing X modulation hard-limits adjacent points at their instantaneous collision");
    Pattern vertical;
    vertical.count = 4;
    vertical.points[0] = { 0.0f, 0.0f, 0.0f };
    vertical.points[1] = { 0.5f, 0.0f, 0.0f };
    vertical.points[2] = { 0.5f, 1.0f, 0.0f };
    vertical.points[3] = { 1.0f, 1.0f, 0.0f };
    vertical.normalise();
    expect (vertical.points[1].x == vertical.points[2].x
            && vertical.valueAt (0.499f) < 0.01f && vertical.valueAt (0.5f) > 0.99f,
            "duplicate-X points preserve a mathematically vertical transition");

    Model transportModel;
    transportModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false;
        s.lanes[0].speedHz = 1.0f;
        s.lanes[0].startPosition = 0.25f;
    }, false);
    Engine transportEngine (transportModel);
    TickContext transportTick;
    transportTick.playing = false;
    transportTick.sampleRate = 100.0;
    transportTick.samplesPerTick = 10;
    transportEngine.tick (transportTick);
    const auto stoppedPhase = transportEngine.phase (0);
    transportEngine.tick (transportTick);
    expect (transportEngine.phase (0) > stoppedPhase, "LFO continues moving while transport is stopped");
    transportTick.playing = true;
    transportEngine.tick (transportTick);
    expect (std::abs (transportEngine.phase (0) - 0.25f) < 0.0001f,
            "Play transition restarts exactly at the Start flag");
    transportEngine.tick (transportTick);
    const auto playingIncrement = transportEngine.phase (0) - 0.25f;
    expect (std::abs (playingIncrement - 0.1f) < 0.0001f,
            "playing and stopped transport use the same sample-clock phase increment");

    Model positionSyncModel;
    positionSyncModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = true;
        s.lanes[0].division = 13; // one beat
        s.lanes[0].startPosition = 0.25f;
        s.lanes[0].positionSync = true;
    }, false);
    Engine positionSyncEngine (positionSyncModel);
    TickContext positionSyncTick;
    positionSyncTick.playing = false;
    positionSyncTick.songBeat = 4.5;
    positionSyncEngine.tick (positionSyncTick);
    expect (std::abs (positionSyncEngine.phase (0) - 0.75f) < 0.0001f,
            "Pos Sync follows FL's stopped song position");
    positionSyncTick.songBeat = 6.125;
    positionSyncEngine.tick (positionSyncTick);
    expect (std::abs (positionSyncEngine.phase (0) - 0.375f) < 0.0001f,
            "moving FL's stopped playhead immediately updates pattern phase");
    positionSyncTick.playing = true;
    positionSyncEngine.tick (positionSyncTick);
    expect (std::abs (positionSyncEngine.phase (0) - 0.375f) < 0.0001f,
            "Pos Sync preserves the project-relative phase when playback starts");

    Model midiPositionSyncModel;
    midiPositionSyncModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false; s.lanes[0].speedHz = 1.0f;
        s.lanes[0].startPosition = 0.25f; s.lanes[0].positionSync = true;
        s.lanes[0].midiTrigger = true;
    }, false);
    Engine midiPositionSyncEngine (midiPositionSyncModel);
    TickContext midiPositionSyncTick;
    midiPositionSyncTick.playing = false; midiPositionSyncTick.songBeat = 7.0;
    midiPositionSyncTick.sampleRate = 100.0; midiPositionSyncTick.samplesPerTick = 10;
    midiPositionSyncEngine.tick (midiPositionSyncTick);
    expect (std::abs (midiPositionSyncEngine.phase (0) - 0.35f) < 0.0001f,
            "MIDI Trigger disables Pos Sync and retains free-running stopped behavior");

    Model mixModel;
    mixModel.mutate ([] (ProjectState& s) { s.lanes[0].baseValue = 0.72f; s.lanes[0].patternMix = 0.0f; }, false);
    Engine mixEngine (mixModel);
    mixEngine.tick ({});
    expect (std::abs (mixEngine.output (0) - 0.72f) < 0.0001f,
            "Amount returns the output to Base at zero");
    mixModel.mutate ([] (ProjectState& s)
    {
        auto& pattern = editPatternBank (s.lanes[0]).patterns[0];
        pattern.points[0].y = pattern.points[1].y = 0.25f;
        s.lanes[0].baseValue = 1.0f;
        s.lanes[0].patternMix = 1.0f;
        s.lanes[0].amountBipolar = false;
    }, false);
    mixEngine.tick ({});
    expect (std::abs (mixEngine.output (0) - 1.0f) < 0.001f,
            "positive Unipolar Amount cannot lower a 100% Base value");
    mixModel.mutate ([] (ProjectState& s) { s.lanes[0].patternMix = -1.0f; }, false);
    mixEngine.tick ({});
    expect (std::abs (mixEngine.output (0) - 0.75f) < 0.001f,
            "negative Unipolar Amount subtracts the pattern from Base");
    mixModel.mutate ([] (ProjectState& s)
    {
        auto& pattern = editPatternBank (s.lanes[0]).patterns[0];
        pattern.points[0].y = pattern.points[1].y = 0.8f;
        s.lanes[0].baseValue = 0.5f;
        s.lanes[0].amountBipolar = true;
        s.lanes[0].patternMix = -0.5f;
    }, false);
    mixEngine.tick ({});
    expect (std::abs (mixEngine.output (0) - 0.2f) < 0.001f,
            "Bipolar Amount centres the pattern around zero before applying it to Base");

    Model slotSettingsModel;
    slotSettingsModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false; s.lanes[0].speedHz = 2.5f;
        s.lanes[0].baseValue = 0.2f; s.lanes[0].patternMix = -0.4f;
        s.lanes[0].amountBipolar = true;
        s.lanes[0].positionSync = true;
    }, false);
    const auto settingsSlot = slotSettingsModel.newPattern (0);
    slotSettingsModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = true; s.lanes[0].division = 22;
        s.lanes[0].baseValue = 0.0f; s.lanes[0].patternMix = 1.0f;
        s.lanes[0].amountBipolar = false;
    }, false);
    slotSettingsModel.selectPattern (0, 0, false);
    expect (! slotSettingsModel.snapshot()->lanes[0].sync
            && std::abs (slotSettingsModel.snapshot()->lanes[0].speedHz - 2.5f) < 0.001f
            && std::abs (slotSettingsModel.snapshot()->lanes[0].patternMix + 0.4f) < 0.001f
            && slotSettingsModel.snapshot()->lanes[0].amountBipolar
            && slotSettingsModel.snapshot()->lanes[0].positionSync,
            "switching slots recalls the original slot controls");
    slotSettingsModel.selectPattern (0, settingsSlot, false);
    expect (slotSettingsModel.snapshot()->lanes[0].sync
            && slotSettingsModel.snapshot()->lanes[0].division == 22
            && slotSettingsModel.snapshot()->lanes[0].patternMix == 1.0f
            && ! slotSettingsModel.snapshot()->lanes[0].amountBipolar,
            "new slots independently retain their controls");

    Model syncModel;
    syncModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = true;
        s.lanes[0].division = 13; // quarter note / one beat
        s.lanes[0].startPosition = 0.25f;
    }, false);
    Engine syncEngine (syncModel);
    TickContext syncTick;
    syncTick.sampleRate = 100.0;
    syncTick.samplesPerTick = 10;
    syncTick.tempoBpm = 150.0; // the sample-clock estimate is exactly 0.25 beat
    syncEngine.tick (syncTick);
    syncTick.playing = true;
    syncTick.songBeat = 10.0;
    syncEngine.tick (syncTick);
    syncTick.songBeat = 10.25;
    syncEngine.tick (syncTick);
    expect (std::abs (syncEngine.phase (0) - 0.5f) < 0.0001f,
            "synced playback advances at the selected musical rate while FL is playing");
    syncTick.songBeat = 8.0;
    syncEngine.tick (syncTick);
    expect (std::abs (syncEngine.phase (0) - 0.25f) < 0.0001f,
            "song loop or seek realigns sync to the project grid plus Start position");

    Model stoppedSyncModel;
    Model playingSyncModel;
    for (auto* candidate : { &stoppedSyncModel, &playingSyncModel })
        candidate->mutate ([] (ProjectState& s)
        {
            s.lanes[0].sync = true;
            s.lanes[0].division = 13;
            s.lanes[0].startPosition = 0.0f;
        }, false);
    Engine stoppedSyncEngine (stoppedSyncModel);
    Engine playingSyncEngine (playingSyncModel);
    TickContext stoppedClock;
    stoppedClock.sampleRate = 100.0;
    stoppedClock.samplesPerTick = 10;
    stoppedClock.tempoBpm = 60.0;
    stoppedClock.runningMilliseconds = 0.0;
    auto playingClock = stoppedClock;
    playingClock.playing = true;
    for (int i = 0; i < 5; ++i)
    {
        stoppedClock.runningMilliseconds += 100.0;
        stoppedSyncEngine.tick (stoppedClock);
        playingClock.runningMilliseconds += 100.0;
        playingClock.songBeat += 0.1; // Same elapsed beat time as the stopped sample clock.
        playingSyncEngine.tick (playingClock);
    }
    expect (std::abs (stoppedSyncEngine.phase (0) - playingSyncEngine.phase (0)) < 0.0001f,
            "Sync mode runs at the same speed whether the FL transport is stopped or playing");

    Model hostClockSyncModel;
    hostClockSyncModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = true;
        s.lanes[0].division = 13; // quarter note / one beat
        s.lanes[0].startPosition = 0.0f;
    }, false);
    Engine hostClockSyncEngine (hostClockSyncModel);
    TickContext hostClock;
    hostClock.sampleRate = 100.0;
    hostClock.samplesPerTick = 10;
    hostClock.tempoBpm = 150.0; // Sample-clock estimate is 0.25 beat per callback.
    hostClock.runningMilliseconds = 0.0;
    hostClockSyncEngine.tick (hostClock);
    hostClock.playing = true;
    hostClock.songBeat = 12.0;
    hostClock.runningMilliseconds = 100.0;
    hostClockSyncEngine.tick (hostClock);
    hostClock.songBeat += 0.1875; // FL actually advanced by only a dotted eighth of a beat.
    hostClock.runningMilliseconds += 75.0; // At 150 BPM this is exactly 0.1875 beat.
    hostClockSyncEngine.tick (hostClock);
    expect (std::abs (hostClockSyncEngine.phase (0) - 0.1875f) < 0.0001f,
            "Sync mode follows FL's continuous mixer clock instead of treating SamplesPerTick as callback duration");

    Model manualRefreshModel;
    manualRefreshModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = true;
        s.lanes[0].division = 13;
        s.lanes[0].startPosition = 0.2f;
        s.lanes[0].changeMode = ChangeMode::latchOneBeat;
    }, false);
    Engine manualRefreshEngine (manualRefreshModel);
    TickContext refreshTick;
    refreshTick.sampleRate = 100.0;
    refreshTick.samplesPerTick = 10;
    refreshTick.tempoBpm = 60.0;
    manualRefreshEngine.tick (refreshTick);
    refreshTick.playing = true;
    refreshTick.songBeat = 0.1;
    manualRefreshEngine.tick (refreshTick);
    refreshTick.songBeat = 0.2;
    manualRefreshEngine.tick (refreshTick);
    const auto phaseBeforeQueuedRefresh = manualRefreshEngine.phase (0);
    manualRefreshEngine.requestManualTrigger (0);
    refreshTick.songBeat = 0.3;
    manualRefreshEngine.tick (refreshTick);
    expect (manualRefreshEngine.phase (0) > phaseBeforeQueuedRefresh,
            "manual refresh keeps running until its selected Trig Sync boundary");
    refreshTick.songBeat = 1.0;
    manualRefreshEngine.tick (refreshTick);
    expect (std::abs (manualRefreshEngine.phase (0) - 0.2f) < 0.0001f,
            "manual refresh restarts at the Start flag on the selected Trig Sync boundary");

    manualRefreshModel.mutate ([] (ProjectState& s) { s.lanes[0].changeMode = ChangeMode::off; }, false);
    manualRefreshEngine.tick (refreshTick);
    manualRefreshEngine.requestManualTrigger (0);
    manualRefreshEngine.tick (refreshTick);
    expect (std::abs (manualRefreshEngine.phase (0) - 0.2f) < 0.0001f,
            "manual refresh is immediate when Trig Sync is Off");

    Model midiModel;
    midiModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false;
        s.lanes[0].speedHz = 1.0f;
        s.lanes[0].startPosition = 0.25f;
        s.lanes[0].sustainPosition = 0.75f;
        s.lanes[0].midiTrigger = true;
        s.lanes[0].midiReleaseMode = MidiReleaseMode::release;
    }, false);
    Engine midiEngine (midiModel);
    TickContext midiTick;
    midiTick.sampleRate = 100.0;
    midiTick.samplesPerTick = 10;
    midiEngine.tick (midiTick);
    expect (midiEngine.phase (0) > 0.25f, "MIDI-enabled LFO advances normally before a note");
    midiEngine.requestMidiTrigger();
    midiEngine.tick (midiTick);
    expect (std::abs (midiEngine.phase (0) - 0.25f) < 0.0001f,
            "MIDI note request restarts an enabled LFO at its loop-start flag");
    for (int i = 0; i < 6; ++i) midiEngine.tick (midiTick);
    expect (midiEngine.phase (0) >= 0.25f && midiEngine.phase (0) < 0.75f,
            "a held MIDI note loops between the L and S markers");
    midiEngine.requestMidiRelease();
    midiEngine.tick (midiTick);
    expect (std::abs (midiEngine.phase (0) - 0.75f) < 0.0001f,
            "MIDI release begins the tail at the sustain marker");
    for (int i = 0; i < 5; ++i) midiEngine.tick (midiTick);
    expect (std::abs (midiEngine.phase (0) - 1.0f) < 0.0001f,
            "the release tail reaches the pattern end and stops");
    const auto releasedEndValue = midiEngine.output (0);
    midiEngine.tick (midiTick);
    expect (std::abs (releasedEndValue - 1.0f) < 0.0001f
            && std::abs (midiEngine.output (0) - releasedEndValue) < 0.0001f,
            "a completed MIDI release holds the final spline value until the next note-on");
    midiEngine.requestMidiTrigger();
    midiEngine.tick (midiTick);
    expect (std::abs (midiEngine.phase (0) - 0.25f) < 0.0001f,
            "the next MIDI note clears the held release endpoint and retriggers normally");

    Model manualMidiModel;
    manualMidiModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false;
        s.lanes[0].speedHz = 1.0f;
        s.lanes[0].startPosition = 0.25f;
        s.lanes[0].sustainPosition = 0.75f;
        s.lanes[0].midiTrigger = true;
        s.lanes[0].midiReleaseMode = MidiReleaseMode::release;
    }, false);
    Engine manualMidiEngine (manualMidiModel);
    manualMidiEngine.tick (midiTick);
    manualMidiEngine.requestManualTrigger (0);
    manualMidiEngine.tick (midiTick);
    expect (std::abs (manualMidiEngine.phase (0) - 0.25f) < 0.0001f,
            "manual trigger button sends a lane-specific MIDI note-on");
    manualMidiEngine.requestManualRelease (0);
    manualMidiEngine.tick (midiTick);
    expect (std::abs (manualMidiEngine.phase (0) - 0.75f) < 0.0001f,
            "releasing the manual trigger button sends the matching MIDI note-off");

    Model loopBackModel;
    loopBackModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false;
        s.lanes[0].speedHz = 1.0f;
        s.lanes[0].startPosition = 0.25f;
        s.lanes[0].sustainPosition = 0.75f;
        s.lanes[0].midiTrigger = true;
        s.lanes[0].midiStartMode = MidiStartMode::loopBack;
        s.lanes[0].midiReleaseMode = MidiReleaseMode::loopForever;
    }, false);
    Engine loopBackEngine (loopBackModel);
    loopBackEngine.tick (midiTick);
    loopBackEngine.requestMidiTrigger();
    loopBackEngine.tick (midiTick);
    expect (std::abs (loopBackEngine.phase (0)) < 0.0001f,
            "Loop-back mode starts a new MIDI note at the beginning of the pattern");
    for (int i = 0; i < 8; ++i) loopBackEngine.tick (midiTick);
    expect (loopBackEngine.phase (0) >= 0.25f && loopBackEngine.phase (0) < 0.75f,
            "Loop-back mode returns to the Start flag after its first traversal");
    loopBackEngine.requestMidiRelease();
    loopBackEngine.tick (midiTick);
    const auto phaseAfterIgnoredRelease = loopBackEngine.phase (0);
    loopBackEngine.tick (midiTick);
    expect (loopBackEngine.phase (0) != phaseAfterIgnoredRelease,
            "Loop Forever continues after MIDI note-off");

    Model legatoModel;
    legatoModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false;
        s.lanes[0].speedHz = 1.0f;
        s.lanes[0].startPosition = 0.2f;
        s.lanes[0].sustainPosition = 0.9f;
        s.lanes[0].midiTrigger = true;
        s.lanes[0].legato = true;
    }, false);
    Engine legatoEngine (legatoModel);
    legatoEngine.tick (midiTick);
    legatoEngine.requestMidiTrigger();
    legatoEngine.tick (midiTick);
    legatoEngine.tick (midiTick);
    const auto beforeOverlap = legatoEngine.phase (0);
    legatoEngine.requestMidiTrigger (true);
    legatoEngine.tick (midiTick);
    expect (legatoEngine.phase (0) > beforeOverlap,
            "Legato ignores overlapping note-ons without restarting the pattern");
    legatoModel.mutate ([] (ProjectState& s) { s.lanes[0].legato = false; }, false);
    legatoEngine.requestMidiTrigger (true);
    legatoEngine.tick (midiTick);
    expect (std::abs (legatoEngine.phase (0) - 0.2f) < 0.0001f,
            "non-Legato overlapping notes retrigger at the selected start position");

    Model sustainModel;
    sustainModel.mutate ([] (ProjectState& s)
    {
        s.lanes[0].sync = false;
        s.lanes[0].speedHz = 1.0f;
        s.lanes[0].startPosition = 0.0f;
        s.lanes[0].sustainPosition = 0.5f;
        s.lanes[0].midiTrigger = true;
        s.lanes[0].midiLoopForever = false;
        s.lanes[0].midiSustain = true;
        s.lanes[0].midiReleaseMode = MidiReleaseMode::loopForever;
        s.lanes[0].legato = true;
    }, false);
    Engine sustainEngine (sustainModel);
    sustainEngine.tick (midiTick);
    sustainEngine.requestMidiTrigger();
    sustainEngine.tick (midiTick);
    for (int i = 0; i < 7; ++i) sustainEngine.tick (midiTick);
    expect (std::abs (sustainEngine.phase (0) - 0.5f) < 0.0001f,
            "Sustain holds playback at the Sustain marker while a note is held");
    sustainEngine.requestMidiRelease();
    sustainEngine.tick (midiTick);
    sustainEngine.tick (midiTick);
    expect (std::abs (sustainEngine.phase (0) - 0.5f) < 0.0001f,
            "Sustain without Release holds its value after note-off");
    sustainEngine.requestMidiTrigger (true);
    sustainEngine.tick (midiTick);
    expect (std::abs (sustainEngine.phase (0) - 0.5f) < 0.0001f,
            "Legato ignores an overlapping note while Sustain is held");
    sustainEngine.requestMidiTrigger();
    sustainEngine.tick (midiTick);
    expect (std::abs (sustainEngine.phase (0)) < 0.0001f,
            "a new note after a gap restarts a sustained pattern");

    model.mutate ([] (ProjectState& s)
    {
        s.lanes[0].startPosition = 0.37f;
        s.lanes[0].sustainPosition = 0.82f;
        s.editorGridX = 12;
        s.editorGridY = 6;
        s.editorDrawMode = DrawMode::eraser;
        s.lanes[0].midiTrigger = true;
        editPatternBank (s.lanes[0]).patterns[s.lanes[0].selectedPattern].points[0].curve = CurveType::stairs;
        editPatternBank (s.lanes[0]).names[s.lanes[0].selectedPattern] = "Saved Name";
        s.lanes[0].baseValue = 0.33f;
        s.lanes[0].patternMix = 0.74f;
        s.lanes[0].amountBipolar = true;
        s.lanes[0].positionSync = true;
        s.lanes[0].modSpeed[0] = -0.35f;
        s.lanes[0].midiStartMode = MidiStartMode::loopBack;
        s.lanes[0].midiReleaseMode = MidiReleaseMode::release;
        s.lanes[0].legato = true;
        s.lanes[0].midiLoopForever = false;
        s.lanes[0].midiSustain = true;
    }, false);

    expect (std::abs (patternBank (model.snapshot()->lanes[0]).settings[model.snapshot()->lanes[0].selectedPattern]
                      .modSpeed[0] + 0.35f) < 0.0001f,
            "active slot captures its Speed modulation assignment before serialization");

    const auto bytes = model.serialize();
    Model restored;
    expect (restored.deserialize (bytes.data(), bytes.size()), "state round-trips");
    expect (restored.snapshot()->activeLaneCount == 2 && restored.snapshot()->lanes[0].selectedPattern == 2,
            "restored state retains lanes and bank selection");
    expect (std::abs (restored.snapshot()->lanes[0].startPosition - 0.37f) < 0.0001f,
            "state round-trip retains the Start flag");
    expect (std::abs (restored.snapshot()->lanes[0].sustainPosition - 0.82f) < 0.0001f,
            "state round-trip retains the Sustain flag");
    expect (restored.snapshot()->editorGridX == 12 && restored.snapshot()->editorGridY == 6
            && restored.snapshot()->editorDrawMode == DrawMode::eraser,
            "state round-trip retains the LFO editor grid and drawing mode");
    expect (restored.snapshot()->lanes[0].midiTrigger,
            "state round-trip retains the per-LFO MIDI Trigger setting");
    expect (restored.snapshot()->lanes[0].positionSync,
            "state round-trip retains the per-pattern Position Sync setting");
    expect (restored.snapshot()->lanes[0].midiStartMode == MidiStartMode::loopBack
            && restored.snapshot()->lanes[0].midiReleaseMode == MidiReleaseMode::release
            && restored.snapshot()->lanes[0].legato
            && ! restored.snapshot()->lanes[0].midiLoopForever
            && restored.snapshot()->lanes[0].midiSustain,
            "state round-trip retains MIDI flag behavior");
    const auto restoredSpeedMod = restored.snapshot()->lanes[0].modSpeed[0];
    expect (std::abs (restoredSpeedMod + 0.35f) < 0.0001f,
            "state round-trip retains per-pattern Speed modulation assignments (got "
            + std::to_string (restoredSpeedMod) + ")");
    expect (patternBank (restored.snapshot()->lanes[0]).patterns[2].points[0].curve == CurveType::stairs,
            "state round-trip retains segment curve families");
    expect (patternBank (restored.snapshot()->lanes[0]).names[2] == "Saved Name"
            && std::abs (restored.snapshot()->lanes[0].baseValue - 0.33f) < 0.0001f
            && std::abs (restored.snapshot()->lanes[0].patternMix - 0.74f) < 0.0001f
            && restored.snapshot()->lanes[0].amountBipolar,
            "state round-trip retains pattern names, Base/Amount values, and Amount mode");
    expect (! restored.deserialize ("bad", 3), "corrupt state is rejected");

    restored.mutate ([] (ProjectState& s) { s.editorGridX = 99; s.editorGridY = 99; }, false);
    expect (restored.snapshot()->editorGridX == 48 && restored.snapshot()->editorGridY == 48,
            "grid resolution is capped at 48 divisions on both axes");

    restored.setParameterRealtime (0, Parameter::pattern, 65536);
    expect (restored.realtimeParameterValue (0, Parameter::pattern) == 65536,
            "automation writes to the lock-free realtime parameter layer");
    expect (restored.synchroniseRealtimeParameters() && restored.snapshot()->lanes[0].selectedPattern == 11,
            "GUI/state synchronization maps automation to the nearest occupied pattern without adding undo");

    for (int pattern = 0; pattern < patternsPerLane; ++pattern)
    {
        const auto& p = patternBank (restored.snapshot()->lanes[0]).patterns[pattern];
        expect (p.count >= 2 && p.points[0].x == 0.0f && p.points[p.count - 1].x == 1.0f,
                "factory patterns maintain valid endpoints");
    }

    if (failures == 0) std::cout << "All Pattern Bank core tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
