#include "helpers/test_helpers.h"

#include <PluginEditor.h>
#include <PluginProcessor.h>
#include <ui/AnalyserGraph.h>
#include <ui/BlendPad.h>
#include <ui/IrStripControl.h>
#include <ui/LibraryPanel.h>
#include <ui/Theme.h>

#include <catch2/catch_test_macros.hpp>

/*
    The window, rendered and then counted.

    "The four traces are drawn in the four colours" is the whole promise of this
    interface, and it is exactly the kind of promise that breaks quietly: a trace that
    stops being fed still leaves a graph that looks like a graph. Counting coloured
    pixels in the picture turns that from something somebody has to notice into
    something the build notices.
*/

namespace
{
    juce::File writeCabinet (const juce::File& directory, const juce::String& name,
                             float brightness, int lengthSamples, int preDelay)
    {
        juce::AudioBuffer<float> buffer (1, lengthSamples);
        buffer.clear();

        juce::Random random { (int) name.hashCode() };

        // Something cabinet-shaped: a transient, a decay, and a roll-off whose corner
        // differs per cabinet so the four traces are visibly different from each other.
        auto state = 0.0f;

        for (int i = preDelay; i < lengthSamples; ++i)
        {
            const auto t = (float) (i - preDelay) / (float) lengthSamples;
            const auto envelope = std::exp (-18.0f * t);
            const auto excitation = (i == preDelay ? 1.0f : (random.nextFloat() * 2.0f - 1.0f)) * envelope;

            state += brightness * (excitation - state);
            buffer.setSample (0, i, state);
        }

        buffer.applyGain (0.9f / juce::jmax (1.0e-6f, buffer.getMagnitude (0, lengthSamples)));

        const auto file = directory.getChildFile (name + ".wav");
        file.deleteFile();

        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        const auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                                .withSampleRate (48000.0)
                                                                .withNumChannels (1)
                                                                .withBitsPerSample (24));

        writer->writeFromAudioSampleBuffer (buffer, 0, lengthSamples);
        return file;
    }

    /** Roughly how many pixels of the image are the given hue.

        Compared by hue rather than by exact value because a trace is drawn over a wash
        of the other three and antialiased against the grid, so almost none of its
        pixels are the colour it was asked for -- but all of them are still that hue. */
    int countHue (const juce::Image& image, juce::Colour wanted, juce::Rectangle<int> area)
    {
        auto found = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto pixel = image.getPixelAt (x, y);

                if (pixel.getSaturation() < 0.25f || pixel.getBrightness() < 0.25f)
                    continue;

                auto difference = std::abs (pixel.getHue() - wanted.getHue());
                difference = juce::jmin (difference, 1.0f - difference);

                if (difference < 0.04f)
                    ++found;
            }

        return found;
    }

    /** Bright pixels with no colour in them, which on this plot is the mix curve and
        nothing else. countHue cannot be used for it: that skips anything under a
        quarter saturated, and the whole point of drawing the blend in the interface's
        own ink is that it has no hue to be confused with a cabinet's. */
    int countPale (const juce::Image& image, juce::Rectangle<int> area)
    {
        auto found = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto pixel = image.getPixelAt (x, y);

                if (pixel.getBrightness() > 0.75f && pixel.getSaturation() < 0.15f)
                    ++found;
            }

        return found;
    }

    struct LoadedEditor
    {
        LoadedEditor()
        {
            directory = temporary.getFile().getParentDirectory();

            files = { writeCabinet (directory, "GALLERY test cab 1", 0.30f, 4800, 0),
                      writeCabinet (directory, "GALLERY test cab 2", 0.16f, 4800, 12),
                      writeCabinet (directory, "GALLERY test cab 3", 0.42f, 3600, 40),
                      writeCabinet (directory, "GALLERY test cab 4", 0.10f, 5200, 74) };

            plugin.prepareToPlay (48000.0, 512);

            for (int slot = 0; slot < ParamID::numSlots; ++slot)
                REQUIRE (plugin.loadImpulseResponse (slot, files[(size_t) slot]).wasOk());

            editor = plugin.createEditorAndMakeActive();
            REQUIRE (editor != nullptr);

            editor->setSize (1500, 667);
            pump();
        }

        ~LoadedEditor()
        {
            plugin.editorBeingDeleted (editor);
            delete editor;

            for (const auto& file : files)
                file.deleteFile();
        }

        /** The displays are fed by a timer, so without a loop to run it the picture
            would be of an empty graph. */
        void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil (250); }

        juce::Image render() const
        {
            return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        }

        Celine::AnalyserGraph& graph() const
        {
            for (auto* child : editor->getChildren())
                if (auto* found = dynamic_cast<Celine::AnalyserGraph*> (child))
                    return *found;

            FAIL ("the editor has no graph");
            std::terminate();
        }

        /** The part of the graph where traces live, and nothing else does.

            Not simply the graph's bounds. The tab bar behind the front tab is the
            toolbar's aubergine, which is a dark violet -- the same *hue* as slot
            three, and thirty thousand pixels of it. Counted as part of the graph it
            reports a purple trace on screen whatever the plugin is doing, which is a
            test that passes for the wrong reason and then fails to fail when the trace
            really does go missing.

            The row of buttons in the plot's top corner -- zoom, output, split -- are
            the same trap in miniature: surface() is a dark blue-violet close enough to
            slot three's hue to be counted, and adding one more button was enough to
            put the total over the threshold. Hence trimming the top past them. */
        juce::Rectangle<int> plotArea() const
        {
            return graph().getBounds()
                .withTrimmedTop (Celine::AnalyserGraph::tabHeight + 80)
                .withTrimmedBottom (30)
                .reduced (50, 0);
        }

        /** Puts the spectrum into split mode, where the four cabinets are drawn
            separately. The default is the one curve they add up to, which is what
            somebody blending is listening to -- so the per-cabinet traces have to be
            asked for before they can be counted. */
        void split()
        {
            for (auto* child : graph().getChildren())
                if (auto* button = dynamic_cast<juce::TextButton*> (child))
                    if (button->getButtonText() == "SPLIT")
                        button->setToggleState (true, juce::sendNotificationSync);
        }

        juce::TemporaryFile temporary;
        juce::File directory;
        std::array<juce::File, (size_t) ParamID::numSlots> files;

        PluginProcessor plugin;
        juce::AudioProcessorEditor* editor = nullptr;
    };
}

//==============================================================================
TEST_CASE ("Every loaded cabinet is drawn, in its own colour", "[ui][graph]")
{
    LoadedEditor loaded;
    loaded.split();

    for (const auto view : { 0, 1 })
    {
        // Both tabs, because the two displays are fed by separate code paths and
        // either can stop drawing without the other noticing.
        for (auto* child : loaded.graph().getChildren())
            if (auto* tabs = dynamic_cast<Celine::TabHeader*> (child))
                tabs->setSelected (view);

        loaded.pump();

        const auto image = loaded.render();
        const auto area = loaded.plotArea();

        for (int slot = 0; slot < ParamID::numSlots; ++slot)
        {
            INFO ("view " << view << ", slot " << slot);
            CHECK (countHue (image, Celine::Theme::irSlot (slot), area) > 200);
        }
    }
}

TEST_CASE ("A cabinet that is not loaded is not drawn", "[ui][graph]")
{
    // The other half of the promise: an empty slot has to leave the graph alone rather
    // than drawing a flat line along the floor, which would read as a cabinet with no
    // top end rather than as no cabinet.
    LoadedEditor loaded;
    loaded.split();

    loaded.plugin.unloadImpulseResponse (2);
    loaded.pump();

    const auto image = loaded.render();
    const auto area = loaded.plotArea();

    CHECK (countHue (image, Celine::Theme::irSlot (2), area) < 60);
    CHECK (countHue (image, Celine::Theme::irSlot (0), area) > 200);
}

TEST_CASE ("The spectrum draws the blend by default", "[ui][graph]")
{
    // What somebody moving the pad is listening to is the sum, so that is what the
    // graph shows until they ask for the workings. Drawn in the interface's own ink,
    // which is the one colour on that plot that is not a cabinet.
    LoadedEditor loaded;
    loaded.pump();

    const auto image = loaded.render();
    const auto area = loaded.plotArea();

    CHECK (countPale (image, area) > 200);

    // ...and the four are not drawn until Split is pressed, or the graph would be
    // answering both questions at once and neither of them clearly.
    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        INFO ("slot " << slot);
        CHECK (countHue (image, Celine::Theme::irSlot (slot), area) < 60);
    }
}

TEST_CASE ("The four strips share the graph's width equally", "[ui][layout]")
{
    // Four cabinets, four equal strips, and the row exactly as wide as the graph above
    // it -- a cabinet's controls sit under the curve they draw, which is what makes
    // reading across from one to the other possible at all.
    LoadedEditor loaded;

    std::vector<juce::Rectangle<int>> strips;
    juce::Rectangle<int> graph;

    for (auto* child : loaded.editor->getChildren())
    {
        if (auto* strip = dynamic_cast<Celine::IrStripControl*> (child))
            strips.push_back (strip->getBounds());

        if (auto* analyser = dynamic_cast<Celine::AnalyserGraph*> (child))
            graph = analyser->getBounds();
    }

    REQUIRE (strips.size() == (size_t) ParamID::numSlots);
    REQUIRE_FALSE (graph.isEmpty());

    std::sort (strips.begin(), strips.end(),
               [] (auto a, auto b) { return a.getX() < b.getX(); });

    for (size_t i = 1; i < strips.size(); ++i)
    {
        INFO ("strip " << i);
        CHECK (std::abs (strips[i].getWidth() - strips[0].getWidth()) <= 1);
        CHECK (strips[i].getY() == strips[0].getY());
        CHECK (strips[i].getX() >= strips[i - 1].getRight());
    }

    INFO ("strips " << strips.front().getX() << ".." << strips.back().getRight()
          << ", graph " << graph.getX() << ".." << graph.getRight());

    CHECK (std::abs (strips.front().getX() - graph.getX()) <= 1);
    CHECK (std::abs (strips.back().getRight() - graph.getRight()) <= 1);
}

TEST_CASE ("The blend pad is square", "[ui][layout]")
{
    // Not decoration. The two axes are the same kind of quantity, so a dot two thirds
    // of the way across has to mean what a dot two thirds of the way up does --
    // stretched into a rectangle the pad would read as one axis being finer than the
    // other, which is exactly the thing it is not.
    LoadedEditor loaded;

    juce::Rectangle<int> pad;

    for (auto* child : loaded.editor->getChildren())
        if (auto* found = dynamic_cast<Celine::BlendPad*> (child))
            pad = found->getBounds();

    REQUIRE_FALSE (pad.isEmpty());

    INFO ("pad " << pad.getWidth() << " x " << pad.getHeight());
    CHECK (std::abs (pad.getWidth() - pad.getHeight()) <= 1);
}

TEST_CASE ("The library and the pad share one column", "[ui][layout]")
{
    // The left-hand column, with the library above and the pad below it. Two panels
    // that are meant to be one column: separate proportions of the window would agree
    // on paper and drift by a few pixels at every size the window can actually be.
    LoadedEditor loaded;

    juce::Rectangle<int> panel, pad;

    for (auto* child : loaded.editor->getChildren())
    {
        if (auto* library = dynamic_cast<Celine::LibraryPanel*> (child))
            panel = library->getBounds();

        if (auto* found = dynamic_cast<Celine::BlendPad*> (child))
            pad = found->getBounds();
    }

    REQUIRE_FALSE (panel.isEmpty());
    REQUIRE_FALSE (pad.isEmpty());

    CHECK (panel.getX() == pad.getX());
    CHECK (panel.getRight() == pad.getRight());

    INFO ("library bottom " << panel.getBottom() << ", pad top " << pad.getY());
    CHECK (pad.getY() >= panel.getBottom());
}

TEST_CASE ("Nothing in a strip is laid out on top of anything else", "[ui][layout]")
{
    // The failure a proportional layout invites at its smallest size: two controls
    // given the same pixels, which draws as one of them having vanished.
    LoadedEditor loaded;

    loaded.editor->setSize (1280, (int) (1280.0 / 2.25));
    loaded.pump();

    for (auto* child : loaded.editor->getChildren())
    {
        auto* strip = dynamic_cast<Celine::IrStripControl*> (child);

        if (strip == nullptr)
            continue;

        const auto& children = strip->getChildren();

        for (int a = 0; a < children.size(); ++a)
            for (int b = a + 1; b < children.size(); ++b)
            {
                const auto first = children[a]->getBounds();
                const auto second = children[b]->getBounds();

                INFO (children[a]->getName() << " over " << children[b]->getName());
                CHECK_FALSE (first.intersects (second));
            }
    }
}
