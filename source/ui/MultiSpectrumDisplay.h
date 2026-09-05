#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

namespace Celine
{
    //==========================================================================
    /**
        Four cabinets' frequency responses on one graph, each in its own colour.

        The point of drawing them together rather than one at a time: what a cabinet
        blend sounds like is decided by where the four curves cross, reinforce and
        cancel, and none of that is visible in four separate pictures.

        The display is told what to draw rather than working it out. Each trace arrives
        as decibels already composed -- the measured response plus what its cuts are
        doing -- because the measurement is rebuilt only when a response changes while
        the cuts move continuously, and a display that recomputed both every frame would
        be doing the expensive one sixty times a second for nothing.
    */
    class MultiSpectrumDisplay : public juce::Component
    {
    public:
        MultiSpectrumDisplay();

        static constexpr int numTraces = 4;

        /** One slot's curve, in decibels, evenly spaced across the axis.

            Evenly spaced is not an approximation. The measurement grid is logarithmic
            over exactly the range this axis covers, and the axis is logarithmic in
            frequency -- so equal steps through the array are equal steps across the
            plot, and neither side needs to know the other's frequencies. Both ends of
            that arrangement are pinned by a test. */
        void setTrace (int index, const std::vector<float>& decibels, juce::Colour, bool visible);

        /** Clears a trace, which is what an empty slot draws. */
        void clearTrace (int index);

        /** What is actually coming out of the plugin, drawn as a filled backdrop
            behind the four responses. Empty switches it off.

            Behind and in grey, not as a fifth colour: it is the ground the four are
            read against -- what the cabinets are doing *to something* -- and a fifth
            trace competing with them would cost more than it told. */
        void setOutputSpectrum (const std::vector<float>& decibels);

        /** The four cabinets as the one curve they add up to -- what is actually being
            heard, rather than what each of them is doing. Empty switches it off.

            Drawn in the interface's own ink rather than in a fifth hue: it is not a
            cabinet, and giving it a colour of its own would put it in the same set as
            the four it is a sum of. */
        void setMixTrace (const std::vector<float>& decibels);

    private:
        void paint (juce::Graphics&) override;

        struct Trace
        {
            std::vector<float> decibels;
            juce::Colour colour;
            bool visible = false;
        };

        juce::Rectangle<float> plotArea() const;
        void drawGrid (juce::Graphics&) const;
        void drawTrace (juce::Graphics&, const Trace&, float thickness, float fillAlpha) const;

        void drawOutput (juce::Graphics&) const;

        std::array<Trace, (size_t) numTraces> traces;
        Trace mix;

        std::vector<float> output;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiSpectrumDisplay)
    };
} // namespace Celine
