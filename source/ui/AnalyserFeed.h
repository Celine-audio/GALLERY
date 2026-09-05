#pragma once

#include "../Parameters.h"
#include "../dsp/CutFilter.h"
#include "../dsp/ImpulseResponse.h"
#include "../dsp/MixSpectrum.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <vector>

class PluginProcessor;

namespace Celine
{
    class AnalyserGraph;

    //==========================================================================
    /**
        Everything the graph draws, gathered and handed to it.

        Its own class rather than a handful of methods on the editor, because feeding
        the graph is most of what the editor was: four responses, eleven parameters
        apiece and a blend, composed three different ways for two views. None of that
        is layout, and none of it is a control being wired to a parameter.

        **Nothing here is driven by the controls that change it.** What the graph draws
        depends on forty-odd parameters and four files, any of which a host can move
        without this window being touched -- so a display wired to its own controls
        would be right except when automation is running. `hasChanged` polls instead,
        which costs one array comparison a frame and is what most frames do.
    */
    class AnalyserFeed
    {
    public:
        AnalyserFeed (PluginProcessor&, AnalyserGraph&);

        /** Sizes the mix measurement and the display filters for the host's rate. */
        void prepare (double sampleRate);

        /** Redraws whichever view is in front. */
        void refresh();

        /** Whether anything the two pictures depend on has moved since the last time
            this was asked. */
        bool hasChanged();

        /** The live trace behind the cabinets. Pulled every tick rather than only on a
            change, because it is the one thing on the graph that moves on its own. */
        void refreshOutput();

    private:
        /** Hands each cabinet, and the cuts it is heard through, to the shared
            filtering cache. Both views are drawn from it, so it runs before either. */
        void refreshResponses();

        void refreshSpectrum();
        void refreshMix();
        void refreshWaveform();

        bool anySoloed() const;

        /** A slot's colour, dimmed when nothing is listening to it. */
        juce::Colour traceColour (int slot, bool soloedElsewhere) const;

        /** Everything the two pictures depend on, gathered so that "has anything moved"
            is one comparison rather than forty questions. Fixed size and held by value:
            this runs thirty times a second, and a vector built and thrown away each
            time is a heap allocation per frame for a result nearly always identical to
            the last. */
        static constexpr int signatureSize = 7 + ParamID::numSlots * 9;
        using Signature = std::array<float, (size_t) signatureSize>;

        Signature signature() const;

        PluginProcessor& processorRef;
        AnalyserGraph& graph;

        Parameters::AllSlotPointers slotParameters;

        // The blend as one curve. Its own object because the filtering behind it is
        // cached: see MixSpectrum for why the four are summed as waveforms rather than
        // as curves, which is the whole reason this is not a weighted average of the
        // four measurements the slots already hand over.
        MixSpectrum mixSpectrum;

        // The graph's own copies of the cut filters, used only to ask what shape to
        // draw. The slots' filters are the audio thread's and reading their
        // coefficients from here would be a race; these are configured from the same
        // parameters and give the same answer, on this thread, where the drawing
        // happens.
        ParamID::PerSlot<CutFilter> displayLowCut {
            CutFilter { CutFilter::Kind::lowCut }, CutFilter { CutFilter::Kind::lowCut },
            CutFilter { CutFilter::Kind::lowCut }, CutFilter { CutFilter::Kind::lowCut }
        };

        ParamID::PerSlot<CutFilter> displayHighCut {
            CutFilter { CutFilter::Kind::highCut }, CutFilter { CutFilter::Kind::highCut },
            CutFilter { CutFilter::Kind::highCut }, CutFilter { CutFilter::Kind::highCut }
        };

        Signature lastSignature {};
        bool hasSignature = false;

        // Reused rather than allocated per frame.
        std::vector<float> mixScratch, spectrumScratch, composedScratch, outputScratch;
        std::vector<ImpulseResponse::Column> summaryScratch;
        juce::AudioBuffer<float> responseScratch;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyserFeed)
    };
} // namespace Celine
