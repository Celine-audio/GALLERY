#pragma once

#include "../Parameters.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace Celine
{
    //==========================================================================
    /**
        The blend: one dot on a square, and the four cabinets at its corners.

        This replaced a gain knob per strip, and the reason is what those knobs were
        actually being used for. Nobody sets a cabinet's level in isolation -- they set
        it against the other three, listening for the blend -- so four independent
        controls were four ways of asking the same question, each of which changed the
        answer to the other three. Four numbers that have to stay summing to one is not
        four controls. It is one control with four readouts.

        A dot on a square is that control. The centre is all four in equal measure, a
        corner is one cabinet alone, and everywhere in between is a blend you arrived at
        with a single movement -- which is the movement people were making anyway, in
        four steps, while the level drifted.

        The pad draws its own arithmetic: each corner brightens with the share its
        cabinet is getting, so the picture and the sound say the same thing. See
        `Parameters::blendWeights` for the law itself, which lives there rather than
        here because the audio thread and the spectrum both need it too.
    */
    class BlendPad : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        BlendPad();

        /** Where the dot sits, -1 to 1 on each axis with y positive upwards. */
        void setPosition (float x, float y);

        /** Which cabinets have anything in them. An empty slot's corner is drawn as a
            place rather than as a cabinet -- the pad still works, since the shares are
            renormalised over what is loaded, but a corner that promises a cabinet
            there is not is worse than an obviously empty one. */
        void setLoaded (int slot, bool isLoaded);

        /** Called as the dot is dragged. */
        std::function<void (float x, float y)> onDragged;

        /** Bracketing a drag, so a host records one gesture rather than a stream of
            unrelated writes. */
        std::function<void (bool starting)> onGesture;

        void paint (juce::Graphics&) override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        /** The frame, which is the whole component: the pad is square and lines up with
            the library panel above it, so there is nothing to inset. */
        juce::Rectangle<float> frame() const;

        /** The square the handle's centre travels in -- the frame pulled in by the
            handle's own radius, so a blend at a corner draws the handle whole rather
            than half outside the frame it belongs to. */
        juce::Rectangle<float> field() const;

        juce::Point<float> positionToPixels() const;
        void dragTo (juce::Point<float> pixels, bool snapping);
        void setHovered (bool);

        void drawWash (juce::Graphics&, juce::Rectangle<float>) const;
        void drawAxes (juce::Graphics&, juce::Rectangle<float>) const;
        void drawHandle (juce::Graphics&) const;

        float blendX = 0.0f, blendY = 0.0f;
        ParamID::PerSlot<bool> loaded {};

        bool dragging = false, hovered = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BlendPad)
    };
} // namespace Celine
