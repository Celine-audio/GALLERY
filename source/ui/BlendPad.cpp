#include "BlendPad.h"

#include "Fonts.h"
#include "Theme.h"

using namespace Celine;

namespace
{
    /** SPACE's EQ handle, to the pixel: the same radius, the same growth when it is
        live, the same ground-filled disc with an accent ring round it. These plugins go
        on the same signal one after the other, and a handle you drag should not be a
        different object in each. */
    constexpr float handleRadius = 7.0f;
    constexpr float handleReach = 14.0f;

    /** The one setting on each axis the handle drops onto: the centre.

        The two centre lines are the only places on the pad with names -- an even blend
        of a pair, or of all four -- and they are the ones you can otherwise spend a
        while creeping up on without quite landing. The corners are deliberately *not*
        snapped: a corner is a judgement about how far to push a cabinet, and there is
        nothing exact about it to land on.

        Holding a modifier turns it off, the way it does for a knob. */
    constexpr float snapPoints[] = { 0.0f };

    /** How near, in pad coordinates. About a twentieth of the travel, which at the
        sizes this window opens at is six or seven pixels -- close enough that aiming
        at the centre lands on it, far enough that it does not fight a deliberate
        setting just beside one. */
    constexpr float snapReach = 0.05f;

    float snapped (float value) noexcept
    {
        for (const auto point : snapPoints)
            if (std::abs (value - point) < snapReach)
                return point;

        return value;
    }

    /** How dark a corner's wash goes when its cabinet is getting nothing. Not to zero:
        with no marks in the corners the wash is the only thing saying which cabinet is
        where, so it has to be readable even at rest. */
    constexpr float dimmest = 0.10f;
}

//==============================================================================
BlendPad::BlendPad()
{
    setTooltip ("Blend the four cabinets."
                "Double-click to recentre."
                "Hold Cmd to ignore the centre lines.");
}

void BlendPad::setPosition (float x, float y)
{
    const auto newX = juce::jlimit (-1.0f, 1.0f, x);
    const auto newY = juce::jlimit (-1.0f, 1.0f, y);

    if (juce::approximatelyEqual (newX, blendX) && juce::approximatelyEqual (newY, blendY))
        return;

    blendX = newX;
    blendY = newY;
    repaint();
}

void BlendPad::setLoaded (int slot, bool isLoaded)
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return;

    if (loaded[(size_t) slot] == isLoaded)
        return;

    loaded[(size_t) slot] = isLoaded;
    repaint();
}

//==============================================================================
juce::Rectangle<float> BlendPad::frame() const
{
    // Square, and the whole component. The layout hands this a square so that it lines
    // up with the library above it -- but a control whose arithmetic assumes one is a
    // control that draws an ellipse the day somebody gives it a rectangle.
    const auto bounds = getLocalBounds().toFloat();
    const auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());

    return bounds.withSizeKeepingCentre (side, side);
}

juce::Rectangle<float> BlendPad::field() const
{
    return frame().reduced (handleRadius + 2.0f);
}

juce::Point<float> BlendPad::positionToPixels() const
{
    const auto square = field();

    return { square.getCentreX() + blendX * square.getWidth() * 0.5f,
             square.getCentreY() - blendY * square.getHeight() * 0.5f };
}

void BlendPad::dragTo (juce::Point<float> pixels, bool snapping)
{
    const auto square = field();

    if (square.getWidth() <= 0.0f)
        return;

    auto x = juce::jlimit (-1.0f, 1.0f,
                           (pixels.x - square.getCentreX()) / (square.getWidth() * 0.5f));
    auto y = juce::jlimit (-1.0f, 1.0f,
                           (square.getCentreY() - pixels.y) / (square.getHeight() * 0.5f));

    // Each axis on its own, so the centre line can be caught without the other axis
    // being dragged onto it as well.
    if (snapping)
    {
        x = snapped (x);
        y = snapped (y);
    }

    setPosition (x, y);

    if (onDragged != nullptr)
        onDragged (x, y);
}

void BlendPad::setHovered (bool nowHovered)
{
    if (hovered == nowHovered)
        return;

    hovered = nowHovered;
    repaint();
}

//==============================================================================
void BlendPad::paint (juce::Graphics& g)
{
    const auto square = frame();

    // The graph's own ground, rounded like everything else in the window, so the pad
    // reads as another window onto the same thing rather than a control panel beside it.
    g.setColour (Theme::consoleBackground());
    g.fillRoundedRectangle (square, Theme::cornerRadius);

    // Clipped to the rounding, so the four washes stop where the frame does instead of
    // filling its corners back in square.
    {
        juce::Graphics::ScopedSaveState saved (g);

        juce::Path rounded;
        rounded.addRoundedRectangle (square, Theme::cornerRadius);
        g.reduceClipRegion (rounded);

        drawWash (g, square);
    }

    drawAxes (g, square);
    drawHandle (g);
}

void BlendPad::drawWash (juce::Graphics& g, juce::Rectangle<float> square) const
{
    const auto weights = Parameters::blendWeights (blendX, blendY);

    // Slot 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right -- the order
    // blendWeights uses.
    const juce::Point<float> corners[] = {
        { square.getX(),     square.getY()      },
        { square.getRight(), square.getY()      },
        { square.getX(),     square.getBottom() },
        { square.getRight(), square.getBottom() }
    };

    const auto reach = square.getWidth() * 0.9f;

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        const auto index = (size_t) slot;

        if (! loaded[index])
            continue;

        const auto colour = Theme::irSlot (slot);
        const auto centre = corners[index];
        const auto share = juce::jlimit (0.0f, 1.0f, weights[index]);

        // The blend drawn as itself: the pad's interior is the four colours mixed in
        // the proportions being heard, so where the handle is and what it is doing are
        // one picture rather than a position to be interpreted. With the corner marks
        // gone this is also the only thing saying which cabinet is where, which is why
        // it never fades out entirely.
        juce::ColourGradient wash (colour.withAlpha (dimmest + share * 0.55f), centre,
                                   colour.withAlpha (0.0f),
                                   centre + juce::Point<float> (
                                       centre.x < square.getCentreX() ? reach : -reach,
                                       centre.y < square.getCentreY() ? reach : -reach),
                                   true);

        g.setGradientFill (wash);
        g.fillRect (square);
    }
}

void BlendPad::drawAxes (juce::Graphics& g, juce::Rectangle<float> square) const
{
    // The two centre lines, which are what make "in the middle" a place you can aim at
    // rather than a guess. Inset from the rounding so neither runs into a corner.
    const auto inset = Theme::cornerRadius;

    g.setColour (Theme::grid().withAlpha (0.4f));
    g.drawHorizontalLine ((int) square.getCentreY(),
                          square.getX() + inset, square.getRight() - inset);
    g.drawVerticalLine ((int) square.getCentreX(),
                        square.getY() + inset, square.getBottom() - inset);

    g.setColour (Theme::line().withAlpha (0.18f));
    g.drawRoundedRectangle (square.reduced (0.5f), Theme::cornerRadius, 1.0f);
}

void BlendPad::drawHandle (juce::Graphics& g) const
{
    const auto centre = positionToPixels();
    const auto live = dragging || hovered;
    const auto radius = live ? handleRadius + 1.5f : handleRadius;
    const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    g.setColour (Theme::background());
    g.fillEllipse (bounds);

    g.setColour (live ? Theme::blendHandle().brighter (0.25f) : Theme::blendHandle());
    g.drawEllipse (bounds, 2.0f);
}

//==============================================================================
void BlendPad::mouseMove (const juce::MouseEvent& event)
{
    setHovered (positionToPixels().getDistanceFrom (event.position) < handleReach);
}

void BlendPad::mouseExit (const juce::MouseEvent&)
{
    setHovered (false);
}

void BlendPad::mouseDown (const juce::MouseEvent& event)
{
    dragging = true;

    if (onGesture != nullptr)
        onGesture (true);

    dragTo (event.position, ! event.mods.isCommandDown());
    repaint();
}

void BlendPad::mouseDrag (const juce::MouseEvent& event)
{
    // The modifier a knob uses for fine adjustment, doing the same job: it is the way
    // out of the snap when the setting you want is just beside one.
    dragTo (event.position, ! event.mods.isCommandDown());
}

void BlendPad::mouseUp (const juce::MouseEvent& event)
{
    dragging = false;
    hovered = positionToPixels().getDistanceFrom (event.position) < handleReach;

    if (onGesture != nullptr)
        onGesture (false);

    repaint();
}

void BlendPad::mouseDoubleClick (const juce::MouseEvent&)
{
    // Back to all four in equal measure, which is the setting somebody who has just
    // loaded four captures wants before they have decided anything.
    if (onGesture != nullptr)
        onGesture (true);

    setPosition (0.0f, 0.0f);

    if (onDragged != nullptr)
        onDragged (0.0f, 0.0f);

    if (onGesture != nullptr)
        onGesture (false);
}
