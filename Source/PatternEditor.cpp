#include "PatternEditor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace stepshaper
{
namespace
{
const auto background = juce::Colour (0xff202837);
const auto gridMajor = juce::Colour (0xff4a566c);
const auto gridMinor = juce::Colour (0xff354156);
const auto accent = juce::Colour (0xffed3035);
const auto accentSoft = juce::Colour (0xffb71f25);
const auto activeDot = juce::Colour (0xff8dde45);
const auto startAccent = juce::Colour (0xffffa51f);
}

PatternEditor::PatternEditor (Model& modelToUse) : model (modelToUse)
{
    // The graph always paints its entire bounds. Declaring it opaque prevents the
    // host/desktop compositor from briefly exposing the detached window beneath it
    // while the peer is being moved or resized.
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

bool PatternEditor::controlDown (const juce::ModifierKeys& modifiers)
{
    return modifiers.isCtrlDown() || modifiers.isCommandDown();
}

void PatternEditor::setLane (int laneToShow)
{
    const auto nextLane = std::clamp (laneToShow, 0, maxLanes - 1);
    const auto state = model.snapshot();
    const auto nextSlot = state->lanes[nextLane].selectedPattern;
    if (nextLane == lane && nextSlot == displayedSlot)
    {
        repaint();
        return;
    }
    lane = nextLane;
    displayedSlot = nextSlot;
    smoothPreview.reset();
    smoothPreviewSource.reset();
    activePoint = activeTension = -1;
    activeStartPosition = brushPainting = freeDrawing = marqueeSelecting = false;
    selected.fill (false);
    repaint();
}

void PatternEditor::setGrid (int xDivisions, int yDivisions)
{
    xDivisions = std::clamp (xDivisions, 1, 48);
    yDivisions = std::clamp (yDivisions, 1, 48);
    if (gridX == xDivisions && gridY == yDivisions) return;
    gridX = xDivisions;
    gridY = yDivisions;
    repaint();
}

void PatternEditor::setDrawMode (DrawMode mode)
{
    drawMode = static_cast<DrawMode> (std::clamp (static_cast<int> (mode), 0, 4));
}

void PatternEditor::setPlayhead (float phase)
{
    phase -= std::floor (phase);
    if (std::abs (phase - playhead) < 0.00025f) return;
    const auto area = plotArea();
    const auto oldX = area.getX() + area.getWidth() * playhead;
    const auto newX = area.getX() + area.getWidth() * phase;
    playhead = phase;
    const auto strip = [&area] (float x)
    {
        return juce::Rectangle<float>::leftTopRightBottom (
            x - 7.0f, area.getY() - 2.0f, x + 7.0f, area.getBottom() + 2.0f)
            .getSmallestIntegerContainer();
    };
    repaint (strip (oldX));
    repaint (strip (newX));
}

juce::Rectangle<float> PatternEditor::plotArea() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (18.0f)
        .withTrimmedRight (18.0f)
        .withTrimmedTop (32.0f)
        .withTrimmedBottom (16.0f);
}

juce::Point<float> PatternEditor::pointPosition (const Point& point) const
{
    const auto area = plotArea();
    return { area.getX() + point.x * area.getWidth(), area.getBottom() - point.y * area.getHeight() };
}

juce::Rectangle<float> PatternEditor::startFlagBounds (float startPosition) const
{
    const auto area = plotArea();
    const auto x = area.getX() + std::clamp (startPosition, 0.0f, 1.0f) * area.getWidth();
    constexpr float width = 15.0f;
    return { x - width * 0.5f, area.getY() - 21.0f, width, 16.0f };
}

juce::Rectangle<float> PatternEditor::sustainFlagBounds (float sustainPosition) const
{
    const auto area = plotArea();
    const auto x = area.getX() + std::clamp (sustainPosition, 0.0f, 1.0f) * area.getWidth();
    constexpr float width = 15.0f;
    return { x - width * 0.5f, area.getY() - 21.0f, width, 16.0f };
}

void PatternEditor::paint (juce::Graphics& g)
{
    const auto area = plotArea();
    g.fillAll (background);
    g.setColour (juce::Colour (0xff283348));
    g.fillRoundedRectangle (area, 5.0f);

    const auto xMajorEvery = std::max (1, gridX / 4);
    const auto yMajorEvery = std::max (1, gridY / 4);
    for (int x = 0; x <= gridX; ++x)
    {
        g.setColour (x % xMajorEvery == 0 ? gridMajor : gridMinor);
        const auto px = area.getX() + area.getWidth() * static_cast<float> (x) / static_cast<float> (gridX);
        g.drawVerticalLine (static_cast<int> (px), area.getY(), area.getBottom());
    }
    for (int y = 0; y <= gridY; ++y)
    {
        g.setColour (y % yMajorEvery == 0 ? gridMajor : gridMinor);
        const auto py = area.getY() + area.getHeight() * static_cast<float> (y) / static_cast<float> (gridY);
        g.drawHorizontalLine (static_cast<int> (py), area.getX(), area.getRight());
    }

    const auto state = model.snapshot();
    const auto& l = state->lanes[lane];
    const auto& basePattern = smoothPreview.has_value() ? *smoothPreview
                                                        : patternBank (l).patterns[l.selectedPattern];
    const auto pattern = basePattern.withModulation (l.mod1, l.mod2);
    juce::Path fill, curve, selectedCurve;
    const auto first = pointPosition (pattern.points[0]);
    curve.startNewSubPath (first);
    fill.startNewSubPath (first.x, area.getBottom());
    fill.lineTo (first);
    for (int segment = 0; segment + 1 < pattern.count; ++segment)
    {
        const auto& a = pattern.points[segment];
        const auto& b = pattern.points[segment + 1];
        const auto selectedSegment = segment == activeTension;
        if (selectedSegment) selectedCurve.startNewSubPath (pointPosition (a));
        const auto width = b.x - a.x;
        if (width <= 0.000001f)
        {
            const auto p = pointPosition (b);
            curve.lineTo (p); fill.lineTo (p); if (selectedSegment) selectedCurve.lineTo (p);
            continue;
        }
        const auto samples = std::max (2, static_cast<int> (std::ceil (width * 384.0f)));
        for (int sample = 1; sample <= samples; ++sample)
        {
            const auto t = static_cast<float> (sample) / static_cast<float> (samples);
            const auto p = pointPosition ({ a.x + width * t,
                                            curveSegment (a.y, b.y, t, a.tension, a.curve), 0.0f });
            curve.lineTo (p); fill.lineTo (p); if (selectedSegment) selectedCurve.lineTo (p);
        }
    }
    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.24f), area.getX(), area.getY(),
                                             accent.withAlpha (0.02f), area.getX(), area.getBottom(), false));
    g.fillPath (fill);
    g.setColour (accent);
    g.strokePath (curve, juce::PathStrokeType (1.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (! selectedCurve.isEmpty())
    {
        g.setColour (startAccent);
        g.strokePath (selectedCurve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
    }

    const auto paintMarker = [&] (float position, juce::Rectangle<float> flag, const char* label, bool active)
    {
        const auto markerX = area.getX() + position * area.getWidth();
        g.setColour (juce::Colour (0xffc4ccd1).withAlpha (active ? 0.95f : 0.60f));
        g.drawLine (markerX, area.getY(), markerX, area.getBottom(), active ? 1.7f : 1.0f);
        g.setColour (active ? juce::Colours::white : juce::Colour (0xffaeb9c0));
        g.fillRect (flag);
        juce::Path pointer;
        pointer.addTriangle (markerX - 3.5f, flag.getBottom() - 1.0f,
                             markerX + 3.5f, flag.getBottom() - 1.0f, markerX, flag.getBottom() + 4.0f);
        g.fillPath (pointer);
        g.setColour (juce::Colour (0xff263039));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (label, flag, juce::Justification::centred);
    };
    paintMarker (l.startPosition, startFlagBounds (l.startPosition), "L", activeStartPosition);
    if (l.midiTrigger)
        paintMarker (l.sustainPosition, sustainFlagBounds (l.sustainPosition), "S", activeSustainPosition);

    for (int i = 0; i + 1 < pattern.count; ++i)
    {
        const auto& a = pattern.points[i];
        const auto& b = pattern.points[i + 1];
        const auto handle = pointPosition ({ (a.x + b.x) * 0.5f,
                                             curveSegment (a.y, b.y, 0.5f, a.tension, a.curve), 0.0f });
        g.setColour (i == activeTension ? startAccent : accentSoft);
        g.drawEllipse (handle.x - 4.25f, handle.y - 4.25f, 8.5f, 8.5f, 1.35f);
        for (int source = 0; source < 2; ++source)
            if (std::abs (pattern.points[i].modTension[source]) > 0.0001f)
            {
                g.setColour (juce::Colour (0xff4fc3f7));
                g.fillEllipse (handle.x + 4.5f + source * 4.0f, handle.y - 7.0f, 3.2f, 3.2f);
            }
    }

    for (int i = 0; i < pattern.count; ++i)
    {
        const auto p = pointPosition (pattern.points[i]);
        const auto isSelected = selected[static_cast<std::size_t> (i)] || i == activePoint;
        if (isSelected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.72f));
            g.drawEllipse (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f, 1.4f);
            g.setColour (i == activePoint ? juce::Colours::white : activeDot);
            g.fillEllipse (p.x - 5.0f, p.y - 5.0f, 10.0f, 10.0f);
            g.setColour (background);
            g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
        }
        else
        {
            g.setColour (background);
            g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
            g.setColour ((i == 0 || i == pattern.count - 1) ? startAccent : accent);
            g.drawEllipse (p.x - 3.5f, p.y - 3.5f, 7.0f, 7.0f, 1.35f);
        }
        for (int source = 0; source < 2; ++source)
            if (std::abs (pattern.points[i].modX[source]) > 0.0001f
                || std::abs (pattern.points[i].modY[source]) > 0.0001f)
            {
                g.setColour (juce::Colour (0xff4fc3f7));
                g.fillEllipse (p.x + 4.5f + source * 4.0f, p.y - 7.0f, 3.2f, 3.2f);
            }
    }

    if (draggedModSource >= 0 && hoveredModPoint >= 0)
    {
        const auto colour = juce::Colour (0xff4fc3f7);
        juce::Point<float> anchor;
        if (hoveredModTarget == ModTarget::tension)
        {
            const auto& a = pattern.points[hoveredModPoint]; const auto& b = pattern.points[hoveredModPoint + 1];
            anchor = pointPosition ({ (a.x + b.x) * 0.5f, curveSegment (a.y, b.y, 0.5f, a.tension, a.curve), 0.0f });
            auto bubble = juce::Rectangle<float> (22.0f, 16.0f).withCentre (anchor + juce::Point<float> (0.0f, -16.0f));
            g.setColour (juce::Colour (0xff1e2429)); g.fillRoundedRectangle (bubble, 3.0f);
            g.setColour (colour); g.drawRoundedRectangle (bubble, 3.0f, 1.2f);
            g.drawText ("T", bubble, juce::Justification::centred);
        }
        else
        {
            anchor = pointPosition (pattern.points[hoveredModPoint]);
            for (int axis = 0; axis < 2; ++axis)
            {
                auto bubble = juce::Rectangle<float> (22.0f, 16.0f).withCentre (
                    anchor + juce::Point<float> (axis == 0 ? -15.0f : 15.0f, -17.0f));
                g.setColour (juce::Colour (0xff1e2429)); g.fillRoundedRectangle (bubble, 3.0f);
                g.setColour ((axis == 0 && hoveredModTarget == ModTarget::pointX)
                             || (axis == 1 && hoveredModTarget == ModTarget::pointY) ? colour : colour.withAlpha (0.5f));
                g.drawRoundedRectangle (bubble, 3.0f, 1.2f);
                g.drawText (axis == 0 ? "X" : "Y", bubble, juce::Justification::centred);
            }
        }
    }

    if (marqueeSelecting)
    {
        const auto selection = juce::Rectangle<float>::leftTopRightBottom (
            std::min (dragStart.x, marqueeEnd.x), std::min (dragStart.y, marqueeEnd.y),
            std::max (dragStart.x, marqueeEnd.x), std::max (dragStart.y, marqueeEnd.y));
        g.setColour (accent.withAlpha (0.10f));
        g.fillRect (selection);
        g.setColour (accent.withAlpha (0.78f));
        g.drawRect (selection, 1.0f);
    }

    const auto playheadX = area.getX() + playhead * area.getWidth();
    g.setColour (juce::Colours::white.withAlpha (0.72f));
    g.drawLine (playheadX, area.getY(), playheadX, area.getBottom(), 1.0f);
    juce::Path marker;
    marker.addTriangle (playheadX - 5.0f, area.getY(), playheadX + 5.0f, area.getY(), playheadX, area.getY() + 7.0f);
    g.fillPath (marker);

    if (showEditorHelp)
    {
        g.setColour (juce::Colour (0xff9ca6b2));
        g.setFont (10.5f);
        g.drawText ("DOUBLE-CLICK ADD/REMOVE  |  RIGHT-CLICK MIDPOINT: CURVE TYPE  |  DRAG MIDPOINT: TENSION",
                    area.withY (area.getY() - 25.0f).withHeight (16.0f), juce::Justification::centred);
    }
}

int PatternEditor::nearestPoint (juce::Point<float> position, float radius) const
{
    const auto state = model.snapshot();
    const auto& l = state->lanes[lane];
    const auto p = patternBank (l).patterns[l.selectedPattern].withModulation (l.mod1, l.mod2);
    int nearest = -1;
    auto best = radius;
    for (int i = 0; i < p.count; ++i)
    {
        const auto distance = pointPosition (p.points[i]).getDistanceFrom (position);
        if (distance < best) { best = distance; nearest = i; }
    }
    return nearest;
}

int PatternEditor::nearestTension (juce::Point<float> position, float radius) const
{
    const auto state = model.snapshot();
    const auto& l = state->lanes[lane];
    const auto p = patternBank (l).patterns[l.selectedPattern].withModulation (l.mod1, l.mod2);
    int nearest = -1;
    auto best = radius;
    for (int i = 0; i + 1 < p.count; ++i)
    {
        const auto& a = p.points[i];
        const auto& b = p.points[i + 1];
        const auto handle = pointPosition ({ (a.x + b.x) * .5f,
                                             curveSegment (a.y, b.y, .5f, a.tension, a.curve), 0.0f });
        const auto distance = handle.getDistanceFrom (position);
        if (distance < best) { best = distance; nearest = i; }
    }
    return nearest;
}

void PatternEditor::mouseDown (const juce::MouseEvent& event)
{
    mouseMove (event);
    if (onCanvasClicked) onCanvasClicked();
    grabKeyboardFocus();
    model.beginGesture();
    const auto state = model.snapshot();
    const auto& laneState = state->lanes[lane];
    const auto& pattern = patternBank (laneState).patterns[laneState.selectedPattern];
    const auto popup = event.mods.isPopupMenu() || event.mods.isRightButtonDown();
    const auto startHit = startFlagBounds (laneState.startPosition).expanded (3.0f, 2.0f).contains (event.position);
    const auto sustainHit = laneState.midiTrigger
        && sustainFlagBounds (laneState.sustainPosition).expanded (3.0f, 2.0f).contains (event.position);
    if (popup && laneState.midiTrigger && startHit)
    {
        model.endGesture();
        showStartFlagMenu (event.getScreenPosition());
        return;
    }
    if (popup && sustainHit)
    {
        model.endGesture();
        showSustainFlagMenu (event.getScreenPosition());
        return;
    }
    activeStartPosition = ! popup && startHit;
    activeSustainPosition = ! popup && ! activeStartPosition && sustainHit;
    if (activeStartPosition)
    {
        activePoint = activeTension = -1;
        repaint();
        return;
    }
    if (activeSustainPosition)
    {
        activePoint = activeTension = -1;
        repaint();
        return;
    }

    if (drawMode != DrawMode::edit && plotArea().contains (event.position))
    {
        selected.fill (false);
        brushPainting = true;
        lastPaintedCell = -1;
        paintSection (event.position, event.mods.isAltDown());
        return;
    }
    if (drawMode == DrawMode::edit && event.mods.isShiftDown() && plotArea().contains (event.position))
    {
        selected.fill (false);
        freeDrawing = true;
        lastPaintedCell = -1;
        freeDrawPoint (event.position, event.mods.isAltDown());
        return;
    }

    if (popup)
    {
        const auto segment = nearestTension (event.position);
        if (segment >= 0)
        {
            model.endGesture();
            showCurveTypeMenu (segment, event.getScreenPosition());
            return;
        }
    }
    activePoint = nearestPoint (event.position);
    if (popup && activePoint >= 0)
    {
        removePoint (activePoint);
        activePoint = -1;
        return;
    }

    activeTension = activePoint >= 0 ? -1 : nearestTension (event.position);
    if (activeTension >= 0)
    {
        dragStart = event.position;
        originalTension = pattern.points[activeTension].tension;
        repaint();
        return;
    }

    if (activePoint < 0 && controlDown (event.mods) && plotArea().contains (event.position))
    {
        marqueeSelecting = true;
        dragStart = marqueeEnd = event.position;
        return;
    }
    if (activePoint < 0)
    {
        if (! controlDown (event.mods)) selected.fill (false);
        repaint();
        return;
    }

    if (controlDown (event.mods))
        selected[static_cast<std::size_t> (activePoint)] = true;
    else if (! selected[static_cast<std::size_t> (activePoint)])
    {
        selected.fill (false);
        selected[static_cast<std::size_t> (activePoint)] = true;
    }
    dragStart = event.position;
    for (int i = 0; i < pattern.count; ++i)
    {
        originalXs[static_cast<std::size_t> (i)] = pattern.points[i].x;
        originalYs[static_cast<std::size_t> (i)] = pattern.points[i].y;
    }
    repaint();
}

void PatternEditor::mouseMove (const juce::MouseEvent& event)
{
    const auto nextShowHelp = plotArea().contains (event.position);
    if (showEditorHelp != nextShowHelp) { showEditorHelp = nextShowHelp; repaint(); }
    if (! onHint) return;
    const auto state = model.snapshot();
    const auto& l = state->lanes[lane];
    const auto& pattern = patternBank (l).patterns[l.selectedPattern];
    const auto point = nearestPoint (event.position, 10.0f);
    if (point >= 0)
    {
        const auto& p = pattern.points[point];
        onHint (("Point " + juce::String (point + 1) + "/" + juce::String (pattern.count)
                 + ": " + juce::String (p.y, 3)).toStdString());
        return;
    }
    const auto tension = nearestTension (event.position, 10.0f);
    if (tension >= 0)
    {
        onHint (("Tension " + juce::String (tension + 1) + "/" + juce::String (pattern.count - 1)
                 + ": " + juce::String (pattern.points[tension].tension, 3)).toStdString());
        return;
    }
    if (startFlagBounds (l.startPosition).expanded (3.0f, 2.0f).contains (event.position))
    {
        onHint (("Loop Start: " + juce::String (l.startPosition * 100.0f, 1) + "%").toStdString());
        return;
    }
    if (l.midiTrigger && sustainFlagBounds (l.sustainPosition).expanded (3.0f, 2.0f).contains (event.position))
    {
        onHint (("Sustain / Loop End: " + juce::String (l.sustainPosition * 100.0f, 1) + "%").toStdString());
        return;
    }
    onHint ("Pattern editor");
}

void PatternEditor::mouseExit (const juce::MouseEvent&)
{
    if (! showEditorHelp) return;
    showEditorHelp = false;
    repaint();
}

void PatternEditor::updateModDrag (int source, juce::Point<float> localPosition)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    draggedModSource = std::clamp (source, 0, 1);
    hoveredModPoint = nearestTension (localPosition, 13.0f);
    hoveredModTarget = hoveredModPoint >= 0 ? ModTarget::tension : ModTarget::none;
    if (hoveredModPoint < 0)
    {
        hoveredModPoint = nearestPoint (localPosition, 30.0f);
        if (hoveredModPoint >= 0)
        {
            const auto state = model.snapshot();
            const auto& l = state->lanes[lane];
            const auto effective = patternBank (l).patterns[l.selectedPattern].withModulation (l.mod1, l.mod2);
            const auto point = pointPosition (effective.points[hoveredModPoint]);
            hoveredModTarget = localPosition.x < point.x ? ModTarget::pointX : ModTarget::pointY;
        }
    }
    repaint();
}

void PatternEditor::finishModDrag (int source, juce::Point<float> localPosition)
{
    updateModDrag (source, localPosition);
    if (hoveredModPoint >= 0 && hoveredModTarget != ModTarget::none)
        setModDepth (source, { hoveredModPoint, hoveredModTarget, 0.25f }, 0.25f);
    draggedModSource = -1; hoveredModPoint = -1; hoveredModTarget = ModTarget::none;
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    repaint();
}

std::vector<PatternEditor::ModRoute> PatternEditor::modRoutes (int source) const
{
    std::vector<ModRoute> result;
    source = std::clamp (source, 0, 1);
    const auto state = model.snapshot();
    const auto& pattern = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
    for (int point = 0; point < pattern.count; ++point)
    {
        const auto& p = pattern.points[point];
        if (std::abs (p.modX[source]) > 0.0001f) result.push_back ({ point, ModTarget::pointX, p.modX[source] });
        if (std::abs (p.modY[source]) > 0.0001f) result.push_back ({ point, ModTarget::pointY, p.modY[source] });
        if (point + 1 < pattern.count && std::abs (p.modTension[source]) > 0.0001f)
            result.push_back ({ point, ModTarget::tension, p.modTension[source] });
    }
    return result;
}

void PatternEditor::setModDepth (int source, ModRoute route, float depth)
{
    source = std::clamp (source, 0, 1);
    const auto state = model.snapshot(); const auto slot = state->lanes[lane].selectedPattern;
    model.mutate ([=] (ProjectState& s)
    {
        auto& pattern = editPatternBank (s.lanes[lane]).patterns[slot];
        if (route.point < 0 || route.point >= pattern.count) return;
        auto& point = pattern.points[route.point];
        const auto value = std::clamp (depth, -1.0f, 1.0f);
        if (route.target == ModTarget::pointX) point.modX[source] = value;
        if (route.target == ModTarget::pointY) point.modY[source] = value;
        if (route.target == ModTarget::tension && route.point + 1 < pattern.count) point.modTension[source] = value;
    });
    repaint();
}

void PatternEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (activeStartPosition) { updateStartPosition (event.position, event.mods.isAltDown()); return; }
    if (activeSustainPosition) { updateSustainPosition (event.position, event.mods.isAltDown()); return; }
    if (brushPainting) { paintSection (event.position, event.mods.isAltDown()); return; }
    if (freeDrawing) { freeDrawPoint (event.position, event.mods.isAltDown()); return; }
    if (marqueeSelecting)
    {
        marqueeEnd = event.position;
        const auto selection = juce::Rectangle<float>::leftTopRightBottom (
            std::min (dragStart.x, marqueeEnd.x), std::min (dragStart.y, marqueeEnd.y),
            std::max (dragStart.x, marqueeEnd.x), std::max (dragStart.y, marqueeEnd.y));
        const auto state = model.snapshot();
        const auto& p = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
        selected.fill (false);
        for (int i = 0; i < p.count; ++i)
            selected[static_cast<std::size_t> (i)] = selection.contains (pointPosition (p.points[i]));
        repaint();
        return;
    }
    if (activePoint >= 0) updatePoint (event.position, event.mods.isAltDown());
    else if (activeTension >= 0) updateTension (event.position);
}

void PatternEditor::mouseUp (const juce::MouseEvent&)
{
    model.endGesture();
    activePoint = activeTension = -1;
    activeStartPosition = activeSustainPosition = brushPainting = freeDrawing = marqueeSelecting = false;
    lastPaintedCell = -1;
    repaint();
}

void PatternEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown() || ! plotArea().contains (event.position)) return;
    const auto existing = nearestPoint (event.position, 9.0f);
    if (existing >= 0) removePoint (existing);
    else insertPoint (event.position, event.mods.isAltDown());
}

void PatternEditor::appendPoint (Pattern& pattern, float x, float y, float tension, CurveType curve)
{
    if (pattern.count >= maxPoints) return;
    pattern.points[pattern.count++] = { std::clamp (x, 0.0f, 1.0f), std::clamp (y, 0.0f, 1.0f),
                                        std::clamp (tension, -1.0f, 1.0f), curve };
}

void PatternEditor::upsertPoint (Pattern& pattern, float x, float y)
{
    x = std::clamp (x, 0.0f, 1.0f);
    y = std::clamp (y, 0.0f, 1.0f);
    for (int i = 0; i < pattern.count; ++i)
        if (std::abs (pattern.points[i].x - x) < 0.0002f)
        {
            pattern.points[i].x = x;
            pattern.points[i].y = y;
            pattern.points[i].tension = 0.0f;
            return;
        }
    appendPoint (pattern, x, y);
}

void PatternEditor::insertPoint (juce::Point<float> position, bool snapY)
{
    const auto area = plotArea();
    if (! area.contains (position)) return;
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    if (patternBank (state->lanes[lane]).patterns[slot].count >= maxPoints) return;
    auto x = std::clamp ((position.x - area.getX()) / area.getWidth(), 0.001f, 0.999f);
    auto y = std::clamp ((area.getBottom() - position.y) / area.getHeight(), 0.0f, 1.0f);
    if (snapY)
    {
        x = std::round (x * gridX) / static_cast<float> (gridX);
        y = std::round (y * gridY) / static_cast<float> (gridY);
    }
    model.mutate ([=] (ProjectState& s)
    {
        auto& p = editPatternBank (s.lanes[lane]).patterns[slot];
        appendPoint (p, x, y);
        p.normalise();
    });
    repaint();
}

void PatternEditor::removePoint (int point)
{
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    const auto count = patternBank (state->lanes[lane]).patterns[slot].count;
    if (point <= 0 || point + 1 >= count) return;
    model.mutate ([=] (ProjectState& s)
    {
        auto& p = editPatternBank (s.lanes[lane]).patterns[slot];
        std::move (p.points.begin() + point + 1, p.points.begin() + p.count, p.points.begin() + point);
        --p.count;
        p.normalise();
    });
    selected.fill (false);
    repaint();
}

void PatternEditor::deleteSelected()
{
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    const auto& current = patternBank (state->lanes[lane]).patterns[slot];
    bool any = false;
    for (int i = 1; i + 1 < current.count; ++i)
        any = any || selected[static_cast<std::size_t> (i)];
    if (! any) return;
    model.mutate ([=, this] (ProjectState& s)
    {
        auto& bank = editPatternBank (s.lanes[lane]);
        const auto old = bank.patterns[slot];
        Pattern next;
        for (int i = 0; i < old.count; ++i)
            if (i == 0 || i + 1 == old.count || ! selected[static_cast<std::size_t> (i)])
                appendPoint (next, old.points[i].x, old.points[i].y, old.points[i].tension, old.points[i].curve);
        next.normalise();
        bank.patterns[slot] = next;
    });
    selected.fill (false);
    repaint();
}

void PatternEditor::updatePoint (juce::Point<float> position, bool snapToGrid)
{
    const auto area = plotArea();
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    const auto& current = patternBank (state->lanes[lane]).patterns[slot];
    if (activePoint < 0 || activePoint >= current.count) return;

    const auto rawDelta = position - dragStart;
    auto targetX = originalXs[static_cast<std::size_t> (activePoint)] + rawDelta.x / area.getWidth();
    auto targetY = originalYs[static_cast<std::size_t> (activePoint)] - rawDelta.y / area.getHeight();
    if (snapToGrid)
    {
        targetX = std::round (targetX * gridX) / static_cast<float> (gridX);
        targetY = std::round (targetY * gridY) / static_cast<float> (gridY);
    }
    auto dx = targetX - originalXs[static_cast<std::size_t> (activePoint)];
    auto dy = targetY - originalYs[static_cast<std::size_t> (activePoint)];

    float minDy = -1.0f, maxDy = 1.0f;
    for (int i = 0; i < current.count; ++i)
        if (selected[static_cast<std::size_t> (i)])
        {
            minDy = std::max (minDy, -originalYs[static_cast<std::size_t> (i)]);
            maxDy = std::min (maxDy, 1.0f - originalYs[static_cast<std::size_t> (i)]);
        }
    dy = std::clamp (dy, minDy, maxDy);

    if (activePoint == 0 || activePoint + 1 == current.count)
        dx = 0.0f;
    else
    {
        float minDx = -1.0f, maxDx = 1.0f;
        for (int i = 1; i + 1 < current.count; ++i)
            if (selected[static_cast<std::size_t> (i)])
            {
                int lower = i - 1;
                while (lower > 0 && selected[static_cast<std::size_t> (lower)]) --lower;
                int upper = i + 1;
                while (upper + 1 < current.count && selected[static_cast<std::size_t> (upper)]) ++upper;
                minDx = std::max (minDx, originalXs[static_cast<std::size_t> (lower)] + 0.0002f
                                           - originalXs[static_cast<std::size_t> (i)]);
                maxDx = std::min (maxDx, originalXs[static_cast<std::size_t> (upper)] - 0.0002f
                                           - originalXs[static_cast<std::size_t> (i)]);
            }
        dx = std::clamp (dx, minDx, maxDx);
    }

    model.mutate ([=, this] (ProjectState& s)
    {
        auto& p = editPatternBank (s.lanes[lane]).patterns[slot];
        for (int i = 0; i < p.count; ++i)
            if (selected[static_cast<std::size_t> (i)])
            {
                p.points[i].x = i == 0 ? 0.0f : i + 1 == p.count ? 1.0f
                                  : originalXs[static_cast<std::size_t> (i)] + dx;
                p.points[i].y = originalYs[static_cast<std::size_t> (i)] + dy;
            }
        p.normalise();
    });
    repaint();
}

void PatternEditor::updateTension (juce::Point<float> position)
{
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    const auto& pattern = patternBank (state->lanes[lane]).patterns[slot];
    const auto rising = pattern.points[activeTension + 1].y >= pattern.points[activeTension].y;
    const auto direction = rising ? 1.0f : -1.0f;
    // Tension shapes time along the segment, so its visible vertical direction flips
    // for falling segments. Compensating here keeps the handle under the mouse:
    // dragging upward always bends the curve upward, and vice versa.
    const auto tension = std::clamp (originalTension - (position.y - dragStart.y) / 80.0f * direction,
                                     -1.0f, 1.0f);
    model.mutate ([=] (ProjectState& s)
    {
        editPatternBank (s.lanes[lane]).patterns[slot].points[activeTension].tension = tension;
    });
    repaint();
}

void PatternEditor::showCurveTypeMenu (int segment, juce::Point<int> screenPosition)
{
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    const auto& pattern = patternBank (state->lanes[lane]).patterns[slot];
    if (segment < 0 || segment + 1 >= pattern.count) return;

    juce::PopupMenu menu;
    for (int type = 0; type < static_cast<int> (CurveType::count); ++type)
        menu.addItem (type + 1, curveTypeName (static_cast<CurveType> (type)), true,
                      type == static_cast<int> (pattern.points[segment].curve));
    const auto safe = juce::Component::SafePointer<PatternEditor> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe, segment, slot] (int result)
                        {
                            if (safe == nullptr || result <= 0) return;
                            const auto type = static_cast<CurveType> (result - 1);
                            safe->model.mutate ([lane = safe->lane, slot, segment, type] (ProjectState& s)
                            {
                                auto& pattern = editPatternBank (s.lanes[lane]).patterns[slot];
                                if (segment >= 0 && segment + 1 < pattern.count)
                                    pattern.points[segment].curve = type;
                            });
                            safe->repaint();
                        });
}

void PatternEditor::showStartFlagMenu (juce::Point<int> screenPosition)
{
    const auto state = model.snapshot();
    const auto mode = state->lanes[lane].midiStartMode;
    juce::PopupMenu menu;
    menu.addSectionHeader ("Start flag behavior");
    menu.addItem (1, "Start position", true, mode == MidiStartMode::startPosition);
    menu.addItem (2, "Loop-back position", true, mode == MidiStartMode::loopBack);
    const auto safe = juce::Component::SafePointer<PatternEditor> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result <= 0) return;
                            const auto selectedMode = result == 2 ? MidiStartMode::loopBack
                                                                  : MidiStartMode::startPosition;
                            safe->model.mutate ([lane = safe->lane, selectedMode] (ProjectState& s)
                            {
                                s.lanes[lane].midiStartMode = selectedMode;
                            });
                            safe->repaint();
                        });
}

void PatternEditor::showSustainFlagMenu (juce::Point<int> screenPosition)
{
    const auto state = model.snapshot();
    const auto& laneState = state->lanes[lane];
    juce::PopupMenu menu;
    menu.addSectionHeader ("Sustain / Loop End");
    menu.addItem (1, "Loop Forever", true, laneState.midiLoopForever);
    menu.addItem (2, "Sustain", true, laneState.midiSustain);
    menu.addItem (3, "Release", true, laneState.midiReleaseMode == MidiReleaseMode::release);
    const auto safe = juce::Component::SafePointer<PatternEditor> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result <= 0) return;
                            safe->model.mutate ([lane = safe->lane, result] (ProjectState& s)
                            {
                                auto& state = s.lanes[lane];
                                if (result == 1)
                                {
                                    state.midiLoopForever = ! state.midiLoopForever;
                                    if (state.midiLoopForever) state.midiSustain = false;
                                }
                                else if (result == 2)
                                {
                                    state.midiSustain = ! state.midiSustain;
                                    if (state.midiSustain) state.midiLoopForever = false;
                                }
                                else if (result == 3)
                                {
                                    state.midiReleaseMode = state.midiReleaseMode == MidiReleaseMode::release
                                        ? MidiReleaseMode::loopForever : MidiReleaseMode::release;
                                }
                            });
                            safe->repaint();
                        });
}

void PatternEditor::updateStartPosition (juce::Point<float> position, bool snapToGrid)
{
    const auto area = plotArea();
    auto start = std::clamp ((position.x - area.getX()) / area.getWidth(), 0.0f, 1.0f);
    if (snapToGrid) start = std::round (start * gridX) / static_cast<float> (gridX);
    model.mutate ([=] (ProjectState& s) { s.lanes[lane].startPosition = start; });
    if (onStartPositionChanged) onStartPositionChanged (lane);
    repaint();
}

void PatternEditor::updateSustainPosition (juce::Point<float> position, bool snapToGrid)
{
    const auto area = plotArea();
    auto requested = std::clamp ((position.x - area.getX()) / area.getWidth(), 0.0f, 1.0f);
    if (snapToGrid) requested = std::round (requested * gridX) / static_cast<float> (gridX);
    const auto state = model.snapshot();
    const auto sustain = std::max (state->lanes[lane].startPosition, requested);
    model.mutate ([=] (ProjectState& s) { s.lanes[lane].sustainPosition = sustain; });
    if (onSustainPositionChanged) onSustainPositionChanged (lane);
    repaint();
}

void PatternEditor::paintSection (juce::Point<float> position, bool snapY)
{
    const auto area = plotArea();
    const auto normalizedX = std::clamp ((position.x - area.getX()) / area.getWidth(), 0.0f, 0.999999f);
    auto y = std::clamp ((area.getBottom() - position.y) / area.getHeight(), 0.0f, 1.0f);
    if (snapY) y = std::round (y * gridY) / static_cast<float> (gridY);
    const auto cell = std::clamp (static_cast<int> (std::floor (normalizedX * gridX)), 0, gridX - 1);
    const auto cellLeft = static_cast<float> (cell) / static_cast<float> (gridX);
    const auto cellRight = static_cast<float> (cell + 1) / static_cast<float> (gridX);
    const auto leftY = drawMode == DrawMode::rampUp ? 0.0f : y;
    const auto rightY = drawMode == DrawMode::rampDown ? 0.0f : y;
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    if (drawMode == DrawMode::eraser)
    {
        model.mutate ([=] (ProjectState& s)
        {
            auto& bank = editPatternBank (s.lanes[lane]);
            const auto old = bank.patterns[slot];
            Pattern next;
            for (int i = 0; i < old.count; ++i)
            {
                const auto endpoint = i == 0 || i + 1 == old.count;
                if (endpoint || old.points[i].x < cellLeft - 0.00002f || old.points[i].x > cellRight + 0.00002f)
                    appendPoint (next, old.points[i].x, old.points[i].y, old.points[i].tension, old.points[i].curve);
            }
            if (next.count < 2) next = Pattern::factory (0);
            for (int i = 0; i + 1 < next.count; ++i)
                if (next.points[i].x <= cellLeft && next.points[i + 1].x >= cellRight)
                {
                    next.points[i].tension = 0.0f;
                    next.points[i].curve = CurveType::singleCurve;
                    break;
                }
            next.normalise();
            bank.patterns[slot] = next;
            bank.occupied[slot] = true;
        });
        lastPaintedCell = cell;
        repaint();
        return;
    }
    model.mutate ([=] (ProjectState& s)
    {
        auto& bank = editPatternBank (s.lanes[lane]);
        const auto old = bank.patterns[slot];
        constexpr float boundaryTolerance = 0.00002f;
        const auto hasLeftNeighbour = std::any_of (old.points.begin(), old.points.begin() + old.count,
                                                   [=] (const Point& p) { return p.x < cellLeft - boundaryTolerance; });
        const auto hasRightNeighbour = std::any_of (old.points.begin(), old.points.begin() + old.count,
                                                    [=] (const Point& p) { return p.x > cellRight + boundaryTolerance; });
        const auto leftNeighbourY = old.valueAt (std::max (0.0f, cellLeft - 0.0001f));
        const auto rightNeighbourY = old.valueAt (std::min (0.999999f, cellRight + 0.0001f));

        std::vector<Point> replacement;
        replacement.reserve (old.count + 4);
        for (int i = 0; i < old.count; ++i)
        {
            if (old.points[i].x < cellLeft - boundaryTolerance)
                replacement.push_back (old.points[i]);
        }
        if (! replacement.empty())
        {
            replacement.back().tension = 0.0f;
            replacement.back().curve = CurveType::singleCurve;
        }
        if (hasLeftNeighbour) replacement.push_back ({ cellLeft, leftNeighbourY, 0.0f, CurveType::singleCurve });
        replacement.push_back ({ cellLeft, leftY, 0.0f, CurveType::singleCurve });
        replacement.push_back ({ cellRight, rightY, 0.0f, CurveType::singleCurve });
        if (hasRightNeighbour) replacement.push_back ({ cellRight, rightNeighbourY, 0.0f, CurveType::singleCurve });
        for (int i = 0; i < old.count; ++i)
            if (old.points[i].x > cellRight + boundaryTolerance)
                replacement.push_back (old.points[i]);

        while (replacement.size() > maxPoints)
        {
            auto removable = replacement.end();
            for (auto it = replacement.begin() + 1; it + 1 != replacement.end(); ++it)
                if (std::abs (it->x - cellLeft) > boundaryTolerance
                    && std::abs (it->x - cellRight) > boundaryTolerance)
                {
                    removable = it;
                    break;
                }
            if (removable == replacement.end()) break;
            replacement.erase (removable);
        }
        Pattern next;
        for (const auto& point : replacement)
            appendPoint (next, point.x, point.y, point.tension, point.curve);
        next.normalise();
        bank.patterns[slot] = next;
        bank.occupied[slot] = true;
    });
    lastPaintedCell = cell;
    repaint();
}

void PatternEditor::freeDrawPoint (juce::Point<float> position, bool snapY)
{
    const auto area = plotArea();
    const auto rawX = std::clamp ((position.x - area.getX()) / area.getWidth(), 0.0f, 1.0f);
    const auto gridLine = std::clamp (static_cast<int> (std::lround (rawX * gridX)), 0, gridX);
    auto y = std::clamp ((area.getBottom() - position.y) / area.getHeight(), 0.0f, 1.0f);
    if (snapY) y = std::round (y * gridY) / static_cast<float> (gridY);
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    const auto previousLine = lastPaintedCell;
    const auto previousY = lastDrawY;
    model.mutate ([=] (ProjectState& s)
    {
        auto& pattern = editPatternBank (s.lanes[lane]).patterns[slot];
        if (previousLine < 0)
            upsertPoint (pattern, static_cast<float> (gridLine) / static_cast<float> (gridX), y);
        else
        {
            const auto direction = gridLine >= previousLine ? 1 : -1;
            const auto distance = std::max (1, std::abs (gridLine - previousLine));
            for (int line = previousLine; line != gridLine + direction; line += direction)
            {
                const auto phase = static_cast<float> (std::abs (line - previousLine)) / static_cast<float> (distance);
                auto lineY = previousY + (y - previousY) * phase;
                if (snapY) lineY = std::round (lineY * gridY) / static_cast<float> (gridY);
                upsertPoint (pattern, static_cast<float> (line) / static_cast<float> (gridX), lineY);
            }
        }
        pattern.normalise();
    });
    lastPaintedCell = gridLine;
    lastDrawY = y;
    repaint();
}

void PatternEditor::replaceCurrentPattern (const Pattern& pattern)
{
    smoothPreview.reset();
    smoothPreviewSource.reset();
    const auto state = model.snapshot();
    const auto slot = state->lanes[lane].selectedPattern;
    model.mutate ([=] (ProjectState& s)
    {
        auto& bank = editPatternBank (s.lanes[lane]);
        bank.patterns[slot] = pattern;
        bank.patterns[slot].normalise();
        bank.occupied[slot] = true;
    });
    selected.fill (false);
    repaint();
}

void PatternEditor::randomize()
{
    Pattern result;
    auto& random = juce::Random::getSystemRandom();
    for (int cell = 0; cell < gridX && result.count + 2 <= maxPoints;)
    {
        const auto style = random.nextInt (3);
        const auto length = std::min (gridX - cell, 1 + random.nextInt (4));
        const auto left = static_cast<float> (cell) / static_cast<float> (gridX);
        const auto right = static_cast<float> (cell + length) / static_cast<float> (gridX);
        if (style == 0)
        {
            appendPoint (result, left, random.nextFloat());
            appendPoint (result, right, random.nextFloat());
        }
        else if (style == 1)
        {
            appendPoint (result, left, random.nextFloat() < 0.2f ? 0.0f : 0.35f + 0.65f * random.nextFloat());
            appendPoint (result, right, 0.0f);
        }
        else
        {
            const auto level = random.nextFloat();
            appendPoint (result, left, level);
            appendPoint (result, right, level);
        }
        cell += length;
    }
    result.normalise();
    replaceCurrentPattern (result);
}

void PatternEditor::randomizeLines()
{
    Pattern result;
    auto& random = juce::Random::getSystemRandom();
    for (int line = 0; line <= gridX && result.count < maxPoints; ++line)
        appendPoint (result, static_cast<float> (line) / static_cast<float> (gridX), 0.05f + 0.9f * random.nextFloat());
    result.normalise();
    replaceCurrentPattern (result);
}

void PatternEditor::randomizeRampUps()
{
    Pattern result;
    auto& random = juce::Random::getSystemRandom();
    for (int cell = 0; cell < gridX && result.count + 2 <= maxPoints;)
    {
        const auto length = std::min (gridX - cell, 1 + random.nextInt (4));
        const auto left = static_cast<float> (cell) / static_cast<float> (gridX);
        const auto right = static_cast<float> (cell + length) / static_cast<float> (gridX);
        const auto top = random.nextFloat() < 0.18f ? 0.0f : 0.30f + 0.70f * random.nextFloat();
        appendPoint (result, left, 0.0f);
        appendPoint (result, right, top);
        cell += length;
    }
    result.normalise();
    replaceCurrentPattern (result);
}

void PatternEditor::randomizeRampDowns()
{
    Pattern result;
    auto& random = juce::Random::getSystemRandom();
    for (int cell = 0; cell < gridX && result.count + 2 <= maxPoints;)
    {
        const auto length = std::min (gridX - cell, 1 + random.nextInt (4));
        const auto left = static_cast<float> (cell) / static_cast<float> (gridX);
        const auto right = static_cast<float> (cell + length) / static_cast<float> (gridX);
        const auto top = random.nextFloat() < 0.22f ? 0.0f : 0.25f + 0.75f * random.nextFloat();
        appendPoint (result, left, top);
        appendPoint (result, right, 0.0f);
        cell += length;
    }
    result.normalise();
    replaceCurrentPattern (result);
}

void PatternEditor::randomizeSteps()
{
    Pattern result;
    auto& random = juce::Random::getSystemRandom();
    for (int cell = 0; cell < gridX && result.count + 2 <= maxPoints;)
    {
        const auto length = std::min (gridX - cell, 1 + random.nextInt (4));
        const auto level = std::round (random.nextFloat() * static_cast<float> (gridY)) / static_cast<float> (gridY);
        const auto left = static_cast<float> (cell) / static_cast<float> (gridX);
        const auto right = static_cast<float> (cell + length) / static_cast<float> (gridX);
        appendPoint (result, left, level);
        appendPoint (result, right, level);
        cell += length;
    }
    result.normalise();
    replaceCurrentPattern (result);
}

void PatternEditor::randomizeMelody (bool minor)
{
    constexpr std::array majorScale { 0, 2, 4, 5, 7, 9, 11 };
    constexpr std::array minorScale { 0, 2, 3, 5, 7, 8, 10 };
    const auto& scale = minor ? minorScale : majorScale;
    auto& random = juce::Random::getSystemRandom();
    const auto steps = std::clamp (gridX, 4, 16);
    const auto semitoneRange = std::max (4, gridY / 2);
    auto degree = random.nextInt (7);
    auto octave = 0;
    Pattern result;
    for (int step = 0; step < steps && result.count + 2 <= maxPoints; ++step)
    {
        if (step > 0)
        {
            degree += random.nextInt (5) - 2;
            while (degree < 0) { degree += 7; --octave; }
            while (degree >= 7) { degree -= 7; ++octave; }
        }
        auto semitone = scale[static_cast<std::size_t> (degree)] + octave * 12;
        semitone = std::clamp (semitone, -semitoneRange, semitoneRange);
        const auto level = std::clamp (0.5f + static_cast<float> (semitone) / static_cast<float> (gridY), 0.0f, 1.0f);
        const auto left = static_cast<float> (step) / static_cast<float> (steps);
        const auto right = static_cast<float> (step + 1) / static_cast<float> (steps);
        appendPoint (result, left, level);
        appendPoint (result, right, level);
    }
    result.normalise();
    replaceCurrentPattern (result);
}

void PatternEditor::resetPattern()
{
    replaceCurrentPattern (Pattern::factory (0));
}

void PatternEditor::flipVertically()
{
    const auto state = model.snapshot();
    auto pattern = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
    for (int point = 0; point < pattern.count; ++point)
    {
        pattern.points[point].y = 1.0f - pattern.points[point].y;
        for (auto& depth : pattern.points[point].modY) depth = -depth;
    }
    replaceCurrentPattern (pattern);
}

void PatternEditor::flipHorizontally()
{
    const auto state = model.snapshot();
    const auto source = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
    Pattern flipped;
    flipped.count = source.count;
    for (int point = 0; point < source.count; ++point)
    {
        auto transformed = source.points[source.count - 1 - point];
        transformed.x = 1.0f - transformed.x;
        for (auto& depth : transformed.modX) depth = -depth;
        if (point + 1 < source.count)
        {
            const auto& oldSegment = source.points[source.count - 2 - point];
            transformed.curve = oldSegment.curve;
            transformed.tension = -oldSegment.tension;
            for (int mod = 0; mod < 2; ++mod)
                transformed.modTension[mod] = -oldSegment.modTension[mod];
        }
        flipped.points[point] = transformed;
    }
    flipped.normalise();
    replaceCurrentPattern (flipped);
}

void PatternEditor::normalizeLevels()
{
    const auto state = model.snapshot();
    auto pattern = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
    auto low = 1.0f;
    auto high = 0.0f;
    for (int point = 0; point < pattern.count; ++point)
    {
        low = std::min (low, pattern.points[point].y);
        high = std::max (high, pattern.points[point].y);
    }
    const auto span = high - low;
    if (span <= 0.00001f) return;
    for (int point = 0; point < pattern.count; ++point)
    {
        pattern.points[point].y = (pattern.points[point].y - low) / span;
        for (auto& depth : pattern.points[point].modY) depth /= span;
    }
    replaceCurrentPattern (pattern);
}

bool PatternEditor::doubleCurrentPattern()
{
    const auto state = model.snapshot();
    const auto selectedLane = std::clamp (lane, 0, maxLanes - 1);
    const auto slot = state->lanes[selectedLane].selectedPattern;
    const auto doubled = patternBank (state->lanes[selectedLane]).patterns[slot].doubled();
    if (! doubled.has_value()) return false;

    model.mutate ([selectedLane, slot, pattern = *doubled] (ProjectState& s)
    {
        auto& target = s.lanes[selectedLane];
        auto& bank = editPatternBank (target);
        bank.patterns[slot] = pattern;
        bank.occupied[slot] = true;
        if (target.sync)
        {
            const auto wantedBeats = divisionBeats (target.division) * 2.0;
            int closest = 0;
            for (int candidate = 1; candidate < 27; ++candidate)
                if (std::abs (divisionBeats (candidate) - wantedBeats)
                    < std::abs (divisionBeats (closest) - wantedBeats))
                    closest = candidate;
            target.division = closest;
        }
        else target.speedHz = std::max (0.01f, target.speedHz * 0.5f);
    });
    selected.fill (false);
    activePoint = activeTension = -1;
    repaint();
    return true;
}

void PatternEditor::smoothAbruptChanges()
{
    const auto state = model.snapshot();
    const auto& source = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
    replaceCurrentPattern (makeSmoothedPattern (source, 0.24f, 0.32f, 0.0f));
}

void PatternEditor::smoothUp (float smooth, float decimation)
{
    const auto state = model.snapshot();
    const auto& source = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
    replaceCurrentPattern (makeFilteredTrace (source, smooth, decimation));
}

void PatternEditor::beginSmoothPreview()
{
    const auto state = model.snapshot();
    smoothPreviewSource = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
    smoothPreview = smoothPreviewSource;
    repaint();
}

void PatternEditor::updateSmoothPreview (float smooth, float decimation)
{
    if (! smoothPreviewSource.has_value()) beginSmoothPreview();
    smoothPreview = makeFilteredTrace (*smoothPreviewSource, smooth, decimation);
    repaint();
}

void PatternEditor::commitSmoothPreview()
{
    if (! smoothPreview.has_value()) return;
    const auto result = *smoothPreview;
    smoothPreview.reset();
    smoothPreviewSource.reset();
    replaceCurrentPattern (result);
}

void PatternEditor::cancelSmoothPreview()
{
    smoothPreview.reset();
    smoothPreviewSource.reset();
    repaint();
}

Pattern PatternEditor::makeSmoothedPattern (const Pattern& source, float attack, float release, float decimation)
{
    attack = std::clamp (attack, 0.0f, 1.0f);
    release = std::clamp (release, 0.0f, 1.0f);
    decimation = std::clamp (decimation, 0.0f, 1.0f);
    Pattern rounded;
    int verticalCount = 0;
    for (int point = 0; point + 1 < source.count; ++point)
        verticalCount += std::abs (source.points[point + 1].x - source.points[point].x) < 0.0002f
                      && std::abs (source.points[point + 1].y - source.points[point].y) > 0.0002f;
    const auto includeMidpoints = static_cast<int> (source.count) + verticalCount <= maxPoints;
    const auto append = [&rounded] (Point point)
    {
        if (rounded.count < maxPoints) rounded.points[rounded.count++] = point;
    };

    for (int point = 0; point < source.count;)
    {
        const auto vertical = point + 1 < source.count
                           && std::abs (source.points[point + 1].x - source.points[point].x) < 0.0002f
                           && std::abs (source.points[point + 1].y - source.points[point].y) > 0.0002f;
        if (! vertical)
        {
            append (source.points[point++]);
            continue;
        }

        const auto& before = source.points[point];
        const auto& after = source.points[point + 1];
        const auto x = before.x;
        const auto leftLimit = point > 0 ? source.points[point - 1].x : 0.0f;
        const auto rightLimit = point + 2 < source.count ? source.points[point + 2].x : 1.0f;
        const auto available = std::max (0.0f, std::min (x - leftLimit, rightLimit - x));
        const auto control = after.y > before.y ? attack : release;
        const auto radius = available * (0.04f + 0.40f * control);
        if (radius <= 0.0001f)
        {
            append (before); append (after); point += 2;
            continue;
        }

        auto left = before;
        left.x = x - radius;
        left.curve = CurveType::doubleCurve;
        left.tension = 0.0f;
        auto middle = before;
        middle.x = x;
        middle.y = (before.y + after.y) * 0.5f;
        middle.curve = CurveType::doubleCurve;
        middle.tension = 0.0f;
        for (int sourceIndex = 0; sourceIndex < 2; ++sourceIndex)
        {
            middle.modX[sourceIndex] = (before.modX[sourceIndex] + after.modX[sourceIndex]) * 0.5f;
            middle.modY[sourceIndex] = (before.modY[sourceIndex] + after.modY[sourceIndex]) * 0.5f;
            middle.modTension[sourceIndex] = 0.0f;
        }
        auto right = after;
        right.x = x + radius;
        append (left);
        if (includeMidpoints) append (middle);
        append (right);
        point += 2;
    }

    if (decimation > 0.001f && rounded.count > 3)
    {
        Pattern reduced;
        reduced.points[reduced.count++] = rounded.points[0];
        const auto tolerance = decimation * 0.035f;
        for (int point = 1; point + 1 < rounded.count; ++point)
        {
            const auto& a = reduced.points[reduced.count - 1];
            const auto& b = rounded.points[point];
            const auto& c = rounded.points[point + 1];
            const auto span = c.x - a.x;
            const auto expected = span > 0.0001f ? a.y + (c.y - a.y) * (b.x - a.x) / span : b.y;
            const auto hasMod = std::abs (b.modX[0]) + std::abs (b.modX[1]) + std::abs (b.modY[0])
                              + std::abs (b.modY[1]) + std::abs (b.modTension[0]) + std::abs (b.modTension[1]) > .0001f;
            if (hasMod || std::abs (b.y - expected) > tolerance || b.curve != CurveType::doubleCurve)
                reduced.points[reduced.count++] = b;
        }
        reduced.points[reduced.count++] = rounded.points[rounded.count - 1];
        rounded = reduced;
    }
    rounded.normalise();
    return rounded;
}

Pattern PatternEditor::makeFilteredTrace (const Pattern& source, float smooth, float decimation)
{
    smooth = std::clamp (smooth, 0.0f, 1.0f);
    decimation = std::clamp (decimation, 0.0f, 1.0f);
    constexpr int traceSamples = 257;
    std::vector<float> values (traceSamples);
    for (int sample = 0; sample < traceSamples; ++sample)
    {
        const auto phase = static_cast<float> (sample) / static_cast<float> (traceSamples - 1);
        values[static_cast<std::size_t> (sample)] = sample + 1 == traceSamples
            ? source.points[source.count - 1].y : source.valueAt (phase);
    }
    const auto originalStart = values.front();
    const auto originalEnd = values.back();

    if (smooth > 0.0001f)
    {
        const auto radius = 1 + juce::roundToInt (smooth * 31.0f);
        const auto passes = 2 + juce::roundToInt (smooth * 2.0f);
        std::vector<float> filtered (traceSamples);
        for (int pass = 0; pass < passes; ++pass)
        {
            filtered.front() = originalStart;
            filtered.back() = originalEnd;
            for (int sample = 1; sample + 1 < traceSamples; ++sample)
            {
                const auto first = std::max (0, sample - radius);
                const auto last = std::min (traceSamples - 1, sample + radius);
                auto total = 0.0f;
                for (int tap = first; tap <= last; ++tap) total += values[static_cast<std::size_t> (tap)];
                filtered[static_cast<std::size_t> (sample)] = total / static_cast<float> (last - first + 1);
            }
            values.swap (filtered);
        }
        values.front() = originalStart;
        values.back() = originalEnd;
    }

    const auto density = std::pow (1.0f - decimation, 1.45f);
    const auto outputCount = std::clamp (2 + juce::roundToInt (density * static_cast<float> (maxPoints - 2)),
                                         2, maxPoints);
    Pattern result;
    result.count = static_cast<std::uint8_t> (outputCount);
    for (int point = 0; point < outputCount; ++point)
    {
        const auto phase = static_cast<float> (point) / static_cast<float> (outputCount - 1);
        const auto location = phase * static_cast<float> (traceSamples - 1);
        const auto left = std::min (traceSamples - 1, static_cast<int> (std::floor (location)));
        const auto right = std::min (traceSamples - 1, left + 1);
        const auto fraction = location - static_cast<float> (left);
        const auto y = values[static_cast<std::size_t> (left)]
                     + (values[static_cast<std::size_t> (right)] - values[static_cast<std::size_t> (left)]) * fraction;
        result.points[point] = { phase, y, 0.0f, CurveType::singleCurve };
    }
    result.points[0].y = originalStart;
    result.points[outputCount - 1].y = originalEnd;
    result.normalise();
    return result;
}

void PatternEditor::turnAllPointsSmooth()
{
    const auto state = model.snapshot();
    auto pattern = patternBank (state->lanes[lane]).patterns[state->lanes[lane].selectedPattern];
    for (int point = 0; point + 1 < pattern.count; ++point)
        if (pattern.points[point + 1].x - pattern.points[point].x > 0.0002f)
        {
            pattern.points[point].curve = CurveType::doubleCurve;
            pattern.points[point].tension = 0.0f;
        }
    replaceCurrentPattern (pattern);
}

bool PatternEditor::keyPressed (const juce::KeyPress& key)
{
    if (controlDown (key.getModifiers()) && (key.getKeyCode() == 'z' || key.getKeyCode() == 'Z'))
    {
        const auto undone = model.undo();
        if (undone) { selected.fill (false); repaint(); }
        return undone;
    }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    return false;
}

void PatternEditor::resized() {}
} // namespace stepshaper
