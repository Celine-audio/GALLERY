#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace Celine
{
    //==========================================================================
    /**
        The band a cabinet is allowed to keep: two handles on one logarithmic
        frequency axis, with the span between them filled.

        One control rather than two, because the thing being set is a band. Two
        separate frequency knobs would let a low cut be dragged above a high cut,
        which is not a setting -- it is silence -- and nothing about a pair of knobs
        says so until you hear it.

        Logarithmic, and sharing PlotGeometry's mapping with the graph above it, so
        that a handle at 500 Hz sits under the 500 Hz gridline. Two axes claiming to
        be frequency and disagreeing about where a frequency is would be worse than
        having no graph.
    */
    class CutRangeSlider : public juce::Component,
                           public juce::SettableTooltipClient
    {
    public:
        CutRangeSlider();

        enum class Handle { low, high };

        /** The slot's own colour, worn by the filled span so the control belongs to
            the cabinet whose strip it is on. */
        void setAccentColour (juce::Colour);

        /** The frequencies the two handles sit at. */
        void setRange (float lowHz, float highHz);

        /** Called while a handle is being dragged, naming the handle and where it has
            been dragged to. One handle, not both: reporting the pair means writing to
            a parameter that has not moved, and opening a host gesture on it. */
        std::function<void (Handle, float hz)> onDragged;

        /** Bracketing a drag, so a host records one gesture rather than a stream of
            unrelated writes. */
        std::function<void (Handle, bool starting)> onGesture;

        void paint (juce::Graphics&) override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        /** The track, which is inset from the component by the room the end labels and
            the handles need. */
        juce::Rectangle<float> trackArea() const;

        float freqToX (float hz) const;
        float xToFreq (float x) const;

        juce::Rectangle<float> handleBounds (Handle) const;
        std::optional<Handle> handleAt (juce::Point<float>) const;

        float lowHz = 20.0f;
        float highHz = 20000.0f;

        juce::Colour accent;

        std::optional<Handle> dragging, hovered;

        // Where the drag started, in pixels and in hertz. A drag moves the handle by
        // how far the pointer has come rather than to where the pointer is: grabbing a
        // handle slightly off its centre would otherwise snap it under the pointer
        // before it had moved, and there would be nowhere for a fine drag to be finer
        // *than*.
        float dragStartX = 0.0f;
        float dragStartHz = 0.0f;

        /** Room at each end for the corner frequency.

            Trimmed to what the widest reading actually needs -- "20 kHz" at this size
            -- rather than the round fifty-two it was. On the one control here whose
            usefulness *is* its length, since how precisely a corner can be placed is
            how many pixels there are to place it in, every one of those given to a
            margin is one the bar does not have. */
        static constexpr float labelWidth = 42.0f;

        // AURA's linear slider exactly: a six-pixel groove and a fourteen-pixel pill
        // that stands a little proud of it. The two plugins put horizontal sliders in
        // front of the same people, and a control that is the same control should not
        // be a different thickness in each.
        static constexpr float handleWidth = 14.0f;
        static constexpr float trackHeight = 6.0f;
        static constexpr float handleHeightProportion = 0.62f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CutRangeSlider)
    };
} // namespace Celine
