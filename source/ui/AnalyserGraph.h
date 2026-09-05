#pragma once

#include "MultiSpectrumDisplay.h"
#include "MultiWaveformDisplay.h"
#include "TabHeader.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace Celine
{
    //==========================================================================
    /**
        The graph, and the tabs that decide which of the two it is showing.

        Tabs rather than both at once, which is what the sibling plugin this kit came
        from does: there the two views are two ends of one signal path and you want to
        watch both. Here they are two questions about the same four responses -- how do
        they sit against each other in frequency, and how do they sit against each
        other in time -- and each wants the whole width of the window to answer it.
    */
    class AnalyserGraph : public juce::Component
    {
    public:
        AnalyserGraph();

        enum class View { spectrum, curve };

        View getView() const noexcept;

        /** Told when the tabs move, so the editor can refresh whichever view has just
            come to the front rather than keeping both fed. */
        std::function<void()> onViewChanged;

        MultiSpectrumDisplay& getSpectrum() noexcept { return spectrum; }
        MultiWaveformDisplay& getWaveform() noexcept { return waveform; }

        bool isZoomed() const noexcept { return waveform.isZoomed(); }

        /** Told when the zoom button moves, for the same reason: the summaries have to
            be made again over the new window. */
        std::function<void()> onZoomChanged;

        /** Whether the output backdrop is switched on, and a way to be told when that
            changes -- the measurement costs the audio thread something, so it is
            started and stopped rather than merely drawn or not. */
        bool isShowingOutput() const noexcept { return outputButton.getToggleState(); }
        std::function<void (bool)> onShowOutputChanged;

        /** Whether the four cabinets are drawn separately rather than as the one curve
            they add up to. Off by default: what somebody blending is listening to is
            the sum, and four curves are the answer to a different question -- which is
            why this is a button rather than the only view. */
        bool isSplit() const noexcept { return splitButton.getToggleState(); }
        std::function<void()> onSplitChanged;

        void paint (juce::Graphics&) override;
        void resized() override;

        /** The band the tabs occupy, which the design draws at the same height as the
            library's header beside it. */
        static constexpr int tabHeight = Theme::tabBarHeight;

    private:
        /** The colours these three buttons wear, re-read whenever the theme moves.

            JUCE tells a widget its colours rather than asking, so a colour taken once
            in a constructor is a snapshot -- and a snapshot does not follow a theme.
            `lookAndFeelChanged` is where the window tells every child to take them
            again; anything here that calls `setColour` belongs in this function. */
        void applyColours();
        void lookAndFeelChanged() override { applyColours(); }

        TabHeader tabs { { "SPECTRUM", "CURVE" } };

        MultiSpectrumDisplay spectrum;
        MultiWaveformDisplay waveform;

        juce::TextButton zoomButton { "ZOOM" };
        juce::TextButton outputButton { "OUTPUT" };
        juce::TextButton splitButton { "SPLIT" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyserGraph)
    };
} // namespace Celine
