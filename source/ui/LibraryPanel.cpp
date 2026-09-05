#include "LibraryPanel.h"

#include "Fonts.h"
#include "PluginLookAndFeel.h"
#include "Theme.h"

using namespace Celine;

namespace
{
    constexpr int gap = 8;
    // Sized against the header rather than shrunk with it: the band came down from
    // 42 and back, and icons scaled with it were a third of the row's height.
    constexpr int iconSize = 28;
    constexpr int rowHeight = 24;

    /** One step in from the folder above. Wide enough that the chevrons line up as a
        column somebody can run down, narrow enough that four levels still leave room
        for a cabinet's name in a panel this width. */
    constexpr int indentWidth = 13;

    /** Room for a chevron at the head of every row, whether or not that row has one --
        a file whose name started where its folder's arrow did would read as a folder
        somebody had forgotten to draw the arrow on. */
    constexpr int chevronWidth = 14;

    /** A library is somebody's cabinet folder, not their home directory, and both of
        these exist so that pointing this at the wrong thing costs a moment rather than
        a hang. Neither is reachable by a folder anybody would actually choose. */
    constexpr int maximumDepth = 6;
    constexpr int maximumEntries = 6000;

    /** The formats the loader reads, written the way juce::File asks for them. Kept
        beside the strip's own copy of the question rather than shared, because the two
        ask it about different things: this one decides what appears in a list, that one
        decides what a drop is allowed to be. */
    const juce::String audioExtensions { "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3" };

    /** The dropdowns' chevron, turned: down when the folder is open, pointing at the
        row when it is shut. */
    void drawChevron (juce::Graphics& g, juce::Rectangle<float> box, bool open,
                      juce::Colour colour)
    {
        const auto reach = juce::jmin (box.getWidth(), box.getHeight()) * 0.3f;

        juce::Path chevron;
        chevron.startNewSubPath (-reach, -reach * 0.55f);
        chevron.lineTo (0.0f, reach * 0.55f);
        chevron.lineTo (reach, -reach * 0.55f);

        chevron.applyTransform (
            juce::AffineTransform::rotation (open ? 0.0f : -juce::MathConstants<float>::halfPi)
                .translated (box.getCentre()));

        g.setColour (colour);
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
    }
}

//==============================================================================
LibraryPanel::LibraryPanel()
{
    title.setText ("LIBRARY", juce::dontSendNotification);

    // The display face at the size a tab sets it, letter for letter -- this header sits
    // beside the graph's tabs on one line across the window, and a title in the body
    // face beside two in the display one reads as a different program.
    //
    // Marked, or it does not survive: getLabelFont is a net under every label in the
    // window and puts the body face on all of them, which is what keeps JUCE's own
    // labels off the platform sans. A face chosen on purpose has to say so.
    title.getProperties().set (keepFontProperty, true);
    title.setFont (Fonts::logo (juce::jmin (24.0f, (float) headerHeight * 0.36f)));
    title.setColour (juce::Label::textColourId, Theme::text());
    title.setJustificationType (juce::Justification::centredLeft);
    title.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title);

    search.setFont (Fonts::light (11.5f));
    search.setColour (juce::TextEditor::backgroundColourId, Theme::background());
    search.setColour (juce::TextEditor::textColourId, Theme::text());
    search.setColour (juce::TextEditor::highlightColourId, Theme::accent().withAlpha (0.4f));

    // No rule around it, like every other field in the window: it is told from the band
    // it sits in by being a darker fill, which is how the rest of them do it.
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);

    search.setJustification (juce::Justification::centredLeft);

    // The default border and top indent between them push the line about three pixels
    // below the middle of a field this short, and the placeholder with it -- both are
    // laid out inside the indented box rather than the field. Padding is the left
    // indent's job here, so the vertical one is nothing.
    search.setBorder (juce::BorderSize<int> (0));
    search.setIndents (10, 0);
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    save.setIconColour (Theme::accent());
    save.setTooltip ("Export the blend as a stereo response.");
    save.setEnabled (false);
    save.onClick = [this] { if (onExport != nullptr) onExport(); };
    addAndMakeVisible (save);

    browse.setIconColour (Theme::accent());
    browse.setTooltip ("Choose a folder of cabinet responses.");
    browse.onClick = [this] { chooseFolder(); };
    addAndMakeVisible (browse);

    list.setRowHeight (rowHeight);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.getViewport()->setScrollBarThickness (8);
    addAndMakeVisible (list);
}

LibraryPanel::~LibraryPanel() = default;

//==============================================================================
void LibraryPanel::setFolder (const juce::File& newFolder)
{
    folder = newFolder;
    rescan();
}

void LibraryPanel::setCanExport (bool canExport)
{
    save.setEnabled (canExport);
    save.setTooltip (canExport ? "Export the blend as a stereo response."
                               : "Load a cabinet before exporting the blend.");
}

void LibraryPanel::rescan()
{
    entries.clear();

    if (folder.isDirectory())
        scan (folder, 0);

    search.setTextToShowWhenEmpty (folder.isDirectory() ? "Search in " + folder.getFileName() + "..."
                                                        : "Choose a folder...",
                                   Theme::comment());
    search.setTooltip (folder.isDirectory() ? folder.getFullPathName() : juce::String());

    applyFilter();
}

void LibraryPanel::scan (const juce::File& directory, int depth)
{
    if (depth > maximumDepth || entries.size() >= maximumEntries)
        return;

    // Sorted, because a folder's order on disk is whatever the filesystem feels like
    // and a list that reorders itself between sessions is a list nobody can learn the
    // shape of. Folders before files at each level, the way every other browser does it.
    juce::Array<juce::File> subfolders, found;

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, "*",
                                                            juce::File::findDirectories))
        subfolders.add (entry.getFile());

    for (const auto& entry : juce::RangedDirectoryIterator (directory, false, audioExtensions,
                                                            juce::File::findFiles))
        found.add (entry.getFile());

    subfolders.sort();
    found.sort();

    for (const auto& child : subfolders)
    {
        entries.add ({ child, depth, true });
        scan (child, depth + 1);
    }

    for (const auto& file : found)
        entries.add ({ file, depth, false });
}

bool LibraryPanel::isExpanded (const juce::File& directory) const
{
    return expanded.contains (directory.getFullPathName());
}

void LibraryPanel::applyFilter()
{
    const auto wanted = search.getText().trim();

    shown.clear();

    if (wanted.isEmpty())
    {
        // Everything down to the first shut folder on each branch. `hidden` is the
        // depth of that folder: rows deeper than it belong to it and are skipped until
        // one comes back at its own level or above.
        auto hidden = -1;

        for (const auto& entry : entries)
        {
            if (hidden >= 0 && entry.depth > hidden)
                continue;

            hidden = -1;

            auto row = entry;
            row.open = entry.isFolder && isExpanded (entry.file);
            shown.add (row);

            if (entry.isFolder && ! row.open)
                hidden = entry.depth;
        }
    }
    else
    {
        // A match, and every folder above it, so a hit still says where it lives. The
        // entries are in reading order, so the folders standing open above a row are
        // exactly the ones on this stack.
        juce::Array<int> open;
        juce::Array<bool> keep;

        keep.insertMultiple (0, false, entries.size());

        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];

            open.removeRange (entry.depth, open.size());

            if (entry.isFolder)
            {
                open.add (i);
                continue;
            }

            if (! entry.file.getFileNameWithoutExtension().containsIgnoreCase (wanted))
                continue;

            keep.set (i, true);

            for (const auto ancestor : open)
                keep.set (ancestor, true);
        }

        // Every folder that survived is standing open, because the only reason it is
        // here is the matches listed under it.
        for (int i = 0; i < entries.size(); ++i)
        {
            if (! keep[i])
                continue;

            auto row = entries[i];
            row.open = row.isFolder;
            shown.add (row);
        }
    }

    list.updateContent();
    list.deselectAllRows();
    repaint();
}

void LibraryPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder of cabinet responses",
                                                   folder.isDirectory() ? folder : juce::File());

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& browser)
                          {
                              const auto chosen = browser.getResult();

                              if (! chosen.isDirectory())
                                  return;

                              setFolder (chosen);

                              if (onFolderChosen != nullptr)
                                  onFolderChosen (chosen);
                          });
}

//==============================================================================
int LibraryPanel::getNumRows()
{
    return shown.size();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                     bool selected)
{
    if (! juce::isPositiveAndBelow (row, shown.size()))
        return;

    const auto& entry = shown.getReference (row);

    auto bounds = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (2.0f, 1.0f);

    if (selected && ! entry.isFolder)
    {
        g.setColour (Theme::accent().withAlpha (0.75f));
        g.fillRoundedRectangle (bounds, Theme::cornerRadius - 2.0f);
    }

    bounds.removeFromLeft ((float) (entry.depth * indentWidth));

    const auto chevron = bounds.removeFromLeft ((float) chevronWidth);

    if (entry.isFolder)
        drawChevron (g, chevron, entry.open, Theme::comment());

    // A folder is somewhere to go rather than something to load, so it is drawn as the
    // brighter of the two: the names under it are what the eye should be running down.
    g.setColour (entry.isFolder ? Theme::text()
               : selected       ? Theme::text()
                                : Theme::textDim());

    g.setFont (Fonts::light (11.5f));

    // Without the extension. Every one of them is an audio file -- that is what the
    // list is -- so four characters of ".wav" on every row is four characters of the
    // name gone at the width this panel actually has.
    g.drawText (entry.isFolder ? entry.file.getFileName()
                               : entry.file.getFileNameWithoutExtension(),
                bounds.reduced (4.0f, 0.0f), juce::Justification::centredLeft, true);
}

juce::var LibraryPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty())
        return {};

    const auto row = rows[0];

    if (! juce::isPositiveAndBelow (row, shown.size()))
        return {};

    const auto& entry = shown.getReference (row);

    // A folder is not a cabinet, and a strip that accepted one would have to guess which
    // of the things inside it was meant.
    if (entry.isFolder)
        return {};

    // The path, which is all a strip needs to load it. Carried as the drag's
    // description rather than as an external file drag so that a drag inside the window
    // stays inside it -- an OS file drag would hand the file to whatever is behind the
    // plugin as readily as to a strip.
    return entry.file.getFullPathName();
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent& event)
{
    if (! juce::isPositiveAndBelow (row, shown.size()))
        return;

    const auto& entry = shown.getReference (row);

    if (! entry.isFolder)
        return;

    // The second press of a double-click arrives here as well, and toggling on both
    // would open the folder and shut it again -- which reads as the arrow not working.
    if (event.getNumberOfClicks() > 1)
        return;

    // While a search is running it decides what is open, so a click would move a set
    // nothing on screen is drawn from: the arrow would do nothing now and something
    // unasked-for once the search was cleared.
    if (search.getText().isNotEmpty())
        return;

    const auto path = entry.file.getFullPathName();

    if (! expanded.contains (path))
        expanded.add (path);
    else
        expanded.removeString (path);

    applyFilter();
}

void LibraryPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    juce::ignoreUnused (row);
}

//==============================================================================
void LibraryPanel::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();

    // The header, in the house chrome the toolbar above it wears, rounded at the top
    // the way the graph's tabs are so the two read as one row across the window.
    juce::Path header;
    header.addRoundedRectangle (full.getX(), full.getY(),
                                full.getWidth(), (float) headerHeight,
                                Theme::cornerRadius, Theme::cornerRadius,
                                true, true, false, false);

    g.setColour (Theme::chrome());
    g.fillPath (header);

    auto area = full.withTrimmedTop ((float) headerHeight);

    // The body, rounded at the bottom to match the graph beside it.
    g.setColour (Theme::background());
    g.fillRoundedRectangle (area, Theme::cornerRadius);
    g.fillRect (area.withHeight (Theme::cornerRadius));

    if (! shown.isEmpty())
        return;

    // Something to read in the space the list would fill, which is different from an
    // empty box: one says there is nothing here, the other says nothing at all.
    const auto empty = area.reduced (16.0f, 24.0f);

    g.setColour (Theme::comment());
    g.setFont (Fonts::light (11.5f));
    g.drawFittedText (! folder.isDirectory() ? "Choose a folder to list cabinets,\n"
                                               "then drag one onto a strip"
                      : entries.isEmpty()     ? "No responses in this folder"
                                              : "Nothing matches that",
                      empty.toNearestInt(), juce::Justification::centredTop, 3);
}

void LibraryPanel::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight).reduced (gap + 2, 0);

    // Export on the right, choosing a folder beside it. The two read in the order they
    // are used: point the panel at a folder, then take something out of it.
    auto buttons = header.withSizeKeepingCentre (header.getWidth(), iconSize);

    save.setBounds (buttons.removeFromRight (iconSize));
    buttons.removeFromRight (gap - 3);
    browse.setBounds (buttons.removeFromRight (iconSize));

    header.removeFromRight (iconSize * 2 + gap + 2);

    // The name takes exactly what it needs and the folder takes the rest, right-aligned
    // against the buttons -- so a long folder name grows towards the middle of the row
    // rather than pushing the name off the left of it.
    //
    // Measured rather than guessed. The display face is a good deal wider than the body
    // one at the same height, so a column picked to fit the word in one of them cuts it
    // off in the other -- which is what "LIBRA..." was.
    const auto needed = juce::GlyphArrangement::getStringWidthInt (title.getFont(),
                                                                   title.getText()) + 6;

    title.setBounds (header.removeFromLeft (juce::jmin (needed, header.getWidth() / 2)));
    header.removeFromLeft (gap);
    search.setBounds (header.withSizeKeepingCentre (header.getWidth(), iconSize));

    // The same air on every side. The rows carry their own inset, so anything uneven
    // here shows up as the list sitting off-centre in its own panel.
    list.setBounds (area.reduced (gap));
}
