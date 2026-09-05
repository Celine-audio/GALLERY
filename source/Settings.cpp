#include "Settings.h"

#include "ProductInfo.h"

namespace Celine
{
    namespace
    {
        constexpr auto libraryFolderKey = "libraryFolder";

        juce::PropertiesFile::Options settingsOptions()
        {
            juce::PropertiesFile::Options options;

            options.applicationName = PRODUCT_NAME_WITHOUT_VERSION;
            options.filenameSuffix = ".settings";
            options.folderName = juce::String (juce::CharPointer_UTF8 (ProductInfo::companyName));
            options.osxLibrarySubFolder = "Application Support";

            return options;
        }

        juce::File& location()
        {
            static juce::File file = settingsOptions().getDefaultFile();
            return file;
        }
    }

    /*
        Opened for each read and write rather than held open for the plugin's lifetime.
        Every instance shares this file, and one holding a copy would write back at
        close what it had read at load -- so an instance opened this morning would
        quietly undo a folder chosen this afternoon.
    */
    juce::File Settings::libraryFolder()
    {
        juce::PropertiesFile file (location(), settingsOptions());

        return juce::File (file.getValue (libraryFolderKey));
    }

    void Settings::setLibraryFolder (const juce::File& folder)
    {
        juce::PropertiesFile file (location(), settingsOptions());

        file.setValue (libraryFolderKey, folder.getFullPathName());

        // Now, rather than whenever this goes out of scope: a host that is killed
        // instead of quitted is an ordinary way for a plugin to end.
        file.saveIfNeeded();
    }

    void Settings::useFile (const juce::File& file)
    {
        location() = file;
    }
} // namespace Celine
