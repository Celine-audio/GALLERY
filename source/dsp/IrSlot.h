#pragma once

#include "../Parameters.h"
#include "CutFilter.h"
#include "ImpulseResponse.h"
#include "LogSpectrum.h"
#include "PartitionedConvolver.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>
#include <vector>

/**
    One of the four cabinets: the file it holds, the processing that shapes it, and the
    two pictures the window draws of it.

    The split that matters here is which thread owns what. Reading the file, resampling
    it, levelling it and measuring it all belong to the message thread, and reach the
    audio thread only as a finished filter handed to the convolution engine. What the
    audio thread runs is a convolution, two cut filters, a delay line and two ramped
    gains -- and nothing it touches is ever resized underneath it.

    Only the resolution setting rebuilds a response. Everything else on a strip is read
    per block and costs nothing, which is why the one control people drag slowly while
    listening -- alignment -- is a delay line rather than a shift baked into the filter.
*/
class IrSlot
{
public:
    IrSlot() = default;

    /** The furthest the alignment control can push a response back. Six milliseconds
        is about two metres of air, which is past any sane distance between two
        microphones on one cabinet -- the control is for taking up the difference
        between captures, not for delay as an effect. */
    static constexpr float maximumAlignMs = 6.0f;

    /** The longest response any slot will convolve, whatever is in the file. Anything
        past it is dropped, which is what makes the shortest setting cheaper rather
        than merely quieter: the engine's cost is proportional to what it is given. */
    static constexpr double maximumResponseSeconds = Parameters::longestResponseSeconds;

    /** How the response is reshaped before it is convolved. Rebuilding is the
        expensive operation in this class, so what triggers one is exactly this
        struct -- everything else about a slot is read per block and costs nothing. */
    struct Shaping
    {
        /** How much of the response to keep. Held in seconds rather than samples so a
            session moving between rates gets the same cabinet rather than a shorter
            one -- see Parameters::referenceRate. */
        double lengthSeconds = Parameters::responseSeconds (Parameters::defaultResolution);

        bool operator== (const Shaping& other) const noexcept
        {
            return juce::approximatelyEqual (lengthSeconds, other.lengthSeconds);
        }

        bool operator!= (const Shaping& other) const noexcept { return ! operator== (other); }
    };

    /** What the audio thread reads at the top of every block. Cheap, all of it: two
        gains to ramp towards and four numbers the filters compare against what they
        are already set to. */
    struct Levels
    {
        /** This cabinet's share of the output, 0 to 1. Already carries mute, solo and
            the blend -- a slot that is not being heard arrives here as zero rather
            than as a flag, so there is one number to ramp instead of a level and a
            switch that can disagree about when they change.

            One by default -- this cabinet, whole -- so that a Levels built and left
            alone is a slot you can hear. The processor sets all four every block. */
        float weight = 1.0f;
        float pan = 0.0f;        // -1 hard left, +1 hard right
        bool inverted = false;

        /** How far this cabinet is pushed back, in milliseconds.

            A delay line on the audio thread rather than a shift baked into the
            response, which is what it used to be. Alignment is the one control people
            drag slowly while listening, and a rebuild per move made that a series of
            steps: the engine walks between two filters over about forty milliseconds,
            so rebuilds any faster than that arrive mid-walk and cut it short -- measured
            at a 45 ms throttle, the worst break in the waveform went up a hundred and
            forty fold. Rebuilding a convolution kernel to express a delay was the wrong
            mechanism for it; a delay line is continuous by construction. */
        float alignMs = 0.0f;

        float lowCutHz = CutFilter::lowestHz;
        float highCutHz = CutFilter::highestHz;
        int lowCutSlope = 12;
        int highCutSlope = 12;
    };

    //==========================================================================
    // Message thread.

    /** Sizes everything for the host's rate and channel count. Allocates. */
    void prepare (double sampleRate, int numChannels, int maximumBlockSize);

    void reset() noexcept;

    /** Reads a file into the slot and rebuilds from it. The failure is worth showing:
        see ImpulseResponse::loadFrom for what it can say. */
    juce::Result loadFrom (const juce::File&,
                           ImpulseResponse::Side = ImpulseResponse::Side::both);

    /** Empties the slot. The engine keeps running -- with nothing in it, which is
        silence -- rather than being torn down, so a slot can be refilled without a
        gap. */
    void unload();

    /** Message thread: whether there is a file in the slot. */
    bool isLoaded() const noexcept { return ! response.isEmpty(); }

    /** Audio thread: the same question, asked of something it is allowed to read.
        `isLoaded` reaches into the buffer the message thread rebuilds, which is a race
        however unlikely it looks; this is a flag published once the rebuild that filled
        that buffer has finished. */
    bool isActive() const noexcept { return active.load(); }

    const ImpulseResponse& getResponse() const noexcept { return response; }

    /** Reshapes and reloads the convolution engine. Cheap to call with the shaping it
        already has -- it returns immediately -- so the caller is free to poll rather
        than having to work out whether anything moved. */
    void setShaping (Shaping);
    Shaping getShaping() const noexcept { return shaping; }

    /** Re-runs the rebuild against the shaping already in force. For a sample-rate
        change, where nothing about the controls has moved but everything downstream
        of the file has. */
    void refresh();

    //==========================================================================
    // What the two displays read. Both are message-thread state, written by a rebuild
    // and read by the editor's timer -- so despite being pictures of what the audio
    // thread is running, neither is shared with it.

    /** The shaped response's magnitude, in dB, on the fixed logarithmic grid below.
        The cuts, the blend and the polarity are *not* in it: those move without a
        rebuild, so the display applies them as it draws. */
    const std::vector<float>& getSpectrumDb() const noexcept { return spectrumDb; }

    /** The response as the engine has it: resampled, truncated and levelled, but
        without the cuts -- those run live on the audio rather than being folded in. The
        mix display applies its own copies of them, which is what lets it show a blend
        of four cabinets that cancel where they disagree. */
    const juce::AudioBuffer<float>& getShapedResponse() const noexcept { return shapedResponse; }

    /** How long the shaped response is, in seconds. */
    double getShapedSeconds() const noexcept;

    // The grid, which belongs to LogSpectrum -- both this and the live output trace are
    // measured onto the same one, and the display plots them against the same axis.
    // Forwarded rather than redefined so there is one definition of it.
    static constexpr int spectrumPoints = LogSpectrum::points;
    static constexpr float spectrumLowHz = LogSpectrum::lowestHz;
    static constexpr float spectrumHighHz = LogSpectrum::highestHz;

    /** The frequency point `index` of the grid stands for. Shared with the display,
        which has to plot the same points this measured. */
    static float spectrumFrequency (int index) noexcept { return LogSpectrum::frequencyForPoint (index); }

    //==========================================================================
    // Audio thread.

    void setLevels (const Levels&) noexcept;

    /** Convolves `input` through this slot and *adds* the result to `output`, using
        `scratch` as its working buffer. All three carry the prepared channel count and
        no more than the prepared block size. Allocates nothing. */
    void process (const juce::AudioBuffer<float>& input,
                  juce::AudioBuffer<float>& scratch,
                  juce::AudioBuffer<float>& output) noexcept;

private:
    void rebuild();

    void analyse (const juce::AudioBuffer<float>& shaped);

    /** Shifts the measured curve so its loudest frequency sits at 0 dB, and returns the
        gain that does the same to the buffer. Call it straight after `analyse`, whose
        result it reads and rewrites.

        This is the answer to the plugin having been very much louder with a response in
        it than without -- see the definition for why it is the peak rather than the
        energy. */
    float levelToPeak();

    /** Drops everything past `keep`, fading the last of what is left so the response
        ends on silence rather than on a step. */
    static void truncate (juce::AudioBuffer<float>&, int keep);

    /** Audio thread: pushes the block through the alignment delay. */
    void delay (juce::AudioBuffer<float>&, int numChannels, int numSamples) noexcept;

    ImpulseResponse response;
    PartitionedConvolver convolver;

    CutFilter lowCut { CutFilter::Kind::lowCut };
    CutFilter highCut { CutFilter::Kind::highCut };

    /** Third-order Lagrange rather than linear. Linear interpolation of a fractional
        sample is a quiet low-pass whose corner moves with the fraction, so aligning two
        cabinets would change the tone of one of them by an amount that depended on how
        far it had been moved -- which is the one thing this control must not do. */
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> alignDelay;
    juce::SmoothedValue<float> alignSamples;

    double rate = 44100.0;
    int channels = 2;

    Shaping shaping;

    /** The response as it is convolved: resampled to the host's rate, truncated to the
        resolution setting and levelled. Kept because both displays are drawn from it
        and neither should re-derive it. */
    juce::AudioBuffer<float> shapedResponse;

    std::vector<float> spectrumDb;

    // The transform `analyse` runs, kept between rebuilds. It is sized for the longest
    // response a slot will hold, so building one per rebuild would be a set of twiddle
    // tables and a sixty-thousand-element buffer thrown away every time a tier moves.
    std::unique_ptr<juce::dsp::FFT> analysisFft;
    int analysisFftSize = 0;
    std::vector<float> analysisScratch, analysisMagnitudes;

    std::atomic<bool> active { false };

    /** Set by a rebuild that found the slot idle, cleared by the audio thread when it
        acts on it. The delay line is downstream of the convolution, so it holds the
        same frozen past the engine does and has to be emptied with it. */
    std::atomic<bool> flushDelay { false };

    // Ramped rather than applied outright. The gain carries the polarity as its sign,
    // so inverting walks through zero instead of stepping across it -- a fade of a few
    // milliseconds where a flip would be a click.
    juce::SmoothedValue<float> gain, panLeft, panRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (IrSlot)
};
