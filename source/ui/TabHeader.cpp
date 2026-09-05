#include "TabHeader.h"

#include "Fonts.h"
#include "Theme.h"

using namespace Celine;

namespace
{
    /** The rounded top corners the mockup gives a tab. Only the outer two are rounded
        -- the edge where two tabs meet is square, or the pair reads as two separate
        pills with a gap rather than as a bar. */
    constexpr float tabCorner = 10.0f;
}

//==============================================================================
TabHeader::TabHeader (juce::StringArray tabNames)
    : names (std::move (tabNames))
{
    setWantsKeyboardFocus (false);
}

void TabHeader::setSelected (int index, juce::NotificationType notification)
{
    const auto clamped = juce::jlimit (0, juce::jmax (0, names.size() - 1), index);

    if (clamped == selected)
        return;

    selected = clamped;
    repaint();

    if (notification != juce::dontSendNotification && onSelectionChanged != nullptr)
        onSelectionChanged (selected);
}

void TabHeader::setBottomCornersRounded (bool shouldRound)
{
    if (roundBottomCorners == shouldRound)
        return;

    roundBottomCorners = shouldRound;
    repaint();
}

//==============================================================================
juce::Rectangle<float> TabHeader::boundsFor (int index) const
{
    if (names.isEmpty())
        return {};

    const auto width = (float) getWidth() / (float) names.size();

    return { (float) index * width, 0.0f, width, (float) getHeight() };
}

int TabHeader::indexAt (juce::Point<float> position) const
{
    for (int i = 0; i < names.size(); ++i)
        if (boundsFor (i).contains (position))
            return i;

    return -1;
}

//==============================================================================
void TabHeader::paint (juce::Graphics& g)
{
    for (int i = 0; i < names.size(); ++i)
    {
        const auto area = boundsFor (i);
        const auto isSelected = i == selected;

        auto fill = isSelected ? Theme::tabActive() : Theme::tabInactive();

        if (i == hovered && ! isSelected)
            fill = fill.overlaidWith (Theme::text().withAlpha (0.06f));

        // Rounded at the outer ends and square where the two tabs meet, so the bar
        // reads as one shape rather than as two buttons and the seam stays a seam.
        // Whether the bottom of those ends is rounded too is the owner's to say.
        const auto first = i == 0;
        const auto last = i == names.size() - 1;

        juce::Path path;
        path.addRoundedRectangle (area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                                  tabCorner, tabCorner,
                                  first, last,                                   // top
                                  first && roundBottomCorners,                   // bottom left
                                  last && roundBottomCorners);                   // bottom right

        g.setColour (fill);
        g.fillPath (path);

        g.setColour (isSelected ? Theme::text() : Theme::textDim());
        g.setFont (Fonts::logo (juce::jmin (24.0f, area.getHeight() * 0.36f)));
        g.drawText (names[i], area.toNearestInt(), juce::Justification::centred, false);
    }
}

//==============================================================================
void TabHeader::mouseDown (const juce::MouseEvent& event)
{
    const auto index = indexAt (event.position);

    if (index >= 0)
        setSelected (index);
}

void TabHeader::mouseMove (const juce::MouseEvent& event)
{
    const auto index = indexAt (event.position);

    if (index == hovered)
        return;

    hovered = index;
    repaint();
}

void TabHeader::mouseExit (const juce::MouseEvent&)
{
    if (hovered < 0)
        return;

    hovered = -1;
    repaint();
}
