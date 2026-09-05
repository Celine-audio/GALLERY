#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace Celine
{
    //==========================================================================
    /**
        The palette, in one place, shared across the house plugins and sampled from the
        Figma files rather than eyeballed -- which is why the values are odd numbers.

        Two rules. Nothing outside this header names a hex value; and the accents are
        named for the job they do rather than for the colour they are, so a change of
        palette does not have to be chased through the call sites.

        The design is two-tone, and that is the thing to hold on to when adding
        anything: the chrome is dark aubergine and the canvas darker still, while the
        panels you reach into are near-white. A new widget has to know which side of
        that line it sits on, because the text colour flips with it.
    */
    namespace Theme
    {
        //======================================================================
        // Surfaces.

        /** Titlebar, toolbar, inspector, ruler chrome. */
        inline juce::Colour chrome() { return juce::Colour (0xff3b334b); }

        /** The sheet you draw on. */
        inline juce::Colour background() { return juce::Colour (0xff28262e); }

        /** The light panels. */
        inline juce::Colour panel() { return juce::Colour (0xfff9fbff); }

        /** Buttons, fields, dropdowns -- the dark slate that sits on chrome. */
        inline juce::Colour surface() { return juce::Colour (0xff37364a); }

        /** Hover and selection, a step up from surface. */
        inline juce::Colour surfaceBright() { return juce::Colour (0xff4f485d); }

        /** Borders. */
        inline juce::Colour line() { return juce::Colour (0xffd9d9d9); }

        /** The graph's ground, darker than anything else so it reads as a hole. */
        inline juce::Colour consoleBackground() { return juce::Colour (0xff17151a); }

        /** A row in a list. */
        inline juce::Colour pill() { return juce::Colour (0xffdcdee4); }

        /** Grid lines. Barely there on purpose -- they are a ruler you read against,
            not part of the picture. */
        inline juce::Colour grid() { return juce::Colour (0xff5c5c5c); }

        //======================================================================
        // Text. Two families, because of the two-tone split above.

        /** On chrome. Céline White -- the same value the light panels are, because the
            ink on the dark half of the design and the ground on the light half are one
            colour used two ways. It was Monokai's warm off-white, which put a faintly
            yellow white beside a faintly blue one wherever the two halves met. */
        inline juce::Colour text() { return juce::Colour (0xfff9fbff); }
        inline juce::Colour textDim() { return juce::Colour (0xffd9d9d9); }
        inline juce::Colour comment() { return juce::Colour (0xff888791); }

        /** Ink for a control that cannot be used right now. Several steps below
            textDim(), which is the *idle* look of a control that does work: if the two
            were close, "greyed out" and "not hovered" would look the same. */
        inline juce::Colour textDisabled() { return comment(); }

        /** On the light panels, where the above would be invisible. */
        inline juce::Colour textOnPanel() { return juce::Colour (0xff28262e); }

        //======================================================================
        // Accents.

        inline juce::Colour teal() { return juce::Colour (0xff8F63D5); }
        inline juce::Colour violet() { return juce::Colour (0xff9761dc); }

        /** The primary accent: whatever the plugin is doing to the signal. Every filled
            control uses this, so changing it here re-skins the plugin. */
        inline juce::Colour accent() { return violet(); }

        /** A second accent, for when one curve or channel has to be told apart from
            another. Sits opposite the primary on the wheel. */
        inline juce::Colour accentAlt() { return juce::Colour (0xff4fc9e8); }

        /** Anything live and committing. */
        inline juce::Colour record() { return juce::Colour (0xfff92672); }

        //======================================================================
        // The four impulse responses, by identity.
        //
        // Not decoration. A cab loader's whole job is telling four responses apart --
        // on the spectrum, on the waveform, and in the strip that controls them -- so a
        // slot's colour is its name, and the four are chosen to stay distinct from each
        // other rather than to flatter the palette.

        inline juce::Colour irTeal() { return juce::Colour (0xff3ecfc0); }
        inline juce::Colour irRed()  { return juce::Colour (0xffe25a5a); }

        /** Pulled deliberately bluewards of the interface's own accent, which it was
            exactly: the one trace sharing its colour with every knob ring in the window
            made "the purple one" ambiguous in the only place it needed not to be. */
        inline juce::Colour irPurple() { return juce::Colour (0xff6f7ceb); }

        inline juce::Colour irGold() { return juce::Colour (0xffe0b64a); }

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

        inline juce::Colour solo()  { return juce::Colour (0xffcdc292); }
        inline juce::Colour mute()  { return juce::Colour (0xffcd9292); }
        inline juce::Colour phase() { return juce::Colour (0xff6c8b77); }

        /** Ink for a state pill that is lit. The three fills above are pale, so the
            letter on them is the dark ground rather than the light text. */
        inline juce::Colour onPill() { return juce::Colour (0xff17151a); }

        //======================================================================
        // The tab bar over the graph. The library's header beside it is chrome(), the
        // band the toolbar wears: it carries no tabs, so a tab colour on it would say
        // there was something there to switch between.

        /** The tab in front. The accent at a fifth of its strength over the graph's
            ground, flattened to an opaque value rather than composited -- as a
            translucent colour it came out differently against the two grounds it sits
            on, which read as the tab not matching itself. */
        inline juce::Colour tabActive() { return juce::Colour (0xff312441); }

        /** The toolbar's own colour, so a tab that is not in front reads as part of the
            chrome rather than as a control waiting to be pressed. */
        inline juce::Colour tabInactive() { return chrome(); }

        /** The button that throws something away. Worn only while there is something to
            throw: a discard control that is always red is a warning nobody reads by the
            second day. */
        inline juce::Colour discard() { return juce::Colour (0xff6d2d2e); }

        /** The ring round the blend pad's handle.

            A darker violet than the accent every other filled control wears. The pad's
            interior is a wash of the four cabinet colours, and a handle in the ordinary
            accent sat in that as one more coloured thing rather than as the thing you
            move. */
        inline juce::Colour blendHandle() { return juce::Colour (0xff6a449a); }

        /** The top of a graded control, where the setting costs something worth
            noticing. A soft green rather than a louder colour: it marks the setting as
            different from the two below it, not as a warning -- there is nothing wrong
            with being on it, it is only expensive. */
        inline juce::Colour ultra() { return juce::Colour (0xff86c99a); }

        /** The unfilled part of a knob's ring and of the cut slider's track. */
        inline juce::Colour track() { return juce::Colour (0xff565656); }

        /** A control that is armed or in force. */
        inline juce::Colour toolActive() { return teal(); }

        inline juce::Colour danger()  { return juce::Colour (0xfff92672); }
        inline juce::Colour warning() { return juce::Colour (0xffe6db74); }
        inline juce::Colour error()   { return juce::Colour (0xfff92672); }

        //======================================================================
        // Geometry the mockup is consistent about, stated once rather than sprinkled
        // through four files as literals.

        /** Corner radius on every button, field and pill. */
        inline constexpr float cornerRadius = 8.0f;

        /** Border weight on buttons and fields. */
        inline constexpr float borderWidth = 1.2f;

        /** Toolbar buttons are square, and sit on a pitch of size + gap. */
        inline constexpr int buttonSize = 33;
        inline constexpr int buttonGap = 7;

        /** The toolbar band: 33px buttons with 6px of air above and below. */
        inline constexpr int toolbarHeight = 45;

        /** The band a panel's own title sits in. Stated here because two panels wear
            it side by side -- the graph's tabs and the library's header -- and the two
            being a few pixels apart reads as one of them having slipped. */
        inline constexpr int tabBarHeight = 42;
    } // namespace Theme

} // namespace Celine
