#pragma once

#include <PluginProcessor.h>

#include <catch2/catch_test_macros.hpp>

/*
    The four cabinets every processor test is written against, and the handful of
    measurements taken off them.

    Single spikes, ten samples apart and at four different heights. That makes the
    output readable straight off the buffer -- each slot's share of it is the height of
    its own spike -- and the varied heights are deliberately *not* expected to reach the
    output: levelling is what removes them, and a test that still saw one would be a
    test of the bug.
*/
namespace Cabinets
{
    inline constexpr double rate = 48000.0;
    inline constexpr int blockSize = 512;

    inline juce::File writeSpike (const juce::File& directory, const juce::String& name,
                           float amplitude, int position = 0)
    {
        juce::AudioBuffer<float> buffer (1, 256);
        buffer.clear();
        buffer.setSample (0, position, amplitude);

        const auto file = directory.getChildFile (name + ".wav");
        file.deleteFile();

        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        const auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                                .withSampleRate (rate)
                                                                .withNumChannels (1)
                                                                .withBitsPerSample (24));

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }

    inline void setParameter (PluginProcessor& plugin, const char* id, float value)
    {
        auto* parameter = plugin.getAPVTS().getParameter (id);
        REQUIRE (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    /** An impulse through the plugin, after enough silence for every ramp and
        crossfade in it to have finished. */
    inline juce::AudioBuffer<float> impulseThrough (PluginProcessor& plugin)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int block = 0; block < 24; ++block)
        {
            buffer.clear();
            plugin.processBlock (buffer, midi);
        }

        buffer.clear();
        buffer.setSample (0, 0, 1.0f);
        buffer.setSample (1, 0, 1.0f);
        plugin.processBlock (buffer, midi);

        return buffer;
    }

    /** Where the pad's dot sits. Two parameters rather than one call, which is the
        shape of the control itself -- see ParamID::blendX. */
    inline void setBlend (PluginProcessor& plugin, float x, float y)
    {
        setParameter (plugin, ParamID::blendX, x);
        setParameter (plugin, ParamID::blendY, y);
    }

    inline float peakOf (PluginProcessor& plugin)
    {
        const auto output = impulseThrough (plugin);
        return output.getMagnitude (0, 0, output.getNumSamples());
    }

    /** Four cabinets whose spikes land at different places, so the sum has four
        countable pieces rather than one indistinguishable total. */
    struct FourCabinets
    {
        FourCabinets()
        {
            const auto directory = temporary.getFile().getParentDirectory();

            for (int slot = 0; slot < ParamID::numSlots; ++slot)
                files[(size_t) slot] = writeSpike (directory,
                                                   "gallery-proc-" + juce::String (slot),
                                                   0.1f * (float) (slot + 1),
                                                   slot * 10);

            plugin.prepareToPlay (rate, blockSize);
        }

        ~FourCabinets()
        {
            for (const auto& file : files)
                file.deleteFile();
        }

        void load (int count)
        {
            for (int slot = 0; slot < count; ++slot)
                REQUIRE (plugin.loadImpulseResponse (slot, files[(size_t) slot]).wasOk());
        }

        juce::TemporaryFile temporary;
        std::array<juce::File, (size_t) ParamID::numSlots> files;
        PluginProcessor plugin;
    };
} // namespace Cabinets
