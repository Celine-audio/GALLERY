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

TEST_CASE ("Re-preparing does not play the dry signal first", "[processor][state]")
{
    // A host calls prepareToPlay on every transport start and every change of rate or
    // buffer size -- with whatever is loaded still loaded. The wet crossfade was snapped
    // to zero there, so the first 50 ms after every play was the dry signal fading out
    // as the cabinets faded in: measured at full scale against a settled 0.25, which on
    // a guitar DI is the unprocessed attack of whatever note the transport started on.
    //
    // The trim beside it was already written to avoid exactly this. This is the same
    // trap, one variable over.
    Cabinets::FourCabinets fixture;
    fixture.load (4);

    const auto settled = Cabinets::impulseThrough (fixture.plugin);
    const auto settledPeak = settled.getMagnitude (0, 0, settled.getNumSamples());

    REQUIRE (settledPeak > 0.0f);

    fixture.plugin.prepareToPlay (Cabinets::rate, Cabinets::blockSize);

    juce::AudioBuffer<float> buffer (2, Cabinets::blockSize);
    juce::MidiBuffer midi;

    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);

    fixture.plugin.processBlock (buffer, midi);

    const auto firstPeak = buffer.getMagnitude (0, 0, buffer.getNumSamples());

    INFO ("settled " << settledPeak << ", first block after re-prepare " << firstPeak);

    // The cabinets, not the impulse that went in. A tenth of a decibel of slack for the
    // ramps that legitimately do restart here.
    CHECK (firstPeak < settledPeak * 1.05f);
}

TEST_CASE ("Re-preparing with nothing loaded still passes the signal", "[processor][state]")
{
    // The other half of the same rule: an empty plugin is a wire, and snapping the
    // crossfade the other way would mute the track on every transport start.
    Cabinets::FourCabinets fixture;

    fixture.plugin.prepareToPlay (Cabinets::rate, Cabinets::blockSize);

    juce::AudioBuffer<float> buffer (2, Cabinets::blockSize);
    juce::MidiBuffer midi;

    buffer.clear();
    buffer.setSample (0, 0, 1.0f);
    buffer.setSample (1, 0, 1.0f);

    fixture.plugin.processBlock (buffer, midi);

    CHECK (buffer.getMagnitude (0, 0, buffer.getNumSamples()) > 0.9f);
}
