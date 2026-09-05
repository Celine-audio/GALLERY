#include "IrStripControl.h"

#include "../PluginProcessor.h"
#include "Fonts.h"
#include "PluginLookAndFeel.h"
#include "Theme.h"

using namespace Celine;

namespace
{
    constexpr int gap = 10;
    constexpr int pillHeight = 26;
    constexpr int pillWidth = 34;
    constexpr int loadHeight = 28;
    constexpr int nameHeight = 15;
    constexpr int knobHeight = 84;
    constexpr int cutHeight = 30;
    constexpr int slopeHeight = 26;
    constexpr int slopeWidth = 92;

    /** Air between the state pills and the Load button, whatever else has to give. */
    constexpr int minimumRowGap = 12;

    constexpr int clearWidth = 36;

    /** How far in from the strip's edge the cut bar starts -- less than the margin the
        other rows keep, since it is the one control that wants the length. */
    constexpr int barInset = 6;

    /** The formats the loader reads, written once: the browser's filter and the
        drop target's test are the same list, and two copies of it is one format
        that can be added to the menu and not to the drag. */
    const juce::String audioExtensions { "wav;aiff;aif;flac;ogg;mp3" };

    juce::String audioWildcards()
    {
        return "*." + audioExtensions.replace (";", ";*.");
    }

    bool isAudioFile (const juce::File& file)
    {
        return file.hasFileExtension (audioExtensions);
    }

    /** A drop from inside the window carries a path as its description. Checked for
        being one before it is made into a juce::File, which asserts on anything that is
        not absolute -- and the description is whatever the drag source chose to put
        there. */
    juce::File droppedFile (const juce::var& description)
    {
        const auto path = description.toString();

        return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }
}

//==============================================================================
IrStripControl::IrStripControl (PluginProcessor& processor, int slotIndex)
    : processorRef (processor),
      slot (slotIndex),
      alignKnob (processor.getAPVTS(), ParamID::align[(size_t) slotIndex], "Align"),
      panKnob (processor.getAPVTS(), ParamID::pan[(size_t) slotIndex], "Pan")
{
    buildStateButtons();
    buildFileControls();
    buildKnobs();
    buildCutControls();

    applyColours();
}

void IrStripControl::buildStateButtons()
{
    auto& state = processorRef.getAPVTS();
    const auto index = (size_t) slot;

    for (auto* button : { &soloButton, &muteButton, &phaseButton })
        addAndMakeVisible (button);

    stateAttachments[0] = std::make_unique<juce::ButtonParameterAttachment> (
        *state.getParameter (ParamID::solo[index]), soloButton);
    stateAttachments[1] = std::make_unique<juce::ButtonParameterAttachment> (
        *state.getParameter (ParamID::mute[index]), muteButton);
    stateAttachments[2] = std::make_unique<juce::ButtonParameterAttachment> (
        *state.getParameter (ParamID::phase[index]), phaseButton);
}

void IrStripControl::buildFileControls()
{
    // The same slate every other button in the window wears: Load is the ordinary
    // action here, and colouring it apart from the rest said it was the exceptional one.
    loadButton.setTooltip ("Load a cabinet response. Files can be drag-and-dropped.");
    loadButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible (loadButton);

    // The house cross, drawn from the icon set rather than set as a multiplication
    // sign: a typeface's glyph is whatever weight and proportion that face gives it,
    // where the artwork matches the other icons in the window.
    clearButton.setActiveColour (Theme::discard());
    clearButton.setTooltip ("Remove the cabinet response.");
    clearButton.onClick = [this]
    {
        processorRef.unloadImpulseResponse (slot);
        refresh();
    };
    addAndMakeVisible (clearButton);

    fileName.setFont (Fonts::light (11.0f));
    fileName.setColour (juce::Label::textColourId, Theme::comment());
    fileName.setJustificationType (juce::Justification::centredRight);
    fileName.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (fileName);
}

void IrStripControl::buildKnobs()
{
    resolutionButton.setTooltip ("Normal : 2048 samples"
                                 "High : 8192 samples"
                                 "Ultra : 52000 samples");

    resolutionButton.onClick = [this]
    {
        // Through the choice parameter's own index rather than through its normalised
        // value: three settings do not divide the zero-to-one range into anything that
        // survives being converted back by hand.
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (
                processorRef.getAPVTS().getParameter (ParamID::resolution[(size_t) slot])))
        {
            const auto next = Parameters::nextResolution (choice->getIndex());

            choice->beginChangeGesture();
            choice->setValueNotifyingHost (choice->convertTo0to1 ((float) next));
            choice->endChangeGesture();
        }

        refresh();
    };

    addAndMakeVisible (resolutionButton);

    // The parameter supplies the number -- its own string function, through the
    // attachment -- and this puts the unit after it, so the two cannot disagree about
    // how many decimals a millisecond gets.
    alignKnob.getSlider().setTextValueSuffix (" ms");

    for (auto* knob : { &alignKnob, &panKnob })
    {
        addAndMakeVisible (knob);
    }
}

void IrStripControl::buildCutControls()
{
    auto& state = processorRef.getAPVTS();
    const auto index = (size_t) slot;

    cutRange.setAccentColour (colour());
    cutRange.onDragged = [this] (auto handle, auto hz) { setCut (handle, hz); };
    cutRange.onGesture = [this] (auto handle, auto starting) { beginCutGesture (handle, starting); };
    addAndMakeVisible (cutRange);

    for (auto* box : { &lowSlope, &highSlope })
    {
        // Filled here, not by the attachment. A ComboBox attachment syncs the selection
        // and assumes the items are already there -- give it an empty box and it selects
        // nothing, which draws as a chevron with no word beside it and reads as a
        // rendering fault rather than a missing list.
        for (const auto slope : Parameters::slopes)
            box->addItem (juce::String (slope) + " dB", slope);

        box->setTooltip ("Sets the slope of the cut, in decibels per octave.");
        addAndMakeVisible (box);
    }

    lowSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, ParamID::lowCutSlope[index], lowSlope);
    highSlopeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, ParamID::highCutSlope[index], highSlope);
}

IrStripControl::~IrStripControl() = default;

void IrStripControl::applyColours()
{
    // The same slate every other button in the window wears: Load is the ordinary
    // action here, and colouring it apart from the rest said it was the exceptional one.
    loadButton.setColour (juce::TextButton::buttonColourId, Theme::surface());

    for (auto* knob : { &alignKnob, &panKnob })
        knob->getSlider().setColour (juce::Slider::rotarySliderFillColourId, colour());

    cutRange.setAccentColour (colour());

    // The rest is refresh()'s: the filename's ink, and the resolution button's three
    // looks. That one only redraws when the tier moves, so forgetting which tier is
    // showing is what makes it take the new colours.
    shownResolution = -1;
    refresh();
}

//==============================================================================
void IrStripControl::refresh()
{
    const auto loaded = processorRef.isSlotLoaded (slot);
    const auto name = processorRef.getResponseName (slot);

    // A slot whose file has gone missing is named as missing rather than left looking
    // empty: the path is still in the session, and "nothing loaded" would be a lie
    // that costs somebody an hour of wondering where their cabinet went.
    const auto stored = processorRef.getAPVTS().state
                            .getProperty (ParamID::fileProperty[(size_t) slot], juce::String())
                            .toString();

    const auto text = loaded ? name
                     : stored.isNotEmpty() ? "Missing: " + juce::File (stored).getFileName()
                                           : juce::String ("Nothing loaded");

    if (fileName.getText() != text)
        fileName.setText (text, juce::dontSendNotification);

    fileName.setColour (juce::Label::textColourId,
                        ! loaded && stored.isNotEmpty() ? Theme::error() : Theme::comment());

    // Nothing to empty is nothing to press. Left enabled it would be a button that
    // sometimes does nothing, which is a button you have to test to understand.
    const auto canDiscard = loaded || stored.isNotEmpty();

    clearButton.setEnabled (canDiscard);

    // ...and it only wears the red while it is live. An empty slot's cross sits in the
    // same slate as everything else: there is nothing there to warn about.
    clearButton.setActive (canDiscard);

    auto& apvts = processorRef.getAPVTS();

    cutRange.setRange (apvts.getRawParameterValue (ParamID::lowCut[(size_t) slot])->load(),
                       apvts.getRawParameterValue (ParamID::highCut[(size_t) slot])->load());

    const auto tier = juce::jlimit (0, (int) Parameters::resolutions.size() - 1,
                                    (int) apvts.getRawParameterValue (ParamID::resolution[(size_t) slot])->load());

    if (tier != shownResolution)
    {
        shownResolution = tier;

        resolutionButton.setButtonText (Parameters::resolutionNames[(size_t) tier]);

        // Three looks for three settings. The middle one wears the accent every other
        // filled control does; the top one wears its own colour, because it is not
        // simply more of the same -- it is past a second of response, which is a reverb
        // rather than a cabinet and costs the convolution accordingly.
        resolutionButton.setColour (juce::TextButton::buttonColourId,
                                    tier == 0 ? Theme::surface()
                                  : tier == 1 ? Theme::accent()
                                              : Theme::ultra());

        resolutionButton.setColour (juce::TextButton::textColourOffId,
                                    tier == 2 ? Theme::onPill() : Theme::text());

        resolutionButton.repaint();
    }
}

//==============================================================================
juce::RangedAudioParameter* IrStripControl::cutParameter (CutRangeSlider::Handle handle) const
{
    const auto index = (size_t) slot;

    return processorRef.getAPVTS().getParameter (handle == CutRangeSlider::Handle::low
                                                     ? ParamID::lowCut[index]
                                                     : ParamID::highCut[index]);
}

void IrStripControl::setCut (CutRangeSlider::Handle handle, float hz)
{
    if (auto* parameter = cutParameter (handle))
    {
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (hz));

        // Led rather than waited for: the poll that would otherwise move the handle
        // runs at thirty a second, and a handle that lags the pointer by a frame feels
        // like a handle that is stuck.
        refresh();
    }
}

void IrStripControl::beginCutGesture (CutRangeSlider::Handle handle, bool starting)
{
    if (auto* parameter = cutParameter (handle))
    {
        if (starting)
            parameter->beginChangeGesture();
        else
            parameter->endChangeGesture();
    }
}

//==============================================================================
void IrStripControl::chooseFile()
{
    // Where this slot's own file lives if it has one, and otherwise wherever the last
    // one came from -- which is nearly always the same folder, because cabinets are
    // loaded a set at a time. Starting every empty slot at Music meant navigating back
    // to the same place four times over.
    const auto existing = processorRef.getResponseFile (slot);

    auto startIn = existing.existsAsFile() ? existing.getParentDirectory()
                                           : processorRef.getLastBrowseDirectory();

    if (! startIn.isDirectory())
        startIn = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Load a cabinet response", startIn,
                                                  audioWildcards());

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto file = browser.getResult();

                              if (! file.existsAsFile())
                                  return;

                              processorRef.setLastBrowseDirectory (file.getParentDirectory());
                              load (file);
                          });
}

void IrStripControl::load (const juce::File& file)
{
    const auto result = processorRef.loadImpulseResponse (slot, file);

    if (result.failed())
    {
        // The loader's own words. It knows whether the file was too long, in a format
        // this build cannot read, or simply not there, and those want different things
        // done about them.
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                     "Could not load that response",
                                                     result.getErrorMessage(), this);
    }

    refresh();
}

//==============================================================================
bool IrStripControl::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& name : files)
        if (isAudioFile (juce::File (name)))
            return true;

    return false;
}

void IrStripControl::filesDropped (const juce::StringArray& files, int, int)
{
    fileHovering = false;
    repaint();

    for (const auto& name : files)
    {
        const juce::File file (name);

        if (isAudioFile (file))
        {
            load (file);
            return;
        }
    }
}

void IrStripControl::fileDragEnter (const juce::StringArray&, int, int)
{
    fileHovering = true;
    repaint();
}

void IrStripControl::fileDragExit (const juce::StringArray&)
{
    fileHovering = false;
    repaint();
}

//==============================================================================
bool IrStripControl::isInterestedInDragSource (const SourceDetails& details)
{
    return isAudioFile (droppedFile (details.description));
}

void IrStripControl::itemDropped (const SourceDetails& details)
{
    fileHovering = false;
    repaint();

    const auto file = droppedFile (details.description);

    if (isAudioFile (file))
        load (file);
}

void IrStripControl::itemDragEnter (const SourceDetails&)
{
    fileHovering = true;
    repaint();
}

void IrStripControl::itemDragExit (const SourceDetails&)
{
    fileHovering = false;
    repaint();
}

//==============================================================================
void IrStripControl::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (Theme::background());
    g.fillRoundedRectangle (bounds, Theme::cornerRadius);

    // The slot's colour, as a rule along the top. Four strips are otherwise identical,
    // and which curve above belongs to which set of knobs is the one thing somebody
    // has to be able to answer without counting across.
    const auto rule = bounds.reduced (Theme::cornerRadius, 0.0f).withHeight (3.0f).translated (0.0f, 1.0f);

    g.setColour (colour());
    g.fillRoundedRectangle (rule, 1.5f);

    if (fileHovering)
    {
        g.setColour (colour().withAlpha (0.6f));
        g.drawRoundedRectangle (bounds.reduced (1.0f), Theme::cornerRadius, 2.0f);
    }
}

void IrStripControl::layOutTopRow (juce::Rectangle<int> row)
{
    const auto buttonHeight = juce::jmin (pillHeight, row.getHeight());

    // The three state pills take at most half the row and give up the rest, so that
    // narrowing the window narrows both groups rather than closing the space between
    // them. A fixed width for the pills left the two groups touching at the smallest
    // size.
    const auto pillsWidth = juce::jmin ((row.getWidth() - minimumRowGap) / 2,
                                        pillWidth * 3 + gap * 2);
    const auto eachPill = (pillsWidth - gap * 2) / 3;

    auto pills = row.removeFromLeft (pillsWidth).withSizeKeepingCentre (pillsWidth, buttonHeight);

    soloButton.setBounds (pills.removeFromLeft (eachPill));
    pills.removeFromLeft (gap);
    muteButton.setBounds (pills.removeFromLeft (eachPill));
    pills.removeFromLeft (gap);
    phaseButton.setBounds (pills.removeFromLeft (eachPill));

    // The air between the two groups, taken before Load can claim it rather than left
    // over afterwards.
    row.removeFromLeft (minimumRowGap);

    // Load and its X take the rest, up to a width past which Load stops looking like a
    // button and starts looking like a banner. Measured before the rectangle is taken:
    // removeFrom mutates what it is called on, so asking `row` for its width a second
    // time in the same expression asks the remainder, and both buttons came out zero
    // pixels wide.
    const auto rightWidth = juce::jmin (row.getWidth(), 170 + clearWidth + 4);

    auto right = row.removeFromRight (rightWidth).withSizeKeepingCentre (rightWidth, buttonHeight);

    clearButton.setBounds (right.removeFromRight (juce::jmin (clearWidth, right.getWidth() / 3)));
    right.removeFromRight (4);

    loadButton.setBounds (right);
}

void IrStripControl::layOutKnobs (juce::Rectangle<int> row)
{
    const auto each = row.getWidth() / 3;

    alignKnob.setBounds (row.removeFromLeft (each));

    {
        // Centred on the knobs beside it rather than on the column, so the row reads as
        // three controls on one line rather than two and something floating.
        const auto middle = row.removeFromLeft (each);
        const auto width = juce::jmin (middle.getWidth() - 8, 76);

        resolutionButton.setBounds (middle.withSizeKeepingCentre (width, slopeHeight));
    }

    panKnob.setBounds (row);
}

void IrStripControl::resized()
{
    auto area = getLocalBounds().reduced (gap + 2, gap);

    // Shares of the strip's height rather than pixel counts. Pixels do not survive this
    // window: its aspect is fixed and the whole of it scales, so a strip is 215 tall at
    // the default size and 175 at the smallest -- and a layout taking fixed heights off
    // the top left the knobs the remainder, which at the small end was fifteen pixels.
    // That drew four labels with no knobs under them, and no error to say so.
    const auto rows = area.getHeight();

    const auto take = [&area, rows] (float share, int most = 10000)
    {
        return area.removeFromTop (juce::jmin (most, juce::roundToInt ((float) rows * share)));
    };

    layOutTopRow (take (0.21f, juce::jmax (pillHeight, loadHeight) + 4));

    fileName.setBounds (take (0.10f, nameHeight));

    // The knobs get whatever is left once the two rows below have had their share, so
    // they grow with the window rather than being squeezed out of it.
    const auto slopes = juce::jmin (slopeHeight, juce::roundToInt ((float) rows * 0.17f));
    const auto cut = juce::jmin (cutHeight, juce::roundToInt ((float) rows * 0.15f));

    layOutKnobs (area.removeFromTop (juce::jmin (knobHeight,
                                                 area.getHeight() - slopes - cut - 4)));

    auto slopeRow = area.removeFromBottom (slopes);
    lowSlope.setBounds (slopeRow.removeFromLeft (juce::jmin (slopeWidth, slopeRow.getWidth() / 2)));
    highSlope.setBounds (slopeRow.removeFromRight (juce::jmin (slopeWidth, slopeRow.getWidth())));

    area.removeFromBottom (4);

    // The bar reaches out to the strip's own edge rather than to the margin the other
    // rows keep. Nothing else here wants that -- a knob against the panel edge reads as
    // falling off it -- but a slider is a distance, and every pixel of margin is a pixel
    // of resolution it does not have.
    const auto bar = area.removeFromBottom (juce::jmin (cut, area.getHeight()));

    cutRange.setBounds (bar.withX (barInset)
                           .withWidth (juce::jmax (0, getWidth() - barInset * 2)));
}
