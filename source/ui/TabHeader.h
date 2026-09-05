#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace Celine
{
    //==========================================================================
    /**
        The two-tab bar the design uses twice: over the graph, and over the library.

        Tabs rather than buttons, and drawn as tabs -- the one in front is brown and
        square-cornered where it meets its panel, the one behind is the toolbar's
        aubergine. That is what makes the pair read as two sheets stacked rather than
        as two things to press, which matters because pressing one does not do
        anything except decide what is underneath.

        Not juce::TabbedComponent, which owns the content it switches between and
        draws a bar of its own; here the content is a sibling that the owner positions,
        and what is wanted is only the bar.
    */
    class TabHeader : public juce::Component
    {
    public:
        explicit TabHeader (juce::StringArray tabNames);

        int getSelected() const noexcept { return selected; }
        void setSelected (int index, juce::NotificationType = juce::sendNotification);

        /** Whether the bar's outer ends are rounded at the bottom as well as the top.

            Off by default, which is the shape a tab bar sitting directly on its panel
            wants: square at the bottom, so the two read as one piece. The graph's bar
            asks for it because it is read as a bar in its own right. */
        void setBottomCornersRounded (bool shouldRound);

        std::function<void (int)> onSelectionChanged;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> boundsFor (int index) const;
        int indexAt (juce::Point<float>) const;

        juce::StringArray names;
        int selected = 0;
        int hovered = -1;
        bool roundBottomCorners = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabHeader)
    };
} // namespace Celine
