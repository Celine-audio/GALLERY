#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", Parameters::createLayout())
{
    for (auto* id : ParamID::shaping)
        apvts.addParameterListener (id, this);

    slotParameters = Parameters::bindSlots (apvts);
    bypassParameter = apvts.getRawParameterValue (ParamID::bypass);
    outputGainParameter = apvts.getRawParameterValue (ParamID::outputGain);
    blendXParameter = apvts.getRawParameterValue (ParamID::blendX);
    blendYParameter = apvts.getRawParameterValue (ParamID::blendY);
    routingParameter = apvts.getRawParameterValue (ParamID::routing);

    startTimer (pollIntervalMs);
}

PluginProcessor::~PluginProcessor()
{
    for (auto* id : ParamID::shaping)
        apvts.removeParameterListener (id, this);

    // It can be mid-flight when a host closes the plugin, and it calls back into
    // members that are about to stop existing.
    stopTimer();
}

//==============================================================================
double PluginProcessor::getTailLengthSeconds() const
{
    // Four of the longest responses end to end, which is what series at the top
    // resolution rings for. Stated for the worst routing rather than the one in use, for
    // the same reason it is stated for the longest resolution: a tail that moved would
    // have to be announced.
    return (double) ParamID::numSlots
         * (IrSlot::maximumResponseSeconds + (double) IrSlot::maximumAlignMs * 0.001);
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate.store (sampleRate);

    const auto numChannels = juce::jmax (1, getMainBusNumOutputChannels());

    const juce::dsp::ProcessSpec spec { sampleRate,
                                        static_cast<juce::uint32> (samplesPerBlock),
                                        static_cast<juce::uint32> (numChannels) };

    outputGain.prepare (spec);
    outputGain.setRampDurationSeconds (0.05);

    // Set before the reset, not after: reset() snaps the smoother to its current
    // target and Gain's target starts at zero, so the other order is a 50 ms fade-in
    // on every transport start.
    outputGain.setGainDecibels (apvts.getRawParameterValue (ParamID::outputGain)->load());
    outputGain.reset();

    scratch.setSize (numChannels, samplesPerBlock, false, true, true);
    wet.setSize (numChannels, samplesPerBlock, false, true, true);
    bypassDry.setSize (numChannels, samplesPerBlock, false, true, true);
    chainIn.setSize (numChannels, samplesPerBlock, false, true, true);
    chainOut.setSize (numChannels, samplesPerBlock, false, true, true);
    fedInput.setSize (numChannels, samplesPerBlock, false, true, true);
    analysisMix.setSize (1, samplesPerBlock, false, true, true);

    // Landed where the bypass already stands, for the same reason as the crossfade
    // below: a session opened bypassed should not fade out of cabinets it never played.
    bypassMix.reset (sampleRate, bypassFadeSeconds);
    bypassMix.setCurrentAndTargetValue (bypassParameter->load() > 0.5f ? 0.0f : 1.0f);

    // Longer than a slot's own gain ramp on purpose. Emptying the last cabinet fades
    // the cabinets out and the dry signal in, and the two overlapping would be the
    // input comb-filtered against itself; the slower half arrives after the cabinets
    // have gone.
    wetMix.reset (sampleRate, 0.05);

    // Where the crossfade already stands, not zero -- the same trap the trim above is
    // written to avoid, one variable over. A host calls prepareToPlay on every
    // transport start and every change of rate or buffer size, and starting the ramp
    // at zero with cabinets loaded plays 50 ms of the dry signal before they arrive.
    // Measured: the first block after a re-prepare came out at full scale against a
    // settled 0.25, which on a guitar DI is the unprocessed attack of whatever note
    // the transport started on.
    // And the routing is simply the one asked for, with the chains at their shares:
    // there is nothing playing yet to fade a change of wiring out of.
    routing = Parameters::routingForChoice ((int) routingParameter->load());
    routingLevel.reset (sampleRate, routingFadeSeconds);
    routingLevel.setCurrentAndTargetValue (1.0f);
    rewiredInput.reset (sampleRate, routingFadeSeconds);
    rewiredInput.setCurrentAndTargetValue (1.0f);

    const auto mix = measureBlend();

    wetMix.setCurrentAndTargetValue (mix.anyLoaded ? 1.0f : 0.0f);

    for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
    {
        // The slots' own gain ramp, so a chain's share and a cabinet's polarity move at
        // the same pace.
        chainShare[slot].reset (sampleRate, 0.02);
        chainShare[slot].setCurrentAndTargetValue (mix.share[slot]);
    }

    // Exponential rather than cumulative: a live picture of what is coming out now,
    // so a frame from ten seconds ago should have stopped counting. Slow, though -- at
    // a third per frame the trace jitters and a flickering backdrop pulls the eye off
    // the four curves in front of it.
    outputAnalyzer.setExponentialMode (true, 0.15f);
    outputAnalyzer.reset();

    {
        // Held across all four: the message thread can be part way through a rebuild
        // of any of them when a host changes rate.
        const juce::ScopedLock lock (rebuildLock);

        for (auto& slot : slots)
            slot.prepare (sampleRate, numChannels, samplesPerBlock);
    }

    ++rebuildCount;

    // Whatever the responses were resampled to, they are not that any more.
    applyShaping();
}

void PluginProcessor::releaseResources()
{
    const juce::ScopedLock lock (rebuildLock);

    for (auto& slot : slots)
        slot.reset();
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    // Refusing a mismatched pair rather than trying to fan out or fold down: a host
    // that wants mono-to-stereo can be told no, and every plugin that says yes to it
    // has to answer what "the other channel" contains.
    return mainIn == mainOut;
}

//==============================================================================
Parameters::Mix PluginProcessor::measureBlend() const noexcept
{
    ParamID::PerSlot<bool> loaded {};

    for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
        loaded[slot] = slots[slot].isActive();

    return Parameters::mixFor (routing, blendXParameter->load(), blendYParameter->load(),
                               loaded, slotParameters);
}

bool PluginProcessor::followRouting (bool anyLoaded) noexcept
{
    const auto asked = Parameters::routingForChoice ((int) routingParameter->load());

    if (asked == routing)
    {
        // Including a change asked for and then taken back before it landed: the fade
        // simply turns round.
        routingLevel.setTargetValue (1.0f);
        return false;
    }

    // With nothing coming out of the cabinets there is nothing to fade, and waiting
    // thirty milliseconds to rewire silence would only make the switch feel slow.
    const auto silent = ! anyLoaded && ! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.0f;

    if (! silent)
    {
        routingLevel.setTargetValue (0.0f);

        if (routingLevel.isSmoothing() || routingLevel.getCurrentValue() > 0.0f)
            return false;
    }

    routing = asked;

    if (silent)
    {
        routingLevel.setTargetValue (1.0f);
        return true;
    }

    // Faded out: every slot forgets the old wiring, and the input is let back in from
    // nothing -- see rewiredInput. The output comes back at once, because there is now
    // nothing in any cabinet for it to let out until the input has started to arrive.
    for (auto& slot : slots)
        slot.forgetHistory();

    rewiredInput.setCurrentAndTargetValue (0.0f);
    rewiredInput.setTargetValue (1.0f);
    routingLevel.setCurrentAndTargetValue (1.0f);
    return true;
}

//==============================================================================
void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (buffer.getNumChannels(), scratch.getNumChannels());

    // An output channel with no input behind it holds whatever the host last left there.
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    const auto bypassed = bypassParameter != nullptr && bypassParameter->load() > 0.5f;

    // A block longer than the one prepared for is a host breaking its own promise.
    // Passing it through neither allocates nor pretends the extra samples were done.
    const auto usable = numChannels > 0 && numSamples <= scratch.getNumSamples();

    if (! usable)
    {
        applyOutputGain (buffer, bypassed ? 0.0f : outputGainParameter->load());
        measureOutput (buffer, juce::jmax (0, numChannels), numSamples);
        return;
    }

    bypassMix.setTargetValue (bypassed ? 0.0f : 1.0f);

    // Settled on the plugin, none of the input is heard, so there is nothing to keep.
    const auto blendingDry = bypassed || bypassMix.isSmoothing();

    if (blendingDry)
        for (int channel = 0; channel < numChannels; ++channel)
            bypassDry.copyFrom (channel, 0, buffer, channel, 0, numSamples);

    // Run whatever the bypass says. A cabinet that is not fed while bypassed keeps the
    // input it last heard, and on the way back in convolves it: the past, at full
    // scale, over whatever is playing now. That costs a bypassed plugin what a running
    // one costs, which is the price of coming back without a ghost.
    runCabinets (buffer, numChannels, numSamples);

    if (blendingDry)
        mixBypass (buffer, numChannels, numSamples);

    measureOutput (buffer, numChannels, numSamples);
}

void PluginProcessor::runCabinets (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
{
    auto anyLoaded = false;

    for (const auto& slot : slots)
        anyLoaded |= slot.isActive();

    const auto rewired = followRouting (anyLoaded);
    const auto mix = measureBlend();

    setSlotLevels (mix);

    // The output is faded out at this moment -- that is when a change is taken -- so
    // everything the new wiring moves can land where it is going rather than spend the
    // fade back in walking there.
    if (rewired)
    {
        for (auto& slot : slots)
            slot.landEngagement();

        for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
            chainShare[slot].setCurrentAndTargetValue (mix.share[slot]);
    }

    // Loaded, not audible: four muted cabinets are still four cabinets, and the answer
    // to muting them is silence rather than the signal that went in.
    wetMix.setTargetValue (mix.anyLoaded ? 1.0f : 0.0f);

    if (! mix.anyLoaded && ! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.0f)
    {
        applyOutputGain (buffer, outputGainParameter->load());
        return;
    }

    juce::AudioBuffer<float> dry (buffer.getArrayOfWritePointers(), numChannels, 0, numSamples);
    juce::AudioBuffer<float> summed (wet.getArrayOfWritePointers(), numChannels, 0, numSamples);

    sumSlots (dry, summed, mix);

    if (routingLevel.isSmoothing())
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samples = summed.getWritePointer (channel);
            auto walk = routingLevel;

            for (int i = 0; i < numSamples; ++i)
                samples[i] *= walk.getNextValue();
        }

        routingLevel.skip (numSamples);
    }
    else if (routingLevel.getCurrentValue() < 1.0f)
    {
        summed.applyGain (routingLevel.getCurrentValue());
    }

    mixWetIntoDry (buffer, summed, numChannels, numSamples);

    // The trim stays where it is set, bypassed or not: bypass takes it away by fading
    // out the signal it is part of. Ramping it to unity instead took fifty milliseconds
    // while the cabinets left in one sample, and for those fifty you heard the one
    // without the other.
    applyOutputGain (buffer, outputGainParameter->load());
}

void PluginProcessor::mixBypass (juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept
{
    if (! bypassMix.isSmoothing())
    {
        // Landed on the bypass: the input, untouched.
        for (int channel = 0; channel < numChannels; ++channel)
            buffer.copyFrom (channel, 0, bypassDry, channel, 0, numSamples);

        return;
    }

    // The same crossfade as the cabinets over the dry signal, one level up: a copy of
    // the ramp per channel so every channel walks the same one, and the real one is
    // advanced once, below.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* out = buffer.getWritePointer (channel);
        const auto* in = bypassDry.getReadPointer (channel);
        auto mix = bypassMix;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto amount = mix.getNextValue();
            out[i] = out[i] * amount + in[i] * (1.0f - amount);
        }
    }

    bypassMix.skip (numSamples);
}

void PluginProcessor::setSlotLevels (const Parameters::Mix& mix) noexcept
{
    for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
    {
        const auto& p = slotParameters[slot];

        IrSlot::Levels levels;
        levels.engaged = mix.engaged[slot];
        levels.pan = p.pan->load();
        levels.inverted = Parameters::SlotPointers::isOn (p.phase);
        levels.alignMs = p.align->load();
        levels.lowCutHz = p.lowCut->load();
        levels.highCutHz = p.highCut->load();
        levels.lowCutSlope = Parameters::slopeForChoice ((int) p.lowCutSlope->load());
        levels.highCutSlope = Parameters::slopeForChoice ((int) p.highCutSlope->load());

        slots[slot].setLevels (levels);
    }
}

void PluginProcessor::sumSlots (const juce::AudioBuffer<float>& dry,
                                juce::AudioBuffer<float>& summed,
                                const Parameters::Mix& mix) noexcept
{
    summed.clear();

    const auto numChannels = summed.getNumChannels();
    const auto numSamples = summed.getNumSamples();

    // Two buffers a chain's signal passes between, one slot's output being the next
    // one's input. Views onto storage sized in prepareToPlay, so nothing here allocates.
    juce::AudioBuffer<float> first (chainIn.getArrayOfWritePointers(), numChannels, 0, numSamples);
    juce::AudioBuffer<float> second (chainOut.getArrayOfWritePointers(), numChannels, 0, numSamples);

    // What the chains are fed: the input, unless the routing has just changed and it is
    // being held off or faded back in. One copy for every chain, so all of them walk
    // the same ramp.
    juce::AudioBuffer<float> faded (fedInput.getArrayOfWritePointers(), numChannels, 0, numSamples);
    const juce::AudioBuffer<float>* input = &dry;

    if (rewiredInput.isSmoothing())
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* in = dry.getReadPointer (channel);
            auto* out = faded.getWritePointer (channel);
            auto walk = rewiredInput;

            for (int i = 0; i < numSamples; ++i)
                out[i] = in[i] * walk.getNextValue();
        }

        rewiredInput.skip (numSamples);
        input = &faded;
    }
    else if (rewiredInput.getCurrentValue() < 1.0f)
    {
        faded.clear();
        input = &faded;
    }

    for (int start = 0; start < ParamID::numSlots;)
    {
        const auto end = Parameters::chainEnd (routing, start);
        auto& share = chainShare[(size_t) end];

        share.setTargetValue (mix.share[(size_t) end]);

        // A chain nobody is hearing still runs if anything in it is loaded. A muted
        // cabinet's engine holds the input history its response is convolved against,
        // and one that stopped listening would come back missing however much of the
        // past its response reaches into. Only a chain with nothing in it is skipped.
        auto running = false;

        for (auto slot = start; slot <= end; ++slot)
            running |= slots[(size_t) slot].isRunning();

        if (! running && ! share.isSmoothing() && share.getCurrentValue() <= 0.0f)
        {
            start = end + 1;
            continue;
        }

        const juce::AudioBuffer<float>* signal = input;

        for (auto slot = start; slot <= end; ++slot)
        {
            auto& next = signal == &first ? second : first;

            next.clear();
            slots[(size_t) slot].process (*signal, scratch, next);
            signal = &next;
        }

        if (! share.isSmoothing())
        {
            if (share.getCurrentValue() > 0.0f)
                for (int channel = 0; channel < numChannels; ++channel)
                    summed.addFrom (channel, 0, *signal, channel, 0, numSamples, share.getCurrentValue());
        }
        else
        {
            // A copy of the ramp per channel, so every channel walks the same one; the
            // real one is advanced once, below.
            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto* out = summed.getWritePointer (channel);
                const auto* in = signal->getReadPointer (channel);
                auto walk = share;

                for (int i = 0; i < numSamples; ++i)
                    out[i] += in[i] * walk.getNextValue();
            }

            share.skip (numSamples);
        }

        start = end + 1;
    }
}

void PluginProcessor::mixWetIntoDry (juce::AudioBuffer<float>& buffer,
                                     const juce::AudioBuffer<float>& summed,
                                     int numChannels, int numSamples) noexcept
{
    // Crossfaded rather than switched: the cabinets and the dry signal are different
    // sounds, and going straight from one to the other is a step in the waveform.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* out = buffer.getWritePointer (channel);
        const auto* processed = summed.getReadPointer (channel);

        // A copy per channel, so every channel walks the same ramp; the real one is
        // advanced once, below.
        auto mix = wetMix;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto amount = mix.getNextValue();
            out[i] = processed[i] * amount + out[i] * (1.0f - amount);
        }
    }

    wetMix.skip (numSamples);
}

void PluginProcessor::applyOutputGain (juce::AudioBuffer<float>& buffer, float decibels) noexcept
{
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    outputGain.setGainDecibels (decibels);
    outputGain.process (context);
}

void PluginProcessor::measureOutput (const juce::AudioBuffer<float>& buffer,
                                     int numChannels, int numSamples) noexcept
{
    if (! analysingOutput.load() || ! uiActive.load())
        return;

    if (numSamples > analysisMix.getNumSamples() || numChannels <= 0)
        return;

    // Folded to mono into a buffer sized in prepareToPlay, so this allocates nothing.
    auto* mix = analysisMix.getWritePointer (0);

    juce::FloatVectorOperations::copy (mix, buffer.getReadPointer (0), numSamples);

    for (int channel = 1; channel < numChannels; ++channel)
        juce::FloatVectorOperations::add (mix, buffer.getReadPointer (channel), numSamples);

    if (numChannels > 1)
        juce::FloatVectorOperations::multiply (mix, 1.0f / (float) numChannels, numSamples);

    outputAnalyzer.pushBlock (mix, numSamples);
}

//==============================================================================
void PluginProcessor::setOutputAnalysisEnabled (bool shouldAnalyse) noexcept
{
    if (analysingOutput.exchange (shouldAnalyse) == shouldAnalyse)
        return;

    // Cleared on the way in as well as out, so switching it on shows what is happening
    // now rather than an average that starts with however long it was off.
    outputAnalyzer.reset();
    outputAnalyzer.setCapturing (shouldAnalyse);
}

bool PluginProcessor::copyOutputSpectrumDb (std::vector<float>& destination) const
{
    if (! analysingOutput.load())
        return false;

    if (! outputAnalyzer.getAveragedMagnitudes (analyzerScratch))
        return false;

    const auto bins = (int) analyzerScratch.size();
    const auto binWidth = getSampleRate() / (double) outputAnalyzer.getFftSize();

    if (bins <= 1 || binWidth <= 0.0)
        return false;

    // The same grid the cabinets are measured on, by the same arithmetic, so the
    // backdrop and the curves in front of it describe the same frequencies.
    LogSpectrum::sample (analyzerScratch.data(), bins, binWidth, destination);

    // Smoothed and tilted, which the cabinets' own curves are not: this is a
    // measurement of a signal rather than of a filter, so its ripple is noise, and a
    // signal is read against what music does -- about three decibels an octave down.
    LogSpectrum::smooth (destination);
    LogSpectrum::applyPinkTilt (destination);

    return true;
}

//==============================================================================
void PluginProcessor::parameterChanged (const juce::String&, float)
{
    // Called from whichever thread moved the parameter, which for host automation is
    // the audio thread. Setting a flag is all that thread may do; the timer below picks
    // it up.
    shapingDirty.store (true);
}

void PluginProcessor::timerCallback()
{
    if (! shapingDirty.load())
        return;

    const auto now = juce::Time::getMillisecondCounter();

    // A drag that keeps moving keeps finding the gap too short, and the position it
    // stops at is the one that gets built.
    if (now - lastRebuildMs < rebuildIntervalMs)
        return;

    // Cleared before the work, not after: a move arriving while this runs has to leave
    // the flag set for the next tick rather than be swallowed by it.
    shapingDirty.store (false);
    lastRebuildMs = now;

    applyShaping();
}

void PluginProcessor::applyShaping()
{
    // Called by prepareToPlay and a state reload as well as by the throttle, and cheap
    // when nothing has moved: whether a slot has anything to do is the slot's own
    // question, and it compares before it works.
    auto rebuilt = false;

    {
        const juce::ScopedLock lock (rebuildLock);

        for (int slot = 0; slot < ParamID::numSlots; ++slot)
        {
            const auto index = (size_t) slot;

            IrSlot::Shaping shaping;
            shaping.lengthSeconds = Parameters::responseSeconds (
                (int) slotParameters[index].resolution->load());

            if (shaping == slots[index].getShaping())
                continue;

            slots[index].setShaping (shaping);
            rebuilt = true;
        }
    }

    if (rebuilt)
        ++rebuildCount;
}

//==============================================================================
juce::Result PluginProcessor::loadImpulseResponse (int slot, const juce::File& file,
                                                   ImpulseResponse::Side side)
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return juce::Result::fail ("There is no such slot.");

    juce::Result result = juce::Result::ok();

    {
        const juce::ScopedLock lock (rebuildLock);
        result = slots[(size_t) slot].loadFrom (file, side);
    }

    if (result.wasOk())
    {
        apvts.state.setProperty (
            ParamID::sideProperty[(size_t) slot],
            side == ImpulseResponse::Side::left    ? juce::String ("left")
            : side == ImpulseResponse::Side::right ? juce::String ("right")
                                                   : juce::String(),
            nullptr);

        apvts.state.setProperty (ParamID::fileProperty[(size_t) slot],
                                 file.getFullPathName(), nullptr);
        ++rebuildCount;
    }

    return result;
}

void PluginProcessor::unloadImpulseResponse (int slot)
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return;

    {
        const juce::ScopedLock lock (rebuildLock);
        slots[(size_t) slot].unload();
    }

    apvts.state.setProperty (ParamID::fileProperty[(size_t) slot], juce::String(), nullptr);
    apvts.state.setProperty (ParamID::sideProperty[(size_t) slot], juce::String(), nullptr);
    ++rebuildCount;
}

int PluginProcessor::getResponseChannels (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return 0;

    const juce::ScopedLock lock (rebuildLock);
    return slots[(size_t) slot].getResponse().getNumChannels();
}

juce::String PluginProcessor::getResponseName (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return {};

    const juce::ScopedLock lock (rebuildLock);
    return slots[(size_t) slot].getResponse().getName();
}

bool PluginProcessor::isSlotLoaded (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return false;

    const juce::ScopedLock lock (rebuildLock);
    return slots[(size_t) slot].isLoaded();
}

juce::File PluginProcessor::getResponseFile (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return {};

    const juce::ScopedLock lock (rebuildLock);
    return slots[(size_t) slot].getResponse().getFile();
}

juce::File PluginProcessor::getLastBrowseDirectory() const
{
    const juce::String path = apvts.state.getProperty (ParamID::browseDirectoryProperty,
                                                       juce::String());

    return path.isNotEmpty() ? juce::File (path) : juce::File();
}

void PluginProcessor::setLastBrowseDirectory (const juce::File& directory)
{
    if (directory.isDirectory())
        apvts.state.setProperty (ParamID::browseDirectoryProperty,
                                 directory.getFullPathName(), nullptr);
}

bool PluginProcessor::copySpectrumDb (int slot, std::vector<float>& destination) const
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return false;

    const juce::ScopedLock lock (rebuildLock);

    if (! slots[(size_t) slot].isLoaded())
        return false;

    destination = slots[(size_t) slot].getSpectrumDb();
    return true;
}

bool PluginProcessor::copyShapedResponse (int slot, juce::AudioBuffer<float>& destination) const
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return false;

    const juce::ScopedLock lock (rebuildLock);

    if (! slots[(size_t) slot].isLoaded())
        return false;

    destination.makeCopyOf (slots[(size_t) slot].getShapedResponse());
    return true;
}

double PluginProcessor::longestResolutionSeconds() const
{
    // The longest chain of whatever is loaded, so an export is long enough for every
    // cabinet in the blend rather than for the first one asked about. A chain rings for
    // its responses end to end: each one convolves the whole of the tail before it.
    const auto wiring = Parameters::routingForChoice ((int) routingParameter->load());
    auto longest = Parameters::responseSeconds (0);

    for (int start = 0; start < ParamID::numSlots;)
    {
        const auto end = Parameters::chainEnd (wiring, start);
        auto chain = 0.0;

        for (auto slot = start; slot <= end; ++slot)
            if (isSlotLoaded (slot))
                chain += Parameters::responseSeconds (
                    (int) slotParameters[(size_t) slot].resolution->load());

        longest = juce::jmax (longest, chain);
        start = end + 1;
    }

    return longest;
}

bool PluginProcessor::renderBlend (juce::AudioBuffer<float>& destination)
{
    auto anythingLoaded = false;

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
        anythingLoaded |= isSlotLoaded (slot);

    if (! anythingLoaded)
        return false;

    const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const auto length = (int) std::ceil (longestResolutionSeconds() * rate);

    constexpr int block = 512;

    PluginProcessor render;
    render.prepareToPlay (rate, block);

    {
        juce::MemoryBlock state;
        getStateInformation (state);
        render.setStateInformation (state.getData(), (int) state.getSize());
    }

    // Neither belongs in the file -- see the note on the declaration.
    for (const auto& setting : { std::pair { ParamID::bypass, 0.0f },
                                 std::pair { ParamID::outputGain, 0.5f } })
        if (auto* parameter = render.getAPVTS().getParameter (setting.first))
            parameter->setValueNotifyingHost (setting.second);

    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    // Silence first, until every ramp in the plugin has landed. The wet mix fades in
    // over fifty milliseconds, the engine walks to its filter over forty, and the cuts
    // engage over eight -- an impulse sent before all of that has finished would come
    // back carrying the fades rather than the filter.
    const auto settling = (int) std::ceil (rate * 0.75 / block);

    for (int i = 0; i < settling; ++i)
    {
        buffer.clear();
        render.processBlock (buffer, midi);
    }

    destination.setSize (2, length, false, true, true);
    destination.clear();

    // One impulse, on both channels: the response to a mono source placed centrally,
    // which is what a cabinet loader convolves against.
    for (int written = 0; written < length;)
    {
        buffer.clear();

        if (written == 0)
            for (int channel = 0; channel < 2; ++channel)
                buffer.setSample (channel, 0, 1.0f);

        render.processBlock (buffer, midi);

        const auto run = juce::jmin (block, length - written);

        for (int channel = 0; channel < 2; ++channel)
            destination.copyFrom (channel, written, buffer, channel, 0, run);

        written += run;
    }

    return true;
}

double PluginProcessor::getShapedSeconds (int slot) const
{
    if (! juce::isPositiveAndBelow (slot, ParamID::numSlots))
        return 0.0;

    const juce::ScopedLock lock (rebuildLock);
    return slots[(size_t) slot].getShapedSeconds();
}

//==============================================================================
juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = apvts.copyState();

    if (! state.isValid())
        return;

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    // Hosts have been known to hand a plugin somebody else's state, and replaceState
    // on a foreign tree throws away every parameter.
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    reloadFilesFromState();
    applyShaping();
}

void PluginProcessor::reloadFilesFromState()
{
    {
        const juce::ScopedLock lock (rebuildLock);

        for (int slot = 0; slot < ParamID::numSlots; ++slot)
        {
            const auto index = (size_t) slot;
            const juce::String path = apvts.state.getProperty (ParamID::fileProperty[index], juce::String());
            const juce::File file (path);

            // A missing file leaves its slot empty rather than substituted, but the path
            // stays in the state: reopening the session where the file lives finds it
            // again, and the strip can say which one is missing rather than looking as
            // though nothing was ever loaded.
            if (path.isEmpty() || ! file.existsAsFile())
                slots[index].unload();
            else
            {
                const juce::String stored =
                    apvts.state.getProperty (ParamID::sideProperty[index], juce::String());

                slots[index].loadFrom (file,
                                       stored == "left"    ? ImpulseResponse::Side::left
                                       : stored == "right" ? ImpulseResponse::Side::right
                                                           : ImpulseResponse::Side::both);
            }
        }
    }

    ++rebuildCount;
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}
