#include "CutRangeSlider.h"

#include "../Parameters.h"
#include "Fonts.h"
#include "PlotGeometry.h"
#include "Theme.h"

using namespace Celine;

namespace
{
    /** How close the pointer has to get to take hold of a handle. Generous, because
        the handle is fourteen pixels of a strip that is a quarter of the window. */
    constexpr float grabReach = 12.0f;

    /** How much of the pointer's movement reaches the handle while the fine modifier
        is held. A quarter, which turns the strip's width into four strips' worth of
        resolution without the drag ever losing contact with the pointer. */
    constexpr float fineDragScale = 0.25f;
}

//==============================================================================
CutRangeSlider::CutRangeSlider()
    : accent (Theme::accent())
{
    setWantsKeyboardFocus (false);
    setTooltip ("Low and high cut. Double-click a handle to switch that cut off.");
}

void CutRangeSlider::setAccentColour (juce::Colour colour)
{
    if (accent == colour)
        return;

    accent = colour;
    repaint();
}

void CutRangeSlider::setRange (float newLow, float newHigh)
{
    if (juce::approximatelyEqual (newLow, lowHz) && juce::approximatelyEqual (newHigh, highHz))
        return;

    lowHz = newLow;
    highHz = newHigh;
    repaint();
}

//==============================================================================
juce::Rectangle<float> CutRangeSlider::trackArea() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (labelWidth)
        .withTrimmedRight (labelWidth)
        .reduced (handleWidth * 0.5f, 0.0f);
}

float CutRangeSlider::freqToX (float hz) const
{
    const PlotGeometry plot { trackArea() };
    return plot.freqToX (hz);
}

float CutRangeSlider::xToFreq (float x) const
{
    const PlotGeometry plot { trackArea() };
    return plot.xToFreq (x);
}

juce::Rectangle<float> CutRangeSlider::handleBounds (Handle handle) const
{
    const auto track = trackArea();
    const auto x = freqToX (handle == Handle::low ? lowHz : highHz);

    // Taller than the groove, as AURA's thumb is: a handle the same height as the
    // channel it runs in is a hard thing to find with a pointer.
    return juce::Rectangle<float> (handleWidth, track.getHeight() * handleHeightProportion)
        .withCentre ({ x, track.getCentreY() });
}

std::optional<CutRangeSlider::Handle> CutRangeSlider::handleAt (juce::Point<float> position) const
{
    const auto lowX = freqToX (lowHz);
    const auto highX = freqToX (highHz);

    const auto toLow = std::abs (position.x - lowX);
    const auto toHigh = std::abs (position.x - highX);

    if (juce::jmin (toLow, toHigh) > grabReach)
        return {};

    // With both cuts switched off their handles sit at opposite ends, so this only
    // matters when they have been dragged together -- and then the side of the pair
    // the pointer is on says which of them it means, where "nearest" would be a coin
    // toss that made one of the two unreachable.
    if (juce::approximatelyEqual (toLow, toHigh))
        return position.x < lowX ? Handle::low : Handle::high;

    return toLow < toHigh ? Handle::low : Handle::high;
}

//==============================================================================
void CutRangeSlider::paint (juce::Graphics& g)
{
    const auto track = trackArea();
    const auto groove = track.withSizeKeepingCentre (track.getWidth(), trackHeight);
    const auto radius = trackHeight * 0.5f;

    // Groove, outlined: a channel cut into the surface rather than a gap left in it,
    // which is how every other slider in the house is drawn.
    g.setColour (Theme::background());
    g.fillRoundedRectangle (groove, radius);
    g.setColour (Theme::line().withAlpha (0.25f));
    g.drawRoundedRectangle (groove, radius, Theme::borderWidth);

    // The span the cabinet is allowed to keep, in the slot's own colour.
    const auto lowX = juce::jlimit (groove.getX(), groove.getRight(), freqToX (lowHz));
    const auto highX = juce::jlimit (groove.getX(), groove.getRight(), freqToX (highHz));

    if (highX > lowX + 1.0f)
    {
        g.setColour (accent);
        g.fillRoundedRectangle (groove.withLeft (lowX).withRight (highX), radius);
    }

    // The two corner frequencies, centred in the space kept for them at each end.
    //
    // Centred rather than pushed outwards, which is what they were. The bar reaches
    // nearer the panel edge than the rows above it do, so a label justified to that
    // edge sat almost against it -- half the margin every other thing on the strip
    // keeps, and reading as though it had slipped off. Centring puts the air on both
    // sides of the word instead of all of it on one.
    g.setColour (Theme::textDim());
    g.setFont (Fonts::light (10.5f));

    g.drawText (Parameters::frequencyText (lowHz),
                getLocalBounds().withWidth ((int) labelWidth),
                juce::Justification::centred, false);

    g.drawText (Parameters::frequencyText (highHz),
                getLocalBounds().withTrimmedLeft (getWidth() - (int) labelWidth),
                juce::Justification::centred, false);

    // The handles stay white, held or not.
    //
    // They used to take a wash of the accent on hover and more of it while dragged,
    // which is how a button says it has been pressed -- and made a thumb halfway
    // through a move look like a different control from the one that was grabbed. What
    // says a handle is live is that it is moving with the pointer, and the cursor
    // changed shape before it was picked up.
    for (const auto handle : { Handle::low, Handle::high })
    {
        const auto bounds = handleBounds (handle);

        g.setColour (Theme::panel());
        g.fillRoundedRectangle (bounds, bounds.getWidth() * 0.5f);
    }
}

//==============================================================================
void CutRangeSlider::mouseMove (const juce::MouseEvent& event)
{
    const auto over = handleAt (event.position);

    if (over == hovered)
        return;

    hovered = over;
    setMouseCursor (over.has_value() ? juce::MouseCursor::LeftRightResizeCursor
                                     : juce::MouseCursor::NormalCursor);
    repaint();
}

void CutRangeSlider::mouseExit (const juce::MouseEvent&)
{
    if (! hovered.has_value())
        return;

    hovered.reset();
    repaint();
}

void CutRangeSlider::mouseDown (const juce::MouseEvent& event)
{
    dragging = handleAt (event.position);

    if (! dragging.has_value())
        return;

    dragStartX = event.position.x;
    dragStartHz = *dragging == Handle::low ? lowHz : highHz;

    if (onGesture != nullptr)
        onGesture (*dragging, true);

    repaint();
}

void CutRangeSlider::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging.has_value() || onDragged == nullptr)
        return;

    // Held down, a quarter of the pointer's travel reaches the handle. Applied to the
    // distance from where the drag began rather than to each mouse movement in turn,
    // so pressing or releasing the key mid-drag re-scales the whole gesture smoothly
    // instead of leaving the handle wherever the previous scaling had walked it to.
    const auto travel = (event.position.x - dragStartX)
                      * (event.mods.isAltDown() ? fineDragScale : 1.0f);

    auto frequency = xToFreq (freqToX (dragStartHz) + travel);

    // Neither handle may pass the other. Clamped here rather than left to the
    // parameters, because the display leads the parameter during a drag: a handle that
    // was allowed to cross would be drawn crossed for a frame and then snapped back by
    // the next poll, which reads as the control fighting the pointer.
    if (*dragging == Handle::low)
        frequency = juce::jmin (frequency, highHz);
    else
        frequency = juce::jmax (frequency, lowHz);

    onDragged (*dragging, frequency);
}

void CutRangeSlider::mouseUp (const juce::MouseEvent&)
{
    if (! dragging.has_value())
        return;

    if (onGesture != nullptr)
        onGesture (*dragging, false);

    dragging.reset();
    repaint();
}

void CutRangeSlider::mouseDoubleClick (const juce::MouseEvent& event)
{
    // Back to the end of its travel, which is where a cut is switched off. The
    // gesture is opened and closed around it so a host records the jump as one edit.
    const auto handle = handleAt (event.position);

    if (! handle.has_value() || onDragged == nullptr)
        return;

    if (onGesture != nullptr)
        onGesture (*handle, true);

    onDragged (*handle, *handle == Handle::low ? PlotGeometry::minFreq : PlotGeometry::maxFreq);

    if (onGesture != nullptr)
        onGesture (*handle, false);
}
