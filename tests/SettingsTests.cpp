#include <Settings.h>

#include <catch2/catch_test_macros.hpp>

/*
    The folder somebody keeps their cabinets in is a property of the person, not of the
    project -- so it has to outlive the instance that was told about it. These run
    against a disposable file: the real one holds whatever the person running the tests
    chose for themselves.
*/

TEST_CASE ("A chosen folder outlives the instance that chose it", "[settings]")
{
    juce::TemporaryFile temporary (".settings");
    Celine::Settings::useFile (temporary.getFile());

    juce::TemporaryFile folderHolder (".dir");
    const auto folder = folderHolder.getFile();
    folder.createDirectory();

    Celine::Settings::setLibraryFolder (folder);

    // Read back through a second open, which is what a second instance does. Held open
    // instead, this would pass while the setting was only ever in memory.
    CHECK (Celine::Settings::libraryFolder() == folder);
    CHECK (temporary.getFile().existsAsFile());
}

TEST_CASE ("Never chosen is not a folder", "[settings]")
{
    // Rather than something plausible-looking that does not exist: the panel says so,
    // and it can only say so if it is asked a question with an honest answer.
    juce::TemporaryFile temporary (".settings");
    Celine::Settings::useFile (temporary.getFile());

    CHECK_FALSE (Celine::Settings::libraryFolder().isDirectory());
}
