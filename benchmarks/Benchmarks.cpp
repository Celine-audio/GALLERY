#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <catch2/catch_test_macros.hpp>

/*
    The three numbers worth watching from day one. Add benchmarks for your own DSP
    beside them -- measuring before optimising is the only way to find out that the
    slow part is somewhere you were not looking.

    Run with:  ./Builds/Benchmarks
*/

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    /** A cabinet-length response, written where the loader can read it. Noise rather
        than a spike: the engine's cost is its length, and a spike would let nothing
        downstream of it be measured honestly. */
    juce::File writeResponse (const juce::File& directory, const juce::String& name,
                              double seconds)
    {
        juce::AudioBuffer<float> buffer (1, (int) (seconds * sampleRate));
        juce::Random random { 7 };

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // Decaying, so it reads as a room rather than as noise the length of one.
            const auto decay = std::exp (-3.0f * (float) i / (float) buffer.getNumSamples());
            buffer.setSample (0, i, (random.nextFloat() * 2.0f - 1.0f) * decay);
        }

        const auto file = directory.getChildFile (name + ".wav");
        file.deleteFile();

        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        const auto writer = format.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                                .withSampleRate (sampleRate)
                                                                .withNumChannels (1)
                                                                .withBitsPerSample (24));

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        return file;
    }
}

TEST_CASE ("Boot performance")
{
    // A host instantiates a plugin to scan it. Slow here is slow for every plugin
    // in the folder, every time the user opens their DAW.
    BENCHMARK_ADVANCED ("Processor constructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::storage_for<PluginProcessor>> storage (size_t (meter.runs()));
        meter.measure ([&] (int i) { storage[(size_t) i].construct(); });
    };

    BENCHMARK_ADVANCED ("Editor open and close")
    (Catch::Benchmark::Chronometer meter)
    {
        PluginProcessor plugin;
        meter.measure ([&] (int)
        {
            auto* editor = plugin.createEditorAndMakeActive();
            plugin.editorBeingDeleted (editor);
            delete editor;
            return 0;
        });
    };
}

TEST_CASE ("Audio thread performance")
{
    // The number that decides whether the plugin is usable. At 48 kHz a 512-sample
    // block is 10.7 ms of audio, so anything over about 1 ms here is 10% of a core
    // for one instance.
    PluginProcessor plugin;
    plugin.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> source (2, blockSize);
    juce::Random random { 1 };

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < blockSize; ++i)
            source.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);

    juce::AudioBuffer<float> work (2, blockSize);
    juce::MidiBuffer midi;

    // Nothing loaded, which is the cheap path: four slots that early out and a dry
    // signal passed through.
    BENCHMARK_ADVANCED ("processBlock, empty")
    (Catch::Benchmark::Chronometer meter)
    {
        meter.measure ([&] (int)
        {
            work.makeCopyOf (source);
            plugin.processBlock (work, midi);
            return work.getSample (0, 0);
        });
    };

    // And the way it is actually used: four cabinets convolving at once. This is the
    // number that decides whether the plugin is usable, and the one the empty case
    // above says nothing about.
    juce::TemporaryFile temporary (".dir");
    const auto directory = temporary.getFile();
    directory.createDirectory();

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        const auto file = writeResponse (directory, "cab" + juce::String (slot), 0.25);
        const auto result = plugin.loadImpulseResponse (slot, file);
        REQUIRE (result.wasOk());
    }

    BENCHMARK_ADVANCED ("processBlock, four cabinets")
    (Catch::Benchmark::Chronometer meter)
    {
        meter.measure ([&] (int)
        {
            work.makeCopyOf (source);
            plugin.processBlock (work, midi);
            return work.getSample (0, 0);
        });
    };
}

TEST_CASE ("UI frame performance")
{
    // Repainting is what makes an interface feel cheap or expensive. This is the
    // whole editor; in practice only the part that changed repaints, but if the
    // whole thing is slow then so is a resize.
    PluginProcessor plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());

    juce::Image canvas (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);

    BENCHMARK ("Editor repaint")
    {
        juce::Graphics g (canvas);
        editor->paintEntireComponent (g, true);
        return canvas.getWidth();
    };
}
