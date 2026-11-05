#include "ReelFileActions.h"
#include "PluginProcessor.h"

ReelFileActions::ReelFileActions (PhenomeEonProcessor& p) : processor (p) {}

void ReelFileActions::say (const juce::String& text)
{
    sayText = text;
    sayTicks = 200;                    // about ten seconds
}

void ReelFileActions::save()
{
    auto& lib = processor.getReelLibrary();
    if (lib.busy())
    {
        say ("Still busy with the last save or load - try again in a moment.");
        return;
    }
    const auto folder = processor.getReelFolder();
    folder.createDirectory();

    // Starts from the linked file when there is one, so Save WAV over a loaded reel offers the same name.
    juce::File start = folder.getChildFile (juce::String::fromUTF8 (lib.suggestedFileName().c_str()));
    const auto info = lib.info();
    if (info.bound && info.fullPath.size() > 0)
        start = juce::File (juce::String::fromUTF8 (info.fullPath.c_str()));

    chooser = std::make_unique<juce::FileChooser> ("Save the reel as a WAV file (it keeps the splice markers; the project then stores a link to it)",
                                                   start, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File())
            return;                                             // cancelled
        if (! f.hasFileExtension ("wav"))
            f = f.withFileExtension ("wav");
        std::string why;
        if (! processor.getReelLibrary().saveReel (f.getFullPathName().toStdString(), why))
            say (juce::String::fromUTF8 (why.c_str()));
    });
}

void ReelFileActions::load()
{
    if (processor.getReelLibrary().busy())
    {
        say ("Still busy with the last save or load - try again in a moment.");
        return;
    }
    chooser = std::make_unique<juce::FileChooser> ("Load a WAV file as the reel (Undo brings the old reel back; the project then stores a link to the file)",
                                                   processor.getReelFolder(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == juce::File())
            return;                                             // cancelled
        std::string why;
        if (! processor.getReelLibrary().loadReel (f.getFullPathName().toStdString(), why))
            say (juce::String::fromUTF8 (why.c_str()));
    });
}

void ReelFileActions::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose the reel folder (saved reels start here; moved files are looked for here)",
                                                   processor.getReelFolder(), "");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto d = fc.getResult();
        if (d == juce::File() || ! d.isDirectory())
            return;
        processor.setReelFolder (d);
        say ("Reel folder set.");
    });
}

juce::String ReelFileActions::folderText (int maxChars) const
{
    auto text = processor.getReelFolder().getFullPathName();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    if (home.isNotEmpty() && text.startsWith (home))
        text = "~" + text.substring (home.length());
    if (text.length() > maxChars && maxChars > 4)
        text = ".." + text.substring (text.length() - (maxChars - 2));
    return text;
}

juce::String ReelFileActions::statusText()
{
    if (sayTicks > 0)
    {
        --sayTicks;
        problem = false;
        return sayText;
    }

    using L = phe::ReelLibrary;
    const auto info = processor.getReelLibrary().info();
    const auto message = juce::String::fromUTF8 (info.message.c_str());
    const auto name = juce::String::fromUTF8 (info.fileName.c_str());

    problem = info.status == L::Missing || info.status == L::Failed;

    if (info.status == L::Working || info.status == L::Waiting || info.status == L::Missing
        || info.status == L::Failed || info.status == L::Changed)
        return message;
    if (info.bound)
        return info.edited ? "edited since saved: " + name + " - Save WAV to keep the changes" : message;
    if (info.unsaved)
        return "reel not saved to a file yet - Save WAV to keep it";
    return {};
}
