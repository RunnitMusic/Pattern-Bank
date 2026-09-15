#pragma once

#include "Core.h"
#include "PatternEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <functional>

namespace stepshaper
{
class VerticalPatternSelector final : public juce::Component
{
public:
    std::function<void()> onGestureBegin;
    std::function<void (int)> onChange;
    std::function<void()> onGestureEnd;
    std::function<void()> onRequestMenu;
    std::function<void (juce::Point<int>)> onParameterMenu;
    std::function<void (juce::String)> onRename;

    VerticalPatternSelector();
    void setSlots (std::vector<int> activeSlots);
    void setValue (int newValue);
    void setPending (int pendingValue);
    void setPatternName (juce::String);
    int value() const noexcept { return currentValue; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    juce::Rectangle<float> switcherBounds() const noexcept;
    juce::Rectangle<float> nameBounds() const noexcept;
    void applyOrdinal (int ordinalToApply);
    int ordinalForValue (int value) const;
    int currentValue = 0;
    int pendingValue = -1;
    int currentOrdinal = 0;
    int dragStartOrdinal = 0;
    std::vector<int> slots { 0 };
    juce::String patternName;
    std::unique_ptr<juce::TextEditor> nameEditor;
    bool draggingSwitcher = false;
};

class StepShaperEditor final : public juce::Component, private juce::Timer
{
public:
    using ParameterCallback = std::function<void (int lane, Parameter parameter)>;
    using HintCallback = std::function<void (const std::string&)>;
    using RetriggerParameterCallback = std::function<void (int lane, bool isDown)>;
    using ParameterMenuCallback = std::function<void (int lane, Parameter parameter, juce::Point<int>)>;

    StepShaperEditor (Model& modelToUse, Engine* engineToDisplay, ParameterCallback parameterCallback = {},
                      HintCallback hintCallback = {}, RetriggerParameterCallback retriggerCallback = {});
    void setParameterMenuCallback (ParameterMenuCallback callback) { onParameterMenu = std::move (callback); }
    ~StepShaperEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    class RateSlider : public juce::Slider
    {
    public:
        std::function<void (juce::Point<int>)> onRightClick;
        void mouseDown (const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu() || event.mods.isRightButtonDown())
            {
                if (onRightClick) onRightClick (event.getScreenPosition());
                return;
            }
            juce::Slider::mouseDown (event);
        }
    };

    class ModSlider final : public RateSlider
    {
    public:
        ModSlider();
        std::function<void (juce::Point<int>)> onSourceDrag;
        std::function<void (juce::Point<int>)> onSourceDrop;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        bool isSourceDragging() const noexcept { return sourceDragging; }
    private:
        std::unique_ptr<juce::Drawable> sourceIcon;
        juce::Colour sourceIconColour { 0xff020202 };
        bool sourceDragging = false;
    };

    class PatternList final : public juce::Component
    {
    public:
        explicit PatternList (Model&);
        std::function<void (int)> onSelect;
        std::function<void (int)> onDelete;
        std::function<void()> onAdd;
        void setLane (int);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    private:
        int slotAt (juce::Point<float>) const;
        bool addRowAt (juce::Point<float>) const;
        juce::Rectangle<float> deleteBoundsForRow (int row) const;
        std::vector<int> visibleSlots() const;
        Model& model;
        int lane = 0;
        int scroll = 0;
        int dragSlot = -1;
        static constexpr int rowHeight = 23;
    };

    class GridControl final : public juce::Component, private juce::Label::Listener
    {
    public:
        explicit GridControl (juce::String axisName);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void setValue (int, bool notify = false);
        int getValue() const noexcept { return value; }
        std::function<void (int)> onValueChange;
        std::function<void()> onGestureBegin;
        std::function<void()> onGestureEnd;

    private:
        void labelTextChanged (juce::Label*) override;
        void editorHidden (juce::Label*, juce::TextEditor&) override;
        void beginInlineEdit();
        juce::String axis;
        juce::Label editor;
        int value = 8;
        int dragStartValue = 8;
    };

    class Theme final : public juce::LookAndFeel_V4
    {
    public:
        Theme();
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool down,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                               juce::Slider&) override;
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                               float startAngle, float endAngle, juce::Slider&) override;
    private:
        std::unique_ptr<juce::Drawable> pencilIcon;
        std::unique_ptr<juce::Drawable> diceIcon;
        std::unique_ptr<juce::Drawable> copyIcon;
        std::unique_ptr<juce::Drawable> pasteIcon;
        std::unique_ptr<juce::Drawable> amountUnipolarIcon;
        std::unique_ptr<juce::Drawable> amountBipolarIcon;
        std::unique_ptr<juce::Drawable> amountModeOutline;
        std::unique_ptr<juce::Drawable> manualTriggerIcon;
        std::unique_ptr<juce::Drawable> syncLockedIcon;
        std::unique_ptr<juce::Drawable> syncUnlockedIcon;
        juce::Colour manualTriggerIconColour { 0xff000000 };
        juce::Colour syncLockedIconColour { 0xff000000 };
        juce::Colour syncUnlockedIconColour { 0xff000000 };
    };

    void timerCallback() override;
    void refreshFromModel (bool force = false);
    void notifyParameter (int lane, Parameter parameter);
    void notifyAllLaneParameters (int lane);
    void styleButton (juce::TextButton&, const juce::String& tooltip);
    void configureRateControl (bool synced);
    void showModMenu (int source, juce::Point<int> position);
    void showPatternStateMenu();
    void showSmoothUpDialog();
    void openPatternFile();
    void savePatternFile();
    juce::File patternDataDirectory() const;
    void setSpeedModDepth (int source, float depth);
    bool isOverSpeedKnob (juce::Point<int> screenPosition) const;
    void updateHoverFeedback (const juce::MouseEvent&);
    juce::String knobValueText (const juce::Slider&) const;
    juce::String hintForComponent (juce::Component*) const;

    Model& model;
    Engine* engine = nullptr;
    ParameterCallback onParameterChanged;
    HintCallback onHint;
    RetriggerParameterCallback onRetriggerParameterChanged;
    ParameterMenuCallback onParameterMenu;
    Theme theme;
    PatternEditor patternEditor;
    VerticalPatternSelector patternSelector;
    PatternList patternList;
    GridControl gridXControl { "GRID X" };
    GridControl gridYControl { "GRID Y" };
    juce::TextButton patternStateMenuButton { juce::String::fromUTF8 ("\xe2\x96\xb8") };
    std::array<juce::TextButton, maxLanes> laneButtons;
    juce::TextButton addLaneButton { "+" };
    juce::TextButton undoButton { juce::String::fromUTF8 ("\xe2\x86\xb6") };
    juce::TextButton redoButton { juce::String::fromUTF8 ("\xe2\x86\xb7") };
    juce::TextButton deletePatternButton { "DEL" };
    juce::TextButton newPatternButton { "NEW" };
    juce::TextButton randomPatternButton { juce::String::fromUTF8 ("\xe2\x9a\x84 P") };
    juce::TextButton randomizeAllButton { juce::String::fromUTF8 ("\xe2\x9a\x84 A") };
    juce::TextButton copyButton { "COPY" };
    juce::TextButton pasteButton { "PASTE" };
    juce::ComboBox drawModeBox;
    juce::TextButton curveRandomizeButton { "RANDOMIZE" };
    juce::TextButton curveRandomizeMoreButton;
    juce::TextButton syncButton;
    juce::ToggleButton midiTriggerButton { "MIDI TRIGGER" };
    juce::TextButton refreshTriggerButton;
    juce::ToggleButton legatoButton { "LEGATO" };
    RateSlider speedSlider;
    RateSlider baseValueSlider;
    RateSlider patternMixSlider;
    juce::TextButton amountModeButton;
    std::array<ModSlider, 2> modSliders;
    juce::ComboBox changeModeBox;
    juce::ToggleButton positionSyncButton { "POS SYNC" };
    juce::Label speedLabel;
    juce::Label retriggerLabel;
    juce::Label baseValueLabel;
    juce::Label patternMixLabel;
    juce::Label amountModeLabel;
    std::array<juce::Label, 2> modLabels;
    juce::Label modeLabel;
    juce::Label editorHelpLabel;
    juce::Label statusLabel;
    juce::String lastHint;
    int speedModDragSource = -1;
    std::uint64_t displayedRevision = 0;
    bool refreshing = false;
    bool refreshTriggerWasDown = false;
    bool refreshTriggerWasMidi = false;
    int refreshTriggerLane = 0;
    std::array<int, 3> bottomDividerXs {};
    std::unique_ptr<juce::FileChooser> patternFileChooser;
};
} // namespace stepshaper
