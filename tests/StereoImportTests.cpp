/*
    A stereo cabinet capture is two microphone positions, not a stereo image. Convolving
    both would put a different cabinet in each ear, so the slot takes one side -- and
    which side is a choice, which means it has to survive the session.
*/
#include <dsp/ImpulseResponse.h>

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
    /** A two-channel file whose sides are told apart by their sign. */
    juce::File writeStereoFile()
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("gallery-stereo-ir.wav");
        file.deleteFile();

        juce::AudioBuffer<float> buffer (2, 64);
        buffer.clear();
        buffer.setSample (0, 0, 1.0f);     // left:  +1
        buffer.setSample (1, 0, -1.0f);    // right: -1

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = wav.createWriterFor (stream,
                                       juce::AudioFormatWriterOptions{}
                                           .withSampleRate (48000.0)
                                           .withNumChannels (2)
                                           .withBitsPerSample (24));

        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        writer.reset();

        return file;
    }
}

TEST_CASE ("a stereo response can be loaded one side at a time", "[stereo]")
{
    const auto file = writeStereoFile();

    CHECK (ImpulseResponse::countChannels (file) == 2);

    SECTION ("the left side alone")
    {
        ImpulseResponse ir;
        REQUIRE (ir.loadFrom (file, ImpulseResponse::Side::left).wasOk());

        CHECK (ir.getNumChannels() == 1);
        CHECK (ir.getSide() == ImpulseResponse::Side::left);
        CHECK (ir.atSampleRate (48000.0).getSample (0, 0) > 0.9f);
    }

    SECTION ("the right side alone")
    {
        ImpulseResponse ir;
        REQUIRE (ir.loadFrom (file, ImpulseResponse::Side::right).wasOk());

        CHECK (ir.getNumChannels() == 1);
        CHECK (ir.getSide() == ImpulseResponse::Side::right);
        CHECK (ir.atSampleRate (48000.0).getSample (0, 0) < -0.9f);
    }

    SECTION ("or both, which is what it always did")
    {
        ImpulseResponse ir;
        REQUIRE (ir.loadFrom (file, ImpulseResponse::Side::both).wasOk());

        CHECK (ir.getNumChannels() == 2);
        CHECK (ir.getSide() == ImpulseResponse::Side::both);
    }

    file.deleteFile();
}

TEST_CASE ("a mono response ignores the question", "[stereo]")
{
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("gallery-mono-ir.wav");
    file.deleteFile();

    juce::AudioBuffer<float> buffer (1, 64);
    buffer.clear();
    buffer.setSample (0, 0, 1.0f);

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    auto writer = wav.createWriterFor (stream,
                                       juce::AudioFormatWriterOptions{}
                                           .withSampleRate (48000.0)
                                           .withNumChannels (1)
                                           .withBitsPerSample (24));
    REQUIRE (writer != nullptr);
    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    writer.reset();

    CHECK (ImpulseResponse::countChannels (file) == 1);

    // Asked for the right side of a file that has only one: it gets the one it has, and
    // says so, rather than coming back empty.
    ImpulseResponse ir;
    REQUIRE (ir.loadFrom (file, ImpulseResponse::Side::right).wasOk());

    CHECK (ir.getNumChannels() == 1);
    CHECK (ir.getSide() == ImpulseResponse::Side::both);

    file.deleteFile();
}
