#pragma once

#include "IconButton.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace Celine
{
    //==========================================================================
    /**
        A folder of cabinet responses, listed so they can be dragged into the strips.

        A browser rather than a managed library: the point of loading cabinets is
        comparing them, and comparing them means trying the next one -- which through
        the Load button is a file dialog per slot, four times over, in the same folder
        each time. A list you drag from turns that into a drag.

        It holds no files of its own and copies nothing. What it shows is whatever is in
        the folder somebody pointed it at, read fresh each time -- so a folder added to
        on disk is a folder that has more in it here, with nothing to re-import.
    */
    class LibraryPanel : public juce::Component,
                         private juce::ListBoxModel
    {
    public:
        LibraryPanel();
        ~LibraryPanel() override;

        /** The folder to list. An empty or missing one leaves the panel saying so
            rather than empty, which reads as broken. */
        void setFolder (const juce::File&);
        juce::File getFolder() const { return folder; }

        /** Told when somebody picks a different folder, so the editor can remember it
            with the rest of the session. */
        std::function<void (const juce::File&)> onFolderChosen;

        /** Told when the export button is pressed. The panel knows nothing about what
            is being exported -- that is the processor's business -- so it asks. */
        std::function<void()> onExport;

        /** Whether there is anything to export, which decides whether the button is
            live. */
        void setCanExport (bool);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** One band, the same height as the graph's tab row beside it. The name, the
            folder and the two buttons all live on that line: a title bar carrying a
            single word that never changes, with a toolbar under it carrying one more
            line of text, was two rows doing one row's work -- and every pixel they gave
            back is a pixel of list. */
        static constexpr int headerHeight = Theme::tabBarHeight;

    private:
        //======================================================================
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

        void listBoxItemClicked (int row, const juce::MouseEvent&) override;

        void chooseFolder();
        void rescan();

        juce::File folder;

        /** One line of the tree: a subfolder to open, or a response to drag.

            `open` is only meaningful in `shown`, and it is what the chevron is drawn
            from rather than the set of folders somebody has opened -- a search opens the
            folders a match lives in without touching that set, and a folder listing its
            contents under a shut arrow reads as the arrow being broken. */
        struct Entry
        {
            juce::File file;
            int depth = 0;
            bool isFolder = false;
            bool open = false;
        };

        /** The whole tree in reading order, and the part of it the search and the shut
            folders leave. Two lists rather than one filtered while painting, because a
            row is painted *and* dragged by index and the two have to mean the same
            file. */
        juce::Array<Entry> entries, shown;

        /** Which folders are open, held by path rather than by index so that rescanning
            a folder somebody has added to does not shut everything they had opened. */
        juce::StringArray expanded;

        void scan (const juce::File& directory, int depth);
        void applyFilter();

        bool isExpanded (const juce::File&) const;

        juce::Label title;

        /** The folder's name lives in here as the placeholder rather than in a label
            beside it. One control instead of two on a row that has to hold four things,
            and the name is still on show until there is something more useful to say --
            which is what you have typed.

            A search reaches the whole tree, open folders or not: somebody typing a
            cabinet's name is asking where it is, and answering only from the folders
            they had already opened would answer a question nobody asked. */
        juce::TextEditor search;

        juce::ListBox list { "Cabinets", this };

        IconButton save { "Export the blend as a stereo response", "download-solid-full.svg" };
        IconButton browse { "Choose a folder of responses", "folder-open-solid-full.svg" };

        std::unique_ptr<juce::FileChooser> chooser;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
    };
} // namespace Celine
