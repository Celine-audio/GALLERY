#pragma once

#include "CutRangeSlider.h"
#include "../dsp/ImpulseResponse.h"
#include "IconButton.h"
#include "LetterToggleButton.h"
#include "ParameterControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

class PluginProcessor;

namespace Celine
{
    //==========================================================================
    /**
        One cabinet's strip: what is loaded in it, and everything done to it.

        Four of these sit across the bottom of the window, and they are identical apart
        from which slot they address and what colour that slot is. Written once and
        given an index rather than four times over -- which is not only less to read
        but the only arrangement in which the fourth strip cannot quietly end up wired
        to the third one's parameters.

        The colour is the slot's identity, the same one its traces are drawn in above.
        It is worn lightly -- the knob rings, the cut band, a rule along the top --
        rather than by tinting the whole strip: four saturated panels side by side is a
        harlequin, and the point of the colour is to answer "which of these is the teal
        curve", not to decorate.
    */
    class IrStripControl : public juce::Component,
                           public juce::FileDragAndDropTarget,
                           public juce::DragAndDropTarget
    {
    public:
        IrStripControl (PluginProcessor&, int slotIndex);
        ~IrStripControl() override;

        /** Pulls the filename and the cut handles back out of the processor. Called by
            the editor's timer, because none of it moves only when this strip's own
            controls are used -- automation and a reloaded session change both. */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;

        // Two kinds of drop, and they are not the same thing. A file dragged from the
        // desktop arrives as a path the operating system hands over; a cabinet dragged
        // from the library panel never leaves the window, and arrives as a description
        // this plugin wrote itself. Both end at the same load.
        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void filesDropped (const juce::StringArray&, int, int) override;
        void fileDragEnter (const juce::StringArray&, int, int) override;
        void fileDragExit (const juce::StringArray&) override;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDropped (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override;
        void itemDragExit (const SourceDetails&) override;

    private:
        void buildStateButtons();
        void buildFileControls();
        void buildKnobs();
        void buildCutControls();

        void layOutTopRow (juce::Rectangle<int> row);
        void layOutKnobs (juce::Rectangle<int> row);

        void chooseFile();
        /** Loads a file into this slot, asking which side first if it is stereo. */
    void load (const juce::File&);

    /** The half of `load` that actually loads, once the question has an answer. */
    void loadSide (const juce::File&, ImpulseResponse::Side);

        /** Writes one of the two cut frequencies, bracketed as a host gesture so a
            drag is recorded as one edit rather than a hundred. */
        void setCut (CutRangeSlider::Handle, float hz);
        void beginCutGesture (CutRangeSlider::Handle, bool starting);

        juce::RangedAudioParameter* cutParameter (CutRangeSlider::Handle) const;

        /** This slot's identity colour, asked for rather than held: it is a theme
            entry, and a copy taken at construction would not follow one being changed. */
        juce::Colour colour() const { return Theme::irSlot (slot); }

        /** The colours this takes once rather than reading as it draws. See Theme.h. */
        void applyColours();
        void lookAndFeelChanged() override { applyColours(); }

        PluginProcessor& processorRef;
        const int slot;

        LetterToggleButton soloButton { "Solo", "S", Theme::solo() };
        LetterToggleButton muteButton { "Mute", "M", Theme::mute() };
        LetterToggleButton phaseButton { "Invert polarity",
                                         juce::String::fromUTF8 ("\xce\xa6"), Theme::phase() };

        std::array<std::unique_ptr<juce::ButtonParameterAttachment>, 3> stateAttachments;

        juce::TextButton loadButton { "Load" + juce::String::fromUTF8 ("\xe2\x80\xa6") };

        /** Empties the slot. Beside Load rather than in a menu, because a cabinet you
            have decided against is something you want gone in one movement -- and
            because with four slots, "load something else over it" is not the same
            intention as "have three". */
        IconButton clearButton { "Empty this slot", "xmark-solid-full.svg" };

        juce::Label fileName;

    /** STEREO or MONO, on the left of the name. A slot loaded from one side of a stereo
        file convolves in mono, and without this the two are indistinguishable once the
        file has been chosen. */
    juce::Label channels;

        KnobControl alignKnob, panKnob;

        CutRangeSlider cutRange;

        juce::ComboBox lowSlope, highSlope;

        // Between Align and Pan, where the Gain knob used to be. Three settings that
        // cycle in one button rather than a list: they are ordered, there are only
        // three, and what somebody wants is usually the next one up.
        juce::TextButton resolutionButton;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lowSlopeAttachment,
                                                                                highSlopeAttachment;

        /** Which tier the button is showing, so `refresh` only redraws it when the
            answer has changed. */
        int shownResolution = -1;

        std::unique_ptr<juce::FileChooser> chooser;

        bool fileHovering = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrStripControl)
    };
} // namespace Celine
