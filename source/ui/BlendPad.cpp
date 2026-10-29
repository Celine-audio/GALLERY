#include "BlendPad.h"

#include <CelineUI/Fonts.h>
#include <CelineUI/Theme.h>

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
    setRouting (Parameters::Routing::parallel);
}

void BlendPad::setRouting (Parameters::Routing newRouting)
{
    routing = newRouting;

    switch (routing)
    {
        case Parameters::Routing::twoChains:
            setTooltip ("Blend the two chains: 1 into 2 at the top, 3 into 4 at the bottom. "
                        "Double-click to recentre. Hold Cmd to ignore the centre line.");
            break;

        case Parameters::Routing::series:
            setTooltip ("In series the four are one path, so there is nothing to blend.");
            dragging = false;
            break;

        case Parameters::Routing::parallel:
            setTooltip ("Blend the four cabinets. Double-click to recentre. "
                        "Hold Cmd to ignore the centre lines.");
            break;
    }

    // A hover left over from a pad that was movable would draw a live handle on one
    // that is not.
    if (! movable())
        hovered = false;

    repaint();
}

void BlendPad::setAttachedBelowHeader (bool isAttached)
{
    attached = isAttached;
    repaint();
}

juce::Point<float> BlendPad::shown() const noexcept
{
    switch (routing)
    {
        case Parameters::Routing::twoChains: return { 0.0f, blendY };
        case Parameters::Routing::series:    return {};
        case Parameters::Routing::parallel:  break;
    }

    return { blendX, blendY };
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

    const auto dot = shown();

    return { square.getCentreX() + dot.x * square.getWidth() * 0.5f,
             square.getCentreY() - dot.y * square.getHeight() * 0.5f };
}

void BlendPad::dragTo (juce::Point<float> pixels, bool snapping)
{
    const auto square = field();

    if (square.getWidth() <= 0.0f)
        return;

    // With two chains the width is left exactly where it was. It means nothing to the
    // sound now, but it will again in parallel, and a blend somebody set there should
    // be waiting for them when they go back.
    auto x = routing == Parameters::Routing::parallel
                 ? juce::jlimit (-1.0f, 1.0f, (pixels.x - square.getCentreX()) / (square.getWidth() * 0.5f))
                 : blendX;
    auto y = juce::jlimit (-1.0f, 1.0f,
                           (square.getCentreY() - pixels.y) / (square.getHeight() * 0.5f));

    // Each axis on its own, so the centre line can be caught without the other axis
    // being dragged onto it as well.
    if (snapping)
    {
        if (routing == Parameters::Routing::parallel)
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
juce::Path BlendPad::outline (juce::Rectangle<float> square) const
{
    juce::Path rounded;
    rounded.addRoundedRectangle (square.getX(), square.getY(), square.getWidth(), square.getHeight(),
                                 Theme::cornerRadius, Theme::cornerRadius,
                                 ! attached, ! attached, true, true);
    return rounded;
}

void BlendPad::paint (juce::Graphics& g)
{
    const auto square = frame();
    const auto rounded = outline (square);

    // The graph's own ground, rounded like everything else in the window, so the pad
    // reads as another window onto the same thing rather than a control panel beside it.
    g.setColour (Theme::consoleBackground());
    g.fillPath (rounded);

    // Clipped to the rounding, so the four washes stop where the frame does instead of
    // filling its corners back in square.
    {
        juce::Graphics::ScopedSaveState saved (g);
        g.reduceClipRegion (rounded);

        drawWash (g, square);
    }

    drawAxes (g, square);
    drawHandle (g);
}

void BlendPad::drawWash (juce::Graphics& g, juce::Rectangle<float> square) const
{
    // What is drawn is what is heard: the wash follows the dot where it is drawn, not
    // where it is stored.
    const auto dot = shown();
    const auto weights = Parameters::blendWeights (dot.x, dot.y);

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
        // Quieter in series: the pad is not a control there, and a wash at full
        // strength would look like one waiting to be used.
        const auto share = juce::jlimit (0.0f, 1.0f, weights[index])
                         * (routing == Parameters::Routing::series ? 0.4f : 1.0f);

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
    // The centre lines, which are what make "in the middle" a place you can aim at
    // rather than a guess. Inset from the rounding so neither runs into a corner. With
    // two chains the vertical one is also the track the dot runs along, so it stays;
    // the horizontal one marks the even blend. In series there is nowhere to aim.
    const auto inset = Theme::cornerRadius;

    g.setColour (Theme::grid().withAlpha (0.4f));

    if (movable())
    {
        g.drawHorizontalLine ((int) square.getCentreY(),
                              square.getX() + inset, square.getRight() - inset);
        g.drawVerticalLine ((int) square.getCentreX(),
                            square.getY() + inset, square.getBottom() - inset);
    }

    g.setColour (Theme::line().withAlpha (0.18f));
    g.strokePath (outline (square.reduced (0.5f)), juce::PathStrokeType (1.0f));
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
    setHovered (movable() && positionToPixels().getDistanceFrom (event.position) < handleReach);
}

void BlendPad::mouseExit (const juce::MouseEvent&)
{
    setHovered (false);
}

void BlendPad::mouseDown (const juce::MouseEvent& event)
{
    if (! movable())
        return;

    dragging = true;

    if (onGesture != nullptr)
        onGesture (true);

    dragTo (event.position, ! event.mods.isCommandDown());
    repaint();
}

void BlendPad::mouseDrag (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    // The modifier a knob uses for fine adjustment, doing the same job: it is the way
    // out of the snap when the setting you want is just beside one.
    dragTo (event.position, ! event.mods.isCommandDown());
}

void BlendPad::mouseUp (const juce::MouseEvent& event)
{
    if (! dragging)
        return;

    dragging = false;
    hovered = positionToPixels().getDistanceFrom (event.position) < handleReach;

    if (onGesture != nullptr)
        onGesture (false);

    repaint();
}

void BlendPad::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! movable())
        return;

    // Back to all four in equal measure, which is the setting somebody who has just
    // loaded four captures wants before they have decided anything. With two chains,
    // back to the two chains evenly -- and the width left where it was, for the same
    // reason a drag leaves it.
    const auto x = routing == Parameters::Routing::parallel ? 0.0f : blendX;

    if (onGesture != nullptr)
        onGesture (true);

    setPosition (x, 0.0f);

    if (onDragged != nullptr)
        onDragged (x, 0.0f);

    if (onGesture != nullptr)
        onGesture (false);
}
