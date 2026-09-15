#include "StepShaperEditor.h"

#include <algorithm>

namespace stepshaper
{
namespace
{
const auto panel = juce::Colour (0xff343b40);
const auto raised = juce::Colour (0xff353a3e);
const auto border = juce::Colour (0xff4b5054);
const auto text = juce::Colour (0xffe8ebef);
const auto muted = juce::Colour (0xffaeb6bd);
const auto accent = juce::Colour (0xffe53136);
const auto laneAccent = juce::Colour (0xffffa51f);
juce::String sharedPatternClipboard;

class SmoothUpPanel final : public juce::Component
{
public:
    SmoothUpPanel (std::function<void (float, float)> preview,
                   std::function<void()> acceptPreview, std::function<void()> cancelPreview)
        : onPreview (std::move (preview)), onAccept (std::move (acceptPreview)),
          onCancel (std::move (cancelPreview))
    {
        const auto configure = [this] (juce::Slider& slider, const juce::String& name, bool rotary)
        {
            slider.setName (name);
            slider.setRange (0.0, 1.0, 0.01);
            slider.setSliderStyle (rotary ? juce::Slider::RotaryHorizontalVerticalDrag
                                          : juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 16);
            slider.setColour (juce::Slider::trackColourId, laneAccent);
            slider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff252c31));
            slider.setColour (juce::Slider::thumbColourId, juce::Colour (0xffc8cdd1));
            addAndMakeVisible (slider);
        };
        configure (smooth, "Smooth", true);
        configure (decimation, "Decimation", false);
        smooth.setValue (.25); decimation.setValue (0.0);
        const auto updatePreview = [this]
        {
            if (onPreview) onPreview (static_cast<float> (smooth.getValue()),
                                      static_cast<float> (decimation.getValue()));
        };
        smooth.onValueChange = updatePreview;
        decimation.onValueChange = updatePreview;
        for (auto* label : { &smoothLabel, &decimationLabel })
        {
            label->setColour (juce::Label::textColourId, text);
            label->setFont (juce::FontOptions (11.0f));
            label->setJustificationType (juce::Justification::centred);
            addAndMakeVisible (*label);
        }
        smoothLabel.setText ("SMOOTH", juce::dontSendNotification);
        decimationLabel.setText ("DECIMATION", juce::dontSendNotification);
        reset.setButtonText ("Reset"); accept.setButtonText ("Accept");
        addAndMakeVisible (reset); addAndMakeVisible (accept);
        reset.onClick = [this]
        {
            smooth.setValue (.25); decimation.setValue (0.0);
        };
        accept.onClick = [this]
        {
            accepted = true;
            if (onAccept) onAccept();
            if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>()) dialog->exitModalState (1);
        };
        setSize (240, 250);
    }

    ~SmoothUpPanel() override
    {
        if (! accepted && onCancel) onCancel();
    }

    void paint (juce::Graphics& g) override { g.fillAll (panel); }
    void resized() override
    {
        smoothLabel.setBounds (70, 12, 100, 18);
        smooth.setBounds (72, 34, 96, 105);
        decimationLabel.setBounds (10, 153, 90, 20); decimation.setBounds (92, 143, 138, 42);
        reset.setBounds (10, 207, 72, 30); accept.setBounds (158, 207, 72, 30);
    }

private:
    juce::Slider smooth, decimation;
    juce::Label smoothLabel, decimationLabel;
    juce::TextButton reset, accept;
    std::function<void (float, float)> onPreview;
    std::function<void()> onAccept, onCancel;
    bool accepted = false;
};

class ModDepthMenuItem final : public juce::PopupMenu::CustomComponent
{
public:
    ModDepthMenuItem (juce::String routeName, float initialDepth, juce::Colour routeColour,
                      std::function<void()> begin, std::function<void (float)> change,
                      std::function<void()> end, std::function<void()> remove)
        : juce::PopupMenu::CustomComponent (false), name (std::move (routeName)), depth (initialDepth),
          colour (routeColour), onBegin (std::move (begin)), onChange (std::move (change)),
          onEnd (std::move (end)), onRemove (std::move (remove)) {}

    void getIdealSize (int& width, int& height) override { width = 240; height = 30; }
    void paint (juce::Graphics& g) override
    {
        if (isItemHighlighted()) { g.setColour (juce::Colour (0xff647580)); g.fillRect (getLocalBounds()); }
        const auto remove = removeBounds();
        g.setColour (juce::Colour (0xffd62f35)); g.fillRect (remove);
        g.setColour (juce::Colours::white);
        g.drawLine (remove.getX() + 4.0f, remove.getY() + 4.0f,
                    remove.getRight() - 4.0f, remove.getBottom() - 4.0f, 1.5f);
        g.drawLine (remove.getRight() - 4.0f, remove.getY() + 4.0f,
                    remove.getX() + 4.0f, remove.getBottom() - 4.0f, 1.5f);
        g.setColour (text); g.setFont (juce::FontOptions (11.5f));
        g.drawText (name, 29, 1, getWidth() - 93, 19, juce::Justification::centredLeft);
        g.drawText ((depth > 0.0f ? "+" : "") + juce::String (juce::roundToInt (depth * 100.0f)) + "%",
                    getWidth() - 61, 1, 53, 19, juce::Justification::centredRight);
        const auto rail = juce::Rectangle<float> (29.0f, 23.0f, getWidth() - 37.0f, 3.0f);
        g.setColour (juce::Colour (0xff30363a)); g.fillRect (rail);
        g.setColour (colour);
        const auto centre = rail.getCentreX(); const auto end = centre + depth * rail.getWidth() * 0.5f;
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (centre, end), rail.getY(),
                                                                std::max (centre, end), rail.getBottom()));
    }
    void mouseDown (const juce::MouseEvent& event) override
    {
        if (removeBounds().contains (event.position)) { if (onRemove) onRemove(); return; }
        dragStart = event.position; startDepth = depth; dragging = true; if (onBegin) onBegin();
    }
    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! dragging) return;
        depth = std::clamp (startDepth + (event.position.x - dragStart.x - event.position.y + dragStart.y) / 150.0f,
                            -1.0f, 1.0f);
        if (onChange) onChange (depth); repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging) return; dragging = false; if (onEnd) onEnd();
    }
private:
    juce::Rectangle<float> removeBounds() const { return { 6.0f, 6.0f, 17.0f, 17.0f }; }
    juce::String name; float depth = 0.0f, startDepth = 0.0f; juce::Colour colour;
    juce::Point<float> dragStart; bool dragging = false;
    std::function<void()> onBegin, onEnd, onRemove; std::function<void (float)> onChange;
};
}

VerticalPatternSelector::VerticalPatternSelector()
{
    setWantsKeyboardFocus (true);
    setTitle ("Pattern selector");
    setDescription ("Drag vertically through the active automatable patterns");
}

juce::Rectangle<float> VerticalPatternSelector::switcherBounds() const noexcept
{
    return { static_cast<float> (getWidth() - 27), 0.0f, 27.0f, static_cast<float> (getHeight()) };
}

juce::Rectangle<float> VerticalPatternSelector::nameBounds() const noexcept
{
    return { 46.0f, 12.0f, std::max (0.0f, static_cast<float> (getWidth()) - 76.0f),
             std::max (0.0f, static_cast<float> (getHeight()) - 13.0f) };
}

void VerticalPatternSelector::setSlots (std::vector<int> activeSlots)
{
    if (activeSlots.empty()) activeSlots.push_back (0);
    slots = std::move (activeSlots);
    currentOrdinal = ordinalForValue (currentValue);
    currentValue = slots[static_cast<std::size_t> (currentOrdinal)];
    repaint();
}

int VerticalPatternSelector::ordinalForValue (int value) const
{
    const auto found = std::find (slots.begin(), slots.end(), value);
    if (found != slots.end()) return static_cast<int> (std::distance (slots.begin(), found));
    return std::clamp (currentOrdinal, 0, static_cast<int> (slots.size()) - 1);
}

void VerticalPatternSelector::setValue (int newValue)
{
    currentOrdinal = ordinalForValue (newValue);
    currentValue = slots[static_cast<std::size_t> (currentOrdinal)];
    repaint();
}

void VerticalPatternSelector::setPending (int valueToShow)
{
    pendingValue = valueToShow;
    repaint();
}

void VerticalPatternSelector::setPatternName (juce::String value)
{
    if (patternName == value) return;
    patternName = std::move (value);
    repaint();
}

void VerticalPatternSelector::applyOrdinal (int ordinalToApply)
{
    ordinalToApply = std::clamp (ordinalToApply, 0, static_cast<int> (slots.size()) - 1);
    const auto valueToApply = slots[static_cast<std::size_t> (ordinalToApply)];
    if (valueToApply == currentValue) return;
    currentOrdinal = ordinalToApply;
    currentValue = valueToApply;
    if (onChange) onChange (currentValue);
    repaint();
}

void VerticalPatternSelector::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff252a2e), 0.0f, bounds.getY(),
                                             juce::Colour (0xff1d2226), 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);
    g.setColour (muted);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("PATTERN", 9, 2, getWidth() - 25, 11, juce::Justification::centredLeft);
    g.setColour (text);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    const auto valueText = juce::String (currentValue + 1).paddedLeft ('0', 2);
    const auto shown = pendingValue >= 0 && pendingValue != currentValue
        ? valueText + "  ->  " + juce::String (pendingValue + 1).paddedLeft ('0', 2)
        : valueText;
    g.drawText (shown, 9, 12, 38, getHeight() - 13, juce::Justification::centredLeft);
    g.setColour (muted);
    g.setFont (juce::FontOptions (12.5f));
    g.drawFittedText (patternName, 50, 12, getWidth() - 75, getHeight() - 13,
                      juce::Justification::centredLeft, 1);
    g.setColour (juce::Colour (0xff4b5054));
    g.drawVerticalLine (getWidth() - 27, 5.0f, static_cast<float> (getHeight() - 5));
    g.setColour (muted);
    juce::Path arrows;
    arrows.addTriangle (getWidth() - 14.0f, 10.0f, getWidth() - 8.0f, 10.0f, getWidth() - 11.0f, 6.0f);
    arrows.addTriangle (getWidth() - 14.0f, getHeight() - 10.0f, getWidth() - 8.0f, getHeight() - 10.0f,
                        getWidth() - 11.0f, getHeight() - 6.0f);
    g.fillPath (arrows);
}

void VerticalPatternSelector::mouseDown (const juce::MouseEvent& event)
{
    if (event.mods.isPopupMenu() || event.mods.isRightButtonDown())
    {
        if (switcherBounds().contains (event.position))
        {
            if (onParameterMenu) onParameterMenu (event.getScreenPosition());
        }
        else if (onRequestMenu) onRequestMenu();
        return;
    }
    if (! switcherBounds().contains (event.position)) return;
    grabKeyboardFocus();
    draggingSwitcher = true;
    dragStartOrdinal = currentOrdinal;
    if (onGestureBegin) onGestureBegin();
}

void VerticalPatternSelector::mouseMove (const juce::MouseEvent& event)
{
    if (switcherBounds().contains (event.position)) setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    else if (nameBounds().contains (event.position)) setMouseCursor (juce::MouseCursor::IBeamCursor);
    else setMouseCursor (juce::MouseCursor::NormalCursor);
}

StepShaperEditor::ModSlider::ModSlider()
{
    static constexpr auto svg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 510 508"><path fill="#020202" d="m247.7 6.7c-3.1 3.8-8.2 9.8-11.4 13.4-3.2 3.6-12.1 13.7-19.8 22.5-7.7 8.7-14.3 16.1-14.6 16.4-0.4 0.3-2.1 2.3-3.9 4.5-1.7 2.2-5.8 6.9-9.1 10.5-3.2 3.6-8.9 9.9-12.5 14-3.7 4.1-7 8.3-7.5 9.3-0.8 1.4-0.3 1.8 2.4 2.2 1.7 0.3 12.8 0.5 24.5 0.5h21.2c0 75.2 0.2 81.2 1.5 82.5 1.3 1.3 6.4 1.5 33.8 1.5 17.7 0 33.9-0.4 36-0.8l3.7-0.7c0-62 0.4-80.5 0.8-81.2 0.5-0.9 7.4-1.3 25.5-1.5l24.9-0.3c-8-9.3-13.1-15.1-16.5-18.9-3.4-3.8-9.8-11-14.2-16.1-4.4-5.1-15-17.3-23.6-27.1-8.6-9.9-19.5-22.2-24.1-27.4-4.6-5.2-9.1-9.6-9.9-9.8-0.8-0.1-4 2.8-7.2 6.5zm-150.2 161.4c-1.6 1.2-5.7 4.8-9 7.8-3.4 3.1-10.3 9.2-15.4 13.6-5.2 4.4-10.6 9.1-12.1 10.5-1.4 1.4-7.3 6.6-13.1 11.6-5.7 5.1-14.7 12.9-19.9 17.5-5.2 4.5-11.6 10.1-14.1 12.3-2.5 2.3-6.5 5.8-9 8l-4.4 3.9c9.5 8.7 16.9 15.3 22.6 20.2 5.6 4.9 12.6 11 15.4 13.5 2.9 2.5 8.3 7.2 12.1 10.6 3.8 3.3 10.3 8.9 14.4 12.5 4.1 3.6 8.8 7.6 10.4 9 1.7 1.3 5.8 4.9 9.1 7.9 3.4 3 8.5 7.4 11.3 9.7l5.2 4.3c0-47 0.1-48 2-49 1.2-0.6 17.3-1 42-1h40v-75c-72.9 0.1-81.8-0.2-82.7-1.2-0.9-0.9-1.3-8.3-1.5-25.1l-0.3-23.9zm311.1 7.2c-0.2 5.1-0.4 10.6-0.3 12.2 0 1.7 0 8.8-0.1 15.8l-0.2 12.7h-83l0.5 74.5c63 0.8 81.7 1.4 82.2 1.9 0.4 0.4 0.9 11.4 1 24.5 0.2 13 0.4 23.8 0.5 23.9 0.2 0.2 3.3-2.3 6.9-5.5 3.6-3.2 11.2-9.8 16.9-14.8 5.7-4.9 13.2-11.4 16.6-14.5 3.4-3 10.3-9.1 15.4-13.5 5-4.4 10-8.6 10.9-9.5 0.9-0.8 5.2-4.6 9.6-8.5 4.4-3.8 9.7-8.5 11.9-10.5 2.2-1.9 5.7-5 7.9-7l3.9-3.5c-9.2-8.5-16.5-15-22.2-20-5.7-4.9-13-11.2-16.1-14-3.1-2.7-10.2-9-15.9-14-5.7-4.9-12.1-10.5-14.2-12.5-2.2-1.9-6.5-5.7-9.6-8.5-3.2-2.8-7.3-6.3-9.2-7.9-1.9-1.5-5.4-4.5-7.8-6.7-2.3-2.1-4.4-3.9-4.6-3.9-0.2 0-0.7 4.2-1 9.3zm-190.1 149.3c-1.5 1.4-1.6 5.7-1.6 41.5l0.1 39.9c-3.5 0.8-14.5 1-26.8 1-14.1 0-22.2 0.4-22.2 1 0 0.6 7 9 15.6 18.8 8.6 9.7 19.5 22.2 24.2 27.6 4.8 5.5 10.9 12.5 13.7 15.6 2.7 3.1 8.1 9.2 12 13.5 3.8 4.3 8.3 9.5 9.9 11.4 1.5 2 4.8 5.7 7.2 8.1l4.5 4.5c5.7-6.2 10.1-11.1 13.4-15 3.3-3.8 7.8-9 10.1-11.4 2.3-2.5 5-5.7 6-7 1-1.4 5-5.9 8.9-10.1 3.8-4.2 10.6-11.8 15-17 4.4-5.1 9.8-11.4 12.1-13.9 2.3-2.5 8-9.1 12.8-14.6 4.7-5.5 8.6-10.3 8.6-10.7 0-0.4-10.8-0.9-24-1-20.1-0.2-24.2-0.5-25-1.8-0.6-0.9-1-18-1-41.2 0-34.5-0.2-39.8-1.5-40.3-0.8-0.2-17-0.5-35.9-0.5-30 0-34.7 0.2-36.1 1.6z"/></svg>)svg";
    if (auto xml = juce::XmlDocument::parse (svg)) sourceIcon = juce::Drawable::createFromSVG (*xml);
}

void StepShaperEditor::ModSlider::paint (juce::Graphics& g)
{
    RateSlider::paint (g);
    const auto colour = sourceDragging ? juce::Colour (0xff4fc3f7) : juce::Colour (0xffc8d0d5);
    if (sourceIcon != nullptr)
    {
        sourceIcon->replaceColour (sourceIconColour, colour);
        sourceIconColour = colour;
        sourceIcon->drawWithin (g, { static_cast<float> (getWidth()) - 16.5f,
                                    static_cast<float> (getHeight()) - 17.5f, 15.0f, 15.0f },
                                juce::RectanglePlacement::centred, 1.0f);
    }
}

void StepShaperEditor::ModSlider::mouseDown (const juce::MouseEvent& event)
{
    const auto handle = juce::Point<float> (getWidth() - 9.0f, getHeight() - 10.0f);
    if (! event.mods.isPopupMenu() && event.position.getDistanceFrom (handle) <= 9.0f)
    {
        sourceDragging = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();
        if (onSourceDrag) onSourceDrag (event.getScreenPosition());
        return;
    }
    RateSlider::mouseDown (event);
}

void StepShaperEditor::ModSlider::mouseDrag (const juce::MouseEvent& event)
{
    if (sourceDragging) { if (onSourceDrag) onSourceDrag (event.getScreenPosition()); return; }
    RateSlider::mouseDrag (event);
}

void StepShaperEditor::ModSlider::mouseUp (const juce::MouseEvent& event)
{
    if (sourceDragging)
    {
        sourceDragging = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
        if (onSourceDrop) onSourceDrop (event.getScreenPosition());
        return;
    }
    RateSlider::mouseUp (event);
}

void VerticalPatternSelector::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! event.mods.isLeftButtonDown() || ! nameBounds().contains (event.position)) return;
    nameEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*nameEditor);
    nameEditor->setBounds (40, 14, getWidth() - 63, getHeight() - 17);
    nameEditor->setText (patternName, false);
    nameEditor->setSelectAllWhenFocused (true);
    nameEditor->setColour (juce::TextEditor::backgroundColourId, raised);
    nameEditor->setColour (juce::TextEditor::textColourId, text);
    nameEditor->onReturnKey = [this]
    {
        if (onRename) onRename (nameEditor->getText());
        nameEditor.reset(); repaint();
    };
    nameEditor->onEscapeKey = [this] { nameEditor.reset(); repaint(); };
    nameEditor->onFocusLost = [this]
    {
        if (nameEditor != nullptr && onRename) onRename (nameEditor->getText());
        nameEditor.reset(); repaint();
    };
    nameEditor->grabKeyboardFocus();
}

StepShaperEditor::PatternList::PatternList (Model& source) : model (source)
{
    setOpaque (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void StepShaperEditor::PatternList::setLane (int value) { lane = std::clamp (value, 0, maxLanes - 1); repaint(); }

std::vector<int> StepShaperEditor::PatternList::visibleSlots() const
{
    std::vector<int> result;
    const auto state = model.snapshot();
    const auto& bank = patternBank (state->lanes[lane]);
    for (int slot = 0; slot < patternsPerLane; ++slot)
        if (bank.occupied[slot]) result.push_back (slot);
    return result;
}

int StepShaperEditor::PatternList::slotAt (juce::Point<float> position) const
{
    const auto slots = visibleSlots();
    const auto row = scroll + std::clamp (static_cast<int> (position.y) / rowHeight, 0, std::max (0, getHeight() / rowHeight - 1));
    return row >= 0 && row < static_cast<int> (slots.size()) ? slots[static_cast<std::size_t> (row)] : -1;
}

bool StepShaperEditor::PatternList::addRowAt (juce::Point<float> position) const
{
    const auto slots = visibleSlots();
    if (slots.size() >= patternsPerLane) return false;
    const auto row = scroll + std::clamp (static_cast<int> (position.y) / rowHeight,
                                          0, std::max (0, getHeight() / rowHeight - 1));
    return row == static_cast<int> (slots.size());
}

juce::Rectangle<float> StepShaperEditor::PatternList::deleteBoundsForRow (int row) const
{
    return { static_cast<float> (getWidth() - 28), static_cast<float> (row * rowHeight + 2), 25.0f,
             static_cast<float> (rowHeight - 4) };
}

void StepShaperEditor::PatternList::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2429));
    const auto state = model.snapshot();
    const auto& bank = patternBank (state->lanes[lane]);
    const auto slots = visibleSlots();
    const auto rows = getHeight() / rowHeight;
    for (int row = 0; row < rows && scroll + row < static_cast<int> (slots.size()); ++row)
    {
        const auto slot = slots[static_cast<std::size_t> (scroll + row)];
        auto bounds = juce::Rectangle<int> (0, row * rowHeight, getWidth(), rowHeight);
        if (slot == state->lanes[lane].selectedPattern) { g.setColour (juce::Colour (0xff647580)); g.fillRect (bounds); }
        g.setColour (text);
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (juce::String (slot + 1).paddedLeft ('0', 2), bounds.removeFromLeft (39).reduced (7, 0), juce::Justification::centredLeft);
        auto deleteArea = bounds.removeFromRight (28);
        g.setColour (text); g.drawFittedText (bank.names[slot], bounds.reduced (4, 0), juce::Justification::centredLeft, 1);
        if (slots.size() > 1)
        {
            const auto centre = deleteArea.getCentre().toFloat();
            g.setColour (text);
            g.drawLine (centre.x - 6.0f, centre.y - 6.0f, centre.x + 6.0f, centre.y + 6.0f, 2.0f);
            g.drawLine (centre.x + 6.0f, centre.y - 6.0f, centre.x - 6.0f, centre.y + 6.0f, 2.0f);
        }
        g.setColour (juce::Colour (0xff20272c)); g.drawHorizontalLine ((row + 1) * rowHeight - 1, 0.0f, static_cast<float> (getWidth()));
    }
    if (slots.size() < patternsPerLane)
    {
        const auto addRow = static_cast<int> (slots.size()) - scroll;
        if (addRow >= 0 && addRow < rows)
        {
            const auto bounds = juce::Rectangle<int> (0, addRow * rowHeight, getWidth(), rowHeight);
            g.setColour (muted);
            g.setFont (juce::FontOptions (17.0f));
            g.drawText ("+", bounds, juce::Justification::centred);
            g.setColour (juce::Colour (0xff20272c));
            g.drawHorizontalLine ((addRow + 1) * rowHeight - 1, 0.0f, static_cast<float> (getWidth()));
        }
    }
}

void StepShaperEditor::PatternList::mouseDown (const juce::MouseEvent& event)
{
    if (addRowAt (event.position))
    {
        if (onAdd) onAdd();
        return;
    }
    const auto row = std::clamp (static_cast<int> (event.position.y) / rowHeight, 0, std::max (0, getHeight() / rowHeight - 1));
    const auto targetSlot = slotAt (event.position);
    if (targetSlot >= 0 && deleteBoundsForRow (row).contains (event.position) && visibleSlots().size() > 1)
    {
        if (onDelete) onDelete (targetSlot);
        return;
    }
    model.beginGesture();
    dragSlot = targetSlot;
    if (dragSlot >= 0) setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    if (dragSlot >= 0) { model.selectPattern (lane, dragSlot, true); if (onSelect) onSelect (dragSlot); repaint(); }
}

void StepShaperEditor::PatternList::mouseUp (const juce::MouseEvent&)
{
    model.endGesture();
    dragSlot = -1;
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void StepShaperEditor::PatternList::mouseMove (const juce::MouseEvent& event)
{
    if (addRowAt (event.position))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        return;
    }
    const auto row = std::clamp (static_cast<int> (event.position.y) / rowHeight, 0, std::max (0, getHeight() / rowHeight - 1));
    setMouseCursor (deleteBoundsForRow (row).contains (event.position)
                        ? juce::MouseCursor::PointingHandCursor
                        : juce::MouseCursor::NormalCursor);
}

void StepShaperEditor::PatternList::mouseDrag (const juce::MouseEvent& event)
{
    const auto target = slotAt (event.position);
    if (dragSlot >= 0 && target >= 0 && target != dragSlot)
    {
        model.reorderPattern (lane, dragSlot, target);
        dragSlot = target;
        if (onSelect) onSelect (target);
        repaint();
    }
}

void StepShaperEditor::PatternList::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto row = std::clamp (static_cast<int> (event.position.y) / rowHeight, 0, std::max (0, getHeight() / rowHeight - 1));
    if (deleteBoundsForRow (row).contains (event.position)) return;
    const auto slot = slotAt (event.position);
    if (slot >= 0) { model.selectPattern (lane, slot, true); if (onSelect) onSelect (slot); setVisible (false); }
}

void StepShaperEditor::PatternList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto occupied = static_cast<int> (visibleSlots().size());
    const auto size = occupied + (occupied < patternsPerLane ? 1 : 0);
    scroll = std::clamp (scroll + (wheel.deltaY < 0.0f ? 1 : -1), 0, std::max (0, size - getHeight() / rowHeight));
    repaint();
}

void VerticalPatternSelector::mouseDrag (const juce::MouseEvent& event)
{
    if (! draggingSwitcher) return;
    applyOrdinal (dragStartOrdinal + event.getDistanceFromDragStartY() / 7);
}

void VerticalPatternSelector::mouseUp (const juce::MouseEvent&)
{
    if (! draggingSwitcher) return;
    draggingSwitcher = false;
    if (onGestureEnd) onGestureEnd();
}

void VerticalPatternSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (onGestureBegin) onGestureBegin();
    applyOrdinal (currentOrdinal + (wheel.deltaY > 0.0f ? 1 : -1));
    if (onGestureEnd) onGestureEnd();
}

bool VerticalPatternSelector::keyPressed (const juce::KeyPress& key)
{
    const auto delta = key.getKeyCode() == juce::KeyPress::upKey ? 1
        : key.getKeyCode() == juce::KeyPress::downKey ? -1 : 0;
    if (delta == 0) return false;
    if (onGestureBegin) onGestureBegin();
    applyOrdinal (currentOrdinal + delta);
    if (onGestureEnd) onGestureEnd();
    return true;
}

StepShaperEditor::GridControl::GridControl (juce::String axisName) : axis (std::move (axisName))
{
    addAndMakeVisible (editor);
    editor.setJustificationType (juce::Justification::centredRight);
    editor.setColour (juce::Label::textColourId, text);
    editor.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    editor.setEditable (false, true, false);
    editor.setInterceptsMouseClicks (false, false);
    editor.addListener (this);
    editor.setText (juce::String (value), juce::dontSendNotification);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void StepShaperEditor::GridControl::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff334154), 0.0f, bounds.getY(),
                                             juce::Colour (0xff283448), 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);
    g.setColour (juce::Colour (0xff46566a));
    g.drawRect (bounds, 1.0f);
    const auto valueWidth = std::clamp (juce::roundToInt (getWidth() * 0.31f), 20, 31);
    g.setColour (text);
    g.setFont (juce::FontOptions (std::clamp (getHeight() * 0.44f, 10.5f, 12.0f)));
    g.drawFittedText (axis, 7, 0, std::max (0, getWidth() - valueWidth - 9), getHeight(),
                      juce::Justification::centredLeft, 1, 0.72f);
}

void StepShaperEditor::GridControl::resized()
{
    const auto valueWidth = std::clamp (juce::roundToInt (getWidth() * 0.31f), 20, 31);
    editor.setBounds (getWidth() - valueWidth - 4, 0, valueWidth, getHeight());
    editor.setFont (juce::FontOptions (std::clamp (getHeight() * 0.44f, 10.5f, 12.0f)));
}

void StepShaperEditor::GridControl::setValue (int newValue, bool notify)
{
    const auto clamped = std::clamp (newValue, 1, 48);
    if (value == clamped) return;
    value = clamped;
    editor.setText (juce::String (value), juce::dontSendNotification);
    repaint();
    if (notify && onValueChange) onValueChange (value);
}

void StepShaperEditor::GridControl::mouseDown (const juce::MouseEvent& event)
{
    dragStartValue = value;
    if (! event.mods.isRightButtonDown())
    {
        if (onGestureBegin) onGestureBegin();
        return;
    }
    beginInlineEdit();
    return;
}

void StepShaperEditor::GridControl::mouseUp (const juce::MouseEvent& event)
{
    if (! event.mods.isRightButtonDown() && onGestureEnd) onGestureEnd();
}

void StepShaperEditor::GridControl::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (editor.getBounds().contains (event.getPosition())) beginInlineEdit();
}

void StepShaperEditor::GridControl::beginInlineEdit()
{
    editor.setInterceptsMouseClicks (true, true);
    editor.showEditor();
    if (auto* textEditor = editor.getCurrentTextEditor())
    {
        textEditor->setInputRestrictions (2, "0123456789");
        textEditor->selectAll();
    }
}

void StepShaperEditor::GridControl::mouseDrag (const juce::MouseEvent& event)
{
    if (event.mods.isRightButtonDown()) return;
    setValue (dragStartValue + juce::roundToInt ((event.getDistanceFromDragStartX()
                                                 - event.getDistanceFromDragStartY()) / 6.0f), true);
}

void StepShaperEditor::GridControl::labelTextChanged (juce::Label*)
{
    setValue (editor.getText().getIntValue(), true);
    editor.setText (juce::String (value), juce::dontSendNotification);
    editor.setInterceptsMouseClicks (false, false);
}

void StepShaperEditor::GridControl::editorHidden (juce::Label*, juce::TextEditor&)
{
    editor.setInterceptsMouseClicks (false, false);
}

StepShaperEditor::Theme::Theme()
{
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 110 111"><path fill="#ffffff" fill-rule="evenodd" d="m46.5 0 5 5.3c2.8 2.8 8.4 9.1 12.5 13.8 4.1 4.7 14.7 16.6 23.5 26.5 8.7 9.8 17.4 19.6 19.2 21.6 3 3.4 3.2 4.2 2.7 8-0.4 2.4-1.5 7-2.5 10.3-1 3.3-3.1 8-4.7 10.5-1.5 2.5-4.5 5.8-6.7 7.5-2.2 1.7-6.9 4-10.5 5.2-3.6 1.3-8 2.3-9.8 2.3-2.4 0-4.7-1.3-9.4-5.3-3.5-2.9-7.2-6.5-8.3-7.9-1.1-1.4-13.9-13.1-28.4-25.9-14.5-12.9-26.8-24.2-27.2-25.2-0.5-0.9-1-11.3-1.1-23-0.2-17.2 0.1-21.5 1.2-22.5 1-0.9 7.6-1.2 44.5-1.2zm-37.5 19.5.5 20c11.9 0 12.6-.3 18-4.5 3.2-2.5 7.4-6.8 9.2-9.5 3.1-4.5 3.4-5.6 3.1-11l-.3-6-19-0.6zm36.9 10.8-2.4 4.8c38 37.9 49.6 48.9 50.3 48.9.7 0 1.9-2.1 2.7-4.7.9-2.6 1.5-4.9 1.5-5 0-.2-10.1-12.1-22.5-26.5-12.4-14.5-22.9-26.4-23.4-26.6-.5-.2-1.5.7-2.4 2-.8 1.3-2.6 4.5-3.8 7.1zm-23.4 20.9c0 .9 10.3 10.5 23 21.3 12.7 10.9 24.6 21.3 26.5 23.1 2 1.9 4.3 3.4 5.3 3.4.9 0 3.2-1.3 5-2.9l3.2-3c-40.4-40.5-49.7-49.1-51-49-1.1 0-4.2 1.2-7 2.7-2.8 1.6-5 3.4-5 4.4z"/></svg>)svg"))
        pencilIcon = juce::Drawable::createFromSVG (*svg);
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 106 107"><path fill="#ffffff" fill-rule="evenodd" d="m49.5 0.1c2.8 0 12.2 0.4 21 0.9 8.8 0.6 17.6 1.6 19.5 2.2 1.9 0.7 5.1 2.6 7 4.3 1.9 1.8 4.3 5.2 5.2 7.6 1.2 3.1 1.9 9.4 2.5 21.4 0.5 9.4 0.5 24.7 0 34-0.5 11.9-1.3 18.2-2.4 21-0.9 2.2-2.8 5.2-4.2 6.8-1.4 1.5-4.6 4.1-8.1 5.5-6.1 2.4-7.4 2.2-37.5 2.2-30.4 0-31.2 0.1-37.4-2.5-3.5-1.4-6.4-3.4-7.5-4.3-1-0.9-3.8-2.6-7.1-11.7v-68l1.5-4.5c1.4-3 4.6-7.2 5.6-8.2 1-0.9 3.7-2.3 5.9-3.2 2.3-0.9 9.7-1.9 17.5-2.4 7.4-0.5 15.8-1 18.5-1.1zm18.9 22.9c-1 1.7-1.3 4.1-1 7.5 0.2 2.8 1.2 5.7 2.1 6.5 1 1 3.6 1.5 7 1.5 4.3 0 5.9-0.5 7.7-2.3 1.8-1.7 2.3-3.4 2.3-7.2 0-3.6-0.5-5.5-2-7-1.1-1.2-3.7-2.2-6-2.5-2.2-0.2-5-0.1-6.3 0.3-1.3 0.5-3 1.9-3.8 3.2zm-26.3 30.4c-0.1 2.6 0.6 4.4 2.7 6.5 1.5 1.6 4.1 3.2 5.7 3.6 2.2 0.6 4 0.3 6.5-1.1 1.9-1 4.1-3.2 4.9-4.9 0.8-1.8 1.1-4.3 0.7-6.2-0.3-1.9-1.9-4.4-3.6-5.8-2-1.6-4.2-2.5-6.6-2.5-2.8 0-4.4 0.8-6.9 3.4-2.4 2.5-3.3 4.4-3.4 7zm-23.2 19.4c-0.6 1.5-0.8 4.5-0.5 6.7 0.3 2.2 1.5 4.9 2.6 6 1.5 1.5 3.4 2 7.1 2 4.3 0 5.5-0.4 7.5-2.7 1.7-2 2.4-4 2.4-6.8 0-2.2-0.4-4.9-1-6-0.5-1.1-2.6-2.7-4.5-3.5-1.9-0.8-4.1-1.5-4.7-1.5-0.7 0.1-2.8 0.7-4.5 1.5-1.9 0.9-3.8 2.7-4.4 4.3z"/></svg>)svg"))
        diceIcon = juce::Drawable::createFromSVG (*svg);
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200"><path fill="#ffffff" fill-rule="evenodd" d="m14.8 22.6h46.6v36.8h26.9v83.4h-73.5zm56.4 2.4l24.5 24.5h-24.5zm26.9 34.3h46.6v36.8h36.8v83.4h-83.4zm55.2 2.4l27 24.6h-27z"/></svg>)svg"))
        copyIcon = juce::Drawable::createFromSVG (*svg);
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 70 72"><path fill="#ffffff" fill-rule="evenodd" d="m0 4h19l3-4h9l3 4h19v19h-5v-15h-12l2 8h-23l2-8h-12v41h30v4h-35zm23 7h7v-3h-7zm14 61v-46h18v15h15v31zm33-34h-12v-12l12 12z"/></svg>)svg"))
        pasteIcon = juce::Drawable::createFromSVG (*svg);
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg version="1.2" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 92 112" width="92" height="112"><style>.a{fill:#fda730}</style><path class="a" d="m23.7 33.2h10.4l11.6 15.9 11.7-15.9h10.5l-16.8 23 16.8 22.8h-10.5l-11.7-15.7-11.6 15.7h-10.4l16.8-22.8z"/></svg>)svg"))
        amountUnipolarIcon = juce::Drawable::createFromSVG (*svg);
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg version="1.2" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 92 112" width="92" height="112"><style>.a{fill:#fda730}</style><path fill-rule="evenodd" class="a" d="m49.5 22v9.9h9.9v5.6h-9.9v9.9h-5.6v-9.9h-9.9v-5.6h9.9v-9.9zm-22.6 32h38v33h-38zm9 19h20v-6h-20z"/></svg>)svg"))
        amountBipolarIcon = juce::Drawable::createFromSVG (*svg);
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg version="1.2" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 92 112" width="92" height="112"><style>.a{fill:#fda730}</style><path fill-rule="evenodd" class="a" d="m46 0c29.7 0 31.7 0.1 34.8 2 1.7 1.1 4.3 3.5 5.7 5.3 1.5 1.9 2.9 5.6 3.5 9.2 0.5 3.3 1.3 16.1 1.6 28.5 0.4 13.5 0.2 28.6-0.5 37.8-0.7 10-1.6 16.3-2.7 18.5-0.9 1.7-2.6 4.3-3.8 5.5-1.1 1.3-3.3 3-4.8 3.8-2.2 1.1-9.1 1.4-33.8 1.4-24.7 0-31.6-0.3-33.8-1.4-1.5-0.8-3.7-2.5-4.8-3.8-1.2-1.2-2.9-3.9-3.9-5.8-1.3-2.7-2-8.1-2.8-23-0.8-13.9-0.8-25.8 0-41.5 0.6-12.1 1.6-23.3 2.3-25 0.6-1.6 2.1-4.1 3.3-5.4 1.2-1.3 3.5-3.2 5.2-4.2 2.8-1.7 5.5-1.9 34.5-1.9zm-31.9 13.1c-0.7 0.8-1.6 6.4-2.1 12.4-0.5 6.1-0.9 20-0.9 31 0 11 0.4 24.7 0.9 30.5 0.7 8.6 1.3 10.9 3 12.5 2 1.9 3.4 2 31 2 27.6 0 29-0.1 31-2 1.7-1.6 2.3-4 3-12 0.5-5.5 0.9-19 0.9-30 0-11-0.4-25.2-0.9-31.5-0.7-9.7-1.2-11.8-3-13.5-1.9-1.9-3.6-2-29.8-2.2-15.2-0.1-28.7 0.2-29.9 0.6-1.2 0.5-2.6 1.5-3.2 2.2z"/></svg>)svg"))
        amountModeOutline = juce::Drawable::createFromSVG (*svg);
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 481 265"><path fill="#000000" fill-rule="evenodd" d="m324 2.47c-5.77 1.26-13.99 3.47-18.25 4.91-4.26 1.44-11.69 4.6-16.5 7.02-4.81 2.42-12.74 7.25-17.62 10.75-4.88 3.49-12.55 10.18-17.05 14.85-4.49 4.67-10.55 11.88-13.47 16-2.91 4.13-7.05 10.99-9.2 15.25l-3.91 7.75-93.5.01c-1.23-32.95-1.79-42.62-2.04-42.76-.26-.14-17.9 12.32-39.21 27.69-21.31 15.36-51.19 36.85-66.4 47.75-22.32 15.99-27.43 20.07-26.5 21.17.63.74 6.78 5.36 13.65 10.26 6.88 4.9 24.47 17.57 39.11 28.15 14.64 10.57 34.44 24.84 44 31.7 9.56 6.86 21.55 15.52 26.64 19.25l9.25 6.78c.01-18.99.35-28.89.76-34.26l.74-9.75 93.5.01c4.82 9.69 8.76 16.32 11.86 21 3.1 4.67 9.52 12.54 14.25 17.5 4.74 4.95 12.62 11.87 17.5 15.37 4.89 3.51 13.39 8.59 18.89 11.3 5.5 2.7 13.6 6.01 18 7.35 4.4 1.34 12.5 3.35 18 4.47 6.91 1.41 13.87 2.04 22.5 2.04 8.63 0 15.59-.63 22.5-2.04 5.5-1.12 13.6-3.13 18-4.47 4.4-1.34 12.5-4.65 18-7.35 5.5-2.71 14-7.79 18.89-11.3 4.88-3.5 12.76-10.42 17.5-15.37 4.73-4.96 11.15-12.83 14.25-17.5 3.1-4.68 7.27-11.76 9.25-15.75 1.99-3.99 4.93-11.19 6.54-16 1.62-4.81 3.75-12.57 4.73-17.25 1.31-6.2 1.8-12.96 1.8-25 0-12.04-.49-18.8-1.8-25-.98-4.68-3.11-12.44-4.73-17.25-1.61-4.81-4.55-12.01-6.54-16-1.98-3.99-6.15-11.07-9.25-15.75-3.1-4.67-9.52-12.54-14.25-17.5-4.74-4.95-12.62-11.87-17.5-15.37-4.89-3.51-13.39-8.59-18.89-11.3-5.5-2.7-13.6-6.01-18-7.35-4.4-1.34-12.5-3.35-18-4.47-7.38-1.5-13.53-2.01-23.5-1.94-10.07.08-16.17.69-24 2.4zm7.5 29.16c4.4-.8 11.82-1.5 16.5-1.55 4.68-.06 12.55.6 17.5 1.46 4.95.86 12.38 2.72 16.5 4.12 4.13 1.41 11.04 4.42 15.37 6.7 4.33 2.28 11.49 7.06 15.92 10.64 4.43 3.57 11.14 10.32 14.93 15 3.95 4.89 8.98 12.75 11.83 18.5 2.72 5.5 6.15 14.5 7.62 20 2.35 8.79 2.67 11.87 2.67 25.5 0 13.63-.32 16.71-2.67 25.5-1.47 5.5-4.9 14.5-7.62 20-2.85 5.75-7.88 13.61-11.83 18.5-3.79 4.68-10.67 11.58-15.3 15.33-4.96 4.03-12.53 8.85-18.42 11.75-5.5 2.69-14.5 6.11-20 7.58-8.81 2.36-11.85 2.68-25.5 2.68-13.65 0-16.69-.32-25.5-2.68-5.5-1.47-14.5-4.89-20-7.58-5.96-2.93-13.43-7.7-18.5-11.83-4.67-3.8-11.56-10.7-15.3-15.33-3.85-4.78-8.94-12.75-11.75-18.42-2.72-5.5-6.15-14.5-7.62-20-2.35-8.79-2.67-11.87-2.67-25.5 0-13.63.32-16.71 2.67-25.5 1.47-5.5 4.9-14.5 7.62-20 2.81-5.67 7.9-13.64 11.75-18.42 3.74-4.63 10.46-11.38 14.93-15 4.47-3.62 11.67-8.44 16-10.72 4.33-2.28 11.25-5.3 15.37-6.71 4.13-1.41 11.1-3.21 15.5-4.02zm7.5 33.03c-1.37.21-5.2 1.1-8.5 1.97-3.3.86-9.6 3.44-13.99 5.72-4.56 2.37-10.74 6.73-14.39 10.15-3.52 3.3-8.05 8.47-10.07 11.5-2.02 3.03-4.72 7.97-6 11-1.28 3.03-2.73 6.85-3.23 8.5-.49 1.65-1.31 6.6-1.82 11-.57 4.86-.57 10.75-.01 15 .5 3.85 1.82 9.93 2.93 13.5 1.12 3.57 3.73 9.42 5.8 12.99 2.08 3.58 7.04 9.76 11.03 13.75 5.69 5.69 9.25 8.24 16.5 11.78 5.09 2.49 12.63 5.24 16.75 6.1 5.36 1.13 10.36 1.42 17.5 1.03 5.5-.31 12.48-1.3 15.5-2.22 3.02-.91 7.98-2.9 11-4.42 3.02-1.52 8.2-4.92 11.5-7.55 3.3-2.64 7.94-7.08 10.3-9.87 2.37-2.8 6.02-8.47 8.1-12.59 2.09-4.13 4.61-11.1 5.59-15.5 1.38-6.16 1.67-10.41 1.23-18.5-.42-7.78-1.28-12.57-3.34-18.5-1.52-4.4-4.87-11.15-7.44-15-2.57-3.85-7.55-9.59-11.06-12.76-3.89-3.51-9.9-7.5-15.38-10.2-5.32-2.62-12.17-5.07-16.75-5.99-4.26-.85-10.34-1.49-13.5-1.41-3.16.07-6.87.31-8.25.52z"/></svg>)svg"))
        manualTriggerIcon = juce::Drawable::createFromSVG (*svg);
    // Lucide Lock / LockOpen (ISC, with the Lock glyph derived from Feather/MIT).
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#000000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>)svg"))
        syncLockedIcon = juce::Drawable::createFromSVG (*svg);
    if (auto svg = juce::XmlDocument::parse (R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#000000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect width="18" height="11" x="3" y="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 9.9-1"/></svg>)svg"))
        syncUnlockedIcon = juce::Drawable::createFromSVG (*svg);
    setColour (juce::Label::textColourId, text);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1e2329));
    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, muted);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1e2429));
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff647580));
    setColour (juce::Slider::trackColourId, accent);
    setColour (juce::Slider::thumbColourId, juce::Colours::white);
    setColour (juce::Slider::backgroundColourId, border);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, raised);
    setColour (juce::Slider::textBoxOutlineColourId, border);
}

void StepShaperEditor::Theme::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                     const juce::Colour&, bool highlighted, bool down)
{
    if (button.getComponentID() == "amount-mode")
    {
        const auto bounds = button.getLocalBounds().toFloat().withSizeKeepingCentre (32.0f, 48.0f);
        if ((highlighted || down) && amountModeOutline != nullptr)
            amountModeOutline->drawWithin (g, bounds, juce::RectanglePlacement::centred, 1.0f);
        return;
    }
    const auto id = button.getComponentID();
    const auto selected = button.getToggleState() && id != "speed-sync";
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    if (selected)
    {
        auto colour = juce::Colour (0xffffa51f);
        if (down) colour = colour.brighter (0.08f);
        else if (highlighted) colour = colour.brighter (0.04f);
        g.setColour (colour);
        g.fillRect (bounds);
        g.setColour (colour.brighter (0.18f));
        g.drawRect (bounds, 1.0f);
    }
    else
    {
        const auto laneTab = id == "lane-tab";
        const auto editorControl = id == "editor-control"
                                || id == "pattern-state-menu"
                                || id == "randomize-main"
                                || id == "randomize-arrow";
        auto top = laneTab ? juce::Colour (0xff1e2329) : editorControl ? juce::Colour (0xff334154) : juce::Colour (0xff303438);
        auto bottom = laneTab ? juce::Colour (0xff1e2329) : editorControl ? juce::Colour (0xff283448) : juce::Colour (0xff262a2e);
        if (down) { top = top.brighter (0.08f); bottom = bottom.brighter (0.08f); }
        else if (highlighted) { top = top.brighter (0.04f); bottom = bottom.brighter (0.04f); }
        g.setGradientFill (juce::ColourGradient (top, 0.0f, bounds.getY(), bottom, 0.0f, bounds.getBottom(), false));
        g.fillRect (bounds);
        g.setGradientFill (juce::ColourGradient (editorControl ? juce::Colour (0xff52647a) : juce::Colour (0xff4b5054), 0.0f, bounds.getY(),
                                                 editorControl ? juce::Colour (0xff3d4b5e) : juce::Colour (0xff3c4145), 0.0f, bounds.getBottom(), false));
        g.drawRect (bounds, 1.0f);
    }
    if (! button.isEnabled())
    {
        g.setColour (juce::Colour (0x660f1316));
        g.fillRect (bounds);
    }
}

void StepShaperEditor::Theme::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto label = button.getButtonText();
    const auto id = button.getComponentID();
    const auto opacity = button.isEnabled() ? 0.94f : 0.34f;
    if (id == "amount-mode")
    {
        auto* icon = button.getToggleState() ? amountBipolarIcon.get() : amountUnipolarIcon.get();
        if (icon != nullptr)
            icon->drawWithin (g, button.getLocalBounds().toFloat().withSizeKeepingCentre (13.0f, 24.0f),
                              juce::RectanglePlacement::centred, opacity);
        return;
    }
    if (id == "pattern-state-menu")
    {
        g.setColour (button.isEnabled() ? muted : muted.withAlpha (0.35f));
        const auto centreX = button.getWidth() * 0.5f;
        const auto centreY = button.getHeight() * 0.5f;
        juce::Path arrow;
        arrow.addTriangle (centreX - 3.5f, centreY - 6.0f,
                           centreX - 3.5f, centreY + 6.0f,
                           centreX + 5.0f, centreY);
        g.fillPath (arrow);
        return;
    }
    if (id == "manual-trigger")
    {
        const auto colour = button.isDown() ? laneAccent : (button.isEnabled() ? text : muted.withAlpha (0.35f));
        if (manualTriggerIcon != nullptr)
        {
            manualTriggerIcon->replaceColour (manualTriggerIconColour, colour);
            manualTriggerIconColour = colour;
            const auto bounds = button.getLocalBounds().toFloat().withSizeKeepingCentre (27.0f, 27.0f);
            const auto centre = bounds.getCentre();
            const juce::Graphics::ScopedSaveState save (g);
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi,
                                                             centre.x, centre.y));
            manualTriggerIcon->drawWithin (g, bounds, juce::RectanglePlacement::centred, 1.0f);
        }
        return;
    }
    if (id == "speed-sync")
    {
        auto* icon = button.getToggleState() ? syncLockedIcon.get() : syncUnlockedIcon.get();
        auto& lastColour = button.getToggleState() ? syncLockedIconColour : syncUnlockedIconColour;
        const auto colour = button.getToggleState() ? laneAccent
                                                     : (button.isEnabled() ? text : muted.withAlpha (0.35f));
        if (icon != nullptr)
        {
            icon->replaceColour (lastColour, colour);
            lastColour = colour;
            icon->drawWithin (g, button.getLocalBounds().toFloat().withSizeKeepingCentre (18.0f, 18.0f),
                              juce::RectanglePlacement::centred, 1.0f);
        }
        return;
    }
    if (id == "randomize-arrow")
    {
        g.setColour (button.isEnabled() ? muted : muted.withAlpha (0.35f));
        juce::Path arrow;
        const auto centreX = button.getWidth() * 0.5f;
        const auto centreY = button.getHeight() * 0.52f;
        arrow.addTriangle (centreX - 4.0f, centreY - 2.5f,
                           centreX + 4.0f, centreY - 2.5f,
                           centreX, centreY + 3.0f);
        g.fillPath (arrow);
        return;
    }
    if (id == "random-preset" || id == "random-all" || id == "randomize-main")
    {
        if (diceIcon != nullptr)
        {
            const auto iconSize = id == "randomize-main" ? 14.0f : 17.0f;
            diceIcon->drawWithin (g, { id == "randomize-main" ? 8.0f : 6.0f,
                                      (button.getHeight() - iconSize) * 0.5f,
                                      iconSize, iconSize }, juce::RectanglePlacement::centred, opacity);
        }
        g.setColour (button.isEnabled() ? text : muted.withAlpha (0.35f));
        g.setFont (juce::FontOptions (id == "randomize-main" ? 12.5f : 13.0f,
                                      id == "randomize-main" ? juce::Font::plain : juce::Font::bold));
        g.drawText (id == "randomize-main" ? "RANDOMIZE" : id == "random-preset" ? "P" : "A",
                    id == "randomize-main" ? 27 : 26, 0,
                    button.getWidth() - (id == "randomize-main" ? 30 : 28), button.getHeight(),
                    juce::Justification::centredLeft);
        return;
    }
    if (id == "copy-icon" || id == "paste-icon")
    {
        auto* icon = id == "copy-icon" ? copyIcon.get() : pasteIcon.get();
        if (icon != nullptr)
            icon->drawWithin (g, button.getLocalBounds().toFloat().reduced (15.0f, 6.0f),
                              juce::RectanglePlacement::centred, opacity);
        return;
    }
    if (button.getComponentID() == "undo" || button.getComponentID() == "redo")
    {
        const auto undo = button.getComponentID() == "undo";
        const auto colour = button.isEnabled() ? text : muted.withAlpha (0.35f);
        g.setColour (colour);
        juce::Path icon;
        icon.startNewSubPath (0.06f, 0.42f);
        icon.lineTo (0.38f, 0.08f);
        icon.lineTo (0.38f, 0.29f);
        icon.cubicTo (0.72f, 0.29f, 0.91f, 0.48f, 0.91f, 0.84f);
        icon.lineTo (0.69f, 0.84f);
        icon.cubicTo (0.69f, 0.61f, 0.59f, 0.50f, 0.38f, 0.50f);
        icon.lineTo (0.38f, 0.72f);
        icon.closeSubPath();
        auto bounds = button.getLocalBounds().toFloat().reduced (7.0f, 6.0f);
        if (! undo) icon.applyTransform (juce::AffineTransform::horizontalFlip (1.0f));
        icon.applyTransform (icon.getTransformToScaleToFit (bounds, true));
        g.fillPath (icon);
        return;
    }
    juce::LookAndFeel_V4::drawButtonText (g, button, false, false);
}

juce::Font StepShaperEditor::Theme::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    const auto symbol = button.getButtonText();
    if (symbol == juce::String::fromUTF8 ("\xe2\x86\xb6") || symbol == juce::String::fromUTF8 ("\xe2\x86\xb7"))
        return juce::FontOptions (std::min (24.0f, buttonHeight * 0.72f), juce::Font::plain);
    return juce::FontOptions (std::min (13.5f, buttonHeight * 0.48f), juce::Font::plain);
}

void StepShaperEditor::Theme::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                            int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0, 0, static_cast<float> (width), static_cast<float> (height)).reduced (.5f);
    const auto editorControl = box.getComponentID() == "editor-control" || box.getComponentID() == "draw-mode"
                            || box.getComponentID() == "trig-sync";
    g.setGradientFill (juce::ColourGradient (editorControl ? juce::Colour (0xff334154) : juce::Colour (0xff353a3e), 0.0f, bounds.getY(),
                                             editorControl ? juce::Colour (0xff283448) : juce::Colour (0xff2a2f33), 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);
    g.setGradientFill (juce::ColourGradient (editorControl ? juce::Colour (0xff52647a) : juce::Colour (0xff4b5054), 0.0f, bounds.getY(),
                                             editorControl ? juce::Colour (0xff3d4b5e) : juce::Colour (0xff3c4145), 0.0f, bounds.getBottom(), false));
    g.drawRect (bounds, 1.0f);
    g.setColour (muted);
    if (box.getComponentID() == "draw-mode" && pencilIcon != nullptr)
        pencilIcon->drawWithin (g, { 8.0f, 6.0f, 15.0f, static_cast<float> (height - 12) },
                                juce::RectanglePlacement::centred, 0.92f);
    juce::Path arrow;
    arrow.addTriangle (width - 17.0f, height * .42f, width - 9.0f, height * .42f, width - 13.0f, height * .61f);
    g.fillPath (arrow);
}

void StepShaperEditor::Theme::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    const auto leftTrim = box.getComponentID() == "trig-sync" ? 70 : box.getComponentID() == "draw-mode" ? 29 : 8;
    label.setBounds (box.getLocalBounds().withTrimmedLeft (leftTrim)
                                         .withTrimmedRight (22));
    label.setFont (juce::FontOptions (12.5f));
}

void StepShaperEditor::Theme::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down)
{
    const auto centre = juce::Point<float> (9.0f, button.getHeight() * 0.5f);
    g.setColour (juce::Colour (0xff1a2025));
    g.fillEllipse (centre.x - 6.0f, centre.y - 6.0f, 12.0f, 12.0f);
    g.setColour (highlighted || down ? juce::Colour (0xff7b878f) : juce::Colour (0xff59656d));
    g.drawEllipse (centre.x - 5.5f, centre.y - 5.5f, 11.0f, 11.0f, 1.0f);
    if (button.getToggleState())
    {
        const auto onColour = button.getComponentID() == "position-sync" ? text : juce::Colour (0xffffa51f);
        g.setColour (button.isEnabled() ? onColour : juce::Colour (0xff59656d));
        g.fillEllipse (centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }
    g.setColour (button.isEnabled() ? text : muted.withAlpha (0.65f));
    g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    g.drawText (button.getButtonText(), 19, 0, button.getWidth() - 19, button.getHeight(),
                juce::Justification::centredLeft);
}

void StepShaperEditor::Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                                 float sliderPos, float minSliderPos, float maxSliderPos,
                                                 juce::Slider::SliderStyle style,
                                                 juce::Slider& slider)
{
    if (slider.getComponentID() != "speed-value")
    {
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos,
                                                maxSliderPos, style, slider);
        return;
    }

    auto bounds = slider.getLocalBounds().toFloat().reduced (0.5f);
    auto top = juce::Colour (0xff252a2e);
    auto bottom = juce::Colour (0xff1d2226);
    if (slider.isMouseOverOrDragging()) { top = top.brighter (0.04f); bottom = bottom.brighter (0.04f); }
    g.setGradientFill (juce::ColourGradient (top, 0.0f, bounds.getY(), bottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);
    const auto isModTarget = static_cast<bool> (slider.getProperties().getWithDefault ("modDropTarget", false));
    if (isModTarget)
    {
        g.setColour (juce::Colour (0xff4fc3f7));
        g.drawRect (bounds, 1.8f);
    }

    const auto arrowColumn = bounds.removeFromRight (20.0f);
    g.setColour (juce::Colour (0xff52647a));
    g.drawVerticalLine (juce::roundToInt (arrowColumn.getX()), arrowColumn.getY() + 6.0f,
                        arrowColumn.getBottom() - 6.0f);
    g.setColour (muted);
    juce::Path up;
    up.addTriangle (arrowColumn.getCentreX() - 4.0f, arrowColumn.getCentreY() - 6.5f,
                    arrowColumn.getCentreX() + 4.0f, arrowColumn.getCentreY() - 6.5f,
                    arrowColumn.getCentreX(), arrowColumn.getCentreY() - 11.5f);
    juce::Path down;
    down.addTriangle (arrowColumn.getCentreX() - 4.0f, arrowColumn.getCentreY() + 6.5f,
                      arrowColumn.getCentreX() + 4.0f, arrowColumn.getCentreY() + 6.5f,
                      arrowColumn.getCentreX(), arrowColumn.getCentreY() + 11.5f);
    g.fillPath (up);
    g.fillPath (down);

    g.setColour (text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::plain));
    g.drawText (slider.getTextFromValue (slider.getValue()), bounds.reduced (7.0f, 0.0f).toNearestInt(),
                juce::Justification::centredLeft, true);
}

void StepShaperEditor::Theme::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                                 float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto knobWidth = slider.getComponentID() == "mod-knob" ? std::max (1, width - 16) : width;
    const auto radius = std::max (8.0f, std::min (knobWidth, height) * 0.5f - 4.0f);
    const auto centre = juce::Point<float> (x + knobWidth * 0.5f, y + height * 0.5f);
    const auto arcStart = juce::MathConstants<float>::pi;
    const auto arcEnd = arcStart + juce::MathConstants<float>::twoPi - 0.012f;
    const auto angle = arcStart + position * (arcEnd - arcStart);
    const auto bipolar = slider.getComponentID() == "amount-knob";
    const auto zeroAngle = arcStart + 0.5f * (arcEnd - arcStart);
    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, radius - 2.2f, radius - 2.2f, 0.0f,
                            bipolar ? std::min (zeroAngle, angle) : arcStart,
                            bipolar ? std::max (zeroAngle, angle) : angle, true);
    const auto railBounds = juce::Rectangle<float> (centre.x - radius + 2.2f, centre.y - radius + 2.2f,
                                                     (radius - 2.2f) * 2.0f, (radius - 2.2f) * 2.0f);
    g.setColour (juce::Colour (0xff2b3034));
    g.drawEllipse (railBounds, 4.3f);
    const auto fillColour = slider.getComponentID() == "mod-knob" ? juce::Colour (0xff4fc3f7)
                                                                   : juce::Colour (0xffffab19);
    if (static_cast<bool> (slider.getProperties().getWithDefault ("modDropTarget", false)))
    {
        g.setColour (juce::Colour (0xff4fc3f7));
        g.drawEllipse (railBounds.expanded (3.0f), 1.8f);
    }
    g.setColour (fillColour);
    g.strokePath (valueArc, juce::PathStrokeType (3.7f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    const auto hovering = slider.isMouseOverOrDragging();
    const auto topRadius = (radius - 7.0f) * (hovering ? 0.92f : 1.0f);
    const auto topBounds = juce::Rectangle<float> (centre.x - topRadius, centre.y - topRadius,
                                                   topRadius * 2.0f, topRadius * 2.0f);
    g.setColour (juce::Colours::black.withAlpha (0.50f));
    g.fillEllipse (topBounds.translated (0.0f, 3.0f).expanded (1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff687078), centre.x, topBounds.getY(),
                                             juce::Colour (0xff30373c), centre.x, topBounds.getBottom(), false));
    g.fillEllipse (topBounds);
    g.setColour (juce::Colour (0xff77838b));
    g.drawEllipse (topBounds, 1.45f);
    g.setColour (juce::Colour (0x554f565b));
    g.drawEllipse (topBounds.reduced (1.2f), 0.8f);
    const auto pointer = centre.getPointOnCircumference (topRadius * 0.70f, angle);
    g.setColour (fillColour);
    g.fillEllipse (pointer.x - 1.8f, pointer.y - 1.8f, 3.6f, 3.6f);
}

StepShaperEditor::StepShaperEditor (Model& modelToUse, Engine* engineToDisplay, ParameterCallback callback,
                                    HintCallback hintCallback, RetriggerParameterCallback retriggerCallback)
    : model (modelToUse), engine (engineToDisplay), onParameterChanged (std::move (callback)),
      onHint (std::move (hintCallback)), onRetriggerParameterChanged (std::move (retriggerCallback)),
      patternEditor (model), patternList (model)
{
    setLookAndFeel (&theme);
    setOpaque (true);
    setSize (840, 440);
    juce::Desktop::getInstance().addGlobalMouseListener (this);

    for (int i = 0; i < maxLanes; ++i)
    {
        auto& button = laneButtons[i];
        button.setButtonText (juce::String (i + 1));
        button.setComponentID ("lane-tab");
        button.setClickingTogglesState (false);
        styleButton (button, "Edit LFO " + juce::String (i + 1));
        button.setColour (juce::TextButton::buttonOnColourId, laneAccent);
        button.onClick = [this, i] { model.selectLane (i); refreshFromModel (true); };
        addAndMakeVisible (button);
    }

    styleButton (addLaneButton, "Add another stable Patcher controller output");
    styleButton (undoButton, "Undo last action");
    styleButton (redoButton, "Redo last action");
    undoButton.setComponentID ("undo"); redoButton.setComponentID ("redo");
    styleButton (deletePatternButton, "Delete Current Pattern from Bank");
    styleButton (newPatternButton, "Add New Pattern to Bank");
    styleButton (randomPatternButton, "Pick random preset");
    styleButton (randomizeAllButton, "Randomize all parameters");
    styleButton (copyButton, "Copy current preset");
    styleButton (pasteButton, "Paste copied preset");
    styleButton (curveRandomizeButton, "Randomize current pattern");
    styleButton (curveRandomizeMoreButton, "Randomize options");
    styleButton (patternStateMenuButton, "Pattern state, file, and spline tools");
    styleButton (refreshTriggerButton, "Restart this LFO at the Trig Sync boundary; in MIDI Trigger mode, press and release it like a MIDI note");
    curveRandomizeButton.setComponentID ("editor-control");
    curveRandomizeButton.setComponentID ("randomize-main");
    curveRandomizeMoreButton.setComponentID ("randomize-arrow");
    randomPatternButton.setComponentID ("random-preset");
    randomizeAllButton.setComponentID ("random-all");
    copyButton.setComponentID ("copy-icon");
    pasteButton.setComponentID ("paste-icon");
    drawModeBox.setComponentID ("draw-mode");
    patternStateMenuButton.setComponentID ("pattern-state-menu");
    refreshTriggerButton.setComponentID ("manual-trigger");
    curveRandomizeButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    curveRandomizeMoreButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    curveRandomizeMoreButton.setButtonText ({});
    addAndMakeVisible (addLaneButton);
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    addAndMakeVisible (deletePatternButton);
    addAndMakeVisible (newPatternButton);
    addAndMakeVisible (randomPatternButton);
    addAndMakeVisible (randomizeAllButton);
    addAndMakeVisible (copyButton);
    addAndMakeVisible (pasteButton);
    addAndMakeVisible (patternSelector);
    addAndMakeVisible (gridXControl);
    addAndMakeVisible (gridYControl);
    addAndMakeVisible (patternStateMenuButton);
    addAndMakeVisible (refreshTriggerButton);
    addAndMakeVisible (drawModeBox);
    addAndMakeVisible (curveRandomizeButton);
    addAndMakeVisible (curveRandomizeMoreButton);
    addAndMakeVisible (patternEditor);
    addAndMakeVisible (patternList);
    patternList.setVisible (false);

    addLaneButton.onClick = [this]
    {
        const auto lane = model.addLane();
        if (lane >= 0) notifyParameter (lane, Parameter::enabled);
        refreshFromModel (true);
    };
    undoButton.onClick = [this]
    {
        const auto lane = model.snapshot()->selectedLane;
        if (model.undo()) notifyAllLaneParameters (lane);
    };
    redoButton.onClick = [this]
    {
        const auto lane = model.snapshot()->selectedLane;
        if (model.redo()) notifyAllLaneParameters (lane);
    };
    deletePatternButton.onClick = [this]
    {
        const auto lane = model.snapshot()->selectedLane;
        if (model.deletePattern (lane)) notifyParameter (lane, Parameter::pattern);
    };
    newPatternButton.onClick = [this]
    {
        const auto lane = model.snapshot()->selectedLane;
        if (model.newPattern (lane) >= 0) notifyParameter (lane, Parameter::pattern);
    };
    randomPatternButton.onClick = [this]
    {
        const auto lane = model.snapshot()->selectedLane;
        model.randomPattern (lane);
        notifyParameter (lane, Parameter::pattern);
    };
    randomizeAllButton.onClick = [this]
    {
        const auto lane = model.snapshot()->selectedLane;
        model.randomizeAll (lane);
        notifyAllLaneParameters (lane);
    };
    copyButton.onClick = [this]
    {
        const auto s = model.snapshot();
        sharedPatternClipboard = model.copyPatternText (s->selectedLane, s->lanes[s->selectedLane].selectedPattern);
        juce::SystemClipboard::copyTextToClipboard (sharedPatternClipboard);
        statusLabel.setText ("PRESET COPIED", juce::dontSendNotification);
    };
    pasteButton.onClick = [this]
    {
        const auto s = model.snapshot();
        auto clipboard = juce::SystemClipboard::getTextFromClipboard();
        auto ok = model.pastePatternText (s->selectedLane, s->lanes[s->selectedLane].selectedPattern,
                                          clipboard.toStdString());
        if (! ok && sharedPatternClipboard.isNotEmpty())
            ok = model.pastePatternText (s->selectedLane, s->lanes[s->selectedLane].selectedPattern,
                                         sharedPatternClipboard.toStdString());
        if (ok) notifyAllLaneParameters (s->selectedLane);
        statusLabel.setText (ok ? "PRESET PASTED" : "NO PATTERN BANK PRESET ON CLIPBOARD",
                             juce::dontSendNotification);
    };
    patternSelector.onGestureBegin = [this] { model.beginGesture(); };
    patternSelector.onGestureEnd = [this] { model.endGesture(); };
    patternSelector.onChange = [this] (int value)
    {
        const auto lane = model.snapshot()->selectedLane;
        model.selectPattern (lane, value, true);
        notifyParameter (lane, Parameter::pattern);
    };
    patternSelector.onRename = [this] (juce::String name)
    {
        const auto state = model.snapshot();
        model.renamePattern (state->selectedLane, state->lanes[state->selectedLane].selectedPattern, name.toStdString());
        if (onParameterChanged) notifyParameter (state->selectedLane, Parameter::pattern);
    };
    patternSelector.onRequestMenu = [this]
    {
        patternList.setLane (model.snapshot()->selectedLane);
        patternList.setVisible (true);
        patternList.toFront (true);
        patternList.repaint();
    };
    patternSelector.onParameterMenu = [this] (juce::Point<int> position)
    {
        const auto lane = model.snapshot()->selectedLane;
        if (onParameterMenu) onParameterMenu (lane, Parameter::pattern, position);
    };
    patternList.onSelect = [this] (int)
    {
        const auto lane = model.snapshot()->selectedLane;
        notifyParameter (lane, Parameter::pattern);
        refreshFromModel (true);
        patternList.setVisible (true);
        patternList.toFront (true);
    };
    patternList.onDelete = [this] (int slot)
    {
        const auto lane = model.snapshot()->selectedLane;
        model.selectPattern (lane, slot, false);
        if (model.deletePattern (lane)) notifyParameter (lane, Parameter::pattern);
        refreshFromModel (true);
        patternList.setVisible (true);
        patternList.toFront (true);
    };
    patternEditor.onStartPositionChanged = [this] (int lane)
    {
        notifyParameter (lane, Parameter::startPosition);
    };
    patternEditor.onSustainPositionChanged = [this] (int lane)
    {
        notifyParameter (lane, Parameter::sustainPosition);
    };
    patternEditor.onCanvasClicked = [this] { patternList.setVisible (false); };
    patternEditor.onHint = [this] (const std::string& hint)
    {
        const auto textToShow = juce::String (hint);
        if (lastHint == textToShow) return;
        lastHint = textToShow;
        if (onHint) onHint (hint);
    };

    gridXControl.onGestureBegin = gridYControl.onGestureBegin = [this] { model.beginGesture(); };
    gridXControl.onGestureEnd = gridYControl.onGestureEnd = [this] { model.endGesture(); };
    gridXControl.onValueChange = [this] (int value)
    {
        if (refreshing) return;
        model.mutate ([value] (ProjectState& s) { s.editorGridX = value; });
        patternEditor.setGrid (value, gridYControl.getValue());
    };
    gridYControl.onValueChange = [this] (int value)
    {
        if (refreshing) return;
        model.mutate ([value] (ProjectState& s) { s.editorGridY = value; });
        patternEditor.setGrid (gridXControl.getValue(), value);
    };
    drawModeBox.addItemList ({ "EDIT", "RAMP UP", "RAMP DOWN", "STEPS", "ERASER" }, 1);
    drawModeBox.setTooltip ("Choose point editing, ramp-up, ramp-down, step painting, or erasing");
    drawModeBox.onChange = [this]
    {
        if (refreshing) return;
        const auto mode = static_cast<DrawMode> (drawModeBox.getSelectedItemIndex());
        model.mutate ([mode] (ProjectState& s) { s.editorDrawMode = mode; });
        patternEditor.setDrawMode (mode);
    };
    curveRandomizeButton.onClick = [this] { patternEditor.randomize(); };
    curveRandomizeMoreButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Random Lines");
        menu.addItem (2, "Random Ramp Ups");
        menu.addItem (3, "Random Ramp Downs");
        menu.addItem (4, "Random Steps");
        menu.addSeparator();
        menu.addItem (5, "Random Minor Melody");
        menu.addItem (6, "Random Major Melody");
        menu.addSeparator();
        menu.addItem (7, "Reset Pattern");
        const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&curveRandomizeMoreButton),
                            [safe] (int result)
                            {
                                if (safe == nullptr) return;
                                if (result == 1) safe->patternEditor.randomizeLines();
                                if (result == 2) safe->patternEditor.randomizeRampUps();
                                if (result == 3) safe->patternEditor.randomizeRampDowns();
                                if (result == 4) safe->patternEditor.randomizeSteps();
                                if (result == 5) safe->patternEditor.randomizeMelody (true);
                                if (result == 6) safe->patternEditor.randomizeMelody (false);
                                if (result == 7) safe->patternEditor.resetPattern();
                            });
    };
    patternStateMenuButton.onClick = [this] { showPatternStateMenu(); };

    speedSlider.setComponentID ("speed-value");
    speedSlider.setSliderStyle (juce::Slider::LinearBarVertical);
    speedSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    speedSlider.setSliderSnapsToMousePosition (false);
    speedSlider.setMouseDragSensitivity (160);
    speedSlider.setTooltip ("One rate control: Hz when Sync is off, note divisions when Sync is on");
    speedSlider.onRightClick = [this] (juce::Point<int> screenPosition)
    {
        const auto state = model.snapshot();
        const auto lane = state->selectedLane;
        if (onParameterMenu) { onParameterMenu (lane, state->lanes[lane].sync ? Parameter::division : Parameter::speed, screenPosition); return; }
        if (! state->lanes[lane].sync) return;
        juce::PopupMenu menu;
        for (int division = 0; division < 27; ++division)
            menu.addItem (division + 1, divisionName (division), true, division == state->lanes[lane].division);
        const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ screenPosition.x, screenPosition.y, 1, 1 }),
                            [safe, lane] (int result)
                            {
                                if (safe == nullptr || result <= 0) return;
                                const auto division = result - 1;
                                safe->model.mutate ([=] (ProjectState& s) { s.lanes[lane].division = division; });
                                safe->notifyParameter (lane, Parameter::division);
                            });
    };
    speedSlider.onDragStart = [this] { model.beginGesture(); };
    speedSlider.onDragEnd = [this] { model.endGesture(); };
    speedSlider.onValueChange = [this]
    {
        if (refreshing) return;
        const auto snapshot = model.snapshot();
        const auto lane = snapshot->selectedLane;
        if (snapshot->lanes[lane].sync)
        {
            model.mutate ([&] (ProjectState& s) { s.lanes[lane].division = juce::roundToInt (speedSlider.getValue()); });
            notifyParameter (lane, Parameter::division);
        }
        else
        {
            model.mutate ([&] (ProjectState& s) { s.lanes[lane].speedHz = static_cast<float> (speedSlider.getValue()); });
            notifyParameter (lane, Parameter::speed);
        }
    };
    addAndMakeVisible (speedSlider);

    auto prepareValueKnob = [this] (RateSlider& slider, Parameter parameter, const juce::String& tooltip)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (parameter == Parameter::patternMix ? -1.0 : 0.0, 1.0, 0.001);
        slider.setTooltip (tooltip);
        slider.onRightClick = [this, parameter] (juce::Point<int> position)
        {
            const auto lane = model.snapshot()->selectedLane;
            if (onParameterMenu) onParameterMenu (lane, parameter, position);
        };
        slider.onDragStart = [this] { model.beginGesture(); };
        slider.onDragEnd = [this] { model.endGesture(); };
        slider.onValueChange = [this, &slider, parameter]
        {
            if (refreshing) return;
            const auto lane = model.snapshot()->selectedLane;
            const auto value = static_cast<float> (slider.getValue());
            model.mutate ([=] (ProjectState& s)
            {
                if (parameter == Parameter::baseValue) s.lanes[lane].baseValue = value;
                else if (parameter == Parameter::patternMix) s.lanes[lane].patternMix = value;
                else if (parameter == Parameter::mod1) s.lanes[lane].mod1 = value;
                else if (parameter == Parameter::mod2) s.lanes[lane].mod2 = value;
            });
            notifyParameter (lane, parameter);
        };
        addAndMakeVisible (slider);
    };
    prepareValueKnob (baseValueSlider, Parameter::baseValue, "Output value used when Amount is zero");
    prepareValueKnob (patternMixSlider, Parameter::patternMix,
                      "Pattern depth around Base; its source range is selected with the Mode button");
    patternMixSlider.setComponentID ("amount-knob");
    amountModeButton.setComponentID ("amount-mode");
    amountModeButton.setClickingTogglesState (true);
    styleButton (amountModeButton, "Amount mode: Unipolar adds 0 to +Pattern; Bipolar adds -Pattern to +Pattern around Base");
    amountModeButton.onClick = [this]
    {
        if (refreshing) return;
        const auto lane = model.snapshot()->selectedLane;
        const auto bipolar = amountModeButton.getToggleState();
        model.mutate ([=] (ProjectState& s) { s.lanes[lane].amountBipolar = bipolar; });
    };
    addAndMakeVisible (amountModeButton);
    prepareValueKnob (modSliders[0], Parameter::mod1,
                      "Mod 1 macro; drag its arrow onto a point, tension handle, or Speed knob");
    prepareValueKnob (modSliders[1], Parameter::mod2,
                      "Mod 2 macro; drag its arrow onto a point, tension handle, or Speed knob");
    for (int source = 0; source < 2; ++source)
    {
        modSliders[source].setComponentID ("mod-knob");
        modSliders[source].onSourceDrag = [this, source] (juce::Point<int> screen)
        {
            const auto overSpeed = isOverSpeedKnob (screen);
            speedModDragSource = overSpeed ? source : -1;
            speedSlider.getProperties().set ("modDropTarget", overSpeed);
            speedSlider.repaint();
            patternEditor.updateModDrag (source, overSpeed ? juce::Point<float> (-1000.0f, -1000.0f)
                                                           : patternEditor.getLocalPoint (nullptr, screen).toFloat());
        };
        modSliders[source].onSourceDrop = [this, source] (juce::Point<int> screen)
        {
            const auto overSpeed = isOverSpeedKnob (screen);
            patternEditor.finishModDrag (source, overSpeed ? juce::Point<float> (-1000.0f, -1000.0f)
                                                           : patternEditor.getLocalPoint (nullptr, screen).toFloat());
            if (overSpeed) setSpeedModDepth (source, 0.25f);
            speedModDragSource = -1;
            speedSlider.getProperties().set ("modDropTarget", false);
            speedSlider.repaint();
        };
        modSliders[source].onRightClick = [this, source] (juce::Point<int> position)
        {
            showModMenu (source, position);
        };
    }

    syncButton.setComponentID ("speed-sync");
    syncButton.setClickingTogglesState (true);
    styleButton (syncButton, "Lock Speed to FL Studio tempo; unlock for a free-running Hz rate");
    syncButton.onClick = [this]
    {
        if (refreshing) return;
        const auto lane = model.snapshot()->selectedLane;
        model.mutate ([&] (ProjectState& s) { s.lanes[lane].sync = syncButton.getToggleState(); });
        notifyParameter (lane, Parameter::sync);
        refreshFromModel (true);
    };
    patternList.onAdd = [this]
    {
        const auto lane = model.snapshot()->selectedLane;
        if (model.newPattern (lane) >= 0)
        {
            notifyParameter (lane, Parameter::pattern);
            refreshFromModel (true);
            patternList.setVisible (true);
            patternList.toFront (true);
        }
    };
    addAndMakeVisible (syncButton);

    midiTriggerButton.setClickingTogglesState (true);
    midiTriggerButton.setTooltip ("Loop from L to S while held, then play the remaining tail on MIDI release");
    midiTriggerButton.onClick = [this]
    {
        if (refreshing) return;
        const auto lane = model.snapshot()->selectedLane;
        model.mutate ([&] (ProjectState& s) { s.lanes[lane].midiTrigger = midiTriggerButton.getToggleState(); });
        notifyParameter (lane, Parameter::midiTrigger);
        patternEditor.repaint();
        refreshFromModel (true);
    };
    addAndMakeVisible (midiTriggerButton);

    refreshTriggerButton.onStateChange = [this]
    {
        const auto isDown = refreshTriggerButton.isDown();
        if (isDown == refreshTriggerWasDown) return;
        refreshTriggerWasDown = isDown;
        if (isDown)
        {
            const auto state = model.snapshot();
            refreshTriggerLane = state->selectedLane;
            refreshTriggerWasMidi = state->lanes[refreshTriggerLane].midiTrigger;
            if (engine != nullptr) engine->requestManualTrigger (refreshTriggerLane);
            if (onRetriggerParameterChanged) onRetriggerParameterChanged (refreshTriggerLane, true);
        }
        else
        {
            if (engine != nullptr && refreshTriggerWasMidi) engine->requestManualRelease (refreshTriggerLane);
            if (onRetriggerParameterChanged) onRetriggerParameterChanged (refreshTriggerLane, false);
            refreshTriggerWasMidi = false;
        }
    };

    legatoButton.setClickingTogglesState (true);
    legatoButton.setTooltip ("Do not restart the pattern when a new note overlaps a held note");
    legatoButton.onClick = [this]
    {
        if (refreshing) return;
        const auto lane = model.snapshot()->selectedLane;
        model.mutate ([&] (ProjectState& s) { s.lanes[lane].legato = legatoButton.getToggleState(); });
    };
    addAndMakeVisible (legatoButton);

    for (int i = 0; i < static_cast<int> (ChangeMode::count); ++i)
        changeModeBox.addItem (changeModeName (static_cast<ChangeMode> (i)), i + 1);
    changeModeBox.setComponentID ("trig-sync");
    addAndMakeVisible (changeModeBox);
    changeModeBox.onChange = [this]
    {
        if (refreshing) return; const auto lane = model.snapshot()->selectedLane;
        model.mutate ([&] (ProjectState& s) { s.lanes[lane].changeMode = static_cast<ChangeMode> (changeModeBox.getSelectedItemIndex()); });
        notifyParameter (lane, Parameter::changeMode);
    };

    positionSyncButton.setClickingTogglesState (true);
    positionSyncButton.setComponentID ("position-sync");
    positionSyncButton.setTooltip ("Follow FL Studio's song position; ignored while MIDI Trigger is enabled");
    positionSyncButton.onClick = [this]
    {
        if (refreshing) return;
        const auto lane = model.snapshot()->selectedLane;
        model.mutate ([&] (ProjectState& s) { s.lanes[lane].positionSync = positionSyncButton.getToggleState(); });
        notifyParameter (lane, Parameter::positionSync);
    };
    addAndMakeVisible (positionSyncButton);

    auto prepareLabel = [this] (juce::Label& label, const juce::String& value)
    {
        label.setText (value, juce::dontSendNotification);
        label.setColour (juce::Label::textColourId, muted);
        label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        addAndMakeVisible (label);
    };
    prepareLabel (speedLabel, "SPEED"); prepareLabel (retriggerLabel, "RETRIG");
    prepareLabel (baseValueLabel, "BASE"); prepareLabel (amountModeLabel, "MODE");
    prepareLabel (patternMixLabel, "AMOUNT");
    prepareLabel (modLabels[0], "MOD 1"); prepareLabel (modLabels[1], "MOD 2");
    prepareLabel (modeLabel, "TRIG SYNC:");
    modeLabel.setColour (juce::Label::textColourId, text);
    modeLabel.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    prepareLabel (editorHelpLabel, "SHIFT draw  |  CTRL drag-select / move  |  ALT snap  |  DELETE / CTRL+Z");
    prepareLabel (statusLabel, "MADE BY NICK / RUNNIT");
    modeLabel.setInterceptsMouseClicks (false, false);
    editorHelpLabel.setJustificationType (juce::Justification::centredRight);
    editorHelpLabel.setFont (juce::FontOptions (9.5f));
    editorHelpLabel.setVisible (false);
    statusLabel.setJustificationType (juce::Justification::centredRight);
    speedLabel.setJustificationType (juce::Justification::centred);
    retriggerLabel.setJustificationType (juce::Justification::centred);
    baseValueLabel.setJustificationType (juce::Justification::centred);
    amountModeLabel.setJustificationType (juce::Justification::centred);
    patternMixLabel.setJustificationType (juce::Justification::centred);
    for (auto& label : modLabels) label.setJustificationType (juce::Justification::centred);

    refreshFromModel (true);
    startTimerHz (30);
}

StepShaperEditor::~StepShaperEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
    setLookAndFeel (nullptr);
}

juce::File StepShaperEditor::patternDataDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Image-Line").getChildFile ("Pattern Bank").getChildFile ("Data");
}

void StepShaperEditor::showPatternStateMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("STATE");
    menu.addItem (1, "Open pattern file...");
    menu.addItem (2, "Save pattern file...");
    menu.addSeparator();
    menu.addItem (3, "Copy state");
    menu.addItem (4, "Paste state", juce::SystemClipboard::getTextFromClipboard().isNotEmpty()
                                      || sharedPatternClipboard.isNotEmpty());
    menu.addItem (5, "Reset");
    menu.addSeparator();
    menu.addSectionHeader ("TOOLS");
    menu.addItem (10, "Flip vertically");
    menu.addItem (11, "Flip horizontally");
    menu.addItem (12, "Normalize levels");
    menu.addItem (13, "Smooth up...");
    menu.addItem (14, "Smooth abrupt changes");
    menu.addItem (15, "Turn all points smooth");
    const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternStateMenuButton),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            if (result == 1) { safe->openPatternFile(); return; }
                            if (result == 2) { safe->savePatternFile(); return; }
                            const auto state = safe->model.snapshot();
                            const auto lane = state->selectedLane;
                            const auto slot = state->lanes[lane].selectedPattern;
                            if (result == 3)
                            {
                                sharedPatternClipboard = safe->model.copyPatternText (lane, slot);
                                juce::SystemClipboard::copyTextToClipboard (sharedPatternClipboard);
                                safe->statusLabel.setText ("PATTERN STATE COPIED", juce::dontSendNotification);
                            }
                            if (result == 4)
                            {
                                auto textToPaste = juce::SystemClipboard::getTextFromClipboard();
                                auto ok = safe->model.pastePatternText (lane, slot, textToPaste.toStdString());
                                if (! ok && sharedPatternClipboard.isNotEmpty())
                                    ok = safe->model.pastePatternText (lane, slot, sharedPatternClipboard.toStdString());
                                if (ok) safe->notifyAllLaneParameters (lane);
                                safe->statusLabel.setText (ok ? "PATTERN STATE PASTED" : "INVALID PATTERN STATE",
                                                           juce::dontSendNotification);
                            }
                            if (result == 5) safe->patternEditor.resetPattern();
                            if (result == 10) safe->patternEditor.flipVertically();
                            if (result == 11) safe->patternEditor.flipHorizontally();
                            if (result == 12) safe->patternEditor.normalizeLevels();
                            if (result == 13) safe->showSmoothUpDialog();
                            if (result == 14) safe->patternEditor.smoothAbruptChanges();
                            if (result == 15) safe->patternEditor.turnAllPointsSmooth();
                        });
}

void StepShaperEditor::showSmoothUpDialog()
{
    const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
    patternEditor.beginSmoothPreview();
    patternEditor.updateSmoothPreview (.25f, 0.0f);
    auto panelToOwn = std::make_unique<SmoothUpPanel> (
        [safe] (float smooth, float decimation)
        {
            if (safe != nullptr) safe->patternEditor.updateSmoothPreview (smooth, decimation);
        },
        [safe] { if (safe != nullptr) safe->patternEditor.commitSmoothPreview(); },
        [safe] { if (safe != nullptr) safe->patternEditor.cancelSmoothPreview(); });
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Smooth up";
    options.dialogBackgroundColour = panel;
    options.content.setOwned (panelToOwn.release());
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    options.launchAsync();
}

void StepShaperEditor::openPatternFile()
{
    const auto directory = patternDataDirectory();
    directory.createDirectory();
    const auto destination = model.snapshot();
    const auto destinationLane = destination->selectedLane;
    const auto destinationSlot = destination->lanes[destinationLane].selectedPattern;
    patternFileChooser = std::make_unique<juce::FileChooser> (
        "Open Pattern Bank pattern", directory, "*.patternbank", true);
    const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
    patternFileChooser->launchAsync (juce::FileBrowserComponent::openMode
                                     | juce::FileBrowserComponent::canSelectFiles,
                                     [safe, destinationLane, destinationSlot] (const juce::FileChooser& chooser)
                                     {
                                         if (safe == nullptr) return;
                                         const auto file = chooser.getResult();
                                         if (! file.existsAsFile()) return;
                                         const auto ok = safe->model.pastePatternText (
                                             destinationLane, destinationSlot, file.loadFileAsString().toStdString());
                                         if (ok)
                                         {
                                             safe->model.selectLane (destinationLane);
                                             safe->model.selectPattern (destinationLane, destinationSlot, false);
                                             safe->notifyAllLaneParameters (destinationLane);
                                             safe->refreshFromModel (true);
                                             safe->patternEditor.repaint();
                                         }
                                         safe->statusLabel.setText (ok ? "PATTERN LOADED: " + file.getFileNameWithoutExtension()
                                                                      : "INVALID .PATTERNBANK FILE",
                                                                    juce::dontSendNotification);
                                     });
}

void StepShaperEditor::savePatternFile()
{
    const auto directory = patternDataDirectory();
    directory.createDirectory();
    const auto state = model.snapshot();
    const auto lane = state->selectedLane;
    const auto slot = state->lanes[lane].selectedPattern;
    auto suggestedName = juce::File::createLegalFileName (
        patternBank (state->lanes[lane]).names[slot]).trim();
    if (suggestedName.isEmpty()) suggestedName = "Pattern " + juce::String (slot + 1);
    patternFileChooser = std::make_unique<juce::FileChooser> (
        "Save Pattern Bank pattern", directory.getChildFile (suggestedName + ".patternbank"),
        "*.patternbank", true);
    const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
    patternFileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                     | juce::FileBrowserComponent::canSelectFiles
                                     | juce::FileBrowserComponent::warnAboutOverwriting,
                                     [safe] (const juce::FileChooser& chooser)
                                     {
                                         if (safe == nullptr) return;
                                         auto file = chooser.getResult();
                                         if (file == juce::File()) return;
                                         file = file.withFileExtension (".patternbank");
                                         const auto stateNow = safe->model.snapshot();
                                         const auto laneNow = stateNow->selectedLane;
                                         const auto slotNow = stateNow->lanes[laneNow].selectedPattern;
                                         const auto ok = file.replaceWithText (juce::String (
                                             safe->model.copyPatternText (laneNow, slotNow)));
                                         safe->statusLabel.setText (ok ? "PATTERN SAVED: " + file.getFileNameWithoutExtension()
                                                                      : "COULD NOT SAVE PATTERN",
                                                                    juce::dontSendNotification);
                                     });
}

void StepShaperEditor::showModMenu (int source, juce::Point<int> position)
{
    source = std::clamp (source, 0, 1);
    const auto routes = patternEditor.modRoutes (source);
    const auto state = model.snapshot();
    const auto speedDepth = state->lanes[state->selectedLane].modSpeed[static_cast<std::size_t> (source)];
    juce::PopupMenu menu;
    if (routes.empty() && std::abs (speedDepth) <= 0.0001f) menu.addItem (-1, "No assignments", false, false);
    else menu.addSectionHeader ("Drag row to set depth");
    for (int i = 0; i < static_cast<int> (routes.size()); ++i)
    {
        const auto route = routes[static_cast<std::size_t> (i)];
        const auto axis = route.target == PatternEditor::ModTarget::pointX ? "X"
                          : route.target == PatternEditor::ModTarget::pointY ? "Y" : "Tension";
        const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
        menu.addCustomItem (100 + i, std::make_unique<ModDepthMenuItem> (
            "Point " + juce::String (route.point + 1) + "  " + axis, route.depth,
            juce::Colour (0xff4fc3f7),
            [safe] { if (safe != nullptr) safe->model.beginGesture(); },
            [safe, source, route] (float depth)
            {
                if (safe != nullptr) safe->patternEditor.setModDepth (source, route, depth);
            },
            [safe] { if (safe != nullptr) safe->model.endGesture(); },
            [safe, source, route, position]
            {
                if (safe == nullptr) return;
                safe->patternEditor.setModDepth (source, route, 0.0f);
                juce::PopupMenu::dismissAllActiveMenus();
                juce::MessageManager::callAsync ([safe, source, position]
                {
                    if (safe != nullptr) safe->showModMenu (source, position);
                });
            }), nullptr, "Drag to adjust modulation depth");
    }
    if (std::abs (speedDepth) > 0.0001f)
    {
        const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
        menu.addCustomItem (2500, std::make_unique<ModDepthMenuItem> (
            "Speed", speedDepth, juce::Colour (0xff4fc3f7),
            [safe] { if (safe != nullptr) safe->model.beginGesture(); },
            [safe, source] (float depth) { if (safe != nullptr) safe->setSpeedModDepth (source, depth); },
            [safe] { if (safe != nullptr) safe->model.endGesture(); },
            [safe, source, position]
            {
                if (safe == nullptr) return;
                safe->setSpeedModDepth (source, 0.0f);
                juce::PopupMenu::dismissAllActiveMenus();
                juce::MessageManager::callAsync ([safe, source, position]
                {
                    if (safe != nullptr) safe->showModMenu (source, position);
                });
            }), nullptr, "Drag to adjust speed modulation depth");
    }
    menu.addSeparator(); menu.addItem (3000, "FL parameter menu...");
    const auto safe = juce::Component::SafePointer<StepShaperEditor> (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ position.x, position.y, 1, 1 }),
                        [safe, source, position] (int result)
                        {
                            if (safe == nullptr || result != 3000) return;
                            if (safe->onParameterMenu) safe->onParameterMenu (safe->model.snapshot()->selectedLane,
                                source == 0 ? Parameter::mod1 : Parameter::mod2, position);
                        });
}

bool StepShaperEditor::isOverSpeedKnob (juce::Point<int> screenPosition) const
{
    return speedSlider.getScreenBounds().expanded (7).contains (screenPosition);
}

void StepShaperEditor::setSpeedModDepth (int source, float depth)
{
    source = std::clamp (source, 0, 1);
    const auto lane = model.snapshot()->selectedLane;
    model.mutate ([=] (ProjectState& state)
    {
        state.lanes[lane].modSpeed[static_cast<std::size_t> (source)] = std::clamp (depth, -1.0f, 1.0f);
    });
    speedSlider.repaint();
}

void StepShaperEditor::styleButton (juce::TextButton& button, const juce::String& tooltip)
{
    button.setTooltip (tooltip);
    button.setColour (juce::TextButton::buttonColourId, raised);
    button.setColour (juce::TextButton::buttonOnColourId, accent);
}

void StepShaperEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff22292e));
    g.setColour (panel); g.fillRect (0, 0, getWidth(), 42);
    g.setColour (border); g.drawHorizontalLine (41, 0.0f, static_cast<float> (getWidth()));
    g.setColour (juce::Colour (0xff1f2a3b)); g.fillRect (0, getHeight() - 108, getWidth(), 35);
    g.setColour (juce::Colour (0xff334154)); g.drawHorizontalLine (getHeight() - 108, 0.0f, static_cast<float> (getWidth()));
    g.setColour (panel); g.fillRect (0, getHeight() - 73, getWidth(), 73);
    g.setColour (border); g.drawHorizontalLine (getHeight() - 74, 0.0f, static_cast<float> (getWidth()));
    g.setColour (juce::Colour (0xff4a5359).withAlpha (0.72f));
    for (const auto dividerX : bottomDividerXs)
        if (dividerX > 0)
            g.drawVerticalLine (dividerX, static_cast<float> (getHeight() - 64),
                                static_cast<float> (getHeight() - 8));
}

void StepShaperEditor::mouseDown (const juce::MouseEvent& event)
{
    updateHoverFeedback (event);
    if (! patternList.isVisible()) return;
    auto* clicked = event.originalComponent;
    if (clicked == &patternList || patternList.isParentOf (clicked)
        || clicked == &patternSelector || patternSelector.isParentOf (clicked)) return;
    patternList.setVisible (false);
}

void StepShaperEditor::mouseMove (const juce::MouseEvent& event)
{
    updateHoverFeedback (event);
}

juce::String StepShaperEditor::knobValueText (const juce::Slider& slider) const
{
    if (&slider == &speedSlider)
    {
        const auto state = model.snapshot();
        return state->lanes[state->selectedLane].sync
            ? juce::String (divisionName (juce::roundToInt (slider.getValue())))
            : juce::String (slider.getValue(), 3) + " Hz";
    }
    if (&slider == &patternMixSlider)
        return (slider.getValue() > 0.0 ? "+" : "") + juce::String (juce::roundToInt (slider.getValue() * 100.0)) + "%";
    return juce::String (slider.getValue(), 3);
}

juce::String StepShaperEditor::hintForComponent (juce::Component* component) const
{
    if (component == nullptr) return {};
    for (int i = 0; i < maxLanes; ++i) if (component == &laneButtons[i]) return "Select LFO " + juce::String (i + 1);
    if (component == &addLaneButton) return "Add LFO output";
    if (component == &patternSelector)
    {
        const auto s = model.snapshot(); const auto lane = s->selectedLane; const auto slot = s->lanes[lane].selectedPattern;
        return "Pattern " + juce::String (slot + 1).paddedLeft ('0', 2) + ": " + patternBank (s->lanes[lane]).names[slot];
    }
    if (component == &gridXControl) return "Grid X: " + juce::String (gridXControl.getValue());
    if (component == &gridYControl) return "Grid Y: " + juce::String (gridYControl.getValue());
    if (component == &drawModeBox) return "Drawing mode: " + drawModeBox.getText();
    if (component == &changeModeBox || component == &modeLabel) return "Trig Sync: " + changeModeBox.getText();
    if (component == &positionSyncButton) return "Position Sync: " + juce::String (positionSyncButton.getToggleState() ? "On" : "Off");
    if (component == &syncButton) return "Tempo Sync: " + juce::String (syncButton.getToggleState() ? "On" : "Off");
    if (component == &midiTriggerButton) return "MIDI Trigger: " + juce::String (midiTriggerButton.getToggleState() ? "On" : "Off");
    if (component == &refreshTriggerButton) return midiTriggerButton.getToggleState()
        ? "Manual MIDI trigger: press to retrigger, release for note-off"
        : "Restart pattern using the current Trig Sync setting";
    if (component == &legatoButton) return "Legato: " + juce::String (legatoButton.getToggleState() ? "On" : "Off");
    if (component == &speedSlider) return "Speed: " + knobValueText (speedSlider);
    if (component == &baseValueSlider) return "Base: " + knobValueText (baseValueSlider);
    if (component == &amountModeButton)
        return juce::String ("Amount Mode: ") + (amountModeButton.getToggleState() ? "Bipolar (-/+ Pattern)" : "Unipolar (0/+ Pattern)");
    if (component == &patternMixSlider) return "Amount: " + knobValueText (patternMixSlider);
    if (component == &modSliders[0]) return "Mod 1: " + knobValueText (modSliders[0]);
    if (component == &modSliders[1]) return "Mod 2: " + knobValueText (modSliders[1]);
    if (auto* tooltip = dynamic_cast<juce::TooltipClient*> (component)) return tooltip->getTooltip();
    return {};
}

void StepShaperEditor::updateHoverFeedback (const juce::MouseEvent& event)
{
    auto* component = event.originalComponent;
    if (component == &patternEditor || patternEditor.isParentOf (component)) return;
    while (component != nullptr && component->getParentComponent() != this) component = component->getParentComponent();
    const auto hint = hintForComponent (component);
    if (hint.isNotEmpty() && hint != lastHint)
    {
        lastHint = hint;
        if (onHint) onHint (hint.toStdString());
    }
}

void StepShaperEditor::resized()
{
    auto top = juce::Rectangle<int> (8, 5, getWidth() - 16, 32);
    redoButton.setBounds (top.removeFromRight (35)); top.removeFromRight (3);
    undoButton.setBounds (top.removeFromRight (35)); top.removeFromRight (7);
    for (int i = 0; i < maxLanes; ++i)
        if (laneButtons[i].isVisible())
        {
            laneButtons[i].setBounds (top.removeFromLeft (28));
            top.removeFromLeft (2);
        }
    if (addLaneButton.isVisible()) { addLaneButton.setBounds (top.removeFromLeft (28)); top.removeFromLeft (8); }
    else addLaneButton.setBounds ({});
    const auto activeLanes = model.snapshot()->activeLaneCount;
    constexpr int toolbarGap = 5;
    patternSelector.setBounds (top.removeFromLeft (activeLanes >= 8 ? 195 : activeLanes == 7 ? 185 : 205)); top.removeFromLeft (toolbarGap);
    newPatternButton.setBounds (top.removeFromLeft (42)); top.removeFromLeft (toolbarGap);
    deletePatternButton.setBounds (top.removeFromLeft (38)); top.removeFromLeft (toolbarGap);
    randomPatternButton.setBounds (top.removeFromLeft (42)); top.removeFromLeft (toolbarGap);
    randomizeAllButton.setBounds (top.removeFromLeft (42)); top.removeFromLeft (toolbarGap);
    copyButton.setBounds (top.removeFromLeft (52)); top.removeFromLeft (toolbarGap);
    pasteButton.setBounds (top.removeFromLeft (52));

    patternStateMenuButton.setBounds (5, getHeight() - 104, 19, 27);
    auto editorToolbar = juce::Rectangle<int> (28, getHeight() - 104, getWidth() - 36, 27);
    gridXControl.setBounds (editorToolbar.removeFromLeft (78)); editorToolbar.removeFromLeft (6);
    gridYControl.setBounds (editorToolbar.removeFromLeft (78)); editorToolbar.removeFromLeft (6);
    drawModeBox.setBounds (editorToolbar.removeFromLeft (116)); editorToolbar.removeFromLeft (6);
    curveRandomizeButton.setBounds (editorToolbar.removeFromLeft (112));
    curveRandomizeMoreButton.setBounds (editorToolbar.removeFromLeft (30)); editorToolbar.removeFromLeft (8);
    changeModeBox.setBounds (editorToolbar.removeFromLeft (190));
    modeLabel.setBounds (changeModeBox.getX() + 8, changeModeBox.getY(), 64, changeModeBox.getHeight());
    modeLabel.toFront (false);
    editorToolbar.removeFromLeft (8);
    positionSyncButton.setBounds (editorToolbar.removeFromLeft (92));

    patternEditor.setBounds (0, 42, getWidth(), getHeight() - 150);
    auto bottom = juce::Rectangle<int> (8, getHeight() - 69, getWidth() - 16, 65);
    auto speedGroup = bottom.removeFromLeft (124);
    speedLabel.setBounds (speedGroup.getX(), speedGroup.getY(), speedGroup.getWidth(), 10);
    const auto speedBody = juce::Rectangle<int> (speedGroup.getX(), speedGroup.getY() + 14, 124, 44);
    speedSlider.setBounds (speedBody.withWidth (89));
    syncButton.setBounds (speedBody.getX() + 89, speedBody.getY(), 35, speedBody.getHeight());

    auto divider = bottom.removeFromLeft (13);
    bottomDividerXs[0] = divider.getCentreX();

    auto triggerGroup = bottom.removeFromLeft (194);
    retriggerLabel.setBounds (triggerGroup.getX(), triggerGroup.getY(), triggerGroup.getWidth(), 10);
    constexpr int triggerContentWidth = 176;
    const auto triggerContentX = triggerGroup.getX() + (triggerGroup.getWidth() - triggerContentWidth) / 2;
    refreshTriggerButton.setBounds (triggerContentX, triggerGroup.getY() + 14, 48, 44);
    auto triggerToggles = juce::Rectangle<int> (triggerContentX + 60, triggerGroup.getY() + 16,
                                                triggerContentWidth - 60, 40);
    midiTriggerButton.setBounds (triggerToggles.removeFromTop (20));
    legatoButton.setBounds (triggerToggles.removeFromTop (20));

    divider = bottom.removeFromLeft (13);
    bottomDividerXs[1] = divider.getCentreX();

    auto outputGroup = bottom.removeFromLeft (206);
    outputGroup.removeFromLeft (13);
    auto base = outputGroup.removeFromLeft (64);
    baseValueLabel.setBounds (base.removeFromTop (10));
    baseValueSlider.setBounds (base);
    outputGroup.removeFromLeft (2);
    auto amountMode = outputGroup.removeFromLeft (42);
    amountModeLabel.setBounds (amountMode.removeFromTop (10));
    amountModeButton.setBounds (amountMode.reduced (2, 0));
    outputGroup.removeFromLeft (2);
    auto mix = outputGroup.removeFromLeft (64);
    patternMixLabel.setBounds (mix.removeFromTop (10));
    patternMixSlider.setBounds (mix);

    divider = bottom.removeFromLeft (13);
    bottomDividerXs[2] = divider.getCentreX();

    auto modGroup = bottom.removeFromLeft (164);
    for (int source = 0; source < 2; ++source)
    {
        auto mod = modGroup.removeFromLeft (82);
        auto modLabel = mod.removeFromTop (10);
        modLabel.setWidth (60);
        modLabels[source].setBounds (modLabel);
        modSliders[source].setBounds (mod);
    }
    bottom.removeFromLeft (6);
    statusLabel.setBounds (bottom);
    patternList.setBounds (patternSelector.getX(), patternSelector.getBottom() + 2, 330,
                           std::min (320, getHeight() - patternSelector.getBottom() - 18));
}

void StepShaperEditor::timerCallback()
{
    model.synchroniseRealtimeParameters();
    refreshFromModel();
    const auto lane = model.snapshot()->selectedLane;
    if (engine != nullptr)
    {
        patternEditor.setPlayhead (engine->phase (lane));
        patternSelector.setPending (engine->pendingPattern (lane));
    }
}

void StepShaperEditor::refreshFromModel (bool force)
{
    const auto state = model.snapshot();
    if (! force && state->revision == displayedRevision) return;
    displayedRevision = state->revision;
    refreshing = true;
    const auto lane = state->selectedLane;
    const auto& l = state->lanes[lane];
    for (int i = 0; i < maxLanes; ++i)
    {
        laneButtons[i].setVisible (i < state->activeLaneCount);
        laneButtons[i].setToggleState (i == lane, juce::dontSendNotification);
    }
    addLaneButton.setVisible (state->activeLaneCount < maxLanes);
    patternEditor.setLane (lane);
    patternEditor.setGrid (state->editorGridX, state->editorGridY);
    patternEditor.setDrawMode (state->editorDrawMode);
    gridXControl.setValue (state->editorGridX);
    gridYControl.setValue (state->editorGridY);
    drawModeBox.setSelectedItemIndex (static_cast<int> (state->editorDrawMode), juce::dontSendNotification);
    std::vector<int> activePatternSlots;
    for (int slot = 0; slot < patternsPerLane; ++slot)
        if (patternBank (l).occupied[slot]) activePatternSlots.push_back (slot);
    patternSelector.setSlots (std::move (activePatternSlots));
    patternSelector.setValue (l.selectedPattern);
    patternSelector.setPatternName (patternBank (l).names[l.selectedPattern]);
    patternList.setLane (lane);
    configureRateControl (l.sync);
    speedSlider.setValue (l.sync ? static_cast<double> (l.division) : static_cast<double> (l.speedHz),
                          juce::dontSendNotification);
    syncButton.setToggleState (l.sync, juce::dontSendNotification);
    midiTriggerButton.setToggleState (l.midiTrigger, juce::dontSendNotification);
    legatoButton.setToggleState (l.legato, juce::dontSendNotification);
    legatoButton.setEnabled (l.midiTrigger);
    positionSyncButton.setToggleState (l.positionSync, juce::dontSendNotification);
    positionSyncButton.setEnabled (! l.midiTrigger);
    baseValueSlider.setValue (l.baseValue, juce::dontSendNotification);
    amountModeButton.setToggleState (l.amountBipolar, juce::dontSendNotification);
    patternMixSlider.setValue (l.patternMix, juce::dontSendNotification);
    modSliders[0].setValue (l.mod1, juce::dontSendNotification);
    modSliders[1].setValue (l.mod2, juce::dontSendNotification);
    changeModeBox.setSelectedItemIndex (static_cast<int> (l.changeMode), juce::dontSendNotification);
    undoButton.setEnabled (model.canUndo());
    redoButton.setEnabled (model.canRedo());
    deletePatternButton.setEnabled (std::count (patternBank (l).occupied.begin(), patternBank (l).occupied.end(), true) > 1);
    newPatternButton.setEnabled (std::count (patternBank (l).occupied.begin(), patternBank (l).occupied.end(), true) < patternsPerLane);
    refreshing = false;
    resized();
    repaint();
}

void StepShaperEditor::configureRateControl (bool synced)
{
    speedLabel.setText ("SPEED", juce::dontSendNotification);
    if (synced)
    {
        speedSlider.setRange (0.0, 26.0, 1.0);
        speedSlider.setSkewFactor (1.0);
        speedSlider.setTextValueSuffix ({});
        speedSlider.textFromValueFunction = [] (double value)
        {
            return juce::String (divisionName (juce::roundToInt (value)));
        };
        speedSlider.valueFromTextFunction = [] (const juce::String& value)
        {
            for (int division = 0; division < 27; ++division)
                if (value.trim().equalsIgnoreCase (divisionName (division))) return static_cast<double> (division);
            return static_cast<double> (std::clamp (value.getIntValue(), 0, 26));
        };
    }
    else
    {
        speedSlider.setRange (0.01, 20.0, 0.001);
        speedSlider.setSkewFactorFromMidPoint (0.75);
        speedSlider.setTextValueSuffix ({});
        speedSlider.textFromValueFunction = [] (double value)
        {
            return juce::String (value, value < 10.0 ? 2 : 1) + " Hz";
        };
        speedSlider.valueFromTextFunction = [] (const juce::String& value)
        {
            return std::clamp (value.retainCharacters ("0123456789.-").getDoubleValue(), 0.01, 20.0);
        };
    }
}

void StepShaperEditor::notifyParameter (int lane, Parameter parameter)
{
    if (onParameterChanged) onParameterChanged (lane, parameter);
}

void StepShaperEditor::notifyAllLaneParameters (int lane)
{
    for (int i = 0; i < parameterKinds; ++i)
        notifyParameter (lane, static_cast<Parameter> (i));
}
} // namespace stepshaper
