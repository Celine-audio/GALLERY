#include "MixSpectrum.h"

#include "Lagrange.h"

#include <algorithm>
#include <cmath>

namespace
{
    /** How much of a response the mix is measured over.

        One size for all four, because they have to share a bin grid before they can be
        added together at all -- and the longest any slot will keep, so that a cabinet
        at the highest resolution is measured whole rather than truncated into a
        different response than the one being heard. */
    int mixLengthFor (double rate) noexcept
    {
        return (int) std::ceil ((IrSlot::maximumResponseSeconds
                                 + (double) IrSlot::maximumAlignMs * 0.001) * rate) + 1;
    }

    /** One cabinet added into the sum where its delay line puts it, read the way that
        line reads -- see Lagrange, which is where the two agree.

        Rounded to whole samples instead, the notches would jump from one place to the
        next as the knob moved, which is the one thing this curve exists to show moving
        smoothly. */
    void addDelayed (float* destination, size_t destinationSize,
                     const float* source, size_t sourceSize,
                     double delaySamples, float gain) noexcept
    {
        const auto whole = (long long) std::floor (delaySamples);
        const auto fraction = (float) (delaySamples - (double) whole);

        if (whole == 0 && fraction < 1.0e-6f)
        {
            juce::FloatVectorOperations::addWithMultiply (
                destination, source, gain, (int) juce::jmin (destinationSize, sourceSize));
            return;
        }

        // Destination i takes source (i - delay), which sits between i - whole - 1 and
        // i - whole. `fraction` of the way *back* from the later of those is 1 - it of
        // the way forward from the earlier, which is what the kernel is written for --
        // and getting that turn round wrong puts every cabinet one sample early.
        const auto taps = Lagrange::weights (1.0f - fraction);

        const auto first = juce::jmax (0LL, whole - 2);
        const auto last = juce::jmin ((long long) destinationSize,
                                      (long long) sourceSize + whole + 2LL);

        for (auto i = first; i < last; ++i)
        {
            const auto base = i - whole - 2;
            auto sum = 0.0f;

            for (int k = 0; k < 4; ++k)
            {
                const auto index = base + k;

                if (index >= 0 && index < (long long) sourceSize)
                    sum += taps[(size_t) k] * source[(size_t) index];
            }

            destination[(size_t) i] += gain * sum;
        }
    }
}

//==============================================================================
bool MixSpectrum::Cabinet::matches (std::uint32_t v, float low, int lowSlope,
                                    float high, int highSlope) const noexcept
{
    return valid
        && version == v
        && juce::approximatelyEqual (lowCutHz, low)
        && juce::approximatelyEqual (highCutHz, high)
        && lowCutSlope == lowSlope
        && highCutSlope == highSlope;
}

//==============================================================================
void MixSpectrum::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;

    // Padded well past the response for the same reason IrSlot's own measurement is:
    // zero-padding is exact interpolation of the spectrum, and it is what turns a
    // handful of bins across the bottom octaves into enough of them to draw a line
    // through. See LogSpectrum.
    const auto longest = mixLengthFor (rate);

    fftSize = LogSpectrum::fftSizeFor (longest);
    fft = std::make_unique<juce::dsp::FFT> ((int) std::log2 ((double) fftSize));

    summed.assign ((size_t) fftSize, 0.0f);
    transform.assign ((size_t) fftSize * 2, 0.0f);
    magnitudes.assign ((size_t) fftSize / 2 + 1, 0.0f);

    filterScratch.setSize (1, longest, false, true, true);

    for (auto& cabinet : cabinets)
    {
        cabinet.filtered.assign ((size_t) longest, 0.0f);
        cabinet.valid = false;
    }
}

//==============================================================================
void MixSpectrum::setResponse (int slot, std::uint32_t version,
                               const juce::AudioBuffer<float>& shaped,
                               float lowCutHz, int lowCutSlope,
                               float highCutHz, int highCutSlope)
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots) || fft == nullptr)
        return;

    auto& cabinet = cabinets[(size_t) slot];

    if (cabinet.matches (version, lowCutHz, lowCutSlope, highCutHz, highCutSlope))
        return;

    refilter (cabinet, shaped, lowCutHz, lowCutSlope, highCutHz, highCutSlope);

    cabinet.version = version;
    cabinet.lowCutHz = lowCutHz;
    cabinet.highCutHz = highCutHz;
    cabinet.lowCutSlope = lowCutSlope;
    cabinet.highCutSlope = highCutSlope;
    cabinet.valid = shaped.getNumSamples() > 0;
}

bool MixSpectrum::needsResponse (int slot, std::uint32_t version, float lowCutHz, int lowCutSlope,
                                 float highCutHz, int highCutSlope) const
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return false;

    return ! cabinets[(size_t) slot].matches (version, lowCutHz, lowCutSlope,
                                              highCutHz, highCutSlope);
}

void MixSpectrum::clearResponse (int slot)
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return;

    auto& cabinet = cabinets[(size_t) slot];

    // Already empty is nothing to empty. Without this an unloaded slot cost a fill of
    // the whole padded buffer on every frame, for a buffer that was already zeros.
    if (! cabinet.valid)
        return;

    std::fill (cabinet.filtered.begin(), cabinet.filtered.end(), 0.0f);
    cabinet.valid = false;
}

void MixSpectrum::refilter (Cabinet& cabinet, const juce::AudioBuffer<float>& shaped,
                            float lowCutHz, int lowCutSlope, float highCutHz, int highCutSlope)
{
    std::fill (cabinet.filtered.begin(), cabinet.filtered.end(), 0.0f);

    const auto available = juce::jmin (shaped.getNumSamples(), (int) cabinet.filtered.size());

    if (available <= 0 || shaped.getNumChannels() <= 0)
        return;

    // Folded to mono on the way in, the same as the per-cabinet measurement: this is a
    // mono view of a filter, and a stereo capture drawn as two curves would be eight
    // for four cabinets.
    filterScratch.clear();
    auto* scratch = filterScratch.getWritePointer (0);

    for (int channel = 0; channel < shaped.getNumChannels(); ++channel)
    {
        const auto* samples = shaped.getReadPointer (channel);

        for (int i = 0; i < available; ++i)
            scratch[i] += samples[i] / (float) shaped.getNumChannels();
    }

    // Run over the whole padded buffer rather than only the response's own length. The
    // cuts are deliberately *not* baked into the response the audio thread convolves --
    // a steep one rings for far longer than a cabinet capture lasts, and folding it in
    // would truncate its own tail. Here there is room for it to ring into, so the curve
    // shows the filter the audio is actually running rather than a clipped copy of it.
    juce::AudioBuffer<float> whole (filterScratch.getArrayOfWritePointers(), 1, 0,
                                    (int) cabinet.filtered.size());

    CutFilter low { CutFilter::Kind::lowCut };
    CutFilter high { CutFilter::Kind::highCut };

    low.prepare (rate, 1);
    high.prepare (rate, 1);
    low.setParameters (lowCutHz, lowCutSlope);
    high.setParameters (highCutHz, highCutSlope);

    low.process (whole);
    high.process (whole);

    std::copy (scratch, scratch + cabinet.filtered.size(), cabinet.filtered.begin());
}

//==============================================================================
bool MixSpectrum::summarise (int slot, std::vector<ImpulseResponse::Column>& columns,
                             int numColumns, double seconds) const
{
    columns.assign ((size_t) juce::jmax (0, numColumns), ImpulseResponse::Column {});

    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots) || numColumns <= 0)
        return false;

    const auto& cabinet = cabinets[(size_t) slot];

    if (! cabinet.valid)
        return false;

    const auto span = juce::jmin ((int) cabinet.filtered.size(),
                                  (int) std::ceil (seconds * rate));

    if (span <= 0)
        return false;

    for (int column = 0; column < numColumns; ++column)
    {
        const auto begin = span * column / numColumns;
        const auto end = juce::jmax (begin + 1, span * (column + 1) / numColumns);

        // Seeded from the first sample in the column rather than from zero. Seeding at
        // zero makes every column straddle the axis -- a positive sample summarises as
        // the range 0 to itself -- which at one sample per column draws the whole
        // waveform as bars standing on the centre line rather than as a line.
        auto low = cabinet.filtered[(size_t) juce::jlimit (0, span - 1, begin)];
        auto high = low;

        for (int i = begin; i < end && i < span; ++i)
        {
            low = juce::jmin (low, cabinet.filtered[(size_t) i]);
            high = juce::jmax (high, cabinet.filtered[(size_t) i]);
        }

        columns[(size_t) column] = { low, high };
    }

    return true;
}

//==============================================================================
bool MixSpectrum::compute (const Parameters::BlendWeights& weights,
                           const ParamID::PerSlot<bool>& inverted,
                           const ParamID::PerSlot<double>& alignSeconds,
                           std::vector<float>& destinationDb)
{
    if (fft == nullptr)
        return false;

    auto anything = false;

    for (const auto& cabinet : cabinets)
        anything |= cabinet.valid;

    if (! anything)
        return false;

    std::fill (summed.begin(), summed.end(), 0.0f);

    // Added as waveforms, which is the whole point: where two cabinets disagree they
    // cancel, and those notches are what the Align control is for.
    for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
    {
        const auto& cabinet = cabinets[slot];

        if (! cabinet.valid)
            continue;

        const auto share = weights[slot] * (inverted[slot] ? -1.0f : 1.0f);

        if (std::abs (share) < 1.0e-7f)
            continue;

        addDelayed (summed.data(), summed.size(),
                    cabinet.filtered.data(), cabinet.filtered.size(),
                    juce::jmax (0.0, alignSeconds[slot]) * rate, share);
    }

    std::fill (transform.begin(), transform.end(), 0.0f);
    std::copy (summed.begin(), summed.end(), transform.begin());

    fft->performRealOnlyForwardTransform (transform.data());

    const auto bins = fftSize / 2;

    for (int bin = 0; bin <= bins; ++bin)
    {
        const auto re = transform[(size_t) bin * 2];
        const auto im = transform[(size_t) bin * 2 + 1];

        magnitudes[(size_t) bin] = std::sqrt (re * re + im * im);
    }

    LogSpectrum::sample (magnitudes.data(), bins + 1, rate / (double) fftSize, destinationDb);

    return true;
}
