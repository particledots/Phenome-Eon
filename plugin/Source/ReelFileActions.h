// What the Load WAV, Save WAV and Reel folder buttons do, and the sentence that says what the reel file is doing. Both windows
// (the styled panel and the plain sliders) use the same one, so they behave the same. It only asks the plugin's reel library to
// do the work (the library, in the engine folder, is the part that is tested without Logic).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class PhenomeEonProcessor;

class ReelFileActions
{
public:
    explicit ReelFileActions (PhenomeEonProcessor&);

    void load();                 // opens the file chooser, then loads
    void save();                 // opens the file chooser, then saves
    void chooseFolder();         // opens the folder chooser

    // The reel folder, shortened to fit (the start is cut with ".." and the end kept).
    juce::String folderText (int maxChars) const;

    // One short sentence about the reel file, and whether it is a problem (shown in a warning colour). Empty when there is nothing to say.
    // Call about 20 times a second; the answer changes by itself as saves and loads finish.
    juce::String statusText();
    bool statusIsProblem() const { return problem; }

private:
    void say (const juce::String& text);      // a short-lived sentence (a refusal) that is shown instead of the library's

    PhenomeEonProcessor& processor;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String sayText;
    int sayTicks = 0;
    bool problem = false;
};
