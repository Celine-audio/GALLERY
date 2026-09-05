#pragma once

#include "PluginProcessor.h"
#include "ui/AboutPanel.h"
#include "ui/ThemePanel.h"
#include "ui/AnalyserFeed.h"
#include "ui/AnalyserGraph.h"
#include "ui/BlendPad.h"
#include "ui/IconButton.h"
#include "ui/IrStripControl.h"
#include "ui/LibraryPanel.h"
#include "ui/PluginLookAndFeel.h"
#include "ui/Theme.h"

#include <array>
#include <memory>
#include <vector>

/**
    The window: the house toolbar across the top, the library panel and the graph
    below it, and the four cabinet strips across the bottom.

    Layout and wiring, and little else. What the graph draws is gathered by
    `AnalyserFeed`, which is the larger half of the job and none of it this window's:
    see there for why the pictures are polled rather than driven by the controls that
    change them.
*/
class PluginEditor : public juce::AudioProcessorEditor,
                     public juce::DragAndDropContainer,
                     private juce::Timer,
                     private juce::ChangeListener
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    //==============================================================================
    void buildToolbar();
    void buildContent();
    void layOutContent (juce::Rectangle<int> area);

    void timerCallback() override;

    void refreshBlendPad();

    /** Writes the blend to a file the user picks, as a stereo response. */
    void exportBlend();

    std::unique_ptr<juce::FileChooser> exportChooser;
    juce::RangedAudioParameter* blendParameter (const char* id) const;
    void setBlend (float x, float y);
    void beginBlendGesture (bool starting);

    void showSettingsMenu();

    /** The handful of colours this window takes once rather than reading as it draws. */
    void applyColours();

    /** The theme moved. Re-reads the look and feel's colours, tells every child, and
        repaints -- which is the whole of what a theme change is from here. */
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshBypassLook();

    PluginProcessor& processorRef;
    PluginLookAndFeel lookAndFeel;

    // Every control in the window has a tooltip and none of them can show one without
    // this: JUCE needs a window to draw them in, and there is no default.
    juce::TooltipWindow tooltips { this, 600 };

    Celine::IconButton bypassButton { "Bypass", "power-off-solid-full.svg" };
    Celine::IconButton settingsButton { "Settings", "gear-solid-full.svg" };

    std::unique_ptr<juce::ButtonParameterAttachment> bypassAttachment;

    std::unique_ptr<juce::Drawable> logo, wordmark;
    juce::Rectangle<int> logoBounds, wordmarkBounds, toolbarBand;
    juce::Label wordmarkText;

    //==============================================================================
    Celine::AnalyserGraph analyser;
    Celine::AnalyserFeed feed { processorRef, analyser };

    Celine::LibraryPanel library;

    // Bottom left, under the library and square. What used to be four gain knobs, one
    // per strip -- see BlendPad for why they were one control all along.
    Celine::BlendPad blendPad;

    // Down the right-hand edge, where AURA puts its own. The two plugins are used one
    // after the other on the same signal, and a trim that is in a different place in
    // each is a trim somebody has to go looking for.
    FaderControl outputFader;

    std::array<std::unique_ptr<Celine::IrStripControl>, (size_t) ParamID::numSlots> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};
