#pragma once

#include "Core.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace stepshaper
{
class PatternEditor final : public juce::Component
{
public:
    enum class ModTarget : std::uint8_t { none, pointX, pointY, tension };
    struct ModRoute { int point = -1; ModTarget target = ModTarget::none; float depth = 0.0f; };
    explicit PatternEditor (Model& modelToUse);
    std::function<void (int lane)> onStartPositionChanged;
    std::function<void (int lane)> onSustainPositionChanged;
    std::function<void()> onCanvasClicked;
    std::function<void (const std::string&)> onHint;

    void setLane (int laneToShow);
    void setPlayhead (float phase);
    void setGrid (int xDivisions, int yDivisions);
    void setDrawMode (DrawMode mode);
    void randomize();
    void randomizeLines();
    void randomizeRampUps();
    void randomizeRampDowns();
    void randomizeSteps();
    void randomizeMelody (bool minor);
    void resetPattern();
    void flipVertically();
    void flipHorizontally();
    void normalizeLevels();
    bool doubleCurrentPattern();
    void smoothAbruptChanges();
    void smoothUp (float smooth, float decimation);
    void beginSmoothPreview();
    void updateSmoothPreview (float smooth, float decimation);
    void commitSmoothPreview();
    void cancelSmoothPreview();
    void turnAllPointsSmooth();
    void updateModDrag (int source, juce::Point<float> localPosition);
    void finishModDrag (int source, juce::Point<float> localPosition);
    std::vector<ModRoute> modRoutes (int source) const;
    void setModDepth (int source, ModRoute route, float depth);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> pointPosition (const Point&) const;
    int nearestPoint (juce::Point<float>, float radius = 13.0f) const;
    int nearestTension (juce::Point<float>, float radius = 12.0f) const;
    juce::Rectangle<float> startFlagBounds (float startPosition) const;
    juce::Rectangle<float> sustainFlagBounds (float sustainPosition) const;
    void insertPoint (juce::Point<float>, bool snapY);
    void removePoint (int point);
    void deleteSelected();
    void updatePoint (juce::Point<float>, bool snapToGrid);
    void updateTension (juce::Point<float>);
    void showCurveTypeMenu (int segment, juce::Point<int> screenPosition);
    void showStartFlagMenu (juce::Point<int> screenPosition);
    void showSustainFlagMenu (juce::Point<int> screenPosition);
    void updateStartPosition (juce::Point<float>, bool snapToGrid);
    void updateSustainPosition (juce::Point<float>, bool snapToGrid);
    void paintSection (juce::Point<float>, bool snapY);
    void freeDrawPoint (juce::Point<float>, bool snapY);
    void replaceCurrentPattern (const Pattern&);
    static void appendPoint (Pattern&, float x, float y, float tension = 0.0f,
                             CurveType curve = CurveType::singleCurve);
    static void upsertPoint (Pattern&, float x, float y);
    static bool controlDown (const juce::ModifierKeys&);
    static Pattern makeSmoothedPattern (const Pattern&, float attack, float release, float decimation);
    static Pattern makeFilteredTrace (const Pattern&, float smooth, float decimation);

    Model& model;
    int lane = 0;
    int displayedSlot = -1;
    int gridX = 8;
    int gridY = 8;
    DrawMode drawMode = DrawMode::edit;
    int activePoint = -1;
    int activeTension = -1;
    int lastPaintedCell = -1;
    bool activeStartPosition = false;
    bool activeSustainPosition = false;
    bool brushPainting = false;
    bool freeDrawing = false;
    bool marqueeSelecting = false;
    std::array<bool, maxPoints> selected {};
    std::array<float, maxPoints> originalXs {};
    std::array<float, maxPoints> originalYs {};
    juce::Point<float> dragStart;
    juce::Point<float> marqueeEnd;
    float originalTension = 0.0f;
    float lastDrawY = 0.5f;
    float playhead = 0.0f;
    int draggedModSource = -1;
    int hoveredModPoint = -1;
    ModTarget hoveredModTarget = ModTarget::none;
    bool showEditorHelp = false;
    std::optional<Pattern> smoothPreviewSource;
    std::optional<Pattern> smoothPreview;
};
} // namespace stepshaper
