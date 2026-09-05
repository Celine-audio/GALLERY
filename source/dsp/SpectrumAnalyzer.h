#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cstdint>
#include <vector>

/** The FFT order every analyzer in this plugin uses. Shared with PluginProcessor so
    that arrays of analyzers can be default-constructed at the right size.

    Eight thousand points rather than four, and the reason is the *axis* rather than the
    ear. The graph is logarithmic, so its bottom two octaves -- twenty to eighty hertz
    -- occupy a fifth of its width, and a 4096-point transform puts five bins in that
    span. A trace drawn through five points across two hundred pixels is a staircase
    however smoothly it is drawn.

    Not larger than this, though, and the limit is latency rather than cost: a frame
    cannot be measured until it has been filled, so sixteen thousand points at 48 kHz
    means a third of a second before the first one and a sixth between each after.
    A live view that answers a third of a second late is not a live view. The rest of
    the resolution is bought by interpolating between bins where the axis is finer than
    they are, which costs nothing and no time. */
inline constexpr int spectrumFftOrder = 12; // 4096-point FFT

/** Why not larger. The analyser produces nothing at all until its ring holds a whole
    window of real samples, so the window length *is* the delay between switching the
    trace on and seeing it: 8192 samples is 171 ms at 48 kHz, which reads as the button
    not having worked. Halved, it is 85 ms, which reads as the trace arriving.

    The resolution given up is at the very bottom of the axis and costs little there:
    this trace is a smoothed, tilted backdrop for the cabinets to be read against, not
    a measurement anybody takes a number off. */

/**
    Accumulates a long-term average magnitude spectrum from a mono signal.

    The audio thread feeds samples via pushBlock(). Overlapping (50%) Hann-windowed
    frames are FFT'd and their power spectra summed into an accumulator. The message
    thread can read back the averaged magnitude spectrum via getAveragedMagnitudes().

    Magnitudes come back on an absolute scale: a full-scale sine sitting on a bin
    reads 1.0, i.e. 0 dBFS. That is what lets the graph draw this trace against the same
    decibel axis the cabinets are measured onto, rather than a curve normalised to its
    own peak that would agree with them only by accident.

    Realtime safety: pushBlock() never allocates and never blocks. Accumulation into
    the shared buffer uses a try-lock, so if the message thread happens to be reading,
    the audio thread simply skips accumulating that single frame.
*/
class SpectrumAnalyzer
{
public:
    explicit SpectrumAnalyzer (int fftOrderToUse = spectrumFftOrder);

    /** Selects how frames are combined:
          - cumulative (default): long-term running average, used for capturing the
            material a match is computed from.
          - exponential: a decaying real-time average for a live moving display.
        decayPerFrame is the weight (0..1) given to each new frame in exponential mode. */
    void setExponentialMode (bool shouldUseExponential, float decayPerFrame = 0.4f) noexcept;

    /** Clears the accumulated spectrum and the input FIFO.

        Safe to call from any thread at any time, including while audio is running.
        It has to be: the live analyzers are cleared when an editor opens, which is a
        message-thread event with no relation to the transport. The two halves are
        cleared by whichever thread owns them -- the averages here and now under the
        lock, the input side by the audio thread at the top of its next pushBlock --
        because the input side is otherwise untouched by any other thread and putting
        a lock around it would be a lock on the hot path for no reason. */
    void reset();

    /** Audio thread: push a block of mono samples. Only accumulates while capturing. */
    void pushBlock (const float* data, int numSamples) noexcept;

    void setCapturing (bool shouldCapture) noexcept { capturing.store (shouldCapture); }

    /** Multiplies raw FFT magnitudes onto the 0 dBFS-sine scale: the Hann window's
        coherent gain is fftSize/2, and a real sine splits its energy across the
        positive and negative frequency, hence 4/fftSize. */

    int getFftSize() const noexcept { return fftSize; }

    /** Message thread: copies the averaged magnitude spectrum (length == numBins)
        into dest. Returns false if no frames have been captured yet. */
    bool getAveragedMagnitudes (std::vector<float>& dest) const;

private:
    void processFrame() noexcept;

    juce::dsp::FFT fft;
    const int fftSize;
    const int numBins;
    const int hopSize;
    const float magnitudeScale;

    std::vector<float> window;     // Hann window, length fftSize
    std::vector<float> fifo;       // ring buffer of the most recent fftSize samples
    std::vector<float> fftBuffer;  // scratch for the FFT, length 2 * fftSize

    // Audio thread only, which is why none of it is behind the lock.
    int fifoIndex = 0;
    int samplesSinceHop = 0;
    std::int64_t samplesPushed = 0;

    // Set by reset() from any thread, honoured by the audio thread before it pushes
    // anything. Without it, reset() wrote fifo/fifoIndex underneath a pushBlock that
    // was reading them, which is a data race whatever the accumulator lock is doing.
    std::atomic<bool> inputResetPending { false };

    mutable juce::SpinLock accumulatorLock;
    std::vector<double> powerSum;              // length numBins, guarded by accumulatorLock
    std::vector<double> emaPower;              // length numBins, guarded by accumulatorLock
    bool exponentialMode = false;
    double emaAlpha = 0.4;
    bool emaInitialised = false;
    std::atomic<std::int64_t> frameCount { 0 };
    std::atomic<bool> capturing { false };

    JUCE_LEAK_DETECTOR (SpectrumAnalyzer)
};
