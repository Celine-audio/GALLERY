#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace Celine
{
    //==========================================================================
    /**
        The choices that belong to the person rather than to the session.

        A plugin's state travels with the project it was saved in, which is right for
        everything that makes a sound and wrong for the folder somebody keeps their
        cabinets in: that is one folder, on one machine, and having to point every new
        instance at it again is what stops a library from being used at all.

        Kept in a settings file under the company folder, so it is one answer across
        every instance, every project and every host.
    */
    namespace Settings
    {
        /** The folder the library lists. Missing, or never chosen, comes back as a file
            that is not a directory -- which the panel says rather than hides. */
        juce::File libraryFolder();
        void setLibraryFolder (const juce::File&);

        /** Where the answers are kept. A test points this somewhere disposable: writing
            to the real one would overwrite whatever the person running the tests had
            chosen for themselves. */
        void useFile (const juce::File&);
    } // namespace Settings
} // namespace Celine
