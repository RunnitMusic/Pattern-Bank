#include "Core.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace stepshaper
{
namespace
{
constexpr std::array<double, 27> divisionLengths {
    0.0416666667, 0.0625, 0.09375, 0.0833333333, 0.125, 0.1875,
    0.1666666667, 0.25, 0.375, 0.3333333333, 0.5, 0.75,
    0.6666666667, 1.0, 1.5, 1.3333333333, 2.0, 3.0,
    2.6666666667, 4.0, 6.0, 5.3333333333, 8.0, 12.0,
    10.6666666667, 16.0, 24.0
};
constexpr std::array<const char*, 27> divisionNames {
    "1/64 T", "1/64", "1/64 D", "1/32 T", "1/32", "1/32 D",
    "1/16 T", "1/16", "1/16 D", "1/8 T", "1/8", "1/8 D",
    "1/4 T", "1/4", "1/4 D", "1/2 T", "1/2", "1/2 D",
    "1 bar T", "1 bar", "1 bar D", "2 bars T", "2 bars", "2 bars D",
    "4 bars T", "4 bars", "4 bars D"
};

int migrateLegacyDivisionOrder (int division) noexcept
{
    division = std::clamp (division, 0, 26);
    constexpr std::array withinGroup { 1, 2, 0 }; // old normal/dotted/triplet -> new triplet/normal/dotted
    return (division / 3) * 3 + withinGroup[static_cast<std::size_t> (division % 3)];
}

template <typename T>
void appendPod (std::vector<std::uint8_t>& bytes, const T& value)
{
    static_assert (std::is_trivially_copyable_v<T>);
    const auto* begin = reinterpret_cast<const std::uint8_t*> (&value);
    bytes.insert (bytes.end(), begin, begin + sizeof (T));
}

template <typename T>
bool readPod (const std::uint8_t*& cursor, const std::uint8_t* end, T& value)
{
    static_assert (std::is_trivially_copyable_v<T>);
    if (static_cast<std::size_t> (end - cursor) < sizeof (T))
        return false;
    std::memcpy (&value, cursor, sizeof (T));
    cursor += sizeof (T);
    return true;
}

float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }

void constrainModulatedX (const Pattern& pattern, std::array<float, maxPoints>& xs) noexcept
{
    struct Block
    {
        int first = 0, last = 0, movingCount = 0;
        float movingSum = 0.0f, value = 0.0f, anchor = 0.0f;
        bool anchored = false;
    };

    std::array<Block, maxPoints> blocks {};
    int blockCount = 0;
    for (int point = 0; point < pattern.count; ++point)
    {
        const auto fixed = point == 0 || point + 1 == pattern.count
            || std::abs (xs[point] - pattern.points[point].x) < 0.000001f;
        auto& block = blocks[blockCount++];
        block.first = block.last = point;
        block.anchored = fixed;
        block.anchor = fixed ? pattern.points[point].x : 0.0f;
        block.movingCount = fixed ? 0 : 1;
        block.movingSum = fixed ? 0.0f : xs[point];
        block.value = fixed ? block.anchor : xs[point];

        while (blockCount >= 2 && blocks[blockCount - 2].value > blocks[blockCount - 1].value)
        {
            const auto right = blocks[--blockCount];
            auto& left = blocks[blockCount - 1];
            left.last = right.last;
            left.movingSum += right.movingSum;
            left.movingCount += right.movingCount;
            if (! left.anchored && right.anchored)
            {
                left.anchored = true;
                left.anchor = right.anchor;
            }
            left.value = left.anchored ? left.anchor
                                       : left.movingSum / static_cast<float> (std::max (1, left.movingCount));
        }
    }

    for (int block = 0; block < blockCount; ++block)
        for (int point = blocks[block].first; point <= blocks[block].last; ++point)
            xs[point] = clamp01 (blocks[block].value);
}

std::string defaultPatternName (int slot) { return "Pattern " + std::to_string (slot + 1); }

CurveType legacyCurveType (int type) noexcept
{
    switch (type)
    {
        case 1: return CurveType::hold;
        case 3: return CurveType::doubleCurve;
        case 4: return CurveType::halfSine;
        case 5: return CurveType::stairs;
        case 6: return CurveType::smoothStairs;
        case 7: case 10: return CurveType::pulse;
        case 8: return CurveType::sine;
        case 9: return CurveType::triangle;
        default: return CurveType::singleCurve;
    }
}

Pattern makePattern (std::initializer_list<Point> points)
{
    Pattern p;
    p.count = static_cast<std::uint8_t> (std::min<std::size_t> (points.size(), maxPoints));
    std::copy_n (points.begin(), p.count, p.points.begin());
    p.normalise();
    return p;
}

Pattern makeSteps (std::initializer_list<float> levels)
{
    Pattern pattern;
    const auto count = static_cast<int> (levels.size());
    int step = 0;
    for (const auto level : levels)
    {
        const auto left = static_cast<float> (step) / static_cast<float> (count);
        const auto right = static_cast<float> (step + 1) / static_cast<float> (count);
        if (pattern.count + 2 <= maxPoints)
        {
            pattern.points[pattern.count++] = { left, clamp01 (level), 0.0f, CurveType::singleCurve };
            pattern.points[pattern.count++] = { right, clamp01 (level), 0.0f, CurveType::singleCurve };
        }
        ++step;
    }
    pattern.normalise();
    return pattern;
}

Pattern makeDigitSteps (std::string_view digits)
{
    Pattern pattern;
    const auto count = static_cast<int> (std::min<std::size_t> (digits.size(), maxPoints / 2));
    for (int step = 0; step < count; ++step)
    {
        const auto level = digits[static_cast<std::size_t> (step)] >= '0'
                        && digits[static_cast<std::size_t> (step)] <= '9'
            ? static_cast<float> (digits[static_cast<std::size_t> (step)] - '0') / 9.0f : 0.0f;
        const auto left = static_cast<float> (step) / static_cast<float> (count);
        const auto right = static_cast<float> (step + 1) / static_cast<float> (count);
        pattern.points[pattern.count++] = { left, level, 0.0f, CurveType::singleCurve };
        pattern.points[pattern.count++] = { right, level, 0.0f, CurveType::singleCurve };
    }
    pattern.normalise();
    return pattern;
}

Pattern makePitchCode (std::string_view notes, int range = 12, int octavePattern = 0)
{
    Pattern pattern;
    constexpr std::array musicalStepCounts { 3, 4, 6, 8, 12, 16, 24, 32 };
    const auto requested = static_cast<int> (std::min<std::size_t> (notes.size(), maxPoints / 2));
    const auto count = *std::min_element (musicalStepCounts.begin(), musicalStepCounts.end(),
        [requested] (int a, int b) { return std::abs (a - requested) < std::abs (b - requested); });
    for (int step = 0; step < count; ++step)
    {
        const auto source = std::min (notes.size() - 1, static_cast<std::size_t> (step) * notes.size()
                                                       / static_cast<std::size_t> (count));
        auto semitones = std::clamp (static_cast<int> (notes[source] - 'M'), -12, 12);
        if (range == 24 && octavePattern != 0)
        {
            const auto phrase = std::max (1, count / 4);
            constexpr std::array octaveOffsets { -12, 0, 12, 0 };
            semitones += octaveOffsets[static_cast<std::size_t> ((step / phrase + octavePattern) % 4)];
        }
        semitones = std::clamp (semitones, -range, range);
        const auto level = clamp01 ((static_cast<float> (semitones) + static_cast<float> (range))
                                    / static_cast<float> (range * 2));
        const auto left = static_cast<float> (step) / static_cast<float> (count);
        const auto right = static_cast<float> (step + 1) / static_cast<float> (count);
        pattern.points[pattern.count++] = { left, level, 0.0f, CurveType::singleCurve };
        pattern.points[pattern.count++] = { right, level, 0.0f, CurveType::singleCurve };
    }
    pattern.normalise();
    return pattern;
}

Pattern makeStabPattern (std::string_view digits, float holdFraction = 0.38f,
                         float releaseEndFraction = 0.94f)
{
    Pattern pattern;
    const auto cells = static_cast<int> (std::min<std::size_t> (digits.size(), 32));
    holdFraction = std::clamp (holdFraction, 0.18f, 0.76f);
    releaseEndFraction = std::clamp (releaseEndFraction, holdFraction + 0.035f, 0.985f);
    pattern.points[pattern.count++] = { 0.0f, 0.0f, 0.0f, CurveType::singleCurve };
    for (int cell = 0; cell < cells && pattern.count + 3 < maxPoints; ++cell)
    {
        const auto digit = digits[static_cast<std::size_t> (cell)];
        if (digit <= '0' || digit > '9') continue;
        const auto level = static_cast<float> (digit - '0') / 9.0f;
        const auto left = static_cast<float> (cell) / static_cast<float> (cells);
        const auto width = 1.0f / static_cast<float> (cells);
        if (pattern.points[pattern.count - 1].x < left)
            pattern.points[pattern.count++] = { left, 0.0f, 0.0f, CurveType::singleCurve };
        pattern.points[pattern.count++] = { left + width * 0.035f, level, 0.0f, CurveType::singleCurve };
        pattern.points[pattern.count++] = { left + width * holdFraction, level, -0.48f, CurveType::singleCurve };
        pattern.points[pattern.count++] = { left + width * releaseEndFraction, 0.0f, 0.0f, CurveType::singleCurve };
    }
    if (pattern.points[pattern.count - 1].x < 1.0f && pattern.count < maxPoints)
        pattern.points[pattern.count++] = { 1.0f, 0.0f, 0.0f, CurveType::singleCurve };
    pattern.normalise();
    return pattern;
}

float pitchValue (float semitones, int range = 12) noexcept
{
    const auto span = static_cast<float> (std::clamp (range, 1, 24));
    return clamp01 ((semitones + span) / (span * 2.0f));
}

Pattern makeVectorSteps (const std::vector<float>& levels)
{
    Pattern pattern;
    const auto count = std::min (static_cast<int> (levels.size()), maxPoints / 2);
    for (int step = 0; step < count; ++step)
    {
        const auto left = static_cast<float> (step) / count;
        const auto right = static_cast<float> (step + 1) / count;
        pattern.points[pattern.count++] = { left, clamp01 (levels[static_cast<std::size_t> (step)]), 0.0f,
                                            CurveType::singleCurve };
        pattern.points[pattern.count++] = { right, clamp01 (levels[static_cast<std::size_t> (step)]), 0.0f,
                                            CurveType::singleCurve };
    }
    pattern.normalise();
    return pattern;
}

Pattern makeExperimentalPattern (FactoryBank bank, int variant, int pitchRange = 12)
{
    constexpr std::array curves { CurveType::sine, CurveType::saw, CurveType::triangle, CurveType::pulse,
                                  CurveType::stairs, CurveType::smoothStairs, CurveType::doubleCurve,
                                  CurveType::halfSine };
    Pattern pattern;
    constexpr int segments = 8;
    pattern.count = segments + 1;
    for (int point = 0; point <= segments; ++point)
    {
        const auto x = static_cast<float> (point) / segments;
        const auto pitchSemitones = ((point * 7 + variant * 5) % 25) - 12
                                  + (pitchRange == 24 ? ((point + variant) % 3 - 1) * 12 : 0);
        const auto pitch = pitchValue (static_cast<float> (pitchSemitones), pitchRange);
        const auto y = bank == FactoryBank::filters ? 0.12f + 0.76f * ((point * 3 + variant) % 8) / 7.0f
                     : bank == FactoryBank::volumeGates ? (((point + variant) % 3) == 0 ? 0.08f : 0.95f)
                     : pitch;
        auto& p = pattern.points[point];
        p = { x, y, ((point & 1) == 0 ? 0.78f : -0.72f),
              curves[static_cast<std::size_t> ((point + variant) % curves.size())] };
        if (point > 0 && point < segments)
        {
            p.modY[0] = ((point & 1) == 0 ? 0.18f : -0.18f);
            p.modTension[1] = ((point & 1) == 0 ? 0.45f : -0.45f);
        }
    }
    pattern.normalise();
    return pattern;
}

void addReleaseTimeMod (Pattern& pattern)
{
    for (int point = 0; point + 1 < pattern.count; ++point)
    {
        const auto& upper = pattern.points[point];
        auto& lower = pattern.points[point + 1];
        const auto width = lower.x - upper.x;
        const auto sharpRelease = upper.y - lower.y >= 0.35f && width >= 0.0f && width <= 0.16f;
        if (sharpRelease && lower.x < 0.999f)
            lower.modX[0] = std::max (lower.modX[0], 0.25f);
    }
}

Pattern makeFilterFactoryPattern (int variant)
{
    if (variant >= 48) return makeExperimentalPattern (FactoryBank::filters, variant);
    const auto family = variant / 8;
    const auto variation = variant % 8;
    Pattern pattern;
    if (family == 0) // full-range foundational LFO shapes rather than near-flat lines
    {
        switch (variation)
        {
            case 0: pattern = makePattern ({ { 0, .06f, -.18f }, { 1, .96f, 0 } }); break;
            case 1: pattern = makePattern ({ { 0, .96f, .18f }, { 1, .06f, 0 } }); break;
            case 2: pattern = makePattern ({ { 0, .08f, -.15f, CurveType::halfSine },
                                              { .5f, .96f, .15f, CurveType::halfSine }, { 1, .08f, 0 } }); break;
            case 3: pattern = makePattern ({ { 0, .92f, .12f, CurveType::doubleCurve },
                                              { .5f, .05f, -.12f, CurveType::doubleCurve }, { 1, .92f, 0 } }); break;
            case 4: pattern = makePattern ({ { 0, .10f, 0, CurveType::halfSine }, { .25f, .92f, 0, CurveType::halfSine },
                                              { .5f, .10f, 0, CurveType::halfSine }, { .75f, .92f, 0, CurveType::halfSine },
                                              { 1, .10f, 0 } }); break;
            case 5: pattern = makePattern ({ { 0, .10f, 0, CurveType::singleCurve }, { .125f, .90f, 0, CurveType::singleCurve },
                                              { .375f, .24f, 0, CurveType::singleCurve }, { .5f, .96f, 0, CurveType::singleCurve },
                                              { .75f, .12f, 0, CurveType::singleCurve }, { .875f, .78f, 0, CurveType::singleCurve },
                                              { 1, .10f, 0 } }); break;
            case 6: pattern = makeSteps ({ .10f, .92f, .36f, .78f, .18f, .96f, .48f, .12f }); break;
            default: pattern = makeSteps ({ .08f, .08f, .90f, .90f, .28f, .72f, .96f, .16f }); break;
        }
    }
    else if (family == 1) // distinct low-pass plucks, answers, pumps, and stepped gestures
    {
        switch (variation)
        {
            case 0: pattern = makePattern ({ { 0, .05f, -.72f }, { .09f, .96f, .42f, CurveType::halfSine },
                                              { .72f, .12f, -.10f }, { 1, .05f, 0 } }); break;
            case 1: pattern = makePattern ({ { 0, .82f, .50f, CurveType::doubleCurve }, { .28f, .18f, -.54f },
                                              { .42f, .91f, .32f, CurveType::halfSine }, { 1, .08f, 0 } }); break;
            case 2: pattern = makePattern ({ { 0, .08f, -.62f }, { .07f, .93f, .28f }, { .31f, .14f, -.58f },
                                              { .39f, .78f, .18f }, { .62f, .20f, -.30f }, { 1, .08f, 0 } }); break;
            case 3: pattern = makePattern ({ { 0, .10f, -.35f }, { .18f, .68f, .12f, CurveType::doubleCurve },
                                              { .50f, .30f, -.60f }, { .58f, .98f, .36f, CurveType::halfSine },
                                              { .88f, .16f, -.10f }, { 1, .10f, 0 } }); break;
            case 4: pattern = makeSteps ({ .08f, .92f, .62f, .24f, .78f, .18f, .48f, .10f }); break;
            case 5: pattern = makePattern ({ { 0, .06f, -.75f }, { .08f, .94f, .25f }, { .22f, .94f, .18f },
                                              { .47f, .22f, -.45f }, { .74f, .68f, .22f }, { 1, .08f, 0 } }); break;
            case 6: pattern = makePattern ({ { 0, .14f, -.18f }, { .25f, .86f, .18f, CurveType::smoothStairs },
                                              { .50f, .26f, -.22f, CurveType::smoothStairs },
                                              { .75f, .72f, .18f, CurveType::smoothStairs }, { 1, .12f, 0 } }); break;
            default: pattern = makePattern ({ { 0, .08f, -.70f }, { .06f, .96f, .30f }, { .20f, .18f, -.55f },
                                               { .66f, .18f, -.65f }, { .73f, .82f, .22f }, { .90f, .30f, -.20f },
                                               { 1, .08f, 0 } }); break;
        }
    }
    else if (family == 2) // band-pass/formant motion: scans, vowels, steps, and alternating bands
    {
        switch (variation)
        {
            case 0: pattern = makePattern ({ { 0, .28f, -.35f, CurveType::doubleCurve }, { .22f, .86f, .24f, CurveType::halfSine },
                                              { .55f, .18f, -.18f }, { 1, .60f, 0 } }); break;
            case 1: pattern = makePattern ({ { 0, .70f, .28f, CurveType::halfSine }, { .30f, .24f, -.22f },
                                              { .50f, .78f, .20f, CurveType::doubleCurve }, { .76f, .36f, -.18f }, { 1, .70f, 0 } }); break;
            case 2: pattern = makeSteps ({ .22f, .52f, .82f, .44f, .18f, .64f, .34f, .74f }); break;
            case 3: pattern = makePattern ({ { 0, .18f, -.15f, CurveType::sine }, { .50f, .84f, .15f, CurveType::sine }, { 1, .18f, 0 } }); break;
            case 4: pattern = makePattern ({ { 0, .35f, -.48f }, { .10f, .82f, .30f }, { .34f, .48f, -.30f },
                                              { .58f, .76f, .22f }, { .84f, .20f, -.22f }, { 1, .35f, 0 } }); break;
            case 5: pattern = makeSteps ({ .28f, .72f, .28f, .72f, .48f, .18f, .58f, .38f }); break;
            case 6: pattern = makePattern ({ { 0, .18f, .24f, CurveType::triangle }, { .50f, .82f, -.24f, CurveType::triangle }, { 1, .18f, 0 } }); break;
            default: pattern = makePattern ({ { 0, .16f, -.35f }, { .12f, .88f, .22f }, { .25f, .30f, -.20f },
                                               { .50f, .62f, .18f, CurveType::smoothStairs }, { .75f, .24f, -.24f },
                                               { .88f, .78f, .20f }, { 1, .16f, 0 } }); break;
        }
    }
    else if (family == 3) // one-to-four-cycle phaser sweeps
    {
        const auto cycles = 1 + variation % 4;
        pattern.count = static_cast<std::uint8_t> (cycles * 2 + 1);
        for (int point = 0; point < pattern.count; ++point)
            pattern.points[point] = { static_cast<float> (point) / (cycles * 2),
                                      point & 1 ? .82f : .18f,
                                      (variation >= 4 ? (point & 1 ? .18f : -.18f) : 0.0f),
                                      variation & 1 ? CurveType::doubleCurve : CurveType::halfSine };
        pattern.normalise();
    }
    else if (family == 4) // rhythmic filter accents with deliberately different rhythmic silhouettes
    {
        constexpr std::array<std::string_view, 8> rhythms { "9000500090007000", "0900090007000900",
            "9070005090700030", "9009000070005009", "0907009000507000", "9000703090000507",
            "7090003090507000", "9030700090007050" };
        pattern = makeStabPattern (rhythms[static_cast<std::size_t> (variation)],
                                   .24f + .07f * (variation % 4), .84f + .04f * (variation % 3));
        for (int point = 0; point + 1 < pattern.count; ++point)
            if (pattern.points[point].y > .25f && pattern.points[point + 1].y < .05f)
            {
                pattern.points[point].curve = variation & 1 ? CurveType::doubleCurve : CurveType::halfSine;
                pattern.points[point].tension = -.18f - .08f * (variation % 3);
            }
    }
    else // evolving motions: long arcs, stepped plateaus, asymmetry, and multi-stage travel
    {
        switch (variation)
        {
            case 0: pattern = makePattern ({ { 0, .08f, -.45f }, { .18f, .72f, .18f, CurveType::doubleCurve },
                                              { .58f, .92f, .20f, CurveType::halfSine }, { 1, .16f, 0 } }); break;
            case 1: pattern = makePattern ({ { 0, .88f, .32f }, { .30f, .24f, -.18f, CurveType::smoothStairs },
                                              { .66f, .58f, .18f, CurveType::smoothStairs }, { 1, .10f, 0 } }); break;
            case 2: pattern = makeSteps ({ .10f, .10f, .36f, .36f, .78f, .52f, .92f, .18f }); break;
            case 3: pattern = makePattern ({ { 0, .20f, -.30f, CurveType::sine }, { .34f, .80f, .28f, CurveType::sine },
                                              { .70f, .34f, -.20f, CurveType::sine }, { 1, .68f, 0 } }); break;
            case 4: pattern = makePattern ({ { 0, .12f, -.60f }, { .08f, .90f, .35f }, { .38f, .26f, -.10f },
                                              { .72f, .62f, .25f, CurveType::doubleCurve }, { 1, .18f, 0 } }); break;
            case 5: pattern = makePattern ({ { 0, .48f, .20f, CurveType::smoothStairs }, { .32f, .82f, -.22f, CurveType::smoothStairs },
                                              { .62f, .18f, .20f, CurveType::smoothStairs }, { 1, .56f, 0 } }); break;
            case 6: pattern = makePattern ({ { 0, .10f, -.20f, CurveType::triangle }, { .50f, .86f, .20f, CurveType::triangle },
                                              { 1, .10f, 0 } }); break;
            default: pattern = makePattern ({ { 0, .16f, -.30f }, { .14f, .64f, .16f }, { .40f, .30f, -.30f },
                                               { .52f, .94f, .25f }, { .80f, .44f, -.18f }, { 1, .16f, 0 } }); break;
        }
    }
    if (variant % 4 == 0 && pattern.count > 2) pattern.points[1].modY[0] = 0.18f;
    if (variant % 6 == 0) pattern.points[0].modTension[1] = 0.28f;
    return pattern;
}

Pattern makeVolumeFactoryPattern (int variant)
{
    if (variant >= 48) return makeExperimentalPattern (FactoryBank::volumeGates, variant);
    const auto family = variant / 8;
    const auto variation = variant % 8;
    const auto cells = family == 5 ? 32 : 16;
    std::string rhythm (static_cast<std::size_t> (cells), '0');
    const auto hit = [&] (int position, int level = 9)
    {
        position %= cells;
        rhythm[static_cast<std::size_t> (position)] = static_cast<char> ('0' + std::clamp (level, 1, 9));
    };
    if (family == 0) // techno pulse and pump foundations
    {
        for (int p = 0; p < cells; p += 4) hit (p, 9);
        if (variation & 1) for (int p = 2; p < cells; p += 4) hit (p, 6);
        if (variation >= 4) hit (15, 5);
    }
    else if (family == 1) // rave chord stabs and offbeats
    {
        for (int p = variation & 1 ? 2 : 0; p < cells; p += 4) hit (p, 9);
        if (variation >= 2) hit (7, 7);
        if (variation >= 4) hit (11, 6);
        if (variation >= 6) hit (14, 5);
    }
    else if (family == 2) // medium-syncopation DnB kick/snare skeletons
    {
        hit (0, 9); hit (4, 9); hit (10, 8); hit (12, 9);
        if (variation & 1) hit (7, 5);
        if (variation >= 2) hit (14, 6);
        if (variation >= 4) hit (2, 5);
        if (variation >= 6) hit (9, 5);
    }
    else if (family == 3) // 3-3-2, gallops and loop chops
    {
        const auto spacing = variation < 4 ? 3 : 6;
        for (int p = 0; p < cells; p += spacing) hit (p, p == 0 ? 9 : 7);
        if (variation & 1) hit (8, 9);
        if (variation >= 6) hit (15, 5);
    }
    else if (family == 4) // sparse stabs with one or two glitch peppers
    {
        hit (0, 9); hit (variation & 1 ? 6 : 8, 8); hit (12, 9);
        if (variation >= 2) hit (3 + variation, 5);
        if (variation >= 6) hit (15, 4);
    }
    else // longer two-bar break/rave combinations
    {
        for (int p : { 0, 8, 16, 24 }) hit (p, 9);
        hit (10 + variation % 3, 6); hit (20 + variation, 7);
        if (variation & 1) hit (30, 5);
    }
    const auto holdFraction = family == 0 ? 0.30f + 0.08f * (variation % 4)
                            : family == 1 ? 0.42f + 0.08f * (variation % 4)
                            : family == 2 ? 0.28f + 0.08f * (variation % 5)
                            : family == 3 ? 0.24f + 0.09f * (variation % 5)
                            : family == 4 ? 0.48f + 0.08f * (variation % 4)
                                          : 0.38f + 0.07f * (variation % 5);
    const auto releaseEnd = variation % 4 == 0 ? 0.96f
                          : variation % 4 == 1 ? holdFraction + 0.055f
                          : variation % 4 == 2 ? 0.86f : 0.72f;
    auto pattern = makeStabPattern (rhythm, holdFraction, releaseEnd);
    int shapedReleases = 0;
    for (int point = 0; point + 1 < pattern.count; ++point)
        if (pattern.points[point].y > 0.4f && pattern.points[point + 1].y < 0.1f)
        {
            const auto releaseStyle = variation % 4 == 3 ? shapedReleases % 2 : variation % 4;
            pattern.points[point].curve = releaseStyle == 0 ? CurveType::halfSine
                                        : releaseStyle == 1 ? CurveType::singleCurve
                                                            : CurveType::doubleCurve;
            pattern.points[point].tension = releaseStyle == 1 ? 0.0f : -0.24f - 0.08f * (variation % 3);
            if (variant % 5 == 0 && shapedReleases == 0) pattern.points[point].modTension[0] = 0.30f;
            ++shapedReleases;
        }
    return pattern;
}

void addMelodicModMappings (Pattern& pattern, int variant, int range)
{
    if (pattern.count < 3) return;
    const auto pitchStep = 1.0f / static_cast<float> (range * 2);
    int group = 0;
    for (int first = 0; first < pattern.count;)
    {
        auto last = first;
        while (last + 1 < pattern.count
               && std::abs (pattern.points[last + 1].x - pattern.points[first].x) < 0.0002f) ++last;
        if (first > 0 && last + 1 < pattern.count)
        {
            const auto direction = ((group + variant) & 1) == 0 ? 1.0f : -1.0f;
            const auto semitones = 2 + ((group + variant) % 3 == 0 ? 5 : (group + variant) % 3);
            for (int point = first; point <= last; ++point)
                pattern.points[point].modY[0] = direction * semitones * pitchStep;

            const auto leftGap = pattern.points[first].x - pattern.points[first - 1].x;
            const auto rightGap = pattern.points[last + 1].x - pattern.points[last].x;
            const auto shift = std::max (0.0f, std::min (leftGap, rightGap) * 0.32f)
                             * ((((group * 3 + variant) & 1) == 0) ? 1.0f : -1.0f);
            for (int point = first; point <= last; ++point)
                pattern.points[point].modX[1] = shift;
            ++group;
        }
        first = last + 1;
    }

    // Continuous phrases often have no duplicate-X note boundaries, so seed alternating melodic/timing routes.
    if (group < 2)
        for (int point = 1; point + 1 < pattern.count; point += 2)
        {
            const auto direction = ((point + variant) & 2) == 0 ? 1.0f : -1.0f;
            pattern.points[point].modY[0] = direction * (3 + (point + variant) % 4) * pitchStep;
            const auto gap = std::min (pattern.points[point].x - pattern.points[point - 1].x,
                                       pattern.points[point + 1].x - pattern.points[point].x);
            pattern.points[point].modX[1] = direction * std::max (0.0f, gap * 0.28f);
        }
}

Pattern makePitchFactoryPattern (int variant, int range)
{
    const auto bank = range == 24 ? FactoryBank::melodic24 : FactoryBank::melodic12;
    if (variant >= 48) return makeExperimentalPattern (bank, variant, range);
    const std::vector<int> major = range == 24
        ? std::vector<int> { -24, -20, -17, -12, -8, -5, 0, 12 }
        : std::vector<int> { -12, -5, 0, 4, 7, 12 };
    const std::vector<int> minor = range == 24
        ? std::vector<int> { -24, -21, -17, -12, -9, -5, 0, 12 }
        : std::vector<int> { -12, -5, 0, 3, 7, 12 };
    const auto family = variant / 8;
    const auto variation = variant % 8;
    std::vector<float> levels;
    const auto add = [&] (int semitone)
    {
        levels.push_back (pitchValue (static_cast<float> (std::clamp (semitone, -range, range)), range));
    };
    if (family == 0) // 6-step dotted or 8-step straight chord traversal
    {
        const auto& chord = variation & 1 ? minor : major;
        if (variation < 2)
        {
            for (std::size_t note = 0; note < chord.size(); ++note)
                add (range == 24 && note + 1 == chord.size() ? 24 : chord[note]);
        }
        else if (variation < 4) for (auto it = chord.rbegin(); it != chord.rend(); ++it) add (*it);
        else if (variation < 6)
        {
            for (const auto note : chord) add (note);
            for (auto it = chord.rbegin(); it != chord.rend(); ++it) add (*it);
        }
        else
        {
            for (std::size_t i = 0; i < chord.size(); ++i)
                add (chord[i & 1 ? chord.size() - 1 - i / 2 : i / 2]);
        }
    }
    else if (family == 1) // EDM root/fifth/octave figures
    {
        const std::vector<int> figures = range == 24
            ? std::vector<int> { -12, 0, 7, 12, 24, 12, 7, 0 }
            : std::vector<int> { 0, 7, 12, 7, 0, -5, 0, 7 };
        for (int i = 0; i < 8; ++i) add (figures[static_cast<std::size_t> ((i + variation) % 8)]);
    }
    else if (family == 2) // loop warps use only 8- or 12-step grids
    {
        const std::vector<int> warp = range == 24
            ? std::vector<int> { -24, -12, 0, 7, 12, 24, 12, 0, -5, -12, 0, 12 }
            : std::vector<int> { -12, 0, 7, 0, 12, 7, 0, -5, 0, 7, 12, 0 };
        const auto count = variation % 4 < 2 ? 8 : 12;
        for (int i = 0; i < count; ++i) add (warp[static_cast<std::size_t> ((i + variation) % 12)]);
    }
    else if (family == 3) // eight-step scalar EDM hooks
    {
        const auto& scale = variation & 1 ? minor : major;
        constexpr std::array alternate { 0, 2, 1, 3, 2, 4, 3, 5 };
        for (int i = 0; i < 8; ++i)
        {
            const auto contour = variation < 2 ? (i < 4 ? i : 7 - i)
                               : variation < 4 ? i % 6
                               : variation < 6 ? 5 - (i % 6)
                                               : alternate[static_cast<std::size_t> (i)];
            const auto index = std::clamp (contour, 0, static_cast<int> (scale.size()) - 1);
            add (scale[static_cast<std::size_t> (index)]);
        }
    }
    else // singer-like phrases: locked notes, selected slides, and one controlled vibrato cell
    {
        constexpr std::array vocal { 0, 2, 3, 5, 7, 5, 3, 2, 0, -2, 0, 3 };
        const auto segments = family == 5 ? 12 : 8;
        std::vector<int> notes;
        notes.reserve (static_cast<std::size_t> (segments));
        for (int cell = 0; cell < segments; ++cell)
        {
            auto semitone = vocal[static_cast<std::size_t> ((cell + variation) % vocal.size())];
            if (range == 24 && family == 5)
                semitone += cell < segments / 3 ? -12 : cell >= segments * 2 / 3 ? 12 : 0;
            notes.push_back (std::clamp (semitone, -range, range));
        }
        Pattern pattern;
        const auto addPoint = [&] (float x, int semitone, float tension = 0.0f,
                                    CurveType curve = CurveType::singleCurve)
        {
            if (pattern.count >= maxPoints) return;
            pattern.points[pattern.count++] = { x, pitchValue (static_cast<float> (semitone), range), tension, curve };
        };
        const auto vibratoCell = (2 + variation) % segments;
        const auto slideA = (variation + 1) % (segments - 1);
        const auto slideB = family == 5 ? (variation + 7) % (segments - 1) : -1;
        int vibratoPeakPoint = -1;
        for (int cell = 0; cell < segments; ++cell)
        {
            const auto left = static_cast<float> (cell) / segments;
            const auto right = static_cast<float> (cell + 1) / segments;
            const auto width = right - left;
            const auto note = notes[static_cast<std::size_t> (cell)];
            addPoint (left, note);
            if (cell == vibratoCell)
            {
                addPoint (left + width * .25f, note, 0.0f, CurveType::halfSine);
                vibratoPeakPoint = pattern.count;
                addPoint (left + width * .50f, std::min (range, note + 1), 0.0f, CurveType::halfSine);
                addPoint (left + width * .75f, std::max (-range, note - 1), 0.0f, CurveType::halfSine);
            }
            const auto slides = cell + 1 < segments && (cell == slideA || cell == slideB);
            if (slides)
                addPoint (right - width * .25f, note,
                          variation & 1 ? .10f : -.10f, CurveType::halfSine);
            else
                addPoint (right, note);
        }
        pattern.normalise();
        if (vibratoPeakPoint >= 0 && vibratoPeakPoint < pattern.count)
            pattern.points[vibratoPeakPoint].modY[0] = 1.5f / static_cast<float> (range * 2);
        if (variation % 3 == 0 && pattern.count > 5)
            pattern.points[pattern.count / 3].modY[1] = 2.0f / static_cast<float> (range * 2);
        addMelodicModMappings (pattern, variant, range);
        return pattern;
    }
    auto pattern = makeVectorSteps (levels);
    addMelodicModMappings (pattern, variant, range);
    return pattern;
}

Pattern makeAdvancedFactoryPattern (FactoryBank bank, int variant)
{
    if (bank == FactoryBank::filters) return makeFilterFactoryPattern (variant);
    if (bank == FactoryBank::volumeGates) return makeVolumeFactoryPattern (variant);
    return makePitchFactoryPattern (variant, bank == FactoryBank::melodic24 ? 24 : 12);
}

std::string advancedFactoryName (FactoryBank bank, int variant)
{
    if (variant >= 48) return bank == FactoryBank::filters ? "Experimental Filter Lab " + std::to_string (variant - 47)
                             : bank == FactoryBank::volumeGates ? "Experimental Gate Lab " + std::to_string (variant - 47)
                             : "Experimental Melody Lab " + std::to_string (variant - 47);
    constexpr std::array filterFamilies { "Classic LFO", "LP Sweep", "Bandpass Motion",
                                          "Phaser Sweep", "Rhythmic Filter", "Evolving Filter" };
    constexpr std::array volumeFamilies { "Techno Pulse", "Rave Stab", "DnB Break Gate",
                                          "Loop Chop", "Sparse Stab", "Long Break Gate" };
    constexpr std::array pitchFamilies { "Classic Arp", "EDM Fifth Arp", "Octave Warp",
                                         "EDM Hook", "Vocal Phrase", "Long Vocal Run" };
    const auto family = variant / 8;
    const auto variation = variant % 8 + 1;
    const auto* name = bank == FactoryBank::filters ? filterFamilies[static_cast<std::size_t> (family)]
                     : bank == FactoryBank::volumeGates ? volumeFamilies[static_cast<std::size_t> (family)]
                     : pitchFamilies[static_cast<std::size_t> (family)];
    return std::string (name) + " " + std::to_string (variation);
}

int occupiedPatternCount (const LaneState& lane) noexcept
{
    return static_cast<int> (std::count (patternBank (lane).occupied.begin(),
                                         patternBank (lane).occupied.end(), true));
}

int occupiedPatternOrdinal (const LaneState& lane) noexcept
{
    int ordinal = 0;
    const auto& bank = patternBank (lane);
    for (int slot = 0; slot < patternsPerLane; ++slot)
    {
        if (! bank.occupied[slot]) continue;
        if (slot == lane.selectedPattern) return ordinal;
        ++ordinal;
    }
    return 0;
}

int occupiedPatternSlot (const LaneState& lane, int ordinal) noexcept
{
    const auto& bank = patternBank (lane);
    ordinal = std::clamp (ordinal, 0, std::max (0, occupiedPatternCount (lane) - 1));
    for (int slot = 0; slot < patternsPerLane; ++slot)
        if (bank.occupied[slot] && ordinal-- == 0) return slot;
    return 0;
}

PatternSettings settingsFromLane (const LaneState& lane) noexcept
{
    return { lane.sync, lane.speedHz, lane.division, lane.timingFeel, lane.changeMode,
             lane.startPosition, lane.sustainPosition, lane.midiTrigger, lane.baseValue,
             lane.patternMix, lane.mod1, lane.mod2, lane.modSpeed, lane.positionSync,
             lane.midiStartMode, lane.midiReleaseMode, lane.legato,
             lane.midiLoopForever, lane.midiSustain, lane.amountBipolar };
}

void applySettingsToLane (LaneState& lane, const PatternSettings& settings) noexcept
{
    lane.sync = settings.sync;
    lane.speedHz = settings.speedHz;
    lane.division = settings.division;
    lane.timingFeel = settings.timingFeel;
    lane.changeMode = settings.changeMode;
    lane.startPosition = settings.startPosition;
    lane.sustainPosition = settings.sustainPosition;
    lane.midiTrigger = settings.midiTrigger;
    lane.baseValue = settings.baseValue;
    lane.patternMix = settings.amount;
    lane.mod1 = settings.mod1;
    lane.mod2 = settings.mod2;
    lane.modSpeed = settings.modSpeed;
    lane.positionSync = settings.positionSync;
    lane.midiStartMode = settings.midiStartMode;
    lane.midiReleaseMode = settings.midiReleaseMode;
    lane.legato = settings.legato;
    lane.midiLoopForever = settings.midiLoopForever;
    lane.midiSustain = settings.midiSustain;
    lane.amountBipolar = settings.amountBipolar;
}

PatternSettings validatedSettings (PatternSettings settings) noexcept
{
    settings.speedHz = std::clamp (settings.speedHz, 0.01f, 20.0f);
    settings.division = std::clamp (settings.division, 0, 26);
    settings.timingFeel = static_cast<TimingFeel> (std::clamp (static_cast<int> (settings.timingFeel), 0, 2));
    settings.changeMode = static_cast<ChangeMode> (std::clamp (static_cast<int> (settings.changeMode), 0,
                                                               static_cast<int> (ChangeMode::count) - 1));
    settings.startPosition = clamp01 (settings.startPosition);
    settings.sustainPosition = std::clamp (settings.sustainPosition, settings.startPosition, 1.0f);
    settings.baseValue = clamp01 (settings.baseValue);
    settings.amount = std::clamp (settings.amount, -1.0f, 1.0f);
    settings.mod1 = clamp01 (settings.mod1);
    settings.mod2 = clamp01 (settings.mod2);
    for (auto& depth : settings.modSpeed) depth = std::clamp (depth, -1.0f, 1.0f);
    settings.midiStartMode = static_cast<MidiStartMode> (std::clamp (static_cast<int> (settings.midiStartMode), 0, 1));
    settings.midiReleaseMode = static_cast<MidiReleaseMode> (std::clamp (static_cast<int> (settings.midiReleaseMode), 0, 1));
    if (settings.midiLoopForever) settings.midiSustain = false;
    return settings;
}
} // namespace

float curveSegment (float start, float end, float phase, float tension, CurveType type) noexcept
{
    const auto t = clamp01 (phase);
    const auto bend = -std::clamp (tension, -1.0f, 1.0f);
    const auto curveWithBend = [] (float value, float amount)
    {
        return amount >= 0.0f
        ? std::pow (value, 1.0f + amount * 4.0f)
        : 1.0f - std::pow (1.0f - value, 1.0f - amount * 4.0f);
    };
    float shaped = curveWithBend (t, bend);
    constexpr auto pi = 3.14159265358979323846f;
    switch (type)
    {
        case CurveType::hold: shaped = t >= 1.0f ? 1.0f : 0.0f; break;
        case CurveType::doubleCurve:
            shaped = t < 0.5f ? 0.5f * curveWithBend (t * 2.0f, bend)
                              : 0.5f + 0.5f * curveWithBend ((t - 0.5f) * 2.0f, -bend);
            break;
        case CurveType::halfSine: shaped = std::sin (t * pi * 0.5f); break;
        case CurveType::stairs:
        {
            const auto steps = 2.0f + std::round (std::abs (tension) * 30.0f);
            shaped = t >= 1.0f ? 1.0f : std::floor (t * steps) / steps;
            break;
        }
        case CurveType::smoothStairs:
        {
            const auto steps = 2.0f + std::round (std::abs (tension) * 30.0f);
            const auto scaled = t * steps;
            const auto cell = std::floor (scaled);
            const auto local = scaled - cell;
            shaped = (cell + local * local * (3.0f - 2.0f * local)) / steps;
            break;
        }
        case CurveType::sine:
        {
            const auto extraCycles = std::round (std::abs (tension) * 15.0f);
            const auto halfWaves = 1.0f + extraCycles * 2.0f;
            shaped = 0.5f - 0.5f * std::cos (t * pi * halfWaves);
            break;
        }
        case CurveType::pulse:
        case CurveType::triangle:
        case CurveType::saw:
        {
            const auto repetitions = 1.0f + std::round (std::abs (tension) * 15.0f);
            const auto local = t >= 1.0f ? 1.0f : std::fmod (t * repetitions, 1.0f);
            if (type == CurveType::pulse) shaped = local < 0.5f ? 0.0f : 1.0f;
            else if (type == CurveType::triangle) shaped = local < 0.5f ? local * 2.0f : (1.0f - local) * 2.0f;
            else shaped = local;
            if (t >= 1.0f) shaped = 1.0f;
            break;
        }
        case CurveType::singleCurve:
        case CurveType::count: break;
    }
    return clamp01 (start + (end - start) * shaped);
}

float Pattern::valueAt (float phase) const noexcept
{
    if (count == 0) return 0.5f;
    if (count == 1) return points[0].y;
    phase = clamp01 (phase);
    std::size_t right = 1;
    while (right + 1 < count && phase >= points[right].x) ++right;
    const auto& a = points[right - 1]; const auto& b = points[right];
    const auto width = b.x - a.x;
    if (width <= 0.000001f) return b.y;
    return curveSegment (a.y, b.y, (phase - a.x) / width, a.tension, a.curve);
}

float Pattern::valueAt (float phase, float mod1, float mod2) const noexcept
{
    if (std::abs (mod1) < 0.000001f && std::abs (mod2) < 0.000001f) return valueAt (phase);
    if (count == 0) return 0.5f;
    if (count == 1) return points[0].y;
    phase = clamp01 (phase);
    std::array<float, maxPoints> xs {}, ys {}, tensions {};
    for (int i = 0; i < count; ++i)
    {
        xs[i] = clamp01 (points[i].x + mod1 * points[i].modX[0] + mod2 * points[i].modX[1]);
        ys[i] = clamp01 (points[i].y + mod1 * points[i].modY[0] + mod2 * points[i].modY[1]);
        tensions[i] = std::clamp (points[i].tension + mod1 * points[i].modTension[0]
                                  + mod2 * points[i].modTension[1], -1.0f, 1.0f);
    }
    xs[0] = 0.0f; xs[count - 1] = 1.0f;
    // Project all instantaneous X positions onto a nondecreasing sequence.
    // Colliding moving points meet at the same X; unmodulated adjacent points
    // are hard anchors and never get pushed by a modulation assignment.
    constrainModulatedX (*this, xs);
    std::size_t right = 1;
    while (right + 1 < count && phase >= xs[right])
        ++right;
    const auto& a = points[right - 1];
    const auto width = xs[right] - xs[right - 1];
    if (width <= 0.000001f) return ys[right];
    return curveSegment (ys[right - 1], ys[right], (phase - xs[right - 1]) / width,
                         tensions[right - 1], a.curve);
}

Pattern Pattern::withModulation (float mod1, float mod2) const noexcept
{
    auto result = *this;
    if (count == 0 || (std::abs (mod1) < 0.000001f && std::abs (mod2) < 0.000001f))
        return result;

    for (int i = 0; i < count; ++i)
    {
        const auto& source = points[i];
        auto& target = result.points[i];
        target.x = clamp01 (source.x + mod1 * source.modX[0] + mod2 * source.modX[1]);
        target.y = clamp01 (source.y + mod1 * source.modY[0] + mod2 * source.modY[1]);
        target.tension = std::clamp (source.tension + mod1 * source.modTension[0]
                                     + mod2 * source.modTension[1], -1.0f, 1.0f);
    }
    result.points[0].x = 0.0f;
    result.points[count - 1].x = 1.0f;
    std::array<float, maxPoints> xs {};
    for (int i = 0; i < count; ++i) xs[i] = result.points[i].x;
    constrainModulatedX (*this, xs);
    for (int i = 0; i < count; ++i) result.points[i].x = xs[i];
    return result;
}

void Pattern::normalise() noexcept
{
    count = static_cast<std::uint8_t> (std::clamp<int> (count, 2, maxPoints));
    std::stable_sort (points.begin(), points.begin() + count,
                      [] (const Point& a, const Point& b) { return a.x < b.x; });
    points[0].x = 0.0f;
    points[count - 1].x = 1.0f;
    for (int i = 0; i < count; ++i)
    {
        points[i].x = clamp01 (points[i].x);
        points[i].y = clamp01 (points[i].y);
        points[i].tension = std::clamp (points[i].tension, -1.0f, 1.0f);
        for (int source = 0; source < 2; ++source)
        {
            points[i].modX[source] = std::clamp (points[i].modX[source], -1.0f, 1.0f);
            points[i].modY[source] = std::clamp (points[i].modY[source], -1.0f, 1.0f);
            points[i].modTension[source] = std::clamp (points[i].modTension[source], -1.0f, 1.0f);
        }
        points[i].curve = static_cast<CurveType> (std::clamp (static_cast<int> (points[i].curve), 0,
                                                              static_cast<int> (CurveType::count) - 1));
    }
}

Pattern Pattern::factory (int index)
{
    switch ((index % 12 + 12) % 12)
    {
        case 0: return makePattern ({ { 0, 0, 0 }, { 1, 1, 0 } });
        case 1: return makePattern ({ { 0, 1, 0 }, { 1, 0, 0 } });
        case 2: return makePattern ({ { 0, .5f, -.65f }, { .25f, 1, .65f }, { .75f, 0, -.65f }, { 1, .5f, 0 } });
        case 3: return makePattern ({ { 0, 0, 0 }, { .5f, 1, 0 }, { 1, 0, 0 } });
        case 4: return makePattern ({ { 0, 0, -1 }, { .5f, 1, 1 }, { 1, 0, 0 } });
        case 5: return makePattern ({ { 0, 0, 1 }, { .499f, 0, 0 }, { .5f, 1, 1 }, { 1, 1, 0 } });
        case 6: return makePattern ({ { 0, 1, 1 }, { .249f, 1, 0 }, { .25f, .25f, 1 }, { .499f, .25f, 0 }, { .5f, .75f, 1 }, { .749f, .75f, 0 }, { .75f, 0, 1 }, { 1, 0, 0 } });
        case 7: return makePattern ({ { 0, 0, .8f }, { .2f, 1, .8f }, { .4f, .1f, .8f }, { .6f, .8f, .8f }, { .8f, .2f, .8f }, { 1, 0, 0 } });
        case 8: return makePattern ({ { 0, .5f, 0 }, { .125f, 1, 0 }, { .25f, .5f, 0 }, { .375f, 0, 0 }, { .5f, .5f, 0 }, { .625f, 1, 0 }, { .75f, .5f, 0 }, { .875f, 0, 0 }, { 1, .5f, 0 } });
        case 9: return makePattern ({ { 0, 0, -.8f }, { .33f, 1, -.8f }, { .66f, .35f, -.8f }, { 1, 0, 0 } });
        case 10: return makePattern ({ { 0, 1, .8f }, { .25f, .1f, .8f }, { .5f, .8f, .8f }, { .75f, .2f, .8f }, { 1, 1, 0 } });
        default: return makePattern ({ { 0, .5f, 0 }, { .2f, .85f, -.4f }, { .45f, .15f, .4f }, { .7f, .75f, -.4f }, { 1, .5f, 0 } });
    }
}

const PatternBank& patternBank (const LaneState& lane) noexcept
{
    static const auto fallback = []
    {
        auto bank = std::make_shared<PatternBank>();
        for (int i = 0; i < patternsPerLane; ++i)
        {
            bank->patterns[i] = Pattern::factory (i);
            bank->occupied[i] = i < 12;
            bank->names[i] = defaultPatternName (i);
        }
        return bank;
    }();
    return lane.bank != nullptr ? *lane.bank : *fallback;
}

PatternBank& editPatternBank (LaneState& lane)
{
    auto copy = lane.bank != nullptr ? std::make_shared<PatternBank> (*lane.bank)
                                     : std::make_shared<PatternBank>();
    auto& result = *copy;
    lane.bank = std::move (copy);
    return result;
}

void storeActivePatternSettings (LaneState& lane)
{
    const auto slot = std::clamp (lane.selectedPattern, 0, patternsPerLane - 1);
    const auto settings = validatedSettings (settingsFromLane (lane));
    if (patternBank (lane).settings[slot] == settings) return;
    editPatternBank (lane).settings[slot] = settings;
}

void recallPatternSettings (LaneState& lane)
{
    const auto slot = std::clamp (lane.selectedPattern, 0, patternsPerLane - 1);
    applySettingsToLane (lane, validatedSettings (patternBank (lane).settings[slot]));
}

ProjectState ProjectState::defaults()
{
    ProjectState result;
    initialiseDefaults (result);
    return result;
}

void ProjectState::initialiseDefaults (ProjectState& result)
{
    result.version = 16;
    result.revision = 1;
    result.activeLaneCount = 1;
    result.selectedLane = 0;
    result.editorGridX = 8;
    result.editorGridY = 8;
    result.editorDrawMode = DrawMode::edit;
    for (int lane = 0; lane < maxLanes; ++lane)
    {
        auto& l = result.lanes[lane];
        l.enabled = lane == 0;
        auto bank = std::make_shared<PatternBank>();
        for (int slot = 0; slot < patternsPerLane; ++slot)
        {
            bank->patterns[slot] = Pattern::factory (slot + lane * 3);
            bank->occupied[slot] = slot < 12;
            bank->names[slot] = defaultPatternName (slot);
        }
        l.bank = std::move (bank);
    }
}

void ProjectState::validate() noexcept
{
    version = 17;
    activeLaneCount = std::clamp (activeLaneCount, 1, maxLanes);
    selectedLane = std::clamp (selectedLane, 0, activeLaneCount - 1);
    editorGridX = std::clamp (editorGridX, 1, 48);
    editorGridY = std::clamp (editorGridY, 1, 48);
    editorDrawMode = static_cast<DrawMode> (std::clamp (static_cast<int> (editorDrawMode), 0, 4));
    for (int lane = 0; lane < maxLanes; ++lane)
    {
        auto& l = lanes[lane];
        if (lane >= activeLaneCount) l.enabled = false;
        l.selectedPattern = std::clamp (l.selectedPattern, 0, patternsPerLane - 1);
        l.speedHz = std::clamp (l.speedHz, 0.01f, 20.0f);
        l.division = std::clamp<int> (l.division, 0, static_cast<int> (divisionLengths.size()) - 1);
        l.timingFeel = static_cast<TimingFeel> (std::clamp<int> (static_cast<int> (l.timingFeel), 0, 2));
        l.changeMode = static_cast<ChangeMode> (std::clamp<int> (static_cast<int> (l.changeMode), 0, static_cast<int> (ChangeMode::count) - 1));
        l.startPosition = clamp01 (l.startPosition);
        l.sustainPosition = std::clamp (l.sustainPosition, l.startPosition, 1.0f);
        l.baseValue = clamp01 (l.baseValue);
        l.patternMix = std::clamp (l.patternMix, -1.0f, 1.0f);
        l.mod1 = clamp01 (l.mod1);
        l.mod2 = clamp01 (l.mod2);
        for (auto& depth : l.modSpeed) depth = std::clamp (depth, -1.0f, 1.0f);
        l.midiStartMode = static_cast<MidiStartMode> (std::clamp (static_cast<int> (l.midiStartMode), 0, 1));
        l.midiReleaseMode = static_cast<MidiReleaseMode> (std::clamp (static_cast<int> (l.midiReleaseMode), 0, 1));
        if (l.midiLoopForever) l.midiSustain = false;
        if (l.bank == nullptr)
        {
            auto bank = std::make_shared<PatternBank>();
            for (int slot = 0; slot < patternsPerLane; ++slot)
            {
                bank->patterns[slot] = Pattern::factory (slot + lane * 3);
                bank->occupied[slot] = slot < 12;
                bank->names[slot] = defaultPatternName (slot);
            }
            l.bank = std::move (bank);
        }
        else
        {
            bool any = false;
            bool needsRepair = false;
            for (int slot = 0; slot < patternsPerLane; ++slot)
            {
                any = any || l.bank->occupied[slot];
                needsRepair = needsRepair || l.bank->names[slot].empty() || l.bank->names[slot].size() > 31
                              || l.bank->settings[slot] != validatedSettings (l.bank->settings[slot]);
            }
            if (! any || needsRepair)
            {
                auto bank = std::make_shared<PatternBank> (*l.bank);
                for (int slot = 0; slot < patternsPerLane; ++slot)
                {
                    if (bank->names[slot].empty()) bank->names[slot] = defaultPatternName (slot);
                    if (bank->names[slot].size() > 31) bank->names[slot].resize (31);
                    bank->settings[slot] = validatedSettings (bank->settings[slot]);
                }
                if (! any) bank->occupied[0] = true;
                l.bank = std::move (bank);
            }
        }
        bool selectionRepaired = false;
        if (! patternBank (l).occupied[l.selectedPattern])
            for (int distance = 1; distance < patternsPerLane; ++distance)
            {
                bool found = false;
                for (const int candidate : { l.selectedPattern - distance, l.selectedPattern + distance })
                    if (candidate >= 0 && candidate < patternsPerLane && patternBank (l).occupied[candidate])
                    {
                        l.selectedPattern = candidate;
                        selectionRepaired = true;
                        found = true;
                        break;
                    }
                if (found) break;
            }
        if (selectionRepaired) recallPatternSettings (l);
    }
}

Model::Model() : state (std::make_unique<ProjectState>())
{
    ProjectState::initialiseDefaults (*state);
    writeRealtimeFromStateLocked();
    std::atomic_store_explicit (&published, std::make_shared<const ProjectState> (*state),
                                std::memory_order_release);
}

std::shared_ptr<const ProjectState> Model::snapshot() const noexcept
{
    return std::atomic_load_explicit (&published, std::memory_order_acquire);
}

void Model::pushUndoLocked()
{
    if (undoStack.size() >= 32) undoStack.erase (undoStack.begin());
    undoStack.push_back (*state);
}

void Model::writeRealtimeFromStateLocked() noexcept
{
    for (int lane = 0; lane < maxLanes; ++lane)
        for (int parameter = 0; parameter < parameterKinds; ++parameter)
            realtimeParameters[lane][parameter].store (
                parameterToHost (state->lanes[lane], static_cast<Parameter> (parameter)),
                std::memory_order_relaxed);
    syncedRealtimeRevision = realtimeRevision.fetch_add (1, std::memory_order_release) + 1;
}

void Model::publishLocked (bool updateRealtime)
{
    state->validate();
    for (auto& lane : state->lanes) storeActivePatternSettings (lane);
    ++state->revision;
    std::atomic_store_explicit (&published, std::make_shared<const ProjectState> (*state),
                                std::memory_order_release);
    if (updateRealtime) writeRealtimeFromStateLocked();
}

void Model::mutate (const std::function<void (ProjectState&)>& edit, bool createUndo)
{
    {
        std::lock_guard lock (mutex);
        if (createUndo && ! gestureOpen) { pushUndoLocked(); redoStack.clear(); }
        if (gestureOpen && ! gestureChanged)
        {
            pushUndoLocked();
            redoStack.clear();
            gestureChanged = true;
        }
        edit (*state);
        publishLocked();
    }
    notifyListeners();
}

void Model::beginGesture()
{
    std::lock_guard lock (mutex);
    gestureOpen = true;
    gestureChanged = false;
}

void Model::endGesture()
{
    std::lock_guard lock (mutex);
    gestureOpen = false;
    gestureChanged = false;
}

bool Model::undo()
{
    {
        std::lock_guard lock (mutex);
        if (undoStack.empty()) return false;
        if (redoStack.size() >= 32) redoStack.erase (redoStack.begin());
        redoStack.push_back (*state);
        *state = std::move (undoStack.back());
        undoStack.pop_back();
        publishLocked();
    }
    notifyListeners();
    return true;
}

bool Model::redo()
{
    {
        std::lock_guard lock (mutex);
        if (redoStack.empty()) return false;
        if (undoStack.size() >= 32) undoStack.erase (undoStack.begin());
        undoStack.push_back (*state);
        *state = std::move (redoStack.back());
        redoStack.pop_back();
        publishLocked();
    }
    notifyListeners();
    return true;
}

bool Model::canUndo() const
{
    std::lock_guard lock (mutex);
    return ! undoStack.empty();
}

bool Model::canRedo() const
{
    std::lock_guard lock (mutex);
    return ! redoStack.empty();
}

int Model::addLane()
{
    int result = -1;
    mutate ([&] (ProjectState& s)
    {
        if (s.activeLaneCount >= maxLanes) return;
        result = s.activeLaneCount++;
        s.selectedLane = result;
        s.lanes[result].enabled = true;
    });
    return result;
}

void Model::selectLane (int lane)
{
    mutate ([lane] (ProjectState& s) { s.selectedLane = lane; });
}

void Model::selectPattern (int lane, int pattern, bool createUndo)
{
    mutate ([lane, pattern] (ProjectState& s)
    {
        if (lane >= 0 && lane < maxLanes)
        {
            const auto& bank = patternBank (s.lanes[lane]);
            const auto requested = std::clamp (pattern, 0, patternsPerLane - 1);
            if (bank.occupied[requested])
            {
                s.lanes[lane].selectedPattern = requested;
                recallPatternSettings (s.lanes[lane]);
                return;
            }
            for (int distance = 1; distance < patternsPerLane; ++distance)
                for (const int candidate : { requested - distance, requested + distance })
                    if (candidate >= 0 && candidate < patternsPerLane && bank.occupied[candidate])
                    {
                        s.lanes[lane].selectedPattern = candidate;
                        recallPatternSettings (s.lanes[lane]);
                        return;
                    }
        }
    }, createUndo);
}

void Model::randomPattern (int lane)
{
    const auto snapshotState = snapshot();
    lane = std::clamp (lane, 0, maxLanes - 1);
    std::vector<int> choices;
    const auto& bank = patternBank (snapshotState->lanes[lane]);
    for (int slot = 0; slot < patternsPerLane; ++slot)
        if (bank.occupied[slot]) choices.push_back (slot);
    if (choices.empty()) return;
    std::uniform_int_distribution<std::size_t> distribution (0, choices.size() - 1);
    auto target = choices[distribution (random)];
    const auto current = snapshotState->lanes[lane].selectedPattern;
    if (target == current && choices.size() > 1)
        target = choices[(std::find (choices.begin(), choices.end(), target) - choices.begin() + 1) % choices.size()];
    selectPattern (lane, target, true);
}

void Model::randomizeAll (int lane)
{
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);
    std::uniform_int_distribution<int> countDistribution (3, 10);
    std::uniform_int_distribution<int> divisionDistribution (0, static_cast<int> (divisionLengths.size()) - 1);
    mutate ([&] (ProjectState& s)
    {
        if (lane < 0 || lane >= maxLanes) return;
        auto& l = s.lanes[lane];
        l.sync = unit (random) > 0.25f;
        l.speedHz = std::pow (10.0f, -2.0f + unit (random) * std::log10 (2000.0f));
        l.division = divisionDistribution (random);
        auto& bank = editPatternBank (l);
        auto& p = bank.patterns[l.selectedPattern];
        p.count = static_cast<std::uint8_t> (countDistribution (random));
        p.points[0] = { 0.0f, unit (random), unit (random) * 1.4f - 0.7f };
        for (int i = 1; i < p.count - 1; ++i)
            p.points[i] = { static_cast<float> (i) / static_cast<float> (p.count - 1), unit (random), unit (random) * 1.4f - 0.7f };
        p.points[p.count - 1] = { 1.0f, unit (random), 0.0f };
        p.normalise();
        bank.occupied[l.selectedPattern] = true;
    });
}

int Model::newPattern (int lane)
{
    int created = -1;
    mutate ([&] (ProjectState& s)
    {
        if (lane < 0 || lane >= maxLanes) return;
        auto& targetLane = s.lanes[lane];
        const auto inheritedSettings = settingsFromLane (targetLane);
        auto& bank = editPatternBank (targetLane);
        for (int slot = 0; slot < patternsPerLane; ++slot)
            if (! bank.occupied[slot])
            {
                created = slot;
                bank.occupied[slot] = true;
                bank.patterns[slot] = Pattern::factory (0);
                bank.names[slot] = "New Pattern";
                bank.settings[slot] = inheritedSettings;
                targetLane.selectedPattern = slot;
                recallPatternSettings (targetLane);
                break;
            }
    });
    return created;
}

bool Model::deletePattern (int lane)
{
    bool deleted = false;
    mutate ([&] (ProjectState& s)
    {
        if (lane < 0 || lane >= maxLanes) return;
        auto& bank = editPatternBank (s.lanes[lane]);
        int occupied = 0;
        for (const auto value : bank.occupied) occupied += value ? 1 : 0;
        const auto slot = s.lanes[lane].selectedPattern;
        if (occupied <= 1 || ! bank.occupied[slot]) return;
        bank.occupied[slot] = false;
        deleted = true;
        for (int distance = 1; distance < patternsPerLane; ++distance)
            for (const int candidate : { slot - distance, slot + distance })
                if (candidate >= 0 && candidate < patternsPerLane && bank.occupied[candidate])
                {
                    s.lanes[lane].selectedPattern = candidate;
                    recallPatternSettings (s.lanes[lane]);
                    return;
                }
    });
    return deleted;
}

void Model::renamePattern (int lane, int slot, std::string name)
{
    name.erase (std::remove_if (name.begin(), name.end(), [] (unsigned char c) { return c < 32; }), name.end());
    if (name.size() > 31) name.resize (31);
    if (name.empty()) name = defaultPatternName (slot);
    mutate ([=] (ProjectState& s)
    {
        if (lane < 0 || lane >= maxLanes || slot < 0 || slot >= patternsPerLane) return;
        editPatternBank (s.lanes[lane]).names[slot] = name;
    });
}

void Model::reorderPattern (int lane, int from, int to)
{
    mutate ([=] (ProjectState& s)
    {
        if (lane < 0 || lane >= maxLanes || from < 0 || from >= patternsPerLane
            || to < 0 || to >= patternsPerLane || from == to) return;
        auto& bank = editPatternBank (s.lanes[lane]);
        std::swap (bank.patterns[from], bank.patterns[to]);
        std::swap (bank.occupied[from], bank.occupied[to]);
        std::swap (bank.names[from], bank.names[to]);
        std::swap (bank.settings[from], bank.settings[to]);
        if (s.lanes[lane].selectedPattern == from) s.lanes[lane].selectedPattern = to;
        else if (s.lanes[lane].selectedPattern == to) s.lanes[lane].selectedPattern = from;
    });
}

void Model::loadFactoryBank (int lane, FactoryBank factory)
{
    mutate ([=] (ProjectState& state)
    {
        if (lane < 0 || lane >= maxLanes) return;
        constexpr int factoryPatternCount = 100;
        std::array<Pattern, factoryPatternCount> patterns;
        std::array<std::string, factoryPatternCount> names;
        if (factory == FactoryBank::filters)
        {
            patterns = {
                makePattern ({ { 0, 0, 0 }, { 1, 1, 0 } }),
                makePattern ({ { 0, 1, 0 }, { 1, 0, 0 } }),
                makePattern ({ { 0, 0, 0 }, { .5f, 1, 0 }, { 1, 0, 0 } }),
                makePattern ({ { 0, .5f, 0 }, { .25f, 1, 0 }, { .75f, 0, 0 }, { 1, .5f, 0 } }),
                makePattern ({ { 0, 0, -.8f }, { .12f, 1, .65f }, { 1, .15f, 0 } }),
                makePattern ({ { 0, 1, .65f }, { .18f, .1f, -.55f }, { 1, .85f, 0 } }),
                makeSteps ({ .15f, .42f, .75f, 1.0f, .7f, .45f, .25f, .55f }),
                makeSteps ({ .15f, .85f, .15f, .85f, .15f, .85f, .15f, .85f }),
                makeSteps ({ .2f, .2f, .8f, .8f, .4f, .4f, 1.0f, 1.0f }),
                makeSteps ({ .12f, .68f, .34f, .91f, .47f, .76f, .22f, .58f }),
                makePattern ({ { 0, .1f, 0 }, { .2f, .95f, 0 }, { .35f, .3f, 0 }, { .7f, .75f, 0 }, { 1, .1f, 0 } }),
                makePattern ({ { 0, .5f, 0 }, { .125f, 1, 0 }, { .375f, 0, 0 }, { .625f, 1, 0 }, { .875f, 0, 0 }, { 1, .5f, 0 } }),
                makeSteps ({ .10f, .22f, .34f, .46f, .58f, .70f, .82f, .94f }),
                makeSteps ({ .94f, .82f, .70f, .58f, .46f, .34f, .22f, .10f }),
                makeSteps ({ .12f, .88f, .12f, .88f, .12f, .88f, .12f, .88f,
                             .12f, .88f, .12f, .88f, .12f, .88f, .12f, .88f }),
                makeSteps ({ .10f, .72f, .32f, .92f, .18f, .78f, .42f, .66f }),
                makeSteps ({ .20f, .20f, .78f, .78f, .45f, .92f, .45f, .12f }),
                makePattern ({ { 0, .08f, -.90f }, { .28f, .92f, .62f }, { 1, .35f, 0 } }),
                makePattern ({ { 0, .92f, .72f }, { .35f, .18f, -.68f }, { 1, .62f, 0 } }),
                makePattern ({ { 0, .18f, -.55f }, { .22f, .86f, .45f }, { .48f, .30f, -.45f },
                               { .72f, .76f, .35f }, { 1, .20f, 0 } })
            };
            names = { "Sweep Up", "Sweep Down", "Triangle Sweep", "Sine Sweep", "Filter Pluck",
                      "Reverse Pluck", "Stepped Sequence", "Wobble 1/8", "Wobble Pairs",
                      "Sample and Hold", "Resonant Motion", "Double Sweep", "Ladder Up",
                      "Ladder Down", "Wobble 1/16", "Acid Steps", "Formant Steps",
                      "Opening Stab", "Closing Stab", "Notch Bounce" };
            constexpr std::array<std::string_view, 30> recipes {
                std::string_view { "0900007000005000" }, "9007000090005000", "0909007009005000",
                "9000907005000900", "0970005090700050", "9007005003007005",
                "0990007000509000", "9000009070000090", "0905070903050709",
                "9090709050907030", "9070300090507003", "9000507090003070",
                "0907030090503007", "9050007090300050", "9007003090050700",
                "7030900050703009", "9070503090705030", "9000307050009070",
                "7090003050709000", "9030705090307050",
                "00010003000500070009000700050003", "90007000500030009000500070003000",
                "01234567898765432101357975310000", "97531000357900009753135790002468",
                "09000000070000000500000003000000", "00102030405060708090705030100000",
                "90000030700000509000007050000300", "13579753135797531357975300009000",
                "00009000005070000030900000705000", "90203040506070809080706050403020"
            };
            constexpr std::array<std::string_view, 30> recipeNames {
                std::string_view { "Rave Stab Quarter" }, "Rave Stab Answer", "Rave Stab Sync",
                "Warehouse Lead", "Hoover Chops", "Offbeat Lead Stabs", "Double Stab Drop",
                "Sparse Rave Hits", "Rave Call Response", "Hardcore Lead Gate",
                "DnB Break Accent", "DnB Snare Answer", "DnB Ghost Cuts", "Amen Filter Hits",
                "Jungle Syncopation", "Breakbeat Callout", "Rolling Break Filter",
                "Ghost Snare Sweep", "DnB Kick Snare", "Neuro Step Motion",
                "Long Riser Stabs", "Long Falling Hits", "Long Symmetry Sweep",
                "Long Rave Sequence", "Long Sparse Stabs", "Long Ladder Motion",
                "Long Break Accents", "Long Acid Motion", "Long Ghost Hits", "Long Rolling Rise"
            };
            for (int i = 0; i < static_cast<int> (recipes.size()); ++i)
            {
                patterns[20 + i] = i < 20 ? makeStabPattern (recipes[static_cast<std::size_t> (i)],
                                                              0.28f + 0.10f * (i % 5))
                                          : makeDigitSteps (recipes[static_cast<std::size_t> (i)]);
                names[20 + i] = recipeNames[static_cast<std::size_t> (i)];
            }
        }
        else if (factory == FactoryBank::volumeGates)
        {
            patterns = {
                makeSteps ({ 1.0f }),
                makeSteps ({ 1, 0, 1, 0, 1, 0, 1, 0 }),
                makeSteps ({ 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 }),
                makeSteps ({ 0, 1, 0, 1, 0, 1, 0, 1 }),
                makePattern ({ { 0, 0, -.8f }, { .22f, .25f, -.55f }, { 1, 1, 0 } }),
                makePattern ({ { 0, 0, -.95f }, { .1f, .1f, -.75f }, { 1, 1, 0 } }),
                makePattern ({ { 0, .5f, 0 }, { .25f, 1, 0 }, { .75f, 0, 0 }, { 1, .5f, 0 } }),
                makeSteps ({ 1, 0, 1, 0, 1, 0, 0, 1 }),
                makeSteps ({ 1, 0, 1, 0, 1, 0 }),
                makePattern ({ { 0, 0, 0 }, { 1, 1, 0 } }),
                makePattern ({ { 0, 1, 0 }, { 1, 0, 0 } }),
                makeSteps ({ 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 0, 1, 0 }),
                makeSteps ({ 0, .30f, .68f, 1, 0, .30f, .68f, 1, 0, .30f, .68f, 1, 0, .30f, .68f, 1 }),
                makeSteps ({ 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0 }),
                makeSteps ({ 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0 }),
                makeSteps ({ 1, 0, 1, 1, 0, 1, 0, 0 }),
                makeSteps ({ 1, 0, 0, 1, 0, 0, 1, 0 }),
                makePattern ({ { 0, 1, .80f }, { .78f, .10f, -.65f }, { 1, 0, 0 } }),
                makeSteps ({ 1, 1, 0, 0, 1, 0, 1, 0 }),
                makeSteps ({ 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 })
            };
            names = { "Full Volume", "Gate 1/8", "Gate 1/16", "Offbeat Gate", "Sidechain Pump",
                      "Deep Pump", "Tremolo", "Gate 3-3-2", "Triplet Gate", "Fade In",
                      "Fade Out", "Trance Gate", "Four Floor Duck", "Syncopated 16",
                      "Stutter Burst", "Gallop Gate", "Sparse Gate", "Reverse Pump",
                      "Swing Gate", "Half-Time Chop" };
            constexpr std::array<std::string_view, 30> recipes {
                "9000900090009000", "0900090009000900", "9900009099000090",
                "9009000090900009", "9090009000909000", "9900909090000900",
                "9000090090900900", "9090900090000009", "9000909000090900",
                "9909000900900090", "9090090000909009", "9009900090090009",
                "9900900099009000", "9090000990009090", "9000900990900009",
                "9090909000090000", "9000090990009009", "9900009009090090",
                "9009009000090900", "9099000090000909",
                "90000000000000009000000000000000", "90000090900000009090000000900000",
                "99000000909000009000900000009000", "90009000000090000900009090000000",
                "90000000000909009000000009090000", "90009090900000000900090000000090",
                "99990000900000009090900090000000", "90000000900090000000909000009000",
                "90000000000009009090000000090000", "90900000090000900009090000000009"
            };
            constexpr std::array<std::string_view, 30> recipeNames {
                "Rave Quarter Stabs", "Offbeat Rave Stabs", "Double Hit Stabs", "Syncopated Stabs",
                "Warehouse Stabs", "Hardcore Stab Roll", "Breakbeat Stabs", "Sparse Stab Fill",
                "Answering Stabs", "Rave Chord Chops", "Jungle Stab Gate", "Rolling Stab Gate",
                "Two Step Stabs", "Broken Stab Rhythm", "Late Stab Fill", "Rush Stab Gate",
                "Ghosted Stabs", "Snare Stab Answer", "Kick Stab Pattern", "Rave Fill Gate",
                "Long Quarter Hits", "Long Stab Answers", "Long Glitch Pepper", "Long Rave Gate",
                "Long Sparse Glitch", "Long Break Gate", "Long Rush Fill", "Long Offset Stabs",
                "Long Ghost Pattern", "Long Complex Gate"
            };
            for (int i = 0; i < static_cast<int> (recipes.size()); ++i)
            {
                const auto hold = 0.24f + 0.10f * (i % 5);
                const auto releaseEnd = i % 4 == 0 ? 0.97f
                                      : i % 4 == 1 ? hold + 0.055f
                                      : i % 4 == 2 ? 0.86f : 0.74f;
                patterns[20 + i] = makeStabPattern (recipes[static_cast<std::size_t> (i)], hold, releaseEnd);
                int release = 0;
                for (int point = 0; point + 1 < patterns[20 + i].count; ++point)
                    if (patterns[20 + i].points[point].y > .35f
                        && patterns[20 + i].points[point + 1].y < .05f)
                    {
                        const auto style = i % 4 == 3 ? release % 2 : i % 4;
                        patterns[20 + i].points[point].curve = style == 0 ? CurveType::halfSine
                                                                      : style == 1 ? CurveType::singleCurve
                                                                                   : CurveType::doubleCurve;
                        patterns[20 + i].points[point].tension = style == 1 ? 0.0f : -.30f;
                        ++release;
                    }
                names[20 + i] = recipeNames[static_cast<std::size_t> (i)];
            }
        }
        else
        {
            const auto pitchRange = factory == FactoryBank::melodic24 ? 24 : 12;
            const auto pv = [pitchRange] (float semitones) { return pitchValue (semitones, pitchRange); };
            patterns = {
                makeSteps ({ pv (static_cast<float> (-pitchRange)), pv (0), pv (static_cast<float> (pitchRange)) }),
                makeSteps ({ pv (static_cast<float> (pitchRange)), pv (0), pv (static_cast<float> (-pitchRange)) }),
                makeSteps (pitchRange == 24
                    ? std::initializer_list<float> { pv (-24), pv (-20), pv (-17), pv (-12), pv (-8), pv (-5), pv (0), pv (12) }
                    : std::initializer_list<float> { pv (-12), pv (-8), pv (-5), pv (0), pv (4), pv (7), pv (12), pv (12) }),
                makeSteps (pitchRange == 24
                    ? std::initializer_list<float> { pv (-24), pv (-21), pv (-17), pv (-12), pv (-9), pv (-5), pv (0), pv (12) }
                    : std::initializer_list<float> { pv (-12), pv (-9), pv (-5), pv (0), pv (3), pv (7), pv (12), pv (12) }),
                makeSteps ({ pv (-12), pv (-5), pv (0), pv (7), pv (12), pv (7), pv (0), pv (-5) }),
                makeSteps ({ pv (-12), pv (-5), pv (0), pv (3), pv (7), pv (3), pv (0), pv (-5) }),
                makeSteps ({ pv (0), pv (7), pv (12), pv (7) }),
                makeSteps ({ pv (-12), pv (12), pv (-12), pv (12) }),
                makeSteps ({ pv (0), pv (3), pv (7), pv (10) }),
                makeSteps ({ pv (0), pv (4), pv (7), pv (11) }),
                makeSteps ({ pv (0), pv (7), pv (3), pv (12), pv (-5), pv (5), pv (-12), pv (10) }),
                makeSteps ({ pv (-12), pv (-8), pv (-4), pv (0), pv (4), pv (8), pv (12), pv (12) }),
                makeSteps ({ pv (-12), pv (12), pv (-5), pv (7), pv (0), pv (4) }),
                makeSteps ({ pv (0), pv (4), pv (-5), pv (7), pv (-12), pv (12) }),
                makeSteps ({ pv (-12), pv (-5), pv (0), pv (7), pv (12), pv (4), pv (0), pv (-8) }),
                makeSteps ({ pv (12), pv (7), pv (0), pv (-5), pv (-12), pv (-5), pv (0), pv (7) }),
                makeSteps ({ pv (-12), pv (12), pv (-5), pv (7), pv (0), pv (4), pv (7), pv (-5) }),
                makeSteps ({ pv (-12), pv (12), pv (-5), pv (7), pv (0), pv (4), pv (7), pv (4), pv (0), pv (7), pv (-5), pv (12) }),
                makeSteps ({ pv (-12), pv (-5), pv (0), pv (7), pv (0), pv (-5) }),
                makeSteps ({ pv (-12), pv (-9), pv (-7), pv (-5), pv (-2), pv (0), pv (3), pv (5), pv (7), pv (10), pv (12), pv (12) })
            };
            names = { "Octaves Up", "Octaves Down", "Major Up", "Minor Up", "Major Up Down",
                      "Minor Up Down", "Root Fifth Octave", "Octave Bounce", "Minor Seventh",
                      "Major Seventh", "Melodic Random", "Chromatic Rise", "Converge",
                      "Diverge", "Up Down No Repeat", "Down Up No Repeat", "Pinky Up",
                      "Pinky Up Down", "Fifth Walk", "Minor Pentatonic" };
            constexpr std::array<std::string_view, 30> recipes {
                "MEHQTEHM", "MQTUTQHM", "MQTYTQHE", "MHEMQTQM",
                "MPTUMJHM", "MQTQPMHE", "MHMTPQTM", "MTQHEMQY",
                "MEHMQTUT", "MPTQHMJH", "MMQYMMTQ", "MEEMYYQH",
                "MQQTYYHM", "MHMHQYQT", "MQTTQMMY", "MEHMMYYT",
                "MQYQTMHE", "MMPMTTYM", "MHHQYYTH", "MYMMQTTQ",
                "MEHMLNLNLNQTUTUTUYTQPMHM", "MPTQPNPNPQUTUTUTQPMHM",
                "HEHMLNLNMQPQPTUTUQPMH", "MQTUTUTUYXWXWTQPNMLH",
                "MHMNMLMNPQPOTUTSUTQPM", "MEHMQPNPNPQTYXYXYTQH",
                "MPTQMMNLNMUTUTSUTQPMH", "MHMPTSTUTUYXYWTQPMH",
                "MEHMMNLNMQTUTUTQPMHM", "MPTQPNPNPMYXYXWTQHM"
            };
            constexpr std::array<std::string_view, 30> recipeNames {
                "Major Rave Lead", "Minor Rave Lead", "Festival Lead", "Call Response Lead",
                "Dark Minor Phrase", "Rising Hook", "Fifth Answer", "Octave Lead Run",
                "Major Hook Line", "Minor Hook Line", "Glitch Octave Hits", "Glitch Note Doubles",
                "Glitch Peak Repeat", "Broken Melody", "Jump Cut Melody", "Octave Glitch Run",
                "Falling Glitch Line", "Stuttered Minor", "Jungle Pitch Cuts", "Rave Pitch Chops",
                "Vocal Run Major", "Vocal Run Minor", "Vocal Turnaround", "Vocal High Vibrato",
                "Vocal Grace Notes", "Vocal Octave Run", "Vocal Minor Vibrato", "Vocal Peak Run",
                "Vocal Smooth Run", "Vocal Glitch Run"
            };
            for (int i = 0; i < static_cast<int> (recipes.size()); ++i)
            {
                patterns[20 + i] = makePitchCode (recipes[static_cast<std::size_t> (i)], pitchRange,
                                                  pitchRange == 24 ? 1 + i % 3 : 0);
                names[20 + i] = recipeNames[static_cast<std::size_t> (i)];
            }
        }

        for (int variant = 0; variant < 50; ++variant)
        {
            patterns[50 + variant] = makeAdvancedFactoryPattern (factory, variant);
            names[50 + variant] = advancedFactoryName (factory, variant);
        }

        if (factory == FactoryBank::melodic12 || factory == FactoryBank::melodic24)
        {
            const auto pitchRange = factory == FactoryBank::melodic24 ? 24 : 12;
            for (int slot = 0; slot < factoryPatternCount; ++slot)
                addMelodicModMappings (patterns[slot], slot, pitchRange);
        }
        else
        {
            for (int slot = 0; slot < factoryPatternCount; ++slot)
                addReleaseTimeMod (patterns[slot]);
        }

        auto& bank = editPatternBank (state.lanes[lane]);
        for (int slot = 0; slot < patternsPerLane; ++slot)
        {
            bank.occupied[slot] = slot < factoryPatternCount;
            bank.patterns[slot] = slot < factoryPatternCount ? patterns[slot] : Pattern::factory (slot);
            bank.names[slot] = slot < factoryPatternCount ? names[slot] : defaultPatternName (slot);
            PatternSettings settings;
            settings.sync = true;
            settings.division = (slot >= 90 && slot < 98) ? 25
                              : ((slot >= 40 && slot < 50) || (slot >= 82 && slot < 90)) ? 22 : 19;
            if ((factory == FactoryBank::melodic12 || factory == FactoryBank::melodic24) && slot < 2)
                settings.division = 13;
            settings.baseValue = 0.0f;
            settings.amount = 1.0f;
            bank.settings[slot] = settings;
        }
        auto& target = state.lanes[lane];
        target.selectedPattern = 0;
        if (factory == FactoryBank::melodic12) state.editorGridY = 24;
        if (factory == FactoryBank::melodic24) state.editorGridY = 48;
        recallPatternSettings (target);
    });
}

std::string Model::copyPatternText (int lane, int slot) const
{
    const auto s = snapshot();
    lane = std::clamp (lane, 0, maxLanes - 1);
    slot = std::clamp (slot, 0, patternsPerLane - 1);
    const auto& bank = patternBank (s->lanes[lane]);
    const auto& p = bank.patterns[slot];
    const auto& settings = bank.settings[slot];
    std::ostringstream out;
    out << "PatternBankPattern:12\n" << static_cast<int> (p.count) << '\n'
        << std::quoted (bank.names[slot]) << '\n' << std::setprecision (9)
        << settings.sync << ' ' << settings.speedHz << ' ' << settings.division << ' '
        << static_cast<int> (settings.timingFeel) << ' ' << static_cast<int> (settings.changeMode) << ' '
        << settings.startPosition << ' ' << settings.sustainPosition << ' ' << settings.midiTrigger << ' '
        << settings.baseValue << ' ' << settings.amount << ' ' << settings.mod1 << ' ' << settings.mod2 << ' '
        << settings.modSpeed[0] << ' ' << settings.modSpeed[1] << ' '
        << settings.positionSync << ' ' << static_cast<int> (settings.midiStartMode) << ' '
        << static_cast<int> (settings.midiReleaseMode) << ' ' << settings.legato << ' '
        << settings.midiLoopForever << ' ' << settings.midiSustain << ' '
        << settings.amountBipolar << '\n';
    for (int i = 0; i < p.count; ++i)
        out << p.points[i].x << ' ' << p.points[i].y << ' ' << p.points[i].tension << ' '
            << static_cast<int> (p.points[i].curve) << ' '
            << p.points[i].modX[0] << ' ' << p.points[i].modY[0] << ' ' << p.points[i].modTension[0] << ' '
            << p.points[i].modX[1] << ' ' << p.points[i].modY[1] << ' ' << p.points[i].modTension[1] << '\n';
    return out.str();
}

bool Model::pastePatternText (int lane, int slot, const std::string& text)
{
    std::istringstream input (text);
    std::string header;
    int count = 0;
    if (! std::getline (input, header)) return false;
    if (header.size() >= 3 && static_cast<unsigned char> (header[0]) == 0xef
        && static_cast<unsigned char> (header[1]) == 0xbb && static_cast<unsigned char> (header[2]) == 0xbf)
        header.erase (0, 3);
    while (! header.empty() && std::isspace (static_cast<unsigned char> (header.back()))) header.pop_back();
    while (! header.empty() && std::isspace (static_cast<unsigned char> (header.front()))) header.erase (header.begin());
    const auto clipboardVersion = header == "PatternBankPattern:12" ? 12 : header == "PatternBankPattern:11" ? 11 : header == "PatternBankPattern:10" ? 10 : header == "PatternBankPattern:9" ? 9 : header == "PatternBankPattern:8" ? 8 : header == "PatternBankPattern:7" ? 7 : header == "StepShaperPattern:6" ? 6 : header == "StepShaperPattern:5" ? 5 : header == "StepShaperPattern:4" ? 4 : header == "StepShaperPattern:3" ? 3
        : header == "StepShaperPattern:2" ? 2 : header == "StepShaperPattern:1" ? 1 : 0;
    if (clipboardVersion == 0 || ! (input >> count)
        || count < 2 || count > maxPoints)
        return false;
    Pattern parsed;
    parsed.count = static_cast<std::uint8_t> (count);
    std::string parsedName;
    if (clipboardVersion >= 8 && ! (input >> std::quoted (parsedName))) return false;
    PatternSettings parsedSettings;
    if (clipboardVersion >= 5)
    {
        int timing = 0, change = 0;
        if (! (input >> parsedSettings.sync >> parsedSettings.speedHz >> parsedSettings.division >> timing >> change
              >> parsedSettings.startPosition >> parsedSettings.sustainPosition >> parsedSettings.midiTrigger
              >> parsedSettings.baseValue >> parsedSettings.amount >> parsedSettings.mod1 >> parsedSettings.mod2)) return false;
        if (clipboardVersion >= 7
            && ! (input >> parsedSettings.modSpeed[0] >> parsedSettings.modSpeed[1])) return false;
        if (clipboardVersion >= 6 && ! (input >> parsedSettings.positionSync)) return false;
        if (clipboardVersion >= 9)
        {
            int startMode = 0, releaseMode = 0;
            if (! (input >> startMode >> releaseMode)) return false;
            parsedSettings.midiStartMode = static_cast<MidiStartMode> (startMode);
            parsedSettings.midiReleaseMode = static_cast<MidiReleaseMode> (releaseMode);
        }
        if (clipboardVersion >= 10 && ! (input >> parsedSettings.legato)) return false;
        if (clipboardVersion >= 11
            && ! (input >> parsedSettings.midiLoopForever >> parsedSettings.midiSustain)) return false;
        if (clipboardVersion >= 12 && ! (input >> parsedSettings.amountBipolar)) return false;
        parsedSettings.timingFeel = static_cast<TimingFeel> (timing);
        parsedSettings.changeMode = static_cast<ChangeMode> (change);
        parsedSettings = validatedSettings (parsedSettings);
    }
    for (int i = 0; i < count; ++i)
    {
        if (! (input >> parsed.points[i].x >> parsed.points[i].y >> parsed.points[i].tension)) return false;
        if (clipboardVersion >= 2)
        {
            int curve = 0;
            if (! (input >> curve)) return false;
            parsed.points[i].curve = clipboardVersion >= 3 ? static_cast<CurveType> (curve) : legacyCurveType (curve);
        }
        if (clipboardVersion >= 4)
            for (int source = 0; source < 2; ++source)
                if (! (input >> parsed.points[i].modX[source] >> parsed.points[i].modY[source]
                      >> parsed.points[i].modTension[source])) return false;
    }
    parsed.normalise();
    mutate ([=] (ProjectState& s)
    {
        if (lane < 0 || lane >= maxLanes || slot < 0 || slot >= patternsPerLane) return;
        auto& bank = editPatternBank (s.lanes[lane]);
        bank.patterns[slot] = parsed;
        bank.occupied[slot] = true;
        if (! parsedName.empty()) bank.names[slot] = parsedName.substr (0, 31);
        if (clipboardVersion >= 5) bank.settings[slot] = parsedSettings;
        if (slot == s.lanes[lane].selectedPattern) recallPatternSettings (s.lanes[lane]);
    });
    return true;
}

std::vector<std::uint8_t> Model::serialize() const
{
    const auto s = snapshot();
    std::vector<std::uint8_t> bytes;
    bytes.reserve (450000);
    constexpr std::uint32_t magic = 0x5353464c; // SSFL
    appendPod (bytes, magic);
    appendPod (bytes, s->version);
    appendPod (bytes, s->activeLaneCount);
    appendPod (bytes, s->selectedLane);
    appendPod (bytes, s->editorGridX);
    appendPod (bytes, s->editorGridY);
    appendPod (bytes, s->editorDrawMode);
    for (const auto& lane : s->lanes)
    {
        appendPod (bytes, lane.enabled);
        appendPod (bytes, lane.selectedPattern);
        appendPod (bytes, lane.sync);
        appendPod (bytes, lane.speedHz);
        appendPod (bytes, lane.division);
        appendPod (bytes, lane.timingFeel);
        appendPod (bytes, lane.changeMode);
        appendPod (bytes, lane.startPosition);
        appendPod (bytes, lane.midiTrigger);
        appendPod (bytes, lane.baseValue);
        appendPod (bytes, lane.patternMix);
        appendPod (bytes, lane.sustainPosition);
        appendPod (bytes, lane.mod1);
        appendPod (bytes, lane.mod2);
        appendPod (bytes, lane.modSpeed);
        appendPod (bytes, lane.positionSync);
        appendPod (bytes, lane.midiStartMode);
        appendPod (bytes, lane.midiReleaseMode);
        appendPod (bytes, lane.legato);
        appendPod (bytes, lane.midiLoopForever);
        appendPod (bytes, lane.midiSustain);
        appendPod (bytes, lane.amountBipolar);
        const auto& bank = patternBank (lane);
        for (int slot = 0; slot < patternsPerLane; ++slot)
        {
            appendPod (bytes, bank.occupied[slot]);
            appendPod (bytes, bank.patterns[slot].count);
            for (int point = 0; point < bank.patterns[slot].count; ++point)
            {
                const auto& p = bank.patterns[slot].points[point];
                appendPod (bytes, p.x);
                appendPod (bytes, p.y);
                appendPod (bytes, p.tension);
                appendPod (bytes, p.curve);
                for (int source = 0; source < 2; ++source)
                {
                    appendPod (bytes, p.modX[source]);
                    appendPod (bytes, p.modY[source]);
                    appendPod (bytes, p.modTension[source]);
                }
            }
            const auto nameLength = static_cast<std::uint8_t> (std::min<std::size_t> (31, bank.names[slot].size()));
            appendPod (bytes, nameLength);
            bytes.insert (bytes.end(), bank.names[slot].begin(), bank.names[slot].begin() + nameLength);
            const auto& settings = bank.settings[slot];
            appendPod (bytes, settings.sync);
            appendPod (bytes, settings.speedHz);
            appendPod (bytes, settings.division);
            appendPod (bytes, settings.timingFeel);
            appendPod (bytes, settings.changeMode);
            appendPod (bytes, settings.startPosition);
            appendPod (bytes, settings.sustainPosition);
            appendPod (bytes, settings.midiTrigger);
            appendPod (bytes, settings.baseValue);
            appendPod (bytes, settings.amount);
            appendPod (bytes, settings.mod1);
            appendPod (bytes, settings.mod2);
            appendPod (bytes, settings.modSpeed);
            appendPod (bytes, settings.positionSync);
            appendPod (bytes, settings.midiStartMode);
            appendPod (bytes, settings.midiReleaseMode);
            appendPod (bytes, settings.legato);
            appendPod (bytes, settings.midiLoopForever);
            appendPod (bytes, settings.midiSustain);
            appendPod (bytes, settings.amountBipolar);
        }
    }
    return bytes;
}

bool Model::deserialize (const void* data, std::size_t size)
{
    if (data == nullptr || size < 16) return false;
    const auto* cursor = static_cast<const std::uint8_t*> (data);
    const auto* end = cursor + size;
    ProjectState loaded;
    std::uint32_t magic = 0, version = 0;
    if (! readPod (cursor, end, magic) || magic != 0x5353464c || ! readPod (cursor, end, version)
        || (version < 1 || version > 17)
        || ! readPod (cursor, end, loaded.activeLaneCount) || ! readPod (cursor, end, loaded.selectedLane)) return false;
    loaded.version = version;
    if (version >= 3 && (! readPod (cursor, end, loaded.editorGridX)
                         || ! readPod (cursor, end, loaded.editorGridY)
                         || ! readPod (cursor, end, loaded.editorDrawMode))) return false;
    for (auto& lane : loaded.lanes)
    {
        if (! readPod (cursor, end, lane.enabled) || ! readPod (cursor, end, lane.selectedPattern)
            || ! readPod (cursor, end, lane.sync) || ! readPod (cursor, end, lane.speedHz)
            || ! readPod (cursor, end, lane.division) || ! readPod (cursor, end, lane.timingFeel)
            || ! readPod (cursor, end, lane.changeMode)) return false;
        if (version >= 2 && ! readPod (cursor, end, lane.startPosition)) return false;
        if (version >= 4 && ! readPod (cursor, end, lane.midiTrigger)) return false;
        if (version >= 6 && (! readPod (cursor, end, lane.baseValue)
                             || ! readPod (cursor, end, lane.patternMix))) return false;
        if (version >= 7 && ! readPod (cursor, end, lane.sustainPosition)) return false;
        if (version >= 8 && (! readPod (cursor, end, lane.mod1) || ! readPod (cursor, end, lane.mod2))) return false;
        if (version >= 13 && ! readPod (cursor, end, lane.modSpeed)) return false;
        if (version >= 11 && ! readPod (cursor, end, lane.positionSync)) return false;
        if (version >= 14 && (! readPod (cursor, end, lane.midiStartMode)
                              || ! readPod (cursor, end, lane.midiReleaseMode))) return false;
        if (version >= 15 && ! readPod (cursor, end, lane.legato)) return false;
        if (version >= 16 && (! readPod (cursor, end, lane.midiLoopForever)
                              || ! readPod (cursor, end, lane.midiSustain))) return false;
        if (version >= 17 && ! readPod (cursor, end, lane.amountBipolar)) return false;
        auto bank = std::make_shared<PatternBank>();
        const auto serializedSlots = version >= 12 ? patternsPerLane : 64;
        for (int slot = 0; slot < serializedSlots; ++slot)
        {
            if (! readPod (cursor, end, bank->occupied[slot]) || ! readPod (cursor, end, bank->patterns[slot].count)
                || bank->patterns[slot].count < 2 || bank->patterns[slot].count > maxPoints) return false;
            for (int point = 0; point < bank->patterns[slot].count; ++point)
            {
                auto& p = bank->patterns[slot].points[point];
                if (! readPod (cursor, end, p.x) || ! readPod (cursor, end, p.y)
                    || ! readPod (cursor, end, p.tension)) return false;
                if (version >= 5)
                {
                    std::uint8_t curve = 0;
                    if (! readPod (cursor, end, curve)) return false;
                    p.curve = version >= 6 ? static_cast<CurveType> (curve) : legacyCurveType (curve);
                }
                if (version >= 8)
                    for (int source = 0; source < 2; ++source)
                        if (! readPod (cursor, end, p.modX[source]) || ! readPod (cursor, end, p.modY[source])
                            || ! readPod (cursor, end, p.modTension[source])) return false;
            }
            bank->patterns[slot].normalise();
            if (version >= 6)
            {
                std::uint8_t nameLength = 0;
                if (! readPod (cursor, end, nameLength) || nameLength > 31
                    || static_cast<std::size_t> (end - cursor) < nameLength) return false;
                bank->names[slot].assign (reinterpret_cast<const char*> (cursor), nameLength);
                cursor += nameLength;
            }
            else bank->names[slot] = defaultPatternName (slot);
            if (version >= 10)
            {
                auto& settings = bank->settings[slot];
                if (! readPod (cursor, end, settings.sync) || ! readPod (cursor, end, settings.speedHz)
                    || ! readPod (cursor, end, settings.division) || ! readPod (cursor, end, settings.timingFeel)
                    || ! readPod (cursor, end, settings.changeMode) || ! readPod (cursor, end, settings.startPosition)
                    || ! readPod (cursor, end, settings.sustainPosition) || ! readPod (cursor, end, settings.midiTrigger)
                    || ! readPod (cursor, end, settings.baseValue) || ! readPod (cursor, end, settings.amount)
                    || ! readPod (cursor, end, settings.mod1) || ! readPod (cursor, end, settings.mod2)) return false;
                if (version >= 13 && ! readPod (cursor, end, settings.modSpeed)) return false;
                if (version >= 11 && ! readPod (cursor, end, settings.positionSync)) return false;
                if (version >= 14 && (! readPod (cursor, end, settings.midiStartMode)
                                      || ! readPod (cursor, end, settings.midiReleaseMode))) return false;
                if (version >= 15 && ! readPod (cursor, end, settings.legato)) return false;
                if (version >= 16 && (! readPod (cursor, end, settings.midiLoopForever)
                                      || ! readPod (cursor, end, settings.midiSustain))) return false;
                if (version >= 17 && ! readPod (cursor, end, settings.amountBipolar)) return false;
                settings = validatedSettings (settings);
            }
        }
        for (int slot = serializedSlots; slot < patternsPerLane; ++slot)
        {
            bank->patterns[slot] = Pattern::factory (slot);
            bank->occupied[slot] = false;
            bank->names[slot] = defaultPatternName (slot);
            bank->settings[slot] = validatedSettings (settingsFromLane (lane));
        }
        if (version < 6)
        {
            constexpr std::array<double, 12> oldLengths {
                0.0625, 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0,
                0.3333333333, 0.6666666667, 1.3333333333
            };
            auto oldBeats = oldLengths[std::clamp (lane.division, 0, 11)];
            if (lane.timingFeel == TimingFeel::dotted) oldBeats *= 1.5;
            if (lane.timingFeel == TimingFeel::triplet) oldBeats *= 2.0 / 3.0;
            lane.division = static_cast<int> (std::min_element (divisionLengths.begin(), divisionLengths.end(),
                [oldBeats] (double a, double b) { return std::abs (a - oldBeats) < std::abs (b - oldBeats); })
                - divisionLengths.begin());
            lane.timingFeel = TimingFeel::straight;
        }
        else if (version < 9)
            lane.division = migrateLegacyDivisionOrder (lane.division);
        if (version < 10)
        {
            const auto legacySettings = validatedSettings (settingsFromLane (lane));
            bank->settings.fill (legacySettings);
        }
        lane.bank = std::move (bank);
        recallPatternSettings (lane);
    }
    loaded.validate();
    {
        std::lock_guard lock (mutex);
        pushUndoLocked();
        *state = std::move (loaded);
        publishLocked();
    }
    notifyListeners();
    return true;
}

void Model::setParameterRealtime (int lane, Parameter parameter, int hostValue) noexcept
{
    if (lane < 0 || lane >= maxLanes) return;
    const auto index = static_cast<int> (parameter);
    if (index < 0 || index >= parameterKinds) return;
    realtimeParameters[lane][index].store (std::clamp (hostValue, 0, 65536), std::memory_order_relaxed);
    realtimeRevision.fetch_add (1, std::memory_order_release);
}

int Model::realtimeParameterValue (int lane, Parameter parameter) const noexcept
{
    lane = std::clamp (lane, 0, maxLanes - 1);
    const auto index = std::clamp (static_cast<int> (parameter), 0, parameterKinds - 1);
    return realtimeParameters[lane][index].load (std::memory_order_relaxed);
}

LaneState Model::realtimeLane (int lane, const ProjectState& shapeState) const noexcept
{
    lane = std::clamp (lane, 0, maxLanes - 1);
    auto result = shapeState.lanes[lane];
    const auto previous = result;
    setParameterFromHost (result, Parameter::pattern,
                          realtimeParameters[lane][static_cast<int> (Parameter::pattern)].load (std::memory_order_relaxed));
    for (int parameter = 1; parameter < parameterKinds; ++parameter)
    {
        const auto kind = static_cast<Parameter> (parameter);
        const auto realtime = realtimeParameters[lane][parameter].load (std::memory_order_relaxed);
        if (realtime != parameterToHost (previous, kind)) setParameterFromHost (result, kind, realtime);
    }
    return result;
}

bool Model::synchroniseRealtimeParameters()
{
    const auto revision = realtimeRevision.load (std::memory_order_acquire);
    {
        std::lock_guard lock (mutex);
        if (revision == syncedRealtimeRevision) return false;
        for (int lane = 0; lane < maxLanes; ++lane)
        {
            const auto previous = state->lanes[lane];
            setParameterFromHost (state->lanes[lane], Parameter::pattern,
                                  realtimeParameters[lane][static_cast<int> (Parameter::pattern)].load (std::memory_order_relaxed));
            for (int parameter = 1; parameter < parameterKinds; ++parameter)
            {
                const auto kind = static_cast<Parameter> (parameter);
                const auto realtime = realtimeParameters[lane][parameter].load (std::memory_order_relaxed);
                if (realtime != parameterToHost (previous, kind)) setParameterFromHost (state->lanes[lane], kind, realtime);
            }
        }
        syncedRealtimeRevision = revision;
        publishLocked();
    }
    notifyListeners();
    return true;
}

int Model::addListener (Listener listener)
{
    std::lock_guard lock (mutex);
    const auto token = nextListenerToken++;
    listeners.emplace_back (token, std::move (listener));
    return token;
}

void Model::removeListener (int token)
{
    std::lock_guard lock (mutex);
    std::erase_if (listeners, [token] (const auto& item) { return item.first == token; });
}

void Model::notifyListeners()
{
    std::vector<Listener> copy;
    {
        std::lock_guard lock (mutex);
        copy.reserve (listeners.size());
        for (const auto& [token, listener] : listeners) { (void) token; copy.push_back (listener); }
    }
    for (const auto& listener : copy) if (listener) listener();
}

Engine::Engine (const Model& source) : model (source) {}

double Engine::latchBeats (ChangeMode mode) noexcept
{
    switch (mode)
    {
        case ChangeMode::latchEighthBeat: return 0.125;
        case ChangeMode::latchQuarterBeat: return 0.25;
        case ChangeMode::latchHalfBeat: return 0.5;
        case ChangeMode::latchOneBeat: return 1.0;
        case ChangeMode::latchTwoBeats: return 2.0;
        default: return 0.0;
    }
}

double Engine::periodBeats (const LaneState& lane) noexcept
{
    const auto modulation = std::clamp (lane.mod1 * lane.modSpeed[0] + lane.mod2 * lane.modSpeed[1],
                                        -1.0f, 1.0f);
    const auto division = std::clamp (static_cast<int> (std::lround (lane.division + modulation * 26.0f)),
                                      0, 26);
    return divisionBeats (division);
}

void Engine::handleRequest (RuntimeLane& r, const LaneState& lane, const TickContext& context) noexcept
{
    r.requested = lane.selectedPattern;
    if (! context.playing)
    {
        r.active = r.requested;
        r.pending = -1;
        if (lane.changeMode != ChangeMode::off) r.phase = lane.startPosition;
        return;
    }
    if (lane.changeMode == ChangeMode::off)
    {
        r.active = r.requested;
        r.pending = -1;
        return;
    }
    if (lane.changeMode == ChangeMode::restart)
    {
        r.active = r.requested;
        r.pending = -1;
        r.phase = lane.startPosition;
        return;
    }
    const auto quantum = latchBeats (lane.changeMode);
    r.pending = r.requested;
    r.boundary = (std::floor (context.songBeat / quantum + 1.0e-9) + 1.0) * quantum;
}

void Engine::requestMidiTrigger (bool overlapping) noexcept
{
    (overlapping ? midiOverlapTriggerRequested : midiTriggerRequested).store (true, std::memory_order_release);
}

void Engine::requestMidiRelease() noexcept
{
    midiReleaseRequested.store (true, std::memory_order_release);
}

void Engine::requestManualTrigger (int lane) noexcept
{
    manualTriggerRequested[std::clamp (lane, 0, maxLanes - 1)].store (true, std::memory_order_release);
}

void Engine::requestManualRelease (int lane) noexcept
{
    manualReleaseRequested[std::clamp (lane, 0, maxLanes - 1)].store (true, std::memory_order_release);
}

void Engine::tick (const TickContext& context) noexcept
{
    const auto state = model.snapshot();
    const auto receivedMidiTrigger = midiTriggerRequested.exchange (false, std::memory_order_acq_rel);
    const auto receivedMidiOverlapTrigger = midiOverlapTriggerRequested.exchange (false, std::memory_order_acq_rel);
    const auto receivedMidiRelease = midiReleaseRequested.exchange (false, std::memory_order_acq_rel);
    const auto startedPlaying = hasTicked && context.playing && ! previousPlaying;
    const auto fallbackDeltaSeconds = static_cast<double> (context.samplesPerTick) / std::max (1.0, context.sampleRate);
    const auto runningClockDelta = context.runningMilliseconds - lastRunningMilliseconds;
    const auto runningClockIsUsable = hasRunningClock && std::isfinite (context.runningMilliseconds)
        && std::isfinite (runningClockDelta) && runningClockDelta >= 0.0 && runningClockDelta <= 1000.0;
    const auto deltaSeconds = runningClockIsUsable ? runningClockDelta * 0.001 : fallbackDeltaSeconds;
    const auto deltaBeats = deltaSeconds * std::max (1.0, context.tempoBpm) / 60.0;
    const auto hostBeatDelta = previousPlaying && context.playing ? context.songBeat - lastBeat : deltaBeats;
    // FPD_SetSamplesPerTick is FL's PPQ tick duration, not the elapsed time between
    // NewTick callbacks. Advance both stopped and playing sync from FL's continuous
    // mixer clock so they run identically; songBeat remains authoritative for seeks,
    // transport alignment, Pos Sync, and latching boundaries.
    const auto jumped = previousPlaying && context.playing
        && (hostBeatDelta < -1.0e-8
            || hostBeatDelta > std::max (1.0, deltaBeats * 16.0));
    const auto syncedDeltaBeats = deltaBeats;

    for (int i = 0; i < maxLanes; ++i)
    {
        auto& r = runtime[i];
        const auto lane = model.realtimeLane (i, *state);
        const auto receivedManualTrigger = manualTriggerRequested[i].exchange (false, std::memory_order_acq_rel);
        const auto receivedManualRelease = manualReleaseRequested[i].exchange (false, std::memory_order_acq_rel);
        const auto speedModulation = std::clamp (lane.mod1 * lane.modSpeed[0] + lane.mod2 * lane.modSpeed[1],
                                                 -1.0f, 1.0f);
        const auto baseSpeedNormal = std::log (lane.speedHz / 0.01f) / std::log (2000.0f);
        const auto effectiveSpeedHz = 0.01 * std::pow (2000.0,
            std::clamp (baseSpeedNormal + static_cast<double> (speedModulation), 0.0, 1.0));
        const auto phaseAtSongPosition = [&lane, &context, &effectiveSpeedHz]
        {
            const auto cycles = lane.sync
                ? context.songBeat / std::max (0.0001, periodBeats (lane))
                : context.songBeat * 60.0 / std::max (1.0, context.tempoBpm) * effectiveSpeedHz;
            auto phase = static_cast<float> (lane.startPosition + cycles);
            phase -= std::floor (phase);
            return phase;
        };
        if (! r.initialized)
        {
            r.phase = lane.startPosition;
            r.requested = lane.selectedPattern;
            r.active = lane.selectedPattern;
            r.initialized = true;
        }
        if (startedPlaying)
        {
            r.phase = lane.positionSync && ! lane.midiTrigger ? phaseAtSongPosition() : lane.startPosition;
            r.pending = -1;
            r.active = lane.selectedPattern;
            r.requested = lane.selectedPattern;
            r.refreshPending = false;
        }
        const auto midiTriggered = lane.midiTrigger
            && (receivedManualTrigger || receivedMidiTrigger || (receivedMidiOverlapTrigger && ! lane.legato));
        const auto midiReleased = lane.midiTrigger && (receivedManualRelease || receivedMidiRelease);
        const auto refreshTriggered = receivedManualTrigger && ! lane.midiTrigger;
        auto refreshedThisTick = false;
        if (! lane.midiTrigger)
        {
            r.midiHeld = false;
            r.midiEnvelopeActivated = false;
            r.releaseTail = false;
            r.stoppedAfterRelease = false;
        }
        if (midiTriggered)
        {
            r.phase = lane.midiStartMode == MidiStartMode::loopBack ? 0.0f : lane.startPosition;
            r.midiHeld = true;
            r.midiEnvelopeActivated = true;
            r.releaseTail = false;
            r.stoppedAfterRelease = false;
            r.refreshPending = false;
        }
        if (refreshTriggered)
        {
            const auto quantum = latchBeats (lane.changeMode);
            if (context.playing && quantum > 0.0)
            {
                r.refreshPending = true;
                r.refreshBoundary = (std::floor (context.songBeat / quantum + 1.0e-9) + 1.0) * quantum;
            }
            else
            {
                r.phase = lane.startPosition;
                r.refreshPending = false;
                refreshedThisTick = true;
            }
        }
        if (midiReleased && r.midiEnvelopeActivated)
        {
            if (lane.midiReleaseMode == MidiReleaseMode::release)
            {
                r.phase = lane.sustainPosition;
                r.midiHeld = false;
                r.releaseTail = true;
                r.stoppedAfterRelease = false;
            }
            else if (lane.midiSustain)
            {
                r.phase = lane.sustainPosition;
                r.midiHeld = false;
                r.releaseTail = false;
            }
            else if (! lane.midiLoopForever)
            {
                r.midiHeld = false; // Hold the current value until the next qualifying note-on.
                r.releaseTail = false;
            }
        }
        if (lane.selectedPattern != r.requested) handleRequest (r, lane, context);
        if (jumped && r.pending >= 0) handleRequest (r, lane, context);
        if (jumped && r.refreshPending)
        {
            const auto quantum = latchBeats (lane.changeMode);
            r.refreshBoundary = quantum > 0.0
                ? (std::floor (context.songBeat / quantum + 1.0e-9) + 1.0) * quantum
                : context.songBeat;
        }
        if (jumped && (lane.sync || lane.positionSync) && ! midiTriggered && ! r.midiEnvelopeActivated)
        {
            r.phase = lane.positionSync ? phaseAtSongPosition() : lane.startPosition;
        }
        if (! context.playing && r.pending >= 0)
        {
            r.active = r.pending;
            r.pending = -1;
        }
        if (r.refreshPending && (! context.playing
            || context.songBeat + 1.0e-9 >= r.refreshBoundary))
        {
            r.phase = lane.startPosition;
            r.refreshPending = false;
            refreshedThisTick = true;
        }
        if (context.playing && r.pending >= 0 && context.songBeat + 1.0e-9 >= r.boundary)
        {
            r.active = r.pending;
            r.pending = -1;
            r.phase = lane.startPosition;
        }
        const auto positionLockedWhileStopped = ! context.playing && lane.positionSync && ! lane.midiTrigger;
        if (positionLockedWhileStopped) r.phase = phaseAtSongPosition();
        if (! startedPlaying && ! midiTriggered && ! midiReleased && ! refreshedThisTick
            && ! jumped && ! positionLockedWhileStopped)
        {
            const auto increment = lane.sync
                ? syncedDeltaBeats / std::max (0.0001, periodBeats (lane))
                : deltaSeconds * effectiveSpeedHz;
            if (lane.midiTrigger && r.midiEnvelopeActivated)
            {
                if (r.midiHeld)
                {
                    const auto loopStart = lane.startPosition;
                    const auto loopEnd = lane.sustainPosition;
                    const auto loopLength = loopEnd - loopStart;
                    if (lane.midiSustain)
                    {
                        r.phase = std::min (loopEnd, static_cast<float> (r.phase + increment));
                    }
                    else if (! lane.midiLoopForever)
                    {
                        r.phase = std::min (1.0f, static_cast<float> (r.phase + increment));
                    }
                    else if (loopLength <= 0.0001f) r.phase = loopStart;
                    else
                    {
                        r.phase = static_cast<float> (r.phase + increment);
                        if (r.phase >= loopEnd)
                            r.phase = loopStart + std::fmod (r.phase - loopEnd, loopLength);
                    }
                }
                else if (r.releaseTail)
                {
                    r.phase = static_cast<float> (r.phase + increment);
                    if (r.phase >= 1.0f)
                    {
                        r.phase = 1.0f;
                        r.releaseTail = false;
                        r.stoppedAfterRelease = true;
                    }
                }
            }
            else
            {
                r.phase = static_cast<float> (r.phase + increment);
                r.phase -= std::floor (r.phase);
            }
        }
        r.active = std::clamp (r.active, 0, patternsPerLane - 1);
        const auto patternValue = patternBank (lane).patterns[r.active].valueAt (r.phase, lane.mod1, lane.mod2);
        const auto amountSource = lane.amountBipolar ? patternValue * 2.0f - 1.0f : patternValue;
        r.output = lane.enabled ? clamp01 (lane.baseValue + amountSource * lane.patternMix) : 0.0f;
        phaseDisplay[i].store (r.phase, std::memory_order_relaxed);
        outputDisplay[i].store (r.output, std::memory_order_relaxed);
        activeDisplay[i].store (r.active, std::memory_order_relaxed);
        pendingDisplay[i].store (r.pending, std::memory_order_relaxed);
    }
    lastBeat = context.songBeat;
    if (std::isfinite (context.runningMilliseconds) && context.runningMilliseconds >= 0.0)
    {
        lastRunningMilliseconds = context.runningMilliseconds;
        hasRunningClock = true;
    }
    previousPlaying = context.playing;
    hasTicked = true;
}

float Engine::output (int lane) const noexcept { return outputDisplay[std::clamp (lane, 0, maxLanes - 1)].load (std::memory_order_relaxed); }
float Engine::phase (int lane) const noexcept { return phaseDisplay[std::clamp (lane, 0, maxLanes - 1)].load (std::memory_order_relaxed); }
int Engine::activePattern (int lane) const noexcept { return activeDisplay[std::clamp (lane, 0, maxLanes - 1)].load (std::memory_order_relaxed); }
int Engine::pendingPattern (int lane) const noexcept { return pendingDisplay[std::clamp (lane, 0, maxLanes - 1)].load (std::memory_order_relaxed); }

int parameterIndex (int lane, Parameter parameter) noexcept
{
    lane = std::clamp (lane, 0, maxLanes - 1);
    switch (parameter)
    {
        case Parameter::enabled: return enabledParameterOffset + lane;
        case Parameter::pattern: return patternParameterOffset + lane;
        case Parameter::speed: return speedParameterOffset + lane;
        case Parameter::sync: return syncParameterOffset + lane;
        case Parameter::division: return divisionParameterOffset + lane;
        case Parameter::timingFeel: return timingFeelParameterOffset + lane;
        case Parameter::changeMode: return changeModeParameterOffset + lane;
        case Parameter::positionSync: return positionSyncParameterOffset + lane;
        case Parameter::midiTrigger: return midiTriggerParameterOffset + lane;
        case Parameter::retrigger: return retriggerParameterOffset + lane;
        case Parameter::startPosition: return startPositionParameterOffset + lane;
        case Parameter::sustainPosition: return sustainPositionParameterOffset + lane;
        case Parameter::baseValue: return baseValueParameterOffset + lane;
        case Parameter::patternMix: return patternMixParameterOffset + lane;
        case Parameter::mod1: return mod1ParameterOffset + lane;
        case Parameter::mod2: return mod2ParameterOffset + lane;
    }
    return enabledParameterOffset + lane;
}

int parameterToHost (const LaneState& lane, Parameter parameter) noexcept
{
    constexpr int maxHost = 65536;
    switch (parameter)
    {
        case Parameter::pattern:
        {
            const auto count = occupiedPatternCount (lane);
            return count <= 1 ? 0 : static_cast<int> (std::lround (occupiedPatternOrdinal (lane) * maxHost
                                                                  / static_cast<double> (count - 1)));
        }
        case Parameter::speed: return static_cast<int> (std::lround ((std::log (lane.speedHz / 0.01f) / std::log (2000.0f)) * maxHost));
        case Parameter::sync: return lane.sync ? maxHost : 0;
        case Parameter::division: return static_cast<int> (std::lround (lane.division * maxHost / 26.0));
        case Parameter::timingFeel: return static_cast<int> (lane.timingFeel) * (maxHost / 2);
        case Parameter::changeMode: return static_cast<int> (std::lround (static_cast<int> (lane.changeMode) * maxHost / 6.0));
        case Parameter::enabled: return lane.enabled ? maxHost : 0;
        case Parameter::startPosition: return static_cast<int> (std::lround (clamp01 (lane.startPosition) * maxHost));
        case Parameter::midiTrigger: return lane.midiTrigger ? maxHost : 0;
        case Parameter::baseValue: return static_cast<int> (std::lround (clamp01 (lane.baseValue) * maxHost));
        case Parameter::patternMix: return static_cast<int> (std::lround ((std::clamp (lane.patternMix, -1.0f, 1.0f) + 1.0f)
                                                                          * 0.5f * maxHost));
        case Parameter::sustainPosition: return static_cast<int> (std::lround (clamp01 (lane.sustainPosition) * maxHost));
        case Parameter::mod1: return static_cast<int> (std::lround (clamp01 (lane.mod1) * maxHost));
        case Parameter::mod2: return static_cast<int> (std::lround (clamp01 (lane.mod2) * maxHost));
        case Parameter::positionSync: return lane.positionSync ? maxHost : 0;
        case Parameter::retrigger: return 0;
    }
    return 0;
}

void setParameterFromHost (LaneState& lane, Parameter parameter, int hostValue) noexcept
{
    constexpr float maxHost = 65536.0f;
    const auto normal = std::clamp (hostValue / maxHost, 0.0f, 1.0f);
    switch (parameter)
    {
        case Parameter::pattern:
        {
            const auto count = occupiedPatternCount (lane);
            const auto ordinal = count <= 1 ? 0 : static_cast<int> (std::lround (normal * (count - 1)));
            lane.selectedPattern = occupiedPatternSlot (lane, ordinal);
            recallPatternSettings (lane);
            break;
        }
        case Parameter::speed: lane.speedHz = 0.01f * std::pow (2000.0f, normal); break;
        case Parameter::sync: lane.sync = normal >= 0.5f; break;
        case Parameter::division: lane.division = std::clamp (static_cast<int> (std::lround (normal * 26.0f)), 0, 26); break;
        case Parameter::timingFeel: lane.timingFeel = static_cast<TimingFeel> (std::clamp (static_cast<int> (std::lround (normal * 2.0f)), 0, 2)); break;
        case Parameter::changeMode: lane.changeMode = static_cast<ChangeMode> (std::clamp (static_cast<int> (std::lround (normal * 6.0f)), 0, 6)); break;
        case Parameter::enabled: lane.enabled = normal >= 0.5f; break;
        case Parameter::startPosition: lane.startPosition = normal; break;
        case Parameter::midiTrigger: lane.midiTrigger = normal >= 0.5f; break;
        case Parameter::baseValue: lane.baseValue = normal; break;
        case Parameter::patternMix: lane.patternMix = normal * 2.0f - 1.0f; break;
        case Parameter::sustainPosition: lane.sustainPosition = std::max (lane.startPosition, normal); break;
        case Parameter::mod1: lane.mod1 = normal; break;
        case Parameter::mod2: lane.mod2 = normal; break;
        case Parameter::positionSync: lane.positionSync = normal >= 0.5f; break;
        case Parameter::retrigger: break;
    }
}

intptr_t encodeFLControllerValue (float normalized, bool forPatcher) noexcept
{
    const auto legacy = static_cast<std::int64_t> (std::llround (clamp01 (normalized) * 65536.0f));
    // A normal Mixer-slot "Link to controller" connection consumes the SDK's
    // classic 0..65536 controller range. Patcher's red controller connections
    // use a separate 48-bit fixed-point representation.
    return static_cast<intptr_t> (forPatcher ? legacy * patcherControllerScale : legacy);
}

int decodeModernFLParameterValue (intptr_t value) noexcept
{
    if (value > 65536 || value < 0)
        return std::clamp (static_cast<int> (std::llround (static_cast<double> (value) / modernFLValueScale)), 0, 65536);
    return std::clamp (static_cast<int> (value), 0, 65536);
}

int encodeFLParameterReturnValue (int hostValue, bool fromMidi) noexcept
{
    const auto legacy = std::clamp (hostValue, 0, 65536);
    return fromMidi ? legacy : static_cast<int> (static_cast<std::int64_t> (legacy) * modernFLValueScale);
}

const char* changeModeName (ChangeMode mode) noexcept
{
    constexpr std::array names { "Off", "Restart", "1/8 beat", "1/4 beat", "1/2 beat", "1 beat", "2 beats" };
    return names[std::clamp (static_cast<int> (mode), 0, static_cast<int> (names.size()) - 1)];
}

const char* timingFeelName (TimingFeel feel) noexcept
{
    constexpr std::array names { "Straight", "Dotted", "Triplet" };
    return names[std::clamp (static_cast<int> (feel), 0, 2)];
}

const char* curveTypeName (CurveType type) noexcept
{
    constexpr std::array names { "Hold", "Single curve", "Double curve", "Half sine",
                                 "Stairs", "Smooth stairs", "Pulse", "Sine", "Triangle", "Saw" };
    return names[std::clamp (static_cast<int> (type), 0, static_cast<int> (names.size()) - 1)];
}

const char* factoryBankName (FactoryBank bank) noexcept
{
    constexpr std::array names { "Filter Motion Bank", "Volume and Gate Bank",
                                 "Melodic Bank -12 to +12", "Melodic Bank -24 to +24" };
    return names[std::clamp (static_cast<int> (bank), 0, static_cast<int> (names.size()) - 1)];
}

const char* divisionName (int division) noexcept { return divisionNames[std::clamp<int> (division, 0, static_cast<int> (divisionNames.size()) - 1)]; }
double divisionBeats (int division) noexcept { return divisionLengths[std::clamp<int> (division, 0, static_cast<int> (divisionLengths.size()) - 1)]; }
} // namespace stepshaper
