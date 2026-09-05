#include "CutFilter.h"

#include <cmath>
#include <complex>

namespace
{
    /** How close to the end of its travel a control has to be before the filter is
        treated as off. A hair rather than exactly, because a parameter that has been
        automated to its end can land a rounding error short of it, and a filter that
        stays on at 19999.6 Hz is one that never quite gets out of the way. */
    constexpr float endTolerance = 1.0e-3f;
}

//==============================================================================
double CutFilter::sectionQ (int order, int index) noexcept
{
    // The pole pairs of a Butterworth of this order sit at equal angles round the
    // unit circle, and this is their Q. Order 2 gives the familiar 0.7071; order 4
    // gives 0.5412 and 1.3066, which is what makes two biquads in series a fourth
    // order Butterworth rather than a pair of resonant peaks.
    const auto angle = juce::MathConstants<double>::pi * (2.0 * (double) index + 1.0)
                     / (2.0 * (double) order);

    return 1.0 / (2.0 * std::sin (angle));
}

//==============================================================================
void CutFilter::prepare (double sampleRate, int numChannels) noexcept
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    channels = juce::jlimit (1, maximumChannels, numChannels);

    // Long enough to smooth a fast drag, short enough that the filter still feels like
    // it is following the hand rather than catching up with it.
    ramp.reset (rate, 0.03);
    ramp.setCurrentAndTargetValue (frequency);

    engagement.reset (rate, engageSeconds);

    design();
    engagement.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
    reset();
}

void CutFilter::reset() noexcept
{
    for (auto& section : sections)
        for (auto& state : section.state)
            state = {};
}

//==============================================================================
void CutFilter::setParameters (float frequencyHz, int slopeDbPerOctave) noexcept
{
    const auto clamped = juce::jlimit (lowestHz, highestHz, frequencyHz);

    if (juce::approximatelyEqual (clamped, frequency) && slopeDbPerOctave == slope)
        return;

    frequency = clamped;
    slope = slopeDbPerOctave;

    ramp.setCurrentAndTargetValue (clamped);

    design();

    // The engagement fade snaps too. This is the entry that means "be this now", and a
    // filter told to be off which then spent eight milliseconds fading out would put
    // that fade at the head of every buffer the mix display filters -- drawn as a
    // cabinet whose top end droops, which is a cut nobody asked for.
    engagement.setCurrentAndTargetValue (engaged ? 1.0f : 0.0f);
}

void CutFilter::setTarget (float frequencyHz, int slopeDbPerOctave) noexcept
{
    const auto clamped = juce::jlimit (lowestHz, highestHz, frequencyHz);

    // A change of slope is a change of *order* -- a different number of sections, with
    // different Qs. There is nothing to interpolate between, so it happens at once and
    // the frequency goes with it.
    if (slopeDbPerOctave != slope)
    {
        setParameters (clamped, slopeDbPerOctave);
        return;
    }

    if (! juce::approximatelyEqual (clamped, ramp.getTargetValue()))
        ramp.setTargetValue (clamped);
}

void CutFilter::design() noexcept
{
    // Off at the end of its travel. The two ends mean opposite things -- a low cut is
    // out of the way at the bottom, a high cut at the top -- which is the whole
    // difference between the two instances of this class.
    engaged = kind == Kind::lowCut ? frequency > lowestHz + endTolerance
                                   : frequency < highestHz - endTolerance;

    // And off above Nyquist, where a low-pass has nothing left to remove and the
    // bilinear transform's warping would put the corner somewhere it was not asked
    // for. Not a special case so much as the same "nothing to do" as above.
    const auto nyquist = rate * 0.5;
    const auto corner = juce::jlimit (1.0, nyquist * 0.99, (double) frequency);

    if (kind == Kind::highCut && corner >= nyquist * 0.98)
        engaged = false;

    engagement.setTargetValue (engaged ? 1.0f : 0.0f);

    // The coefficients are built whether or not the cut is engaged, because a cut that
    // has just been switched off is still being faded out through them.
    const auto order = juce::jlimit (1, maximumOrder, slope / 6);
    numSections = (order + 1) / 2;

    auto section = 0;

    // An odd order has one real pole, which is a first-order section rather than a
    // biquad. Taking it first keeps the pairs' indices matching the formula.
    if (order % 2 == 1)
        designOnePole (sections[(size_t) section++], corner);

    for (int pair = 0; pair < order / 2; ++pair)
        designBiquad (sections[(size_t) section++], corner, sectionQ (order, pair));
}

void CutFilter::designBiquad (Section& s, double corner, double q) const noexcept
{
    // RBJ's cookbook, normalised by a0.
    const auto w0 = juce::MathConstants<double>::twoPi * corner / rate;
    const auto cosine = std::cos (w0);
    const auto alpha = std::sin (w0) / (2.0 * q);

    const auto a0 = 1.0 + alpha;

    s.a1 = (-2.0 * cosine) / a0;
    s.a2 = (1.0 - alpha) / a0;

    if (kind == Kind::lowCut)
    {
        const auto b = (1.0 + cosine) * 0.5;
        s.b0 = b / a0;
        s.b1 = (-(1.0 + cosine)) / a0;
        s.b2 = b / a0;
    }
    else
    {
        const auto b = (1.0 - cosine) * 0.5;
        s.b0 = b / a0;
        s.b1 = (1.0 - cosine) / a0;
        s.b2 = b / a0;
    }
}

void CutFilter::designOnePole (Section& s, double corner) const noexcept
{
    // Bilinear, with the corner pre-warped by the tangent so it lands where it was
    // asked for rather than where the transform would otherwise put it.
    const auto k = std::tan (juce::MathConstants<double>::pi * corner / rate);
    const auto norm = 1.0 / (k + 1.0);

    s.a1 = (k - 1.0) * norm;
    s.a2 = 0.0;
    s.b2 = 0.0;

    if (kind == Kind::lowCut)
    {
        s.b0 = norm;
        s.b1 = -norm;
    }
    else
    {
        s.b0 = k * norm;
        s.b1 = k * norm;
    }
}

//==============================================================================
void CutFilter::process (juce::AudioBuffer<float>& buffer) noexcept
{
    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    const auto silent = ! engaged && ! engagement.isSmoothing()
                     && engagement.getCurrentValue() <= 0.0f;

    // Out of the way and settled there. The state is cleared rather than left where it
    // stopped, so that whatever it held is not still there the next time the cut is
    // moved off the end of its travel.
    if (silent && ! ramp.isSmoothing())
    {
        reset();
        return;
    }

    if (! ramp.isSmoothing())
    {
        processRun (buffer, 0, numSamples);
        return;
    }

    // Moving: the block is walked in short runs, with the coefficients rebuilt from
    // where the ramp has got to at the start of each. Sixteen rebuilds across a
    // 512-sample block rather than one is what turns a step into a slide.
    for (int start = 0; start < numSamples;)
    {
        const auto run = juce::jmin (updateInterval, numSamples - start);

        frequency = ramp.skip (run);
        design();

        processRun (buffer, start, run);
        start += run;
    }
}

void CutFilter::processRun (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    if (numSections <= 0 || numSamples <= 0)
        return;

    if (engagement.isSmoothing())
    {
        processBlendedRun (buffer, start, numSamples);
        return;
    }

    if (engagement.getCurrentValue() <= 0.0f)
        return;

    const auto numChannels = juce::jmin (channels, buffer.getNumChannels());

    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel) + start;

        for (int index = 0; index < numSections; ++index)
        {
            auto& s = sections[(size_t) index];
            auto& state = s.state[(size_t) channel];

            // Transposed direct form II: two state words per section, and the one that
            // keeps its arithmetic well conditioned at the low corner frequencies a
            // low cut spends its life at. In double, for the same reason -- a
            // fourth-order high-pass at 30 Hz against a 96 kHz rate has poles close
            // enough to the unit circle that single precision visibly drifts.
            for (int i = 0; i < numSamples; ++i)
            {
                const auto in = (double) samples[i];
                const auto out = s.b0 * in + state.s1;

                state.s1 = s.b1 * in - s.a1 * out + state.s2;
                state.s2 = s.b2 * in - s.a2 * out;

                samples[i] = (float) out;
            }
        }
    }
}

void CutFilter::processBlendedRun (juce::AudioBuffer<float>& buffer, int start,
                                   int numSamples) noexcept
{
    const auto numChannels = juce::jmin (channels, buffer.getNumChannels());
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel) + start;

        // A copy of the smoother per channel, so every channel walks the same fade; the
        // real one is advanced once, below. Copied rather than re-aimed: setting a
        // target again restarts the ramp's own countdown, which would leave each
        // channel fading over the full period from wherever it had already got to.
        auto fade = engagement;

        // Sample-outer here, unlike the settled path: the dry sample has to survive
        // until the cascade has finished with it, which it cannot if each section walks
        // the whole run in turn.
        for (int i = 0; i < numSamples; ++i)
        {
            const auto dry = (double) samples[i];
            auto x = dry;

            for (int index = 0; index < numSections; ++index)
            {
                auto& s = sections[(size_t) index];
                auto& state = s.state[(size_t) channel];

                const auto out = s.b0 * x + state.s1;

                state.s1 = s.b1 * x - s.a1 * out + state.s2;
                state.s2 = s.b2 * x - s.a2 * out;

                x = out;
            }

            const auto amount = (double) fade.getNextValue();
            samples[i] = (float) (dry + (x - dry) * amount);
        }
    }

    engagement.skip (numSamples);

    // Faded all the way out. The state is cleared here rather than left where it
    // stopped, so that whatever it was holding is not still there the next time the cut
    // is moved off the end of its travel -- by then it would be a snapshot of a signal
    // that had long since gone.
    if (! engaged && ! engagement.isSmoothing() && engagement.getCurrentValue() <= 0.0f)
        reset();
}

//==============================================================================
float CutFilter::magnitudeAt (float frequencyHz) const noexcept
{
    if (! engaged || numSections <= 0)
        return 1.0f;

    const auto w = juce::MathConstants<double>::twoPi
                 * juce::jlimit (0.0, rate * 0.5, (double) frequencyHz) / rate;

    const std::complex<double> z { std::cos (-w), std::sin (-w) };
    const auto z2 = z * z;

    auto magnitude = 1.0;

    for (int index = 0; index < numSections; ++index)
    {
        const auto& s = sections[(size_t) index];

        const auto numerator = s.b0 + s.b1 * z + s.b2 * z2;
        const auto denominator = 1.0 + s.a1 * z + s.a2 * z2;

        magnitude *= std::abs (numerator) / juce::jmax (1.0e-12, std::abs (denominator));
    }

    return (float) magnitude;
}
