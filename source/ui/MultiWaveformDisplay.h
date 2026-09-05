#pragma once

#include "../dsp/ImpulseResponse.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace Celine
{
    //==========================================================================
    /**
        The four responses drawn over one another against one time axis, which is what
        makes aligning them possible.

        This is the alignment tool. Four cabinets captured with the microphones at
        different distances arrive at different times, and blending them without
        lining them up first is how a mix loses its low end to cancellation nobody can
        see. Drawn on a shared axis, the disagreement is a picture: the transients do
        not stack, and the Align control moves one until they do.

        Signed peaks, not absolute ones. The polarity of a capture matters as much as
        its timing -- it is what the Φ button is for -- and a waveform drawn from
        absolute peaks looks identical either way up.

        Zoom is not a convenience. At full width the first few milliseconds, which is
        where alignment is decided, are a couple of pixels; the control that matters
        most is the one you cannot otherwise reach.
    */
    class MultiWaveformDisplay : public juce::Component
    {
    public:
        MultiWaveformDisplay();

        static constexpr int numTraces = 4;

        /** The two windows, both fixed.

            Fixed rather than fitted to whatever is loaded, and that is the point: four
            cabinets are being compared against each other, and an axis that rescaled
            itself when one of them was swapped would change what "lined up" looks like
            without anything about the alignment having moved. A millisecond is a
            millisecond wide, always, in both views. */
        static constexpr double windowSeconds = 0.020;
        static constexpr double zoomedSeconds = 0.008;

        struct Trace
        {
            std::vector<ImpulseResponse::Column> columns;

            /** How much time the columns cover. Not the same for every trace -- a
                shorter capture covers less of the axis -- which is exactly the
                difference the display exists to show. */
            double seconds = 0.0;

            /** Where on the axis this trace begins, in seconds.

                Alignment is a delay line on the audio thread rather than a shift baked
                into the response, so a cabinet pushed back is not a different set of
                samples -- it is the same samples arriving later. Drawn without this the
                four would sit on top of each other however far apart they had been
                moved, which would take the one thing this view exists for and leave the
                control apparently doing nothing. */
            double startSeconds = 0.0;

            juce::Colour colour;
            bool inverted = false;

            bool visible = false;
        };

        void setTrace (int index, Trace);
        void clearTrace (int index);

        /** The span of the axis, in seconds. Everything is drawn against this, so a
            trace shorter than it simply stops part way across. */
        void setTimeSpan (double seconds);

        void setZoomed (bool);
        bool isZoomed() const noexcept { return zoomed; }

        /** How many columns a summary should be made of, which is the drawing area's
            width -- one peak per pixel and no more, since a second peak in the same
            column is a peak nobody can see. */
        int getPlotWidth() const noexcept;

    private:
        void paint (juce::Graphics&) override;

        juce::Rectangle<float> plotArea() const;
        void drawTimeAxis (juce::Graphics&) const;
        void drawTrace (juce::Graphics&, int index, float scale) const;

        std::array<Trace, (size_t) numTraces> traces;

        double timeSpan = 0.2;
        bool zoomed = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiWaveformDisplay)
    };
} // namespace Celine
