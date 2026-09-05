#pragma once

#include "Fonts.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    //==========================================================================
    /**
        A pill carrying one letter, lit when it is on: solo, mute, polarity.

        Neither of the two things this kit already has would do. `IconButton` wants a
        drawable, and S, M and Φ are letters -- drawing them as artwork would mean
        three SVGs that have to be redrawn to change a typeface. The look and feel's
        pill switch is a travelling dot in a track, which says "this setting is one of
        two" where these three say "this is in force"; a row of three sliding dots
        under a Load button would read as settings rather than as states.

        The lit colour is given rather than taken from the palette's accent, because
        these three mean different things and the design colours them accordingly --
        and because a slot's own colour is deliberately *not* used here: solo means the
        same thing on all four cabinets.
    */
    class LetterToggleButton : public juce::Button
    {
    public:
        LetterToggleButton (const juce::String& name, const juce::String& glyph,
                            juce::Colour litColour)
            : juce::Button (name), letter (glyph), lit (litColour)
        {
            setTooltip (name);
            setClickingTogglesState (true);
            setWantsKeyboardFocus (false);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto bounds = getLocalBounds().toFloat().reduced (Theme::borderWidth * 0.5f);

            // The house corner, not a full pill. These sit in a row with Load and the
            // discard button, and a lozenge among rounded rectangles reads as a
            // different kind of control rather than as the same one wearing a letter.
            const auto radius = juce::jmin (Theme::cornerRadius,
                                            juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f);
            const auto on = getToggleState();

            auto fill = on ? lit : Theme::surface();

            if (down || highlighted)
                fill = fill.overlaidWith (Theme::text().withAlpha (down ? 0.16f : 0.08f));

            g.setColour (fill);
            g.fillRoundedRectangle (bounds, radius);

            // No rule around it. An unlit pill is told from its ground by being a
            // lighter fill, which is the same way every other button here does it.

            // Dark ink on a lit pill, light on an unlit one. The three lit colours are
            // pale by design, and text() on any of them is unreadable.
            g.setColour (on ? Theme::onPill() : Theme::textDim());
            g.setFont (Fonts::bold (juce::jmin (13.0f, bounds.getHeight() * 0.55f)));

            // Lifted by a hair, for the same reason the look and feel lifts its button
            // text: Jura centres on its cap height, and the eye centres lower.
            g.drawText (letter, getLocalBounds().translated (0, -1), juce::Justification::centred, false);
        }

    private:
        juce::String letter;
        juce::Colour lit;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LetterToggleButton)
    };
} // namespace Celine
