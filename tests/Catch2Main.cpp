// All test files are included in the executable via the Glob in CMakeLists.txt

#include "juce_gui_basics/juce_gui_basics.h"
#include <Settings.h>
#include <catch2/catch_session.hpp>

int main (int argc, char* argv[])
{
    // This lets us use JUCE's MessageManager without leaking.
    // PluginProcessor might need this if you use the APVTS for example.
    // You'll also need it for tests that rely on juce::Graphics, juce::Timer, etc.
    // It's nicer DX when placed here vs. manually in Catch2 SECTIONs
    juce::ScopedJuceInitialiser_GUI gui;

    // Nothing in here reads or writes the settings the person running it chose for
    // themselves. Opening an editor asks for the library folder, and left pointing at
    // the real file the suite would scan whatever happened to be in it -- a different
    // answer on every machine, and a slow one on a full library.
    const juce::TemporaryFile settings (".settings");
    Celine::Settings::useFile (settings.getFile());

    const int result = Catch::Session().run (argc, argv);

    return result;
}
#include <catch2/catch_test_macros.hpp>
