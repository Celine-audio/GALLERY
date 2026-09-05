#include "../helpers/CabinetFixture.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace Cabinets;

/*
    A session closed and reopened. What a slot holds is a file rather than a parameter,
    so it rides in the state tree and has to survive the round trip -- including the
    case where the file no longer exists.
*/

TEST_CASE ("A slot's file survives a state round trip", "[processor][state]")
{
    juce::TemporaryFile temporary;
    const auto file = writeSpike (temporary.getFile().getParentDirectory(), "gallery-state", 0.5f);

    PluginProcessor source;
    source.prepareToPlay (rate, blockSize);
    REQUIRE (source.loadImpulseResponse (2, file).wasOk());

    juce::MemoryBlock state;
    source.getStateInformation (state);

    PluginProcessor restored;
    restored.prepareToPlay (rate, blockSize);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (restored.isSlotLoaded (2));
    CHECK (restored.getResponseFile (2) == file);
    CHECK_FALSE (restored.isSlotLoaded (0));

    file.deleteFile();
}

TEST_CASE ("A file that has gone missing leaves its slot empty, not broken",
           "[processor][state]")
{
    juce::TemporaryFile temporary;
    const auto file = writeSpike (temporary.getFile().getParentDirectory(), "gallery-missing", 0.5f);

    PluginProcessor source;
    source.prepareToPlay (rate, blockSize);
    REQUIRE (source.loadImpulseResponse (1, file).wasOk());

    juce::MemoryBlock state;
    source.getStateInformation (state);

    file.deleteFile();

    PluginProcessor restored;
    restored.prepareToPlay (rate, blockSize);
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK_FALSE (restored.isSlotLoaded (1));

    // The path stays, so that reopening the session where the file lives finds it --
    // and so the strip can say which cabinet is missing rather than looking as though
    // one was never loaded.
    const juce::String remembered = restored.getAPVTS().state
                                        .getProperty (ParamID::fileProperty[1], juce::String())
                                        .toString();

    CHECK (remembered.isNotEmpty());

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    buffer.clear();

    for (int block = 0; block < 4; ++block)
        restored.processBlock (buffer, midi);

    for (int i = 0; i < blockSize; ++i)
        REQUIRE (std::isfinite (buffer.getSample (0, i)));
}
