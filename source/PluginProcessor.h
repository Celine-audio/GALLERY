#pragma once

#include "Parameters.h"
#include "dsp/IrSlot.h"
#include "dsp/SpectrumAnalyzer.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <memory>

/**
    GALLERY: four cabinet responses, wired in parallel or in chains, and blended.

    The shape of the thing is worth stating once. Each slot holds a file, a convolution
    engine and two filters; the audio thread runs the input through each chain of slots
    in turn -- a chain of one, in parallel -- and adds each chain's output to the blend
    at that chain's share. Everything expensive --
    reading files, resampling them, levelling them and measuring them for the graph --
    happens on the message thread and reaches the audio thread only as a finished filter
    handed over under a try-lock the audio thread never waits on.

    That leaves one thing to be careful about, and it is not the audio thread. The
    message thread and whichever thread the host calls prepareToPlay on can both be
    inside a slot's rebuild at once -- loading a cabinet while a host changes sample
    rate is enough -- and they would be writing the same buffers. `rebuildLock` is what makes
    that impossible. The audio thread never takes it, so it can be held for as long as
    a rebuild needs.
*/
class PluginProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener,
                        private juce::Timer
{
public:
    PluginProcessor();
    ~PluginProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    /** The plugin's own Bypass, handed to the host as its bypass. Without it the host
        made a second one of its own, which neither moved the button on the toolbar nor
        went through the crossfade -- two switches that could disagree about whether the
        plugin was on. With it there is one bypass, wherever it is pressed from. */
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter (ParamID::bypass); }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    /** A constant rather than whatever is loaded right now. A tail length that changed
        with the files would have to be announced, and announcing it makes hosts re-plan
        their graph -- several by suspending processing, which during a drag is audible
        as the transport breaking up. */
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    /** The editor calls this so any analysis only runs while a UI is open. There is no
        sense paying for a picture nobody is looking at. */
    void setUiActive (bool active) noexcept { uiActive.store (active); }

    //==============================================================================
    // Everything below is message thread only, and takes rebuildLock on the caller's
    // behalf. The editor is the only caller.

    /** Reads a file into a slot. The failure carries something worth putting in front
        of somebody: too long and unreadable are different problems. */
    juce::Result loadImpulseResponse (int slot, const juce::File&,
                                      ImpulseResponse::Side = ImpulseResponse::Side::both);

    void unloadImpulseResponse (int slot);

    /** What is in a slot: its name for the strip's label, and whether there is
        anything there at all. */
    juce::String getResponseName (int slot) const;
    bool isSlotLoaded (int slot) const;

    /** How many channels the loaded response has -- one after a side was chosen from a
        stereo file, two when the whole file is being convolved. Zero for an empty slot.

        What the strip puts beside the name: a cabinet convolved in mono and one
        convolved in stereo behave differently, and nothing else on the strip says
        which of the two you have. */
    int getResponseChannels (int slot) const;

    /** The file a slot holds. */
    juce::File getResponseFile (int slot) const;

    /** The folder the last response was chosen from, remembered so that filling four
        slots does not mean navigating to the same place four times. Kept in the state
        tree, so it survives closing the window and reopening the session. */
    juce::File getLastBrowseDirectory() const;
    void setLastBrowseDirectory (const juce::File&);

    /** The slot's measured magnitude on IrSlot's fixed logarithmic grid. Copied rather
        than handed out by reference, or a rebuild would be writing the vector the graph
        is drawing from. False for an empty slot, which should not be drawn at all. */
    bool copySpectrumDb (int slot, std::vector<float>& destination) const;

    /** The slot's shaped response, copied out for the mix display to filter and blend.
        Copied rather than handed out by reference for the same reason the measured
        curve is: a rebuild on another thread would be writing the buffer being read.
        False for an empty slot. */
    bool copyShapedResponse (int slot, juce::AudioBuffer<float>& destination) const;

    /** Renders what the plugin is doing right now as a stereo impulse response, so the
        blend can be taken somewhere else as one cabinet.

        Rendered by running an impulse through a *copy of this plugin* rather than by
        summing the four responses again with the same arithmetic written out a second
        time. Two implementations of one signal path drift, and the day they do the file
        somebody exported stops being the sound they were listening to -- which is the
        one thing an export must never be. This way the path is the path.

        Stereo, because pan is part of the blend and a mono render would throw it away.
        As long as the longest resolution setting in the blend asks for, since the
        exported response has to be long enough for the longest thing in it.

        The output trim and bypass are left out of it. Those are how loud the plugin
        sits in a mix and whether it is switched in, neither of which is a property of
        the cabinet -- baked into the file they would make an export that no longer
        matches the blend the moment either is touched.

        False when there is nothing loaded to render. Message thread; allocates. */
    bool renderBlend (juce::AudioBuffer<float>& destination);

    /** How long the blend takes to die away, which is how long an export runs: the
        longest chain's responses end to end, at the resolution each slot is keeping.
        In parallel that is simply the longest response; in series, all of them added
        together. */
    double longestResolutionSeconds() const;

    /** Seconds of shaped response in the slot. Zero for an empty slot. */
    double getShapedSeconds (int slot) const;

    /** Bumped every time any slot is rebuilt. The editor watches it rather than
        re-summarising four responses thirty times a second for pictures that have not
        changed. */
    std::uint32_t getRebuildCount() const noexcept { return rebuildCount.load(); }

    //==============================================================================
    /** Whether the plugin's own output is being measured for the graph. Off by default
        and off whenever nothing is looking: it is the only thing here that costs the
        audio thread anything it does not have to spend. */
    void setOutputAnalysisEnabled (bool) noexcept;
    bool isOutputAnalysisEnabled() const noexcept { return analysingOutput.load(); }

    /** The measured output spectrum, in dB on IrSlot's grid, or false when there is
        nothing measured yet. Message thread. */
    bool copyOutputSpectrumDb (std::vector<float>& destination) const;

    double getSampleRate() const noexcept { return currentSampleRate.load(); }

private:
    //==============================================================================
    /** What every slot is doing this block, under the routing the audio thread is
        running -- which, for the few milliseconds of a change, is not yet the one the
        parameter asks for. See Parameters::mixFor. */
    Parameters::Mix measureBlend() const noexcept;

    /** Audio thread: steps the routing towards the one asked for, and says whether it
        changed this block. A change is taken with the cabinets faded out -- see
        routingLevel and rewiredInput. */
    bool followRouting (bool anyLoaded) noexcept;

    /** Audio thread: everything the plugin does to the signal, trim included -- what
        comes out when it is not bypassed. */
    void runCabinets (juce::AudioBuffer<float>&, int numChannels, int numSamples) noexcept;

    /** Audio thread: blends what runCabinets made with the copy of the input taken
        before it, towards the input while bypassed and away from it while not. */
    void mixBypass (juce::AudioBuffer<float>&, int numChannels, int numSamples) noexcept;

    /** Audio thread: runs each chain of slots and adds it to `summed` at its share. */
    void sumSlots (const juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& summed,
                   const Parameters::Mix&) noexcept;

    /** Audio thread: hands every slot its controls for this block. */
    void setSlotLevels (const Parameters::Mix&) noexcept;

    /** Audio thread: crossfades the cabinets over the signal that went in. */
    void mixWetIntoDry (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>& summed,
                        int numChannels, int numSamples) noexcept;

    void applyOutputGain (juce::AudioBuffer<float>&, float decibels) noexcept;

    //==============================================================================
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    /** Reads the shaping parameters and hands them to the slots. Cheap when nothing
        has moved -- a slot compares before it rebuilds -- so it can be called from a
        throttle that does not know what changed. */
    void applyShaping();

    void reloadFilesFromState();

    /** Audio thread: folds the finished output to mono and hands it to the analyser.
        A no-op unless something is looking at the result. */
    void measureOutput (const juce::AudioBuffer<float>&, int numChannels, int numSamples) noexcept;

    juce::AudioProcessorValueTreeState apvts;

    std::array<IrSlot, (size_t) ParamID::numSlots> slots;
    Parameters::AllSlotPointers slotParameters;

    std::atomic<float>* bypassParameter = nullptr;
    std::atomic<float>* outputGainParameter = nullptr;
    std::atomic<float>* blendXParameter = nullptr;
    std::atomic<float>* blendYParameter = nullptr;
    std::atomic<float>* routingParameter = nullptr;

    /** The routing the audio thread is running. Its own copy rather than the parameter
        read afresh, because a change is not taken the moment it is asked for: see
        routingLevel. */
    Parameters::Routing routing = Parameters::Routing::parallel;

    /** How much of the cabinets' output is let out while the routing changes -- 1 at
        rest, walked to nothing, the wiring swapped, and walked back.

        Faded rather than crossfaded. A crossfade would mean running the old wiring and
        the new side by side, which is every convolution twice for the length of it; and
        swapped without a fade, a cabinet whose input goes from the dry signal to another
        cabinet's output in one sample is a step in the waveform. Fifteen milliseconds
        each way is short enough to read as the switch being pressed rather than as the
        sound dropping out. */
    static constexpr double routingFadeSeconds = 0.015;
    juce::SmoothedValue<float> routingLevel;

    /** The input to the cabinets, faded back in after a change of routing.

        Faded out and rewired is not enough on its own. Every cabinet still holds the
        input it heard before -- in its convolution, its alignment line and its cuts --
        and a cabinet that was hearing the dry signal and now hears another cabinet's
        output would play that past back through the new wiring, which is a step in the
        waveform arriving a few milliseconds into the fade back in. Measured at seven to
        twenty-eight times the baseline on the test cabinets.

        So at the swap every slot forgets what it heard, and the *input* is faded in
        rather than the output: with every history empty, the new wiring is only filters
        fed a signal that starts smoothly, and nothing that comes out of filters fed that
        can step. Emptying all four is a pass over history sized for the longest
        resolution -- measured at 15 us a slot, so the four fit in any block. */
    juce::SmoothedValue<float> rewiredInput;

    /** Each chain's share of the output, ramped, kept at the chain's last slot as
        Parameters::Mix keeps it. */
    ParamID::PerSlot<juce::SmoothedValue<float>> chainShare;

    /** How much of the output comes from the cabinets rather than straight through.
        Ramped, because loading the first cabinet or deleting the last one switches
        between two different signals. */
    juce::SmoothedValue<float> wetMix;

    /** How much of the output is the plugin rather than the input, 1 when it is on and
        0 when it is bypassed. AURA's figure, so the same switch feels the same in both:
        long enough not to be heard as a click, short enough that it still feels like
        pressing a switch. */
    static constexpr double bypassFadeSeconds = 0.03;
    juce::SmoothedValue<float> bypassMix;

    // The audio thread's working buffers, sized in prepareToPlay. One scratch shared
    // between the slots rather than one each: they run in sequence, so the second has
    // no use for what the first left behind. `bypassDry` is the input as it arrived,
    // kept only while bypass is on or moving.
    //
    // `chainIn` and `chainOut` carry a signal from one slot of a chain to the next,
    // swapping roles as it goes.
    //
    // `fedInput` is what goes into the chains while the input is fading back in after
    // a change of routing.
    juce::AudioBuffer<float> scratch, wet, bypassDry, chainIn, chainOut, fedInput;

    juce::dsp::Gain<float> outputGain;

    /** Written by prepareToPlay, on whichever thread the host chooses, and read by the
        editor and the export. Atomic because those are not the same thread. */
    std::atomic<double> currentSampleRate { 44100.0 };

    /** Guards every rebuild and every read of what a rebuild writes. Never taken by
        the audio thread -- see the class comment. */
    mutable juce::CriticalSection rebuildLock;

    std::atomic<bool> uiActive { false };
    std::atomic<std::uint32_t> rebuildCount { 0 };

    // What is actually coming out, drawn behind the four cabinets. Mono: this is a
    // backdrop for the curves to be read against, and two near-identical grey traces
    // would be one more thing in the way of the four that matter.
    SpectrumAnalyzer outputAnalyzer;
    std::atomic<bool> analysingOutput { false };
    mutable std::vector<float> analyzerScratch;
    juce::AudioBuffer<float> analysisMix;

    // Shaping arrives from a host's automation on the audio thread, where none of the
    // work it implies can be done. A flag is all that thread may set: posting a message
    // -- which is what juce::AsyncUpdater does -- takes a lock and can allocate, and
    // this is the one thread that must do neither.
    std::atomic<bool> shapingDirty { false };
    juce::uint32 lastRebuildMs = 0;

    /** How often the flag above is looked at. Fast enough that pressing the resolution
        button feels like pressing a button, slow enough to cost nothing while the flag
        is clear -- which is nearly always. */
    static constexpr int pollIntervalMs = 40;

    /** And the shortest gap between two rebuilds. A control being dragged asks far
        faster than a rebuild can run, and building every intermediate position is a
        queue that never empties. Longer than the convolver\'s own eight-frame crossfade
        -- about forty milliseconds -- so a new response never arrives mid-walk and cuts
        the last one short. */
    static constexpr juce::uint32 rebuildIntervalMs = 150;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};
