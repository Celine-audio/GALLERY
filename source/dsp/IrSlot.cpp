#include "IrSlot.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace
{
    /** Long enough that a polarity flip walking through zero is a fade rather than a
        step, short enough that unmuting feels like a switch rather than a swell. */
    constexpr double gainRampSeconds = 0.02;

    /** How long the alignment delay takes to arrive where it is being dragged.

        Short enough to follow the hand, long enough that the read pointer slides rather
        than jumps -- moving a delay continuously is a change of pitch for as long as it
        moves, which is what makes a delay being dragged sound like one thing moving
        rather than two things cutting between each other. */
    constexpr double alignRampSeconds = 0.05;

    /** No headroom is needed under the interpolator, and that is worth knowing before
        anybody adds some. JUCE's delay line writes to *decreasing* indices, so the four
        samples a third-order Lagrange reads are the newest and three older ones -- all
        real history, at every delay including none. At a delay of exactly nothing the
        fractional part is zero, which makes the interpolation return the newest sample
        untouched: a slot with the control at rest passes through bit for bit.
    */
}

//==============================================================================
void IrSlot::prepare (double sampleRate, int numChannels, int maximumBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = juce::jmax (1, numChannels);

    // The longest response any slot will keep, which is the top resolution tier -- not
    // the longest *file* the loader accepts. Everything past the tier is dropped before
    // it reaches the engine, so preparing for the file limit would be several times the
    // ring for a tail nothing will ever put in it. Alignment needs no room here at all:
    // it is a delay line, downstream of the convolution.
    const auto longest = (int) std::ceil (maximumResponseSeconds * rate) + 1;

    convolver.prepare (channels, longest);

    const auto furthest = (int) std::ceil ((double) maximumAlignMs * 0.001 * rate) + 4;

    alignDelay.setMaximumDelayInSamples (furthest);
    alignDelay.prepare ({ rate, (juce::uint32) juce::jmax (1, maximumBlockSize),
                          (juce::uint32) channels });
    alignDelay.reset();

    alignSamples.reset (rate, alignRampSeconds);
    alignSamples.setCurrentAndTargetValue (0.0f);

    lowCut.prepare (rate, channels);
    highCut.prepare (rate, channels);

    for (auto* ramp : { &gain, &panLeft, &panRight })
        ramp->reset (rate, gainRampSeconds);

    spectrumDb.assign ((size_t) spectrumPoints, -120.0f);

    // The response was resampled to the old rate, and the engine has just been resized
    // out from under whatever it was holding.
    refresh();
}

void IrSlot::reset() noexcept
{
    convolver.reset();
    alignDelay.reset();
    lowCut.reset();
    highCut.reset();
}

//==============================================================================
juce::Result IrSlot::loadFrom (const juce::File& file, ImpulseResponse::Side side)
{
    const auto result = response.loadFrom (file, side);

    if (result.wasOk())
        rebuild();

    return result;
}

void IrSlot::unload()
{
    response.clear();
    rebuild();
}

void IrSlot::setShaping (Shaping newShaping)
{
    if (newShaping == shaping)
        return;

    shaping = newShaping;
    rebuild();
}

void IrSlot::refresh()
{
    rebuild();
}

double IrSlot::getShapedSeconds() const noexcept
{
    if (rate <= 0.0)
        return 0.0;

    return (double) shapedResponse.getNumSamples() / rate;
}

//==============================================================================
void IrSlot::rebuild()
{
    if (! isLoaded())
    {
        shapedResponse.setSize (0, 0);
        std::fill (spectrumDb.begin(), spectrumDb.end(), -120.0f);

        // The engine keeps the response it had and simply stops being listened to:
        // `active` false fades the slot's gain out, and once that lands nothing calls
        // the convolver again.
        //
        // Handing it an empty response instead sounds tidier and clicks. The engine
        // walks between filters in eight steps at frame boundaries, which is inaudible
        // when the two are alike and a twelve per cent jump when one is silence.
        active.store (false);
        return;
    }

    // Whether the engine has been sitting idle. A slot that has stopped being called
    // holds input from whenever it stopped, and a response loaded on top of that
    // convolves it -- so a cabinet dropped into a slot cleared a minute ago would arrive
    // carrying a ghost of what was playing then.
    const auto wasIdle = ! active.load();

    const auto keep = juce::jmax (1, (int) std::ceil (shaping.lengthSeconds * rate));

    shapedResponse = response.atSampleRate (rate);
    truncate (shapedResponse, keep);

    // Measured before it is levelled, because levelling is read off the measurement:
    // levelToPeak shifts the curve and returns the gain that does the same to the
    // buffer, so the graph goes on showing what is heard.
    analyse (shapedResponse);
    shapedResponse.applyGain (levelToPeak());

    convolver.setImpulseResponse (shapedResponse, wasIdle);

    if (wasIdle)
        flushDelay.store (true);

    // Last, once there is something behind it for the audio thread to find.
    active.store (true);
}

//==============================================================================
void IrSlot::truncate (juce::AudioBuffer<float>& buffer, int keep)
{
    if (buffer.getNumSamples() <= keep)
        return;

    // Faded out rather than cut off. A cabinet capture has stopped by 2048 samples and
    // would not notice, but a room does not -- and a response that ends on a non-zero
    // sample is a step convolved into every transient that goes through it, which is
    // heard as a click on the attack rather than as a shortened tail.
    const auto fade = juce::jmin (keep / 8, 512);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int i = 0; i < fade; ++i)
        {
            const auto phase = juce::MathConstants<float>::pi * (float) i / (float) fade;
            samples[keep - fade + i] *= 0.5f + 0.5f * std::cos (phase);
        }
    }

    buffer.setSize (buffer.getNumChannels(), keep, true, true, true);
}

//==============================================================================
float IrSlot::levelToPeak()
{
    // Convolution sums a tap per sample, so a raw capture is not a filter at unity --
    // it is one with however much gain its own length and level add up to, routinely
    // twenty or thirty decibels for a cabinet.
    //
    // Levelled so the loudest frequency sits at 0 dB, which is what the loaders this is
    // compared against do and what makes a cabinet a filter that can only take away.
    // Normalising total energy instead sounds more principled and comes out ten
    // decibels hot: a cabinet has nothing above six kilohertz, so three quarters of the
    // spectrum being near silent drags the average down and the compensating gain up.
    //
    // Read off the measured curve rather than a raw transform, so a single narrow
    // resonance cannot decide the level of the whole cabinet.
    const auto loudest = spectrumDb.empty()
                             ? 0.0f
                             : *std::max_element (spectrumDb.begin(), spectrumDb.end());

    if (loudest <= -120.0f)
        return 1.0f;

    for (auto& level : spectrumDb)
        level = juce::jmax (-120.0f, level - loudest);

    return juce::Decibels::decibelsToGain (-loudest);
}

//==============================================================================
void IrSlot::analyse (const juce::AudioBuffer<float>& shaped)
{
    std::fill (spectrumDb.begin(), spectrumDb.end(), -120.0f);

    const auto numSamples = shaped.getNumSamples();

    if (numSamples <= 0 || rate <= 0.0)
        return;

    // Padded well past the response, which is what makes the curve smooth rather than
    // stepped -- see LogSpectrum. Twice its length stops the transform folding the tail
    // onto the head; eight times samples the result densely enough to draw a line
    // through at the bottom of a logarithmic axis.
    const auto size = LogSpectrum::fftSizeFor (numSamples);
    const auto bins = size / 2;

    // Kept between rebuilds: the twiddle tables and the buffer are both sized for the
    // longest response a slot holds, and a tier change would otherwise throw away and
    // rebuild the pair every time.
    if (analysisFft == nullptr || analysisFftSize != size)
    {
        analysisFft = std::make_unique<juce::dsp::FFT> ((int) std::log2 ((double) size));
        analysisFftSize = size;
    }

    analysisScratch.assign ((size_t) size * 2, 0.0f);

    // Averaged to mono: a stereo capture drawn as two traces would be eight curves for
    // four slots, which is not a comparison anybody can read.
    for (int channel = 0; channel < shaped.getNumChannels(); ++channel)
    {
        const auto* samples = shaped.getReadPointer (channel);

        for (int i = 0; i < juce::jmin (numSamples, size); ++i)
            analysisScratch[(size_t) i] += samples[i] / (float) shaped.getNumChannels();
    }

    analysisFft->performRealOnlyForwardTransform (analysisScratch.data());

    // Interleaved complex, one pair per bin, collapsed to a magnitude per bin.
    analysisMagnitudes.assign ((size_t) bins + 1, 0.0f);

    for (int bin = 0; bin <= bins; ++bin)
    {
        const auto re = analysisScratch[(size_t) bin * 2];
        const auto im = analysisScratch[(size_t) bin * 2 + 1];

        analysisMagnitudes[(size_t) bin] = std::sqrt (re * re + im * im);
    }

    LogSpectrum::sample (analysisMagnitudes.data(), bins + 1, rate / (double) size, spectrumDb);

    // Deliberately no smoothing pass along the points: the fractional-octave averaging
    // LogSpectrum does is already the resolution this curve is meant to have, and
    // blurring it again takes out the detail the cabinet is being read for. The live
    // output trace behind this one is smoothed, in the processor, because its ripple is
    // measurement noise with nothing underneath it.
    //
    // Left absolute. What refers it to anything is levelToPeak.
}

//==============================================================================
void IrSlot::delay (juce::AudioBuffer<float>& block, int numChannels, int numSamples) noexcept
{
    // Every sample pushed is a sample popped, always. The line keeps a read position of
    // its own and advances it on the pop, so a block that only pushed would leave the
    // two walking apart -- and the delay would then be whatever the drift had reached
    // rather than what was asked for.
    if (! alignSamples.isSmoothing())
    {
        const auto held = alignSamples.getCurrentValue();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = block.getWritePointer (channel);

            for (int i = 0; i < numSamples; ++i)
            {
                alignDelay.pushSample (channel, samples[i]);
                samples[i] = alignDelay.popSample (channel, held, true);
            }
        }

        return;
    }

    // Per sample, not per block. The read position is what a delay *is*, so stepping it
    // between blocks would be the same discontinuity the rebuild used to be -- and this
    // control's whole job is being dragged slowly while somebody listens.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* samples = block.getWritePointer (channel);

        // A copy of the smoother per channel, so both walk the same ramp; the real one
        // is advanced once, below.
        auto walk = alignSamples;

        for (int i = 0; i < numSamples; ++i)
        {
            alignDelay.pushSample (channel, samples[i]);
            samples[i] = alignDelay.popSample (channel, walk.getNextValue(), true);
        }
    }

    alignSamples.skip (numSamples);
}

//==============================================================================
void IrSlot::setLevels (const Levels& levels) noexcept
{
    const auto linear = juce::jlimit (0.0f, 1.0f, levels.weight);
    const auto signed_ = levels.inverted ? -linear : linear;

    gain.setTargetValue (signed_);

    // Equal power, referred to the centre rather than the corners. The textbook
    // sine/cosine pair puts 0.707 a side at centre, which is right for placing a mono
    // source in a stereo field and wrong here: a cabinet sits centred until somebody
    // moves it, so every slot at its default would be three decibels down for no
    // visible reason. Scaling by root two makes the centre unity and keeps the law
    // equal-power either side of it.
    const auto angle = (juce::jlimit (-1.0f, 1.0f, levels.pan) + 1.0f) * 0.25f
                     * juce::MathConstants<float>::pi;

    constexpr auto centreGain = juce::MathConstants<float>::sqrt2;

    panLeft.setTargetValue (std::cos (angle) * centreGain);
    panRight.setTargetValue (std::sin (angle) * centreGain);

    alignSamples.setTargetValue (juce::jlimit (0.0f, maximumAlignMs, levels.alignMs)
                                 * 0.001f * (float) rate);

    // Ramped, not set: recomputing a cascade between one block and the next steps the
    // filter, and a step in an IIR with energy in its state is heard as a crackle
    // rather than as a change of tone. See CutFilter::setTarget.
    lowCut.setTarget (levels.lowCutHz, levels.lowCutSlope);
    highCut.setTarget (levels.highCutHz, levels.highCutSlope);
}

void IrSlot::process (const juce::AudioBuffer<float>& input,
                      juce::AudioBuffer<float>& scratch,
                      juce::AudioBuffer<float>& output) noexcept
{
    const auto numSamples = input.getNumSamples();
    const auto numChannels = juce::jmin (channels, input.getNumChannels(),
                                         scratch.getNumChannels(), output.getNumChannels());

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Emptied here rather than by the rebuild that asked for it: this line is the audio
    // thread's, and the message thread resetting it would be writing what this one is
    // reading.
    if (flushDelay.exchange (false))
        alignDelay.reset();

    // An emptied slot goes quiet by fading, not by stopping: dropping out the instant
    // the file is cleared steps the output. Once the fade has landed there is nothing
    // left to add and the work can be skipped -- but not before.
    //
    // A muted slot keeps convolving regardless. Its engine holds the input history the
    // response is convolved against, and one that stopped listening would come back
    // missing however much of the past its response reaches into.
    if (! active.load())
    {
        gain.setTargetValue (0.0f);

        if (! gain.isSmoothing())
        {
            panLeft.skip (numSamples);
            panRight.skip (numSamples);
            return;
        }
    }

    for (int channel = 0; channel < numChannels; ++channel)
        scratch.copyFrom (channel, 0, input, juce::jmin (channel, input.getNumChannels() - 1),
                          0, numSamples);

    juce::AudioBuffer<float> block (scratch.getArrayOfWritePointers(), numChannels, 0, numSamples);

    convolver.process (block);

    lowCut.process (block);
    highCut.process (block);

    delay (block, numChannels, numSamples);

    // Mono or stereo, guaranteed by isBusesLayoutSupported. Pan has no meaning on one
    // channel and no agreed meaning on more than two, so both are handled by not
    // offering it rather than by inventing a law for them.
    jassert (numChannels <= 2);

    if (numChannels == 1)
    {
        auto* samples = block.getWritePointer (0);
        auto* out = output.getWritePointer (0);

        for (int i = 0; i < numSamples; ++i)
            out[i] += samples[i] * gain.getNextValue();

        panLeft.skip (numSamples);
        panRight.skip (numSamples);
        return;
    }

    auto* left = block.getWritePointer (0);
    auto* right = block.getWritePointer (1);
    auto* outLeft = output.getWritePointer (0);
    auto* outRight = output.getWritePointer (1);

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = gain.getNextValue();

        outLeft[i] += left[i] * g * panLeft.getNextValue();
        outRight[i] += right[i] * g * panRight.getNextValue();
    }
}
