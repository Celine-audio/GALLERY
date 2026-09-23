/*
    The colours ThemeReachTests cannot see.

    That test moves every role, re-renders the window, and fails if a shipped colour is
    still on screen -- which only catches a straggler that draws its colour in the state
    the test happens to find it in. The three state pills on a strip draw their lit
    colour only while they are lit, and the test's slots are all off, so a colour taken
    once at construction sat there unnoticed: changing Solo, Mute or Polarity in the
    theme editor moved nothing until the window was reopened.
*/
#include <ui/LetterToggleButton.h>
#include <CelineUI/Theme.h>
#include <CelineUI/ThemePalette.h>

#include <catch2/catch_test_macros.hpp>

using namespace Celine;

namespace
{
    int pixelsOf (const juce::Image& image, juce::Colour wanted)
    {
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
        int count = 0;

        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
                if (data.getPixelColour (x, y).getARGB() == wanted.getARGB())
                    ++count;

        return count;
    }
}

TEST_CASE ("A lit state pill follows the theme", "[theme]")
{
    const struct Restore { ~Restore() { Theme::palette().reset(); } } restore;

    LetterToggleButton solo { "Solo", "S", Theme::Role::solo };
    solo.setBounds (0, 0, 28, 22);
    solo.setToggleState (true, juce::dontSendNotification);

    INFO ("lit, it should be filled with the shipped Solo colour");
    REQUIRE (pixelsOf (solo.createComponentSnapshot (solo.getLocalBounds(), false, 1.0f),
                       Theme::solo()) > 100);

    Theme::palette().set (Theme::Role::solo, juce::Colour (0xff20c020));
    Theme::palette().sendSynchronousChangeMessage();

    const auto after = solo.createComponentSnapshot (solo.getLocalBounds(), false, 1.0f);

    CHECK (pixelsOf (after, juce::Colour (0xff20c020)) > 100);
    CHECK (pixelsOf (after, juce::Colour (0xffcdc292)) == 0);
}
