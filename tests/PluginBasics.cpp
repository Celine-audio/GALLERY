#include "helpers/test_helpers.h"

#include <Parameters.h>
#include <PluginEditor.h>
#include <PluginProcessor.h>
#include <ui/AboutPanel.h>
#include <ui/PluginLookAndFeel.h>
#include <ui/Theme.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

/*
    The tests a plugin gets for free. None of them know anything about what your
    plugin does -- they check the scaffolding underneath it, which is exactly the
    part that breaks silently: a bypass that does not bypass, state that does not
    round-trip, an editor that throws on open.

    Keep them. Add yours beside them.
*/

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    void fillWithNoise (juce::AudioBuffer<float>& buffer, int seed = 1)
    {
        juce::Random random { seed };

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
    }

    void setParameter (PluginProcessor& plugin, const char* id, float value)
    {
        auto* parameter = plugin.getAPVTS().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
}

//==============================================================================
TEST_CASE ("The plugin reports itself sensibly", "[instance]")
{
    PluginProcessor plugin;

    CHECK (plugin.getName().isNotEmpty());
    CHECK (plugin.hasEditor());
    CHECK_FALSE (plugin.isMidiEffect());
}

TEST_CASE ("Every declared parameter exists in the tree", "[parameters]")
{
    PluginProcessor plugin;

    // A parameter can be named in ParamID and forgotten in createLayout, and nothing
    // complains until getRawParameterValue returns null and the plugin crashes in a
    // host. The two lists live next to each other precisely so this stays true.
    //
    // Forty-six of them now, four slots of which are the same eleven controls written
    // out four times over -- so this is no longer a formality. It is the check that
    // catches the fourth strip having been copied from the third and one of its IDs
    // left saying "ir3".
    const auto check = [&plugin] (const char* id)
    {
        INFO ("parameter: " << id);
        CHECK (plugin.getAPVTS().getParameter (id) != nullptr);
        CHECK (plugin.getAPVTS().getRawParameterValue (id) != nullptr);
    };

    for (const auto* id : { ParamID::bypass, ParamID::outputGain,
                            ParamID::blendX, ParamID::blendY })
        check (id);

    for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
        for (const auto& group : { ParamID::solo, ParamID::mute, ParamID::phase,
                                   ParamID::align, ParamID::resolution,
                                   ParamID::pan, ParamID::lowCut, ParamID::highCut,
                                   ParamID::lowCutSlope, ParamID::highCutSlope })
            check (group[slot]);
}

TEST_CASE ("No parameter ID is used twice", "[parameters]")
{
    // The failure a slot loop invites: two strips pointing at one parameter, so moving
    // a knob on the third cabinet moves the fourth one's as well. APVTS does not
    // complain -- it simply hands both attachments the same parameter.
    PluginProcessor plugin;

    juce::StringArray seen;

    for (const auto* parameter : plugin.getParameters())
        if (const auto* withID = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter))
        {
            INFO ("duplicate: " << withID->paramID);
            REQUIRE_FALSE (seen.contains (withID->paramID));
            seen.add (withID->paramID);
        }

    // Bypass, the output trim and the blend pad's two axes, plus ten per cabinet.
    CHECK (seen.size() == 4 + ParamID::numSlots * 10);
}

TEST_CASE ("Processing a block leaves the signal finite", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    fillWithNoise (buffer);

    plugin.processBlock (buffer, midi);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < blockSize; ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}

TEST_CASE ("Silence in, silence out", "[audio]")
{
    // Anything that rings, feeds back or reads uninitialised memory shows up here
    // before it shows up as a burst of noise in somebody's session.
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    buffer.clear();

    for (int block = 0; block < 8; ++block)
        plugin.processBlock (buffer, midi);

    CHECK (buffer.getMagnitude (0, blockSize) == 0.0f);
}

TEST_CASE ("Output gain scales the signal, and bypass takes it out again", "[audio]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::MidiBuffer midi;

    const auto peakAfterProcessing = [&] (float gainDb, bool bypassed)
    {
        setParameter (plugin, ParamID::outputGain, gainDb);
        setParameter (plugin, ParamID::bypass, bypassed ? 1.0f : 0.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);

        // Several blocks: the gain is ramped, so the first one is mid-fade.
        for (int block = 0; block < 8; ++block)
        {
            fillWithNoise (buffer, 7);
            plugin.processBlock (buffer, midi);
        }

        return buffer.getMagnitude (0, blockSize);
    };

    const auto unity = peakAfterProcessing (0.0f, false);
    const auto boosted = peakAfterProcessing (6.0f, false);

    REQUIRE (unity > 0.0f);
    CHECK_THAT (boosted / unity, Catch::Matchers::WithinRel (2.0f, 0.05f));

    // Bypassed, the trim goes with it: A/B-ing has to compare like with like rather
    // than comparing the effect against a dry signal that has been quietly boosted.
    CHECK_THAT (peakAfterProcessing (6.0f, true), Catch::Matchers::WithinRel (unity, 0.05f));
}

TEST_CASE ("State survives a round trip", "[state]")
{
    PluginProcessor source;
    source.prepareToPlay (sampleRate, blockSize);
    setParameter (source, ParamID::outputGain, -7.5f);

    juce::MemoryBlock state;
    source.getStateInformation (state);

    PluginProcessor restored;
    restored.prepareToPlay (sampleRate, blockSize);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK_THAT (restored.getAPVTS().getRawParameterValue (ParamID::outputGain)->load(),
                Catch::Matchers::WithinAbs (-7.5f, 1.0e-4f));
}

TEST_CASE ("Foreign state is ignored rather than applied", "[state]")
{
    // Hosts have been known to hand a plugin somebody else's state. Applying it
    // would replace the whole parameter tree with an empty one.
    PluginProcessor plugin;
    setParameter (plugin, ParamID::outputGain, 3.0f);

    juce::XmlElement foreign ("SomeOtherPlugin");
    juce::MemoryBlock block;
    plugin.copyXmlToBinary (foreign, block);

    plugin.setStateInformation (block.getData(), (int) block.getSize());

    CHECK_THAT (plugin.getAPVTS().getRawParameterValue (ParamID::outputGain)->load(),
                Catch::Matchers::WithinAbs (3.0f, 1.0e-4f));
}

//==============================================================================
TEST_CASE ("The editor opens, sizes itself and closes", "[ui]")
{
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        auto* editor = plugin.getActiveEditor();
        REQUIRE (editor != nullptr);

        CHECK (editor->getWidth() > 0);
        CHECK (editor->getHeight() > 0);

        // The stored size is written on resize and read back on open. It used to be
        // read after setResizeLimits, which had already overwritten it with the
        // minimum, so every fresh instance opened at the smallest size allowed.
        CHECK (editor->getWidth() >= 600);
    });
}

TEST_CASE ("The palette is the palette, not a default-constructed Colour", "[ui]")
{
    // Theme's colours are function-local statics in another translation unit. Held
    // as static Colour objects instead, nothing orders their initialisation against
    // this one, and whichever link order put them first got an opaque black.
    using namespace Celine;

    for (const auto colour : { Theme::accent(), Theme::accentAlt(), Theme::text(),
                               Theme::background(), Theme::chrome() })
        CHECK (colour != juce::Colour());

    CHECK (Theme::accent() != Theme::accentAlt());
    CHECK (Theme::text() != Theme::background());
}

TEST_CASE ("A dropdown's background colour is transparent", "[ui]")
{
    // Not a style choice -- a structural one, and invisible until somebody opens a
    // menu. PopupMenu makes its window opaque when this colour is opaque, and an
    // opaque component must paint every pixel it owns; the look and feel paints a
    // *rounded* panel, so the four corners went unpainted and came out as dark
    // squares. Anyone tidying this colour back to surface() brings that straight back,
    // and will not see it until they click a slope dropdown.
    juce::ScopedJuceInitialiser_GUI gui;

    PluginLookAndFeel lookAndFeel;

    CHECK_FALSE (lookAndFeel.findColour (juce::PopupMenu::backgroundColourId).isOpaque());
}

TEST_CASE ("The About window builds and names the product", "[ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;

    AboutPanel panel;
    panel.setSize (760, 640);

    // The notice is the one thing a host user can reach, and the AGPL leans on it,
    // so an empty or unnamed one is a real failure rather than a cosmetic one.
    juce::TextEditor* body = nullptr;

    for (auto* child : panel.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*> (child))
            body = editor;

    REQUIRE (body != nullptr);
    CHECK (body->getText().contains (JucePlugin_Name));
    CHECK (body->getText().contains ("AGPL"));
}

TEST_CASE ("The About window draws its format marks at its own size", "[ui]")
{
    // A component's constructor that calls setSize before loading its artwork runs
    // resized() with nothing to measure, so every mark is placed at zero size and
    // stays there until something resizes the window again. A dialog that happens to
    // open at a different size hides that completely -- which is why this asks for
    // the panel at exactly the size it gives itself.
    juce::ScopedJuceInitialiser_GUI gui;

    AboutPanel panel;
    REQUIRE (panel.getWidth() > 0);

    const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
    const auto ground = Celine::Theme::chrome();

    // The footer's left half is where the marks go; the Close button is on the right.
    int inkPixels = 0;

    for (int y = panel.getHeight() - 90; y < panel.getHeight() - 14; ++y)
        for (int x = 18; x < panel.getWidth() / 2; ++x)
            if (image.getPixelAt (x, y) != ground)
                ++inkPixels;

    CHECK (inkPixels > 500);
}

TEST_CASE ("The plugin does not fade in when the host prepares it", "[audio]")
{
    // juce::dsp::Gain::reset() snaps its smoother to the *current target*, and Gain's
    // target starts at zero -- so resetting before the gain has been set leaves the
    // plugin ramping up from silence over the ramp duration on every prepareToPlay.
    // A 50 ms fade-in at the top of every take, quiet enough to be blamed on
    // something else.
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize), original (2, blockSize);
    juce::MidiBuffer midi;

    fillWithNoise (buffer, 11);
    original.makeCopyOf (buffer);

    plugin.processBlock (buffer, midi);

    // The very first samples, which is exactly where a fade-in hides.
    for (int i = 0; i < 32; ++i)
        REQUIRE_THAT (buffer.getSample (0, i),
                      Catch::Matchers::WithinAbs (original.getSample (0, i), 1.0e-5f));
}

TEST_CASE ("A zero window size is never stored", "[ui]")
{
    // resized() runs during construction, before setSize, and storing what it sees
    // then is how the stored size becomes the reason the window will not open. The
    // guard makes that impossible rather than merely unlikely, whatever order a
    // plugin's own constructor ends up in.
    runWithinPluginEditor ([] (PluginProcessor& plugin)
    {
        const auto& state = plugin.getAPVTS().state;

        CHECK ((int) state.getProperty ("uiWidth", 0) > 0);
        CHECK ((int) state.getProperty ("uiHeight", 0) > 0);
    });
}
