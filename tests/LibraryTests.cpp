#include <ui/LibraryPanel.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("Opening a folder in the library leaves the list, and its selection, where they were", "[ui][library]")
{
    // A folder of responses long enough to scroll: two subfolders and plenty loose.
    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("gallery-library-" + juce::Uuid().toString());

    for (const auto* folder : { "Greenbacks", "V30" })
        for (int i = 0; i < 20; ++i)
            root.getChildFile (folder).getChildFile (juce::String (folder) + " " + juce::String (i) + ".wav").create();

    for (int i = 0; i < 40; ++i)
        root.getChildFile ("Loose " + juce::String (i).paddedLeft ('0', 2) + ".wav").create();

    {
        Celine::LibraryPanel library;
        library.setSize (260, 220);
        library.setFolder (root);

        juce::ListBox* list = nullptr;

        for (auto* child : library.getChildren())
            if (auto* found = dynamic_cast<juce::ListBox*> (child))
                list = found;

        REQUIRE (list != nullptr);

        // A loose file selected, then the list scrolled as far down as it goes.
        list->selectRow (list->getListBoxModel()->getNumRows() - 1);
        const auto selected = list->getSelectedRow();
        list->getViewport()->setViewPosition (0, 100000);

        const auto before = library.getScrollPosition();
        REQUIRE (before > 0);

        // A folder opening above it: the list stays put, and the selection follows its
        // file down by the twenty rows that appeared.
        library.toggleFolder (root.getChildFile ("V30"));
        CHECK (library.getScrollPosition() == before);
        CHECK (list->getSelectedRow() == selected + 20);
    }

    root.deleteRecursively();
}
