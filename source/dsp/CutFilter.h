#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>

/**
    One of a slot's two cut filters: a Butterworth cascade whose slope is chosen
    rather than fixed, and which can say what it is doing to any frequency so that the
    graph draws the filter the audio is actually running.

    Written out rather than built on `juce::dsp::IIR`, and the reason is threading.
    JUCE holds a filter's coefficients behind a reference-counted pointer, so changing
    them means assigning that pointer -- which either happens on the message thread,
    racing the audio thread that is reading it, or on the audio thread, where dropping
    the last reference to the old set is a deallocation on the one thread that must
    never deallocate. Both are wrong, and the usual ways round it (a duplicator, a
    lock-free mailbox of coefficient objects) are more machinery than the arithmetic
    they protect. Plain floats recomputed in place have neither problem: `setSlope`
    does a handful of trig calls into storage that already exists, so the audio thread
    can call it at the top of a block and be done.

    Cuts are run live rather than baked into the impulse response. A steep cut rings
    for far longer than a cabinet response lasts --
    a fourth-order high-pass at 80 Hz is still sounding tens of milliseconds after a
    100 ms capture has ended -- so folding one into the response would truncate its
    own tail and quietly get the bottom octave wrong. Filters are cheap; being wrong
    about the low end of a guitar cabinet is not.
*/
class CutFilter
{
public:
    /** Which end is being taken off. Named for what the control says rather than for
        the filter it is: a "low cut" is a high-pass, and every time that gets written
        the other way round somebody has to stop and translate. */
    enum class Kind
    {
        lowCut,  // high-pass: removes everything below the corner
        highCut  // low-pass:  removes everything above it
    };

    /** The ends of the frequency range, shared with the parameters so that a control
        at the end of its travel and a filter that has switched itself off are the same
        thing rather than two things that have to be kept in step. */
    static constexpr float lowestHz = 20.0f;
    static constexpr float highestHz = 20000.0f;

    /** The slopes the design can be asked for are Parameters::slopes -- six decibels
        an octave per Butterworth order, so the list is exactly orders one to four. A
        slope is passed in as that number rather than as an index, so the two ends
        cannot disagree about which is which. */
    static constexpr int maximumOrder = 4;
    static constexpr int maximumSections = (maximumOrder + 1) / 2;
    static constexpr int maximumChannels = 2;

    explicit CutFilter (Kind kindToUse) noexcept : kind (kindToUse) {}

    /** Sizes the state and clears it. Allocates nothing -- the storage is fixed -- but
        belongs in prepareToPlay all the same, because it needs the rate. */
    void prepare (double sampleRate, int numChannels) noexcept;

    void reset() noexcept;

    /** Moves the filter at once. For the copies the graph and the mix display keep,
        which are asked what shape to draw rather than run over audio: those never call
        process(), so a ramp they never advance would leave them drawing the filter's
        previous setting for ever. */
    void setParameters (float frequencyHz, int slopeDbPerOctave) noexcept;

    /** Moves the filter over the next few milliseconds rather than at once. This is the
        audio path's entry, and the ramp is the whole point of it.

        Recomputing a cascade's coefficients between one block and the next steps the
        filter, and a step in an IIR with energy in its state is a transient rather than
        a change of tone -- measured on a fast drag of the high cut, the worst break in
        the waveform went up a thousandfold. The frequency is ramped instead, and the
        coefficients are rebuilt several times a block from where it has got to, so what
        the ear gets is a filter moving rather than a filter jumping.

        Multiplicative, because frequency is heard in ratios: a linear ramp from 20 kHz
        to 300 Hz would spend most of its time in the top octave and cross the bottom
        four in its last instant, which is the jump this exists to remove. */
    void setTarget (float frequencyHz, int slopeDbPerOctave) noexcept;

    /** Audio thread: filters in place. A no-op while switched off. */
    void process (juce::AudioBuffer<float>&) noexcept;

    /** Whether the cut is doing anything, which is what the graph asks before it
        draws one. */
    bool isActive() const noexcept { return engaged; }

    /** The gain this filter applies at a frequency, as a multiplier. Message thread:
        the graph asks it what shape to draw, so that the curve on screen is derived
        from the same coefficients the audio is running rather than from a second
        implementation that can disagree with it. */
    float magnitudeAt (float frequencyHz) const noexcept;

    /** The Q of section `index` of a Butterworth cascade of `order`, which is the one
        piece of this worth stating on its own: it is what makes a cascade of biquads
        Butterworth rather than merely several filters in a row. Exposed for the tests,
        which check the cascade against the slope it claims. */
    static double sectionQ (int order, int index) noexcept;

private:
    struct Section
    {
        // Normalised: a0 is divided out when the section is designed.
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        struct State { double s1 = 0.0, s2 = 0.0; };
        std::array<State, maximumChannels> state {};
    };

    void design() noexcept;

    /** Runs `numSamples` from `start` through the cascade as it currently stands. */
    void processRun (juce::AudioBuffer<float>&, int start, int numSamples) noexcept;

    /** Runs a blended run, dry against filtered, while the cut is fading in or out. */
    void processBlendedRun (juce::AudioBuffer<float>&, int start, int numSamples) noexcept;

    /** How long a cut takes to arrive when it is moved off the end of its travel, and
        to leave when it is moved back onto it.

        A filter switched on part way through a signal starts from silence, so its first
        output is its *step* response rather than its steady one -- for a cut near the
        end of its travel that is a jump of a third of the signal's amplitude, into
        whatever is playing. Measured on a fast drag, that one sample was the whole of
        the crackle.

        Faded rather than primed. The state a filter *would* have been in depends on
        what the signal has been doing, and the two kinds disagree about it in the worst
        possible way -- a low cut passes a steady signal and a high cut blocks it, so
        any single assumption is exactly wrong for one of them. A fade makes no
        assumption at all. */
    static constexpr double engageSeconds = 0.008;

    void designBiquad (Section&, double frequency, double q) const noexcept;
    void designOnePole (Section&, double frequency) const noexcept;

    /** How often the coefficients are rebuilt while the frequency is moving. Short
        enough that the steps between rebuilds are small, long enough that the
        transcendentals a rebuild costs are amortised over a useful run of samples. */
    static constexpr int updateInterval = 32;

    Kind kind;

    double rate = 44100.0;
    int channels = 2;

    float frequency = lowestHz;
    int slope = 12;

    int numSections = 0;
    std::array<Section, maximumSections> sections {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> ramp;
    juce::SmoothedValue<float> engagement;

    /** Whether the cut is off the end of its travel, which is what `engagement` is
        ramping towards. Distinct from whether there is work to do: a cut just switched
        off is still being faded out. */
    bool engaged = false;

    JUCE_LEAK_DETECTOR (CutFilter)
};
