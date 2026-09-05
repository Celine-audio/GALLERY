#pragma once

#include "../Parameters.h"
#include "CutFilter.h"
#include "ImpulseResponse.h"
#include "IrSlot.h"
#include "LogSpectrum.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

/**
    What the four cabinets add up to, measured as one curve.

    The four traces answer "what is each cabinet doing"; this answers "what am I
    listening to", which is a different question and the one somebody blending has in
    front of them. It is the reason the pad and this display belong together: moving the
    dot changes this curve, and watching it change is how you find the blend.

    **The responses are summed as waveforms, not as curves.** Adding four magnitude
    curves would be easy and would draw a cabinet blend that has none of the notches a
    real one has -- two captures of one speaker a fraction of a millisecond apart cancel
    where they disagree, and those notches are the entire reason the Align control
    exists. A sum of magnitudes cannot show them, so it would draw the one picture that
    makes the alignment controls look pointless.

    The expensive half is the filtering, and it is cached: a slot's response is passed
    through its cuts only when the response or those cuts have actually moved. Dragging
    the pad then costs a weighted sum of four buffers and one transform, which is what
    lets the curve follow the dot rather than catching up after it.
*/
class MixSpectrum
{
public:
    MixSpectrum() = default;

    void prepare (double sampleRate);

    /** Hands one cabinet's shaped response over, together with the cuts it is being
        heard through.

        `version` is whatever the caller uses to mean "this response has been rebuilt";
        together with the four cut settings it decides whether any filtering has to
        happen at all, so this is cheap to call on every frame. */
    void setResponse (int slot, std::uint32_t version, const juce::AudioBuffer<float>& shaped,
                      float lowCutHz, int lowCutSlope, float highCutHz, int highCutSlope);

    /** Whether `setResponse` would do any work with these. Asked before the response is
        copied out of the processor, because that copy is up to fifty thousand samples a
        cabinet and would otherwise run four times over on every frame in which anything
        at all had moved -- the blend and the cuts included, neither of which changes a
        response. */
    bool needsResponse (int slot, std::uint32_t version, float lowCutHz, int lowCutSlope,
                        float highCutHz, int highCutSlope) const;

    void clearResponse (int slot);

    /** A peak-per-column summary of one cabinet as it is actually heard -- through its
        own cuts -- over the first `seconds` of it.

        The waveform view is drawn from this rather than from the raw response, because
        a cut is part of what a cabinet sounds like and a view that ignored one would
        show a bottom end that is not there. False for a slot with nothing in it. */
    bool summarise (int slot, std::vector<ImpulseResponse::Column>& columns,
                    int numColumns, double seconds) const;

    /** The blend, in decibels on LogSpectrum's grid. False when there is nothing
        loaded to blend, which is a curve that should not be drawn rather than a flat
        one at the bottom of the graph.

        `alignSeconds` is where each cabinet arrives, and it is not optional. Alignment
        is a delay line on the audio thread rather than a shift baked into the response,
        so a sum taken at offset zero is a blend nobody is listening to -- and the
        notches it would be missing are the only reason to move the control. */
    bool compute (const Parameters::BlendWeights& weights,
                  const ParamID::PerSlot<bool>& inverted,
                  const ParamID::PerSlot<double>& alignSeconds,
                  std::vector<float>& destinationDb);

private:
    /** One cabinet, already filtered and folded to mono, with what it was made from so
        that making it again can be skipped. */
    struct Cabinet
    {
        std::vector<float> filtered;

        std::uint32_t version = 0;
        float lowCutHz = 0.0f, highCutHz = 0.0f;
        int lowCutSlope = 0, highCutSlope = 0;
        bool valid = false;

        bool matches (std::uint32_t v, float low, int lowSlope, float high, int highSlope) const noexcept;
    };

    void refilter (Cabinet&, const juce::AudioBuffer<float>& shaped,
                   float lowCutHz, int lowCutSlope, float highCutHz, int highCutSlope);

    double rate = 44100.0;

    ParamID::PerSlot<Cabinet> cabinets;

    // Sized once in prepare, so a frame allocates nothing.
    std::unique_ptr<juce::dsp::FFT> fft;
    int fftSize = 0;

    std::vector<float> summed, transform, magnitudes;

    // Scratch for the filtering, which runs on the message thread when a cut moves.
    juce::AudioBuffer<float> filterScratch;
};
