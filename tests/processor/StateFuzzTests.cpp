#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>

/*
    What a host is allowed to hand back, which is anything at all.

    setStateInformation is the one entry point a plugin does not control the input to.
    A project file truncated by a crash, a session saved by a newer build, a host that
    hands the wrong plugin's chunk to the wrong plugin -- all of these arrive here, and
    all of them arrive as a pointer and a length with nothing to say which it is.

    The bar is not that the plugin recovers the settings. It is that it comes back with
    a working parameter tree and keeps passing audio, because the alternative is a crash
    inside the host's project load, which takes the session with it.
*/
TEST_CASE ("Malformed state is survived rather than trusted", "[processor][state]")
{
    Cabinets::FourCabinets fixture;
    fixture.load (4);

    // What good state looks like, kept to compare against and to restore from.
    juce::MemoryBlock good;
    fixture.plugin.getStateInformation (good);
    REQUIRE (good.getSize() > 0);

    const auto stillWorks = [&]
    {
        // The tree is intact and the audio path still runs.
        REQUIRE (fixture.plugin.getAPVTS().getParameter (ParamID::blendX) != nullptr);

        juce::AudioBuffer<float> buffer (2, Cabinets::blockSize);
        juce::MidiBuffer midi;

        for (int block = 0; block < 4; ++block)
        {
            buffer.clear();
            buffer.setSample (0, 0, 1.0f);
            fixture.plugin.processBlock (buffer, midi);
        }

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int s = 0; s < buffer.getNumSamples(); ++s)
                REQUIRE (std::isfinite (buffer.getSample (channel, s)));
    };

    SECTION ("nothing at all")
    {
        fixture.plugin.setStateInformation (nullptr, 0);
        stillWorks();
    }

    SECTION ("a length that does not match the data")
    {
        fixture.plugin.setStateInformation (good.getData(), 0);
        stillWorks();

        fixture.plugin.setStateInformation (good.getData(), 1);
        stillWorks();
    }

    SECTION ("good state, truncated anywhere")
    {
        // Every prefix of a valid chunk, which is what a crash mid-write leaves.
        for (int size = 1; size < (int) good.getSize(); size += 17)
        {
            fixture.plugin.setStateInformation (good.getData(), size);
            REQUIRE (fixture.plugin.getAPVTS().getParameter (ParamID::blendX) != nullptr);
        }

        stillWorks();
    }

    SECTION ("random bytes")
    {
        juce::Random random { 20260912 };

        for (int attempt = 0; attempt < 64; ++attempt)
        {
            juce::MemoryBlock noise ((size_t) random.nextInt ({ 1, 512 }));

            for (size_t i = 0; i < noise.getSize(); ++i)
                noise[i] = (char) random.nextInt (256);

            fixture.plugin.setStateInformation (noise.getData(), (int) noise.getSize());
        }

        stillWorks();
    }

    SECTION ("another plugin's state")
    {
        // Well-formed XML in a binary chunk, with a tag this plugin has never heard of.
        // replaceState on a foreign tree throws away every parameter, so the guard for
        // this is a tag-name check rather than a well-formedness one.
        juce::XmlElement foreign ("SomeOtherPluginsState");
        foreign.setAttribute ("gain", 0.5);

        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary (foreign, block);

        fixture.plugin.setStateInformation (block.getData(), (int) block.getSize());

        // The tree it had is the tree it still has.
        REQUIRE (fixture.plugin.getAPVTS().getParameter (ParamID::blendX) != nullptr);
        stillWorks();
    }

    SECTION ("values a long way outside every range")
    {
        auto tree = fixture.plugin.getAPVTS().copyState();

        for (auto child : tree)
            if (child.hasProperty ("value"))
                child.setProperty ("value", 1.0e30, nullptr);

        if (const auto xml = tree.createXml())
        {
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
            fixture.plugin.setStateInformation (block.getData(), (int) block.getSize());
        }

        stillWorks();
    }

    SECTION ("a file path pointing at something that is not a response")
    {
        auto tree = fixture.plugin.getAPVTS().copyState();

        // A directory, and a path that is nothing at all. Both are things a session
        // carried to another machine can turn a cabinet into.
        tree.setProperty (ParamID::fileProperty[0],
                          juce::File::getSpecialLocation (juce::File::tempDirectory).getFullPathName(),
                          nullptr);
        tree.setProperty (ParamID::fileProperty[1], "/no/such/path/at/all.wav", nullptr);

        if (const auto xml = tree.createXml())
        {
            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
            fixture.plugin.setStateInformation (block.getData(), (int) block.getSize());
        }

        CHECK_FALSE (fixture.plugin.isSlotLoaded (1));
        stillWorks();
    }

    SECTION ("good state still restores after all of that")
    {
        fixture.plugin.setStateInformation (nullptr, 0);
        fixture.plugin.setStateInformation (good.getData(), (int) good.getSize());

        for (int slot = 0; slot < ParamID::numSlots; ++slot)
            CHECK (fixture.plugin.isSlotLoaded (slot));

        stillWorks();
    }
}
