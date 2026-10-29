#pragma once

#include "../Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace Celine
{
    //==========================================================================
    /**
        How the four cabinets are wired, as three pictures of the wiring.

        A word for each would have been shorter to build and slower to read. "Two
        chains" says nothing about which two, and the answer -- 1 into 2, 3 into 4 -- is
        exactly the thing somebody choosing needs to know. So each choice is drawn: a
        line for each path the signal takes, left to right, with a dot on it for each
        cabinet it passes through, in that cabinet's colour. Four lines of one dot is
        parallel; one line of four is series; and the colours say which cabinet is
        where without a number on any of them.

        The header of the panel the blend pad sits in, in the band the library's title
        sits in, because the two answer one question between them: how the cabinets
        are wired, and how much of each wiring is heard.
    */
    class RoutingSelector : public juce::Component
    {
    public:
        explicit RoutingSelector (juce::RangedAudioParameter& routingParameter);
        ~RoutingSelector() override;

        /** The wiring a choice draws, as dots on lines -- one line per chain, a dot per
            cabinet, in its colour. */
        static void drawWiring (juce::Graphics&, Parameters::Routing, juce::Rectangle<float> area,
                                float alpha);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** The title is a Label, which is told its colour rather than asking -- so a
            theme change has to tell it again. */
        void lookAndFeelChanged() override;

        /** Called when the routing moves, from here or from a host -- the pad and the
            strips' connectors are drawn from it. */
        std::function<void (Parameters::Routing)> onChanged;

        Parameters::Routing getRouting() const noexcept { return current; }

    private:
        class Choice;

        void select (Parameters::Routing);

        juce::Label title;
        std::array<std::unique_ptr<Choice>, Parameters::routingNames.size()> choices;

        Parameters::Routing current = Parameters::Routing::parallel;

        // The parameter, wired both ways: a click is a gesture the host records, and a
        // host moving it moves the picture.
        std::unique_ptr<juce::ParameterAttachment> attachment;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoutingSelector)
    };
} // namespace Celine
