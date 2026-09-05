#include "AnalyserGraph.h"

#include "Fonts.h"
#include "PlotGeometry.h"
#include "Theme.h"

using namespace Celine;

//==============================================================================
AnalyserGraph::AnalyserGraph()
{
    tabs.setBottomCornersRounded (true);
    addAndMakeVisible (tabs);

    tabs.onSelectionChanged = [this] (int)
    {
        const auto showingSpectrum = getView() == View::spectrum;

        spectrum.setVisible (showingSpectrum);
        waveform.setVisible (! showingSpectrum);
        zoomButton.setVisible (! showingSpectrum);
        outputButton.setVisible (showingSpectrum);
        splitButton.setVisible (showingSpectrum);

        if (onViewChanged != nullptr)
            onViewChanged();
    };

    addAndMakeVisible (spectrum);
    addChildComponent (waveform);

    zoomButton.setClickingTogglesState (true);
    zoomButton.setTooltip ("Zoom to the first few milliseconds for precise alignment.");

    zoomButton.onClick = [this]
    {
        waveform.setZoomed (zoomButton.getToggleState());

        if (onZoomChanged != nullptr)
            onZoomChanged();
    };

    addChildComponent (zoomButton);

    outputButton.setClickingTogglesState (true);

    // On to begin with. It is the only trace that moves on its own, and a graph that
    // sits perfectly still until you find the button reads as a picture rather than as
    // a meter.
    outputButton.setToggleState (true, juce::dontSendNotification);
    outputButton.setTooltip ("Show the frequency analyser (post).");

    outputButton.onClick = [this]
    {
        if (onShowOutputChanged != nullptr)
            onShowOutputChanged (outputButton.getToggleState());
    };

    addAndMakeVisible (outputButton);

    splitButton.setClickingTogglesState (true);
    splitButton.setTooltip ("Show the distinct IR spectra.");

    splitButton.onClick = [this]
    {
        if (onSplitChanged != nullptr)
            onSplitChanged();
    };

    addAndMakeVisible (splitButton);

    applyColours();
}

void AnalyserGraph::applyColours()
{
    for (auto* button : { &zoomButton, &outputButton, &splitButton })
    {
        button->setColour (juce::TextButton::buttonColourId, Theme::surface());
        button->setColour (juce::TextButton::buttonOnColourId, Theme::accent());
    }
}

AnalyserGraph::View AnalyserGraph::getView() const noexcept
{
    return tabs.getSelected() == 0 ? View::spectrum : View::curve;
}

//==============================================================================
void AnalyserGraph::paint (juce::Graphics& g)
{
    // The ground behind the graph, which shows at its rounded bottom corners.
    auto area = getLocalBounds().toFloat().withTrimmedTop ((float) tabHeight);

    g.setColour (Theme::consoleBackground());
    g.fillRoundedRectangle (area, Theme::cornerRadius);

    // The top corners are square, because a tab bar sits on them.
    g.fillRect (area.withHeight (Theme::cornerRadius));
}

void AnalyserGraph::resized()
{
    auto area = getLocalBounds();

    tabs.setBounds (area.removeFromTop (tabHeight));

    spectrum.setBounds (area);
    waveform.setBounds (area);

    // The two share a corner, and only one of them is ever on show: zoom belongs to
    // the waveform, the output backdrop to the spectrum.
    //
    // Placed against the *plot* rather than against this component, and inset equally
    // from its top and right edges -- the 0 dB line and the 20 kHz one, which are the
    // two rules the eye reads the corner against. Measured from the component instead,
    // the button cleared the right-hand gridline by the axis margin and the top one by
    // something else entirely, and sat visibly off-square in the corner it was meant
    // to occupy.
    const auto plot = PlotGeometry::plotAreaWithin (area.toFloat()).toNearestInt();

    constexpr int inset = 10;
    constexpr int buttonWidth = 74;
    constexpr int buttonHeight = 22;

    const auto corner = juce::Rectangle<int> (plot.getRight() - inset - buttonWidth,
                                              plot.getY() + inset,
                                              buttonWidth, buttonHeight);

    zoomButton.setBounds (corner);
    outputButton.setBounds (corner);

    // Beside the output button, which is the other thing that decides what the spectrum
    // is showing rather than how it is drawn.
    splitButton.setBounds (corner.translated (-(buttonWidth + 6), 0));
}
