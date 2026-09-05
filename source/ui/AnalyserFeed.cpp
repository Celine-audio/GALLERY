#include "AnalyserFeed.h"

#include "../PluginProcessor.h"
#include "AnalyserGraph.h"
#include "Theme.h"

#include <cmath>

using namespace Celine;

namespace
{
    /** A response is levelled so its loudest frequency is exactly 0 dB, which is exactly
        the top of the graph -- so drawn where it sits, a cabinet turned up by the blend
        has nowhere to go and flattens itself against the ceiling. */
    constexpr float displayHeadroomDb = -6.0f;
}

//==============================================================================
AnalyserFeed::AnalyserFeed (PluginProcessor& processor, AnalyserGraph& graphToFeed)
    : processorRef (processor),
      graph (graphToFeed),
      slotParameters (Parameters::bindSlots (processor.getAPVTS()))
{
}

void AnalyserFeed::prepare (double sampleRate)
{
    mixSpectrum.prepare (sampleRate);

    for (auto& filter : displayLowCut)
        filter.prepare (sampleRate, 1);

    for (auto& filter : displayHighCut)
        filter.prepare (sampleRate, 1);
}

//==============================================================================
bool AnalyserFeed::hasChanged()
{
    const auto now = signature();

    if (hasSignature && now == lastSignature)
        return false;

    lastSignature = now;
    hasSignature = true;

    return true;
}

AnalyserFeed::Signature AnalyserFeed::signature() const
{
    Signature values {};
    size_t next = 0;

    values[next++] = (float) processorRef.getRebuildCount();
    values[next++] = graph.getView() == AnalyserGraph::View::spectrum ? 0.0f : 1.0f;
    values[next++] = graph.isZoomed() ? 1.0f : 0.0f;
    values[next++] = graph.isSplit() ? 1.0f : 0.0f;
    values[next++] = (float) graph.getWidth();

    // The blend moves the mix curve, so the picture depends on it the same way it
    // depends on a cut frequency.
    values[next++] = processorRef.getAPVTS().getRawParameterValue (ParamID::blendX)->load();
    values[next++] = processorRef.getAPVTS().getRawParameterValue (ParamID::blendY)->load();

    for (const auto& p : slotParameters)
        for (const auto* value : { p.pan, p.align, p.lowCut, p.highCut, p.lowCutSlope,
                                   p.highCutSlope, p.phase, p.mute, p.solo })
            values[next++] = value->load();

    jassert (next == values.size());

    return values;
}

//==============================================================================
void AnalyserFeed::refresh()
{
    refreshResponses();

    if (graph.getView() == AnalyserGraph::View::spectrum)
        refreshSpectrum();
    else
        refreshWaveform();
}

void AnalyserFeed::refreshOutput()
{
    if (! graph.isShowingOutput() || graph.getView() != AnalyserGraph::View::spectrum)
        return;

    if (processorRef.copyOutputSpectrumDb (outputScratch))
        graph.getSpectrum().setOutputSpectrum (outputScratch);
}

//==============================================================================
void AnalyserFeed::refreshResponses()
{
    const auto version = processorRef.getRebuildCount();

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        const auto& p = slotParameters[(size_t) slot];

        const auto lowHz = p.lowCut->load();
        const auto highHz = p.highCut->load();
        const auto lowSlope = Parameters::slopeForChoice ((int) p.lowCutSlope->load());
        const auto highSlope = Parameters::slopeForChoice ((int) p.highCutSlope->load());

        // Asked before the response is fetched, not after. Everything else that brings
        // us here -- the blend, alignment, a mute -- leaves the cabinets exactly as they
        // were, and copying four of them out of the processor to be told so is most of
        // a megabyte a frame for nothing.
        if (! mixSpectrum.needsResponse (slot, version, lowHz, lowSlope, highHz, highSlope))
            continue;

        if (! processorRef.copyShapedResponse (slot, responseScratch))
        {
            mixSpectrum.clearResponse (slot);
            continue;
        }

        mixSpectrum.setResponse (slot, version, responseScratch,
                                 lowHz, lowSlope, highHz, highSlope);
    }
}

//==============================================================================
void AnalyserFeed::refreshSpectrum()
{
    refreshMix();

    auto& spectrum = graph.getSpectrum();

    // The four on their own answer "what is each cabinet doing", which is a different
    // question from the one the mix answers -- so they are a button rather than always
    // on, and off by default because the blend is what is being listened to.
    if (! graph.isSplit())
    {
        for (int slot = 0; slot < ParamID::numSlots; ++slot)
            spectrum.clearTrace (slot);

        return;
    }

    const auto soloed = anySoloed();

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        const auto& p = slotParameters[(size_t) slot];

        if (! processorRef.copySpectrumDb (slot, spectrumScratch))
        {
            spectrum.clearTrace (slot);
            continue;
        }

        auto& low = displayLowCut[(size_t) slot];
        auto& high = displayHighCut[(size_t) slot];

        low.setParameters (p.lowCut->load(), Parameters::slopeForChoice ((int) p.lowCutSlope->load()));
        high.setParameters (p.highCut->load(), Parameters::slopeForChoice ((int) p.highCutSlope->load()));

        composedScratch.resize (spectrumScratch.size());

        for (size_t point = 0; point < spectrumScratch.size(); ++point)
        {
            const auto frequency = IrSlot::spectrumFrequency ((int) point);

            // Composed as the curve is drawn: the measurement is rebuilt only when a
            // response changes, where the cuts move continuously.
            const auto cut = juce::Decibels::gainToDecibels (
                low.magnitudeAt (frequency) * high.magnitudeAt (frequency), -120.0f);

            composedScratch[point] = spectrumScratch[point] + cut + displayHeadroomDb;
        }

        // A cabinet nobody is listening to is still drawn, faintly: removing it would
        // take away the thing the blend is being compared against.
        spectrum.setTrace (slot, composedScratch, traceColour (slot, soloed), true);
    }
}

void AnalyserFeed::refreshMix()
{
    // The shares actually being heard, which is the blend with mute and solo worked in
    // -- the same arithmetic the audio thread does, so the curve is the sound.
    auto weights = Parameters::blendWeights (
        processorRef.getAPVTS().getRawParameterValue (ParamID::blendX)->load(),
        processorRef.getAPVTS().getRawParameterValue (ParamID::blendY)->load());

    const auto soloed = anySoloed();
    ParamID::PerSlot<bool> inverted {};
    ParamID::PerSlot<double> alignSeconds {};
    auto total = 0.0f;

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        const auto index = (size_t) slot;
        const auto& p = slotParameters[index];

        const auto heard = processorRef.isSlotLoaded (slot)
                        && ! Parameters::SlotPointers::isOn (p.mute)
                        && (Parameters::SlotPointers::isOn (p.solo) || ! soloed);

        if (! heard)
            weights[index] = 0.0f;

        total += weights[index];
        inverted[index] = Parameters::SlotPointers::isOn (p.phase);
        alignSeconds[index] = (double) p.align->load() * 0.001;
    }

    if (total > 1.0e-6f)
        for (auto& weight : weights)
            weight /= total;

    if (! mixSpectrum.compute (weights, inverted, alignSeconds, mixScratch))
    {
        graph.getSpectrum().setMixTrace ({});
        return;
    }

    for (auto& level : mixScratch)
        level += displayHeadroomDb;

    graph.getSpectrum().setMixTrace (mixScratch);
}

void AnalyserFeed::refreshWaveform()
{
    auto& waveform = graph.getWaveform();

    // One axis for all four, fixed: see MultiWaveformDisplay for why both windows are
    // constants rather than fitted to whatever happens to be loaded.
    const auto span = waveform.isZoomed() ? MultiWaveformDisplay::zoomedSeconds
                                          : MultiWaveformDisplay::windowSeconds;

    waveform.setTimeSpan (span);

    const auto soloed = anySoloed();

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
    {
        const auto& p = slotParameters[(size_t) slot];
        const auto seconds = processorRef.getShapedSeconds (slot);

        if (seconds <= 0.0)
        {
            waveform.clearTrace (slot);
            continue;
        }

        // Where this cabinet arrives. Alignment is a delay line on the audio thread
        // rather than a shift baked into the response, so it is not in the samples --
        // the display has to place them, or the control this view exists for would
        // appear to do nothing.
        const auto start = (double) p.align->load() * 0.001;

        // Only as much as the view still has room for after that, summarised at the
        // width it will be drawn at -- so zooming asks for detail rather than
        // stretching what was already there.
        const auto room = juce::jmax (0.0, span - start);
        const auto shown = juce::jmin (seconds, room);

        // Never more columns than there are samples to fill them, and counted against
        // what this trace actually covers rather than against the window. At twenty
        // milliseconds the plot is already wider than the window is long; a trace
        // pushed back by alignment covers less of it still, and a column asked to
        // summarise less than one sample draws as a staircase.
        const auto samplesShown = (int) std::ceil (shown * processorRef.getSampleRate());
        const auto columns = juce::jlimit (2, waveform.getPlotWidth(), samplesShown);

        if (shown <= 0.0 || ! mixSpectrum.summarise (slot, summaryScratch, columns, shown))
        {
            waveform.clearTrace (slot);
            continue;
        }

        MultiWaveformDisplay::Trace trace;
        trace.columns = summaryScratch;
        trace.seconds = shown;
        trace.startSeconds = start;
        trace.colour = traceColour (slot, soloed);
        trace.inverted = Parameters::SlotPointers::isOn (p.phase);
        trace.visible = true;

        waveform.setTrace (slot, std::move (trace));
    }
}

//==============================================================================
bool AnalyserFeed::anySoloed() const
{
    for (const auto& p : slotParameters)
        if (Parameters::SlotPointers::isOn (p.solo))
            return true;

    return false;
}

juce::Colour AnalyserFeed::traceColour (int slot, bool soloedElsewhere) const
{
    const auto& p = slotParameters[(size_t) slot];

    const auto audible = ! Parameters::SlotPointers::isOn (p.mute)
                      && (Parameters::SlotPointers::isOn (p.solo) || ! soloedElsewhere);

    return audible ? Theme::irSlot (slot) : Theme::irSlot (slot).withAlpha (0.3f);
}
