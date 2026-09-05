// GALLERY's own colour accessors, included inside namespace Celine::Theme by
// ui/Theme.h. The roles behind them are declared in PluginThemeRoles.h.
//
// No include guard and no includes of its own: this is a fragment, included at one
// point inside a namespace, and anything it needs is already there.

        //======================================================================
        // The four impulse responses, by identity.
        //
        // Not decoration. A cab loader's whole job is telling four responses apart --
        // on the spectrum, on the waveform, and in the strip that controls them -- so a
        // slot's colour is its name, and the four are chosen to stay distinct from each
        // other rather than to flatter the palette. Which is also why they are in the
        // theme: a palette that could not reach them could not re-skin this plugin.

        inline juce::Colour irTeal() { return colour (Role::irTeal); }
        inline juce::Colour irRed()  { return colour (Role::irRed); }

        /** Pulled deliberately bluewards of the interface's own accent, which it was
            exactly: the one trace sharing its colour with every knob ring in the window
            made "the purple one" ambiguous in the only place it needed not to be. */
        inline juce::Colour irPurple() { return colour (Role::irPurple); }

        inline juce::Colour irGold() { return colour (Role::irGold); }

        /** Slot 0 to 3, in the order they appear across the window. Out of range answers
            gold rather than asserting: a wrong hue is a better failure than a missing
            trace. */
        inline juce::Colour irSlot (int index)
        {
            switch (index)
            {
                case 0:  return irTeal();
                case 1:  return irRed();
                case 2:  return irPurple();
                default: return irGold();
            }
        }

        //======================================================================
        // The three states a slot can be put into, which wear the same colours in every
        // strip. Deliberately not the slot's own colour: solo means the same thing on
        // all four, and colouring it by slot would say the opposite.

        inline juce::Colour solo()  { return colour (Role::solo); }
        inline juce::Colour mute()  { return colour (Role::mute); }
        inline juce::Colour phase() { return colour (Role::phase); }

        /** Ink for a state pill that is lit. The three fills above are pale, so the
            letter on them is the dark ground rather than the light text. */
        inline juce::Colour onPill() { return colour (Role::onPill); }

        //======================================================================
        // The tab bar over the graph. The library's header beside it is chrome(), the
        // band the toolbar wears: it carries no tabs, so a tab colour on it would say
        // there was something there to switch between.

        /** The tab in front. The accent at a fifth of its strength over the graph's
            ground, flattened to an opaque value rather than composited -- as a
            translucent colour it came out differently against the two grounds it sits
            on, which read as the tab not matching itself. */
        inline juce::Colour tabActive() { return colour (Role::tabActive); }

        /** The toolbar's own colour, so a tab that is not in front reads as part of the
            chrome rather than as a control waiting to be pressed. Its own role rather
            than an alias of chrome(): a theme that moved one and not the other would
            otherwise have no way to say so. */
        inline juce::Colour tabInactive() { return colour (Role::tabInactive); }

        /** The button that throws something away. Worn only while there is something to
            throw: a discard control that is always red is a warning nobody reads by the
            second day. */
        inline juce::Colour discard() { return colour (Role::discard); }

        /** The ring round the blend pad's handle.

            A darker violet than the accent every other filled control wears. The pad's
            interior is a wash of the four cabinet colours, and a handle in the ordinary
            accent sat in that as one more coloured thing rather than as the thing you
            move. */
        inline juce::Colour blendHandle() { return colour (Role::blendHandle); }

        /** The top of a graded control, where the setting costs something worth
            noticing. A soft green rather than a louder colour: it marks the setting as
            different from the two below it, not as a warning -- there is nothing wrong
            with being on it, it is only expensive. */
        inline juce::Colour ultra() { return colour (Role::ultra); }
