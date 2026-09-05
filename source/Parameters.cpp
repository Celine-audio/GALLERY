#include "Parameters.h"

#include "dsp/CutFilter.h"
#include "dsp/IrSlot.h"

namespace Parameters
{
    juce::String frequencyText (float hz)
    {
        if (hz < 1000.0f)
            return juce::String (juce::roundToInt (hz)) + " Hz";

        // One decimal below ten kilohertz, none above it, and no trailing zero either
        // way: "6.4 kHz", "10 kHz", "2 kHz" -- never "6.35 kHz" or "2.0 kHz".
        const auto kilohertz = hz / 1000.0f;

        if (kilohertz >= 10.0f)
            return juce::String (juce::roundToInt (kilohertz)) + " kHz";

        const auto rounded = juce::roundToInt (kilohertz * 10.0f);

        if (rounded % 10 == 0)
            return juce::String (rounded / 10) + " kHz";

        return juce::String (kilohertz, 1) + " kHz";
    }

    BlendWeights blendWeights (float x, float y) noexcept
    {
        // Onto the unit square, with v measured downwards so that slot 0 is the
        // top-left corner in the coordinates the pad is drawn in.
        const auto u = (juce::jlimit (-1.0f, 1.0f, x) + 1.0f) * 0.5f;
        const auto v = (1.0f - juce::jlimit (-1.0f, 1.0f, y)) * 0.5f;

        // Bilinear, then lifted off the floor: every cabinet keeps an even share of
        // `blendFloor` and the pad divides what is left. The shares still sum to one,
        // because the floor is shared out rather than added on.
        constexpr auto even = blendFloor / (float) ParamID::numSlots;
        constexpr auto span = 1.0f - blendFloor;

        return { even + span * (1.0f - u) * (1.0f - v),
                 even + span * u * (1.0f - v),
                 even + span * (1.0f - u) * v,
                 even + span * u * v };
    }

    AllSlotPointers bindSlots (juce::AudioProcessorValueTreeState& state)
    {
        AllSlotPointers slots;

        for (size_t slot = 0; slot < (size_t) ParamID::numSlots; ++slot)
        {
            auto& p = slots[slot];

            p.solo = state.getRawParameterValue (ParamID::solo[slot]);
            p.mute = state.getRawParameterValue (ParamID::mute[slot]);
            p.phase = state.getRawParameterValue (ParamID::phase[slot]);
            p.resolution = state.getRawParameterValue (ParamID::resolution[slot]);
            p.pan = state.getRawParameterValue (ParamID::pan[slot]);
            p.lowCut = state.getRawParameterValue (ParamID::lowCut[slot]);
            p.highCut = state.getRawParameterValue (ParamID::highCut[slot]);
            p.lowCutSlope = state.getRawParameterValue (ParamID::lowCutSlope[slot]);
            p.highCutSlope = state.getRawParameterValue (ParamID::highCutSlope[slot]);
            p.align = state.getRawParameterValue (ParamID::align[slot]);
        }

        return slots;
    }

    namespace
    {
        // Attribute helpers. JUCE will happily render a gain parameter as
        // "0.7071068", so every parameter that a human reads gets a string function.
        // They live here rather than at each declaration so that two parameters of
        // the same kind cannot drift into formatting themselves differently.

        juce::AudioParameterFloatAttributes decibels()
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int)
                {
                    return (v > 0.0f ? "+" : "") + juce::String (v, 1);
                })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
        }

        /** A frequency control wants far more resolution at the bottom than the top:
            without a skew, half the travel is spent above 10 kHz. The centre is the
            frequency that lands under the middle of the knob. */
        juce::NormalisableRange<float> frequencyRange (float low, float high, float centre)
        {
            juce::NormalisableRange<float> range { low, high };
            range.setSkewForCentre (centre);
            return range;
        }

        juce::AudioParameterFloatAttributes frequency()
        {
            return juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int) { return frequencyText (v); })
                .withValueFromStringFunction ([] (const juce::String& t)
                {
                    const auto value = t.getFloatValue();
                    return t.containsIgnoreCase ("k") ? value * 1000.0f : value;
                });
        }

        /** Two decimals and a unit, because the whole point of the control is the
            fraction of a millisecond by which two captures disagree. A whole-number
            readout would round away the thing being adjusted. */
        juce::AudioParameterFloatAttributes milliseconds()
        {
            return juce::AudioParameterFloatAttributes()
                .withLabel ("ms")
                .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
        }

        /** Which side, and how far. A bare signed number was what the mockup drew, but
            it leaves "17" meaning either side depending on a knob pointer a few pixels
            across; L and R cost the same width and say it outright. */
        juce::AudioParameterFloatAttributes panning()
        {
            return juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int)
                {
                    const auto amount = juce::roundToInt (std::abs (v) * 100.0f);

                    if (amount == 0)
                        return juce::String ("C");

                    return (v < 0.0f ? "L" : "R") + juce::String (amount);
                })
                .withValueFromStringFunction ([] (const juce::String& t)
                {
                    const auto amount = t.retainCharacters ("0123456789.").getFloatValue() * 0.01f;
                    return t.containsIgnoreCase ("L") ? -amount : amount;
                });
        }

        /** Which way along an axis, and how far -- the same shape of answer the pan
            readout gives, because it is the same question. */
        juce::AudioParameterFloatAttributes blendAxis()
        {
            return juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float v, int)
                {
                    const auto amount = juce::roundToInt (std::abs (v) * 100.0f);

                    if (amount == 0)
                        return juce::String ("C");

                    return (v < 0.0f ? "-" : "+") + juce::String (amount);
                })
                .withValueFromStringFunction ([] (const juce::String& t)
                {
                    return t.retainCharacters ("-0123456789.").getFloatValue() * 0.01f;
                });
        }

        //======================================================================
        // Declaration helpers. The version hint of 1 on every ParameterID is what
        // lets a later version add parameters without invalidating saved state.

        auto boolParam (const char* id, const juce::String& name, bool defaultValue)
        {
            return std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { id, 1 }, name, defaultValue);
        }

        auto floatParam (const char* id, const juce::String& name,
                         juce::NormalisableRange<float> range, float defaultValue,
                         juce::AudioParameterFloatAttributes attributes = {})
        {
            return std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, 1 }, name, range, defaultValue, std::move (attributes));
        }

        auto slopeParam (const char* id, const juce::String& name)
        {
            juce::StringArray choices;

            for (auto slope : slopes)
                choices.add (juce::String (slope) + " dB");

            // 12 dB per octave: steep enough to do something, shallow enough not to
            // ring where a cabinet's own bottom end lives.
            return std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { id, 1 }, name, choices, 1);
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add (boolParam (ParamID::bypass, "Bypass", false));

        layout.add (floatParam (ParamID::outputGain, "Output",
                                juce::NormalisableRange<float> { -12.0f, 12.0f, 0.1f },
                                0.0f, decibels()));

        // The blend, as the pad's own coordinates. Centred by default, which is all
        // four cabinets in equal measure -- the setting somebody who has just loaded
        // four captures wants before they have decided anything.
        layout.add (floatParam (ParamID::blendX, "Blend X",
                                juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f },
                                0.0f, blendAxis()));

        layout.add (floatParam (ParamID::blendY, "Blend Y",
                                juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f },
                                0.0f, blendAxis()));

        // One group per cabinet, in the order the strips appear across the window, so
        // a host's generic editor reads the same way the plugin does.
        for (int slot = 0; slot < ParamID::numSlots; ++slot)
        {
            const auto index = (size_t) slot;
            const auto prefix = "IR " + juce::String (slot + 1) + " ";

            layout.add (boolParam (ParamID::solo[index],  prefix + "Solo",  false));
            layout.add (boolParam (ParamID::mute[index],  prefix + "Mute",  false));
            layout.add (boolParam (ParamID::phase[index], prefix + "Phase", false));
            {
                juce::StringArray choices;

                for (const auto* name : resolutionNames)
                    choices.add (name);

                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    juce::ParameterID { ParamID::resolution[index], 1 },
                    prefix + "Resolution", choices, defaultResolution));
            }

            // Skewed towards the bottom: two captures of one cabinet differ by a
            // fraction of a millisecond, and an unskewed control spends nine tenths of
            // its travel on distances nobody is aligning.
            layout.add (floatParam (ParamID::align[index], prefix + "Align",
                                    frequencyRange (0.0f, IrSlot::maximumAlignMs, 2.0f),
                                    0.0f, milliseconds()));

            layout.add (floatParam (ParamID::pan[index], prefix + "Pan",
                                    juce::NormalisableRange<float> { -1.0f, 1.0f, 0.01f },
                                    0.0f, panning()));

            // Both cuts sit at the end of their travel by default, which is where each
            // of them is switched off -- see CutFilter, which treats the two ends as
            // exactly that rather than as a filter doing almost nothing.
            layout.add (floatParam (ParamID::lowCut[index], prefix + "Low Cut",
                                    frequencyRange (CutFilter::lowestHz, CutFilter::highestHz, 500.0f),
                                    CutFilter::lowestHz, frequency()));

            layout.add (floatParam (ParamID::highCut[index], prefix + "High Cut",
                                    frequencyRange (CutFilter::lowestHz, CutFilter::highestHz, 2000.0f),
                                    CutFilter::highestHz, frequency()));

            layout.add (slopeParam (ParamID::lowCutSlope[index],  prefix + "Low Cut Slope"));
            layout.add (slopeParam (ParamID::highCutSlope[index], prefix + "High Cut Slope"));
        }

        return layout;
    }
}
