#include "PluginEditor.h"

#include "ProductInfo.h"
#include "Settings.h"
#include "ui/EmbeddedAssets.h"
#include "ui/Fonts.h"
#include "ui/PlotGeometry.h"

using namespace Celine;

namespace
{
    // The mockup's proportions: 2250 by 1000, and locked so the window can only be
    // scaled rather than reproportioned. Four strips of four knobs and a graph do not
    // survive being made narrow -- there is nothing here that can give.
    constexpr float aspectRatio = 2.25f;

    constexpr int defaultWidth = 1500;
    constexpr int defaultHeight = (int) (defaultWidth / aspectRatio);

    // Below this a strip has not the height for four knobs, their names and their
    // readouts, and the knobs are what gives. Found by rendering the window at the
    // limit and looking at it, which is the only way this number is ever right: at
    // 1100 the labels were all there and the knobs under them were fifteen pixels of
    // nothing, and no test that did not look at the picture would have said so.
    constexpr int minimumWidth = 1280;

    // The kit's toolbar band, the same one AURA and SPACE wear. The mockup drew it
    // taller -- 84 of its 1000 -- but a house whose plugins have different-height
    // toolbars is a house with no toolbar, and this one is shared for the same reason
    // the About window is.
    constexpr int headerHeight = Theme::toolbarHeight;
    constexpr int gap = 10;

    /** The output fader's column, AURA's width exactly. */
    constexpr int faderWidth = 64;

    /** How much of what is left below the toolbar the strips take. The mockup gives
        them 314 of the 862 beneath its header. */
    constexpr float stripProportion = 314.0f / 862.0f;

    // The library's width is not a proportion of its own. It is one strip wide, taken
    // from the strip row so the two cannot disagree -- which is the mockup's intent
    // (540 of 2250, a quarter) stated in the way that keeps it true at every size.

    const juce::String ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
}

//==============================================================================
PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (&p),
      processorRef (p),
      outputFader (p.getAPVTS(), ParamID::outputGain, "Output")
{
    setLookAndFeel (&lookAndFeel);

    // A tooltip paints a rounded panel, so it must not be opaque -- an opaque component
    // has to fill every pixel it owns, and the four corners outside the rounding are
    // exactly the ones it does not paint; left opaque they came out as square spikes of
    // whatever was in the buffer. TooltipWindow sets the flag in its constructor and
    // offers no way to ask otherwise. Safe because this one is parented to the editor
    // rather than put on the desktop, so what shows through the corners is this window.
    tooltips.setOpaque (false);

    // Read before anything below can lay the window out. Everything that does writes
    // the size back into the state, so reading afterwards returns whatever the last of
    // those wrote -- which, before setSize, is zero.
    const auto& state = processorRef.getAPVTS().state;
    const auto storedWidth = (int) state.getProperty ("uiWidth", defaultWidth);
    const auto storedHeight = (int) state.getProperty ("uiHeight", defaultHeight);

    buildToolbar();
    buildContent();

    // The second flag is the corner grip. With a fixed ratio that is the only handle
    // that means anything: dragging an edge would have to move the other dimension with
    // it, which reads as the window fighting the mouse.
    setResizable (true, true);
    setResizeLimits (minimumWidth, (int) (minimumWidth / aspectRatio),
                     minimumWidth * 3, (int) (minimumWidth * 3 / aspectRatio));

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) aspectRatio);

    setSize (storedWidth, storedHeight);

    processorRef.setUiActive (true);

    // The theme is process-wide, so a colour changed in one window has to reach every
    // other -- including this one, when the change was made somewhere else.
    Theme::palette().addChangeListener (this);

    // Fast enough that dragging a knob moves the curve with the pointer, slow enough to
    // cost nothing when nothing is moving -- see signature(), which is what most of
    // these ticks do and then stop.
    startTimerHz (30);
}

void PluginEditor::buildToolbar()
{
    // Tinted rather than used as drawn: the artwork carries whatever colours it was
    // saved with, and the palette is the one that has to win.
    logo = Assets::drawable ("logo.svg");

    if (logo != nullptr)
        Assets::tint (*logo, Theme::text());

    wordmark = Assets::drawable (ProductInfo::wordmarkAsset, Assets::IfMissing::returnNull);

    if (wordmark != nullptr)
        Assets::tint (*wordmark, Theme::text());

    wordmarkText.setText (juce::String (JucePlugin_Name).toLowerCase(), juce::dontSendNotification);
    wordmarkText.setFont (Fonts::logo (24.0f));
    wordmarkText.setJustificationType (juce::Justification::centredLeft);
    wordmarkText.setInterceptsMouseClicks (false, false);
    addChildComponent (wordmarkText);
    wordmarkText.setVisible (wordmark == nullptr);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setActiveColour (Theme::danger());
    bypassButton.onClick = [this] { refreshBypassLook(); };
    addAndMakeVisible (bypassButton);

    bypassAttachment = std::make_unique<juce::ButtonParameterAttachment> (
        *processorRef.getAPVTS().getParameter (ParamID::bypass), bypassButton);

    refreshBypassLook();

    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    applyColours();
}

PluginEditor::~PluginEditor()
{
    Theme::palette().removeChangeListener (this);

    stopTimer();
    processorRef.setUiActive (false);
    setLookAndFeel (nullptr);
}

//==============================================================================
void PluginEditor::buildContent()
{
    {
        // The folder belongs to the person rather than to the project -- see Settings.
        // The session's own copy is the fallback, so a project saved before there was a
        // machine-wide answer still opens on the folder it was saved with.
        auto folder = Settings::libraryFolder();

        if (! folder.isDirectory())
        {
            const juce::String remembered = processorRef.getAPVTS().state
                                                .getProperty (ParamID::libraryFolderProperty,
                                                              juce::String());

            folder = juce::File (remembered);
        }

        library.setFolder (folder);
    }

    library.onFolderChosen = [this] (const juce::File& chosen)
    {
        Settings::setLibraryFolder (chosen);

        // Kept in the session as well, so a project carried to a machine that has never
        // run this plugin opens where it was left rather than on nothing.
        processorRef.getAPVTS().state.setProperty (ParamID::libraryFolderProperty,
                                                   chosen.getFullPathName(), nullptr);
    };

    library.onExport = [this] { exportBlend(); };
    addAndMakeVisible (library);
    addAndMakeVisible (outputFader);

    blendPad.onDragged = [this] (float x, float y) { setBlend (x, y); };
    blendPad.onGesture = [this] (bool starting) { beginBlendGesture (starting); };
    addAndMakeVisible (blendPad);

    analyser.onViewChanged = [this] { feed.refresh(); };
    analyser.onZoomChanged = [this] { feed.refresh(); };
    analyser.onSplitChanged = [this] { feed.refresh(); };

    analyser.onShowOutputChanged = [this] (bool shouldShow)
    {
        processorRef.setOutputAnalysisEnabled (shouldShow);

        if (! shouldShow)
            analyser.getSpectrum().setOutputSpectrum ({});
    };

    addAndMakeVisible (analyser);

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        auto strip = std::make_unique<IrStripControl> (processorRef, slot);
        addAndMakeVisible (*strip);
        strips[(size_t) slot] = std::move (strip);
    }

    refreshBlendPad();

    feed.prepare (processorRef.getSampleRate());

    // The processor follows the button rather than the other way round: a previous
    // editor may have left the measurement running or switched off, and either way what
    // the audio thread is measuring has to be what this window is about to draw.
    processorRef.setOutputAnalysisEnabled (analyser.isShowingOutput());

    feed.refresh();
}

void PluginEditor::layOutContent (juce::Rectangle<int> area)
{
    // The left-hand column, one strip wide. Derived from the strip rather than from its
    // own proportion of the window, which was two numbers for one column drifting apart
    // at every size.
    const auto column = (area.getWidth() - gap * (ParamID::numSlots - 1)) / ParamID::numSlots;

    auto left = area.removeFromLeft (column);
    area.removeFromLeft (gap);

    // The pad is square, and that is not decoration: the two axes are the same kind of
    // quantity, so a dot two thirds of the way across has to mean what a dot two thirds
    // of the way up does. Stretched into a rectangle it would read as one axis being
    // finer than the other, which is exactly the thing it is not.
    //
    // Taken from the bottom, so the pad's own size decides the split and the library
    // gets what is left. The library is a list and can be any height; the pad cannot.
    blendPad.setBounds (left.removeFromBottom (juce::jmin (column, left.getHeight())));
    left.removeFromBottom (gap);
    library.setBounds (left);

    // On the far right, so the row reads as what goes in, what it becomes and what
    // comes out. Full height: the travel is worth more than lining up with the tabs.
    outputFader.setBounds (area.removeFromRight (faderWidth));
    area.removeFromRight (gap - 4);

    // What is left is the graph with the four strips beneath it, and the strips are
    // exactly as wide as the graph -- a cabinet's controls sit under the curve they
    // draw, which is what makes reading across from one to the other possible.
    auto strip = area.removeFromBottom ((int) ((float) area.getHeight() * stripProportion));
    area.removeFromBottom (gap);

    analyser.setBounds (area);

    const auto each = (strip.getWidth() - gap * (ParamID::numSlots - 1)) / ParamID::numSlots;

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        strips[(size_t) slot]->setBounds (slot < ParamID::numSlots - 1
                                              ? strip.removeFromLeft (each)
                                              : strip);

        if (slot < ParamID::numSlots - 1)
            strip.removeFromLeft (gap);
    }
}

//==============================================================================
void PluginEditor::timerCallback()
{
    for (auto& strip : strips)
        strip->refresh();

    refreshBlendPad();
    feed.refreshOutput();

    if (feed.hasChanged())
        feed.refresh();
}

//==============================================================================
void PluginEditor::refreshBlendPad()
{
    // Polled like everything else: a host can move the blend, and a file can arrive in
    // a slot, without this window being touched -- so a pad wired only to its own mouse
    // would be a pad that is right except when something else is driving.
    auto& state = processorRef.getAPVTS();

    blendPad.setPosition (state.getRawParameterValue (ParamID::blendX)->load(),
                          state.getRawParameterValue (ParamID::blendY)->load());

    auto anythingLoaded = false;

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        const auto loaded = processorRef.isSlotLoaded (slot);

        blendPad.setLoaded (slot, loaded);
        anythingLoaded |= loaded;
    }

    library.setCanExport (anythingLoaded);
}

void PluginEditor::exportBlend()
{
    juce::AudioBuffer<float> rendered;

    if (! processorRef.renderBlend (rendered))
        return;

    const auto folder = library.getFolder();

    exportChooser = std::make_unique<juce::FileChooser> (
        "Export the blend as a stereo response",
        (folder.isDirectory() ? folder : juce::File::getSpecialLocation (juce::File::userMusicDirectory))
            .getChildFile (juce::String (JucePlugin_Name).toLowerCase() + "-blend.wav"),
        "*.wav");

    const auto rate = processorRef.getSampleRate();

    exportChooser->launchAsync (juce::FileBrowserComponent::saveMode
                                    | juce::FileBrowserComponent::canSelectFiles
                                    | juce::FileBrowserComponent::warnAboutOverwriting,
                                [this, rendered, rate] (const juce::FileChooser& browser)
    {
        const auto file = browser.getResult();

        if (file.getFullPathName().isEmpty())
            return;

        file.deleteFile();

        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
        {
            juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                         "Could not export the blend",
                                                         "That file could not be written to.", this);
            return;
        }

        // Twenty-four bit, which is what cabinet responses are distributed as and what
        // every loader reads. Float would keep the last few bits of a signal whose
        // loudest point is already at unity, which is nothing anybody can hear.
        const auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                                .withSampleRate (rate)
                                                                .withNumChannels (2)
                                                                .withBitsPerSample (24));

        if (writer == nullptr)
            return;

        writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());

        // Listed straight away if it landed in the folder being shown, so exporting
        // into your own library does not need the folder choosing again.
        if (file.getParentDirectory() == library.getFolder())
            library.setFolder (library.getFolder());
    });
}

juce::RangedAudioParameter* PluginEditor::blendParameter (const char* id) const
{
    return processorRef.getAPVTS().getParameter (id);
}

void PluginEditor::setBlend (float x, float y)
{
    for (const auto& axis : { std::pair { ParamID::blendX, x },
                              std::pair { ParamID::blendY, y } })
        if (auto* parameter = blendParameter (axis.first))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (axis.second));

    // Led rather than waited for: the poll that redraws the graph runs at thirty a
    // second, and a mix curve that lags the pointer by a frame feels like a pad that
    // is stuck.
    feed.refresh();
}

void PluginEditor::beginBlendGesture (bool starting)
{
    for (const auto* id : { ParamID::blendX, ParamID::blendY })
        if (auto* parameter = blendParameter (id))
        {
            if (starting)
                parameter->beginChangeGesture();
            else
                parameter->endChangeGesture();
        }
}

//==============================================================================
//==============================================================================
void PluginEditor::paint (juce::Graphics& g)
{
    // The surround is the darkest thing here, so anything placed on it reads as an
    // opening rather than a box.
    g.fillAll (Theme::consoleBackground());

    g.setColour (Theme::chrome());
    g.fillRect (toolbarBand);

    // Both marks are drawn off their ink rather than their viewBox. Artwork is rarely
    // centred inside the box it was exported in, so placing it by the box sits it
    // visibly high -- drawWithin against getDrawableBounds() does not.
    if (logo != nullptr && ! logoBounds.isEmpty())
        logo->drawWithin (g, logoBounds.toFloat(), juce::RectanglePlacement::centred, 1.0f);

    // The wordmark is centred on its letters rather than on its box: "gallery" has two
    // descenders, so the box reaches well below the line the word stands on and
    // centring it sits the word visibly high against the mark beside it.
    if (wordmark != nullptr && ! wordmarkBounds.isEmpty())
        Assets::drawWordmark (g, *wordmark, wordmarkBounds.toFloat());
}

void PluginEditor::resized()
{
    // Remembered so reopening the editor lands where you left it.
    auto& state = processorRef.getAPVTS().state;

    // Zero is not a size worth remembering. resized() runs during construction, before
    // setSize, and storing what it sees then is how the stored size becomes the reason
    // the window will not open.
    if (state.isValid() && getWidth() > 0 && getHeight() > 0)
    {
        state.setProperty ("uiWidth", getWidth(), nullptr);
        state.setProperty ("uiHeight", getHeight(), nullptr);
    }

    auto area = getLocalBounds();
    toolbarBand = area.removeFromTop (headerHeight);

    {
        auto header = toolbarBand.reduced (gap + 2, 0);

        // Fitted to its own aspect so it is never squashed, and sat on the band's
        // centre line.
        const auto place = [&header] (const std::unique_ptr<juce::Drawable>& art,
                                      int height, juce::Rectangle<int>& out)
        {
            if (art == nullptr)
                return 0;

            const auto ink = art->getDrawableBounds();
            const auto aspect = ink.getHeight() > 0.0f ? ink.getWidth() / ink.getHeight() : 1.0f;
            const auto width = juce::roundToInt ((float) height * aspect);

            out = header.removeFromLeft (width).withSizeKeepingCentre (width, height);
            return width;
        };

        place (logo, 26, logoBounds);
        header.removeFromLeft (14);

        if (wordmark != nullptr)
            place (wordmark, 18, wordmarkBounds);
        else
            wordmarkText.setBounds (header.removeFromLeft (220));

        // Right to left, so the pair keeps its pitch whatever the window's width.
        const auto square = [&header] (juce::Component& c)
        {
            c.setBounds (header.removeFromRight (Theme::buttonSize)
                             .withSizeKeepingCentre (Theme::buttonSize, Theme::buttonSize));
            header.removeFromRight (Theme::buttonGap);
        };

        square (settingsButton);
        square (bypassButton);
    }

    layOutContent (area.reduced (gap));
}

//==============================================================================
void PluginEditor::showSettingsMenu()
{
    juce::PopupMenu menu;

    juce::PopupMenu::Item theme ("Theme" + ellipsis);
    theme.setAction ([this] { showThemeWindow (this); });
    menu.addItem (theme);

    juce::PopupMenu::Item about ("About " + juce::String (JucePlugin_Name) + ellipsis);
    about.setAction ([this] { showAboutWindow (this); });
    menu.addItem (about);

    // A menu has no parent to inherit a look and feel from.
    menu.setLookAndFeel (&lookAndFeel);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&settingsButton));
}

void PluginEditor::applyColours()
{
    wordmarkText.setColour (juce::Label::textColourId, Theme::text());
}

void PluginEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyColours();

    // Everything JUCE draws for us is *told* its colours, so the look and feel has to
    // re-read them before anything repaints -- see PluginLookAndFeel::applyPalette.
    lookAndFeel.applyPalette();

    // And every child that took a colour once and kept it gets a chance to take it
    // again. JUCE walks the tree for us; a control that snapshots colours says so by
    // overriding lookAndFeelChanged().
    sendLookAndFeelChange();

    repaint();
}

void PluginEditor::refreshBypassLook()
{
    bypassButton.setActive (bypassButton.getToggleState());
}
