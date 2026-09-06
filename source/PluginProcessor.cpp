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
    return ImpulseResponse::maximumSeconds + (double) IrSlot::maximumAlignMs * 0.001;
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
    analysisMix.setSize (1, samplesPerBlock, false, true, true);

    // Longer than a slot's own gain ramp on purpose. Emptying the last cabinet fades
    // the cabinets out and the dry signal in, and the two overlapping would be the
    // input comb-filtered against itself; the slower half arrives after the cabinets
    // have gone.
    wetMix.reset (sampleRate, 0.05);
    wetMix.setCurrentAndTargetValue (0.0f);

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
PluginProcessor::Blend PluginProcessor::measureBlend() const noexcept
{
    Blend result;

    // Solo belongs to the set rather than to a slot, so it is settled first.
    auto anySoloed = false;

    for (const auto& p : slotParameters)
        anySoloed |= Parameters::SlotPointers::isOn (p.solo);

    const auto raw = Parameters::blendWeights (blendXParameter->load(),
                                               blendYParameter->load());

    std::array<bool, (size_t) ParamID::numSlots> heard {};
    auto total = 0.0f;
    auto count = 0;

    for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
    {
        const auto& p = slotParameters[slot];
        const auto loaded = slots[slot].isActive();

        result.anyLoaded |= loaded;

        heard[slot] = loaded
                   && ! Parameters::SlotPointers::isOn (p.mute)
                   && (Parameters::SlotPointers::isOn (p.solo) || ! anySoloed);

        if (heard[slot])
        {
            total += raw[slot];
            ++count;
        }
    }

    // Renormalised across whatever is being heard, so the shares still sum to one and
    // the plugin stays at the level it was. This is what stops muting one of four from
    // quietening the other three -- the same rule the old per-slot trim followed, now
    // that the shares are unequal.
    constexpr auto negligible = 1.0e-6f;

    if (total > negligible)
    {
        for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
            result.weight[slot] = heard[slot] ? raw[slot] / total : 0.0f;

        return result;
    }

    // The dot is sitting on a corner whose cabinet is not one of the ones being heard,
    // so every share that survived is zero. Solo is an instruction to listen to
    // something and has to produce something to listen to, so the soloed cabinets share
    // the output equally; a mute is an instruction not to, and answers with silence.
    if (anySoloed && count > 0)
        for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
            result.weight[slot] = heard[slot] ? 1.0f / (float) count : 0.0f;

    return result;
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

    if (bypassed || ! usable)
    {
        // Bypass takes the trim with it, so A/B-ing compares like with like.
        applyOutputGain (buffer, bypassed ? 0.0f : outputGainParameter->load());
        measureOutput (buffer, juce::jmax (0, numChannels), numSamples);
        return;
    }

    const auto blend = measureBlend();

    // Loaded, not audible: four muted cabinets are still four cabinets, and the answer
    // to muting them is silence rather than the signal that went in.
    wetMix.setTargetValue (blend.anyLoaded ? 1.0f : 0.0f);

    if (! blend.anyLoaded && ! wetMix.isSmoothing() && wetMix.getCurrentValue() <= 0.0f)
    {
        applyOutputGain (buffer, outputGainParameter->load());
        measureOutput (buffer, numChannels, numSamples);
        return;
    }

    juce::AudioBuffer<float> dry (buffer.getArrayOfWritePointers(), numChannels, 0, numSamples);
    juce::AudioBuffer<float> summed (wet.getArrayOfWritePointers(), numChannels, 0, numSamples);

    sumSlots (dry, summed, blend);
    mixWetIntoDry (buffer, summed, numChannels, numSamples);

    applyOutputGain (buffer, outputGainParameter->load());
    measureOutput (buffer, numChannels, numSamples);
}

void PluginProcessor::sumSlots (const juce::AudioBuffer<float>& dry,
                                juce::AudioBuffer<float>& summed,
                                const Blend& blend) noexcept
{
    summed.clear();

    for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
    {
        const auto& p = slotParameters[slot];

        IrSlot::Levels levels;
        levels.weight = blend.weight[slot];
        levels.pan = p.pan->load();
        levels.inverted = Parameters::SlotPointers::isOn (p.phase);
        levels.alignMs = p.align->load();
        levels.lowCutHz = p.lowCut->load();
        levels.highCutHz = p.highCut->load();
        levels.lowCutSlope = Parameters::slopeForChoice ((int) p.lowCutSlope->load());
        levels.highCutSlope = Parameters::slopeForChoice ((int) p.highCutSlope->load());

        slots[slot].setLevels (levels);
        slots[slot].process (dry, scratch, summed);
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
    // The longest of whatever is loaded, so an export is long enough for every cabinet
    // in the blend rather than for the first one asked about.
    auto longest = Parameters::responseSeconds (0);

    for (int slot = 0; slot < ParamID::numSlots; ++slot)
        if (isSlotLoaded (slot))
            longest = juce::jmax (longest, Parameters::responseSeconds (
                                               (int) slotParameters[(size_t) slot].resolution->load()));

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
