// Offline preview and control checks (not part of the plugin itself; only built with -DPHE_BUILD_PREVIEW=ON).
//
// It does two things without needing Logic:
//   1. draws the windows to picture files, so the look can be compared with the approved mockup;
//   2. moves every control in both windows and checks that each one changes exactly one parameter, that
//      every parameter has exactly one control, and that the choice of window is remembered in saved state.
//
// Usage: PhenomeEonPreview <folder for the pictures>

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <iostream>
#include <map>
#include <set>

namespace
{
    int checks = 0, failures = 0;

    void expect (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::cout << "FAIL: " << what << std::endl;
        }
    }

    template <typename T>
    void collect (juce::Component& c, std::vector<T*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (child))
                out.push_back (t);
            collect<T> (*child, out);
        }
    }

    std::map<juce::String, float> snapshotParams (juce::AudioProcessor& p)
    {
        std::map<juce::String, float> m;
        for (auto* param : p.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                m[withId->paramID] = withId->getValue();
        return m;
    }

    std::vector<juce::String> changedBetween (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b)
    {
        std::vector<juce::String> changed;
        for (auto& [id, v] : a)
            if (std::abs (b.at (id) - v) > 1.0e-6f)
                changed.push_back (id);
        return changed;
    }

    void resetAll (juce::AudioProcessor& p)
    {
        for (auto* param : p.getParameters())
            param->setValueNotifyingHost (param->getDefaultValue());
    }

    void save (juce::Component& c, const juce::File& file, float scale)
    {
        auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (out.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, out);
        }
        expect (file.existsAsFile() && file.getSize() > 1000, "picture written: " + file.getFileName());
        std::cout << "wrote " << file.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")" << std::endl;
    }


    // ---- driving the real processor with audio, the way Logic does ----
    void setParameter (PhenomeEonProcessor& proc, const char* id, float value01)
    {
        if (auto* p = proc.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (value01);
    }

    float parameterValue (PhenomeEonProcessor& proc, const char* id)
    {
        return proc.getValueTreeState().getParameter (id)->getValue();
    }

    // Feeds 'numBlocks' blocks of 512 samples. Sound: a 220 Hz tone with a slow swell and a little noise,
    // so the waveform picture has some shape. 'clock' keeps the sound continuous from call to call.
    void feed (PhenomeEonProcessor& proc, long long& clock, int numBlocks, float level = 1.0f)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        juce::Random noise (7);
        for (int b = 0; b < numBlocks; ++b)
        {
            for (int i = 0; i < 512; ++i, ++clock)
            {
                const double seconds = (double) clock / 48000.0;
                const double swell = 0.15 + 0.85 * std::abs (std::sin (juce::MathConstants<double>::pi * seconds / 4.7));
                const float v = level * (float) (0.7 * swell * std::sin (juce::MathConstants<double>::twoPi * 220.0 * seconds))
                              + 0.02f * (noise.nextFloat() - 0.5f);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v * 0.8f);
            }
            proc.processBlock (buffer, midi);
        }
    }

    void letWindowThreadRun (int milliseconds)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (milliseconds);
    }

    // Moves every control in the window that is showing and checks the result.
    void checkControls (PhenomeEonProcessor& proc, PhenomeEonEditor& editor, const juce::String& viewName)
    {
        resetAll (proc);

        std::set<juce::String> allIds;
        for (auto& kv : snapshotParams (proc))
            allIds.insert (kv.first);

        // One control per parameter, none missing, none twice
        const auto& ids = editor.controlledParameterIds();
        std::set<juce::String> controlled (ids.begin(), ids.end());
        expect (ids.size() == controlled.size(), viewName + ": no parameter has two controls");
        expect (controlled == allIds, viewName + ": every parameter has a control");
        expect (allIds.size() == 21, viewName + ": 21 parameters");

        std::set<juce::String> moved;
        auto moveAndCheck = [&] (const juce::String& what, const std::function<void()>& action)
        {
            const auto before = snapshotParams (proc);
            action();
            const auto changed = changedBetween (before, snapshotParams (proc));
            expect (changed.size() == 1, viewName + ": " + what + " changes exactly one parameter (changed " + juce::String ((int) changed.size()) + ")");
            if (changed.size() == 1)
            {
                expect (moved.insert (changed[0]).second, viewName + ": " + what + " is the only control for " + changed[0]);
                std::cout << "  " << viewName << ": " << what << " -> " << changed[0] << std::endl;
            }
        };

        std::vector<juce::Slider*> sliders;
        collect (editor, sliders);
        expect (sliders.size() == 8, viewName + ": 8 knobs / sliders (found " + juce::String ((int) sliders.size()) + ")");
        for (auto* s : sliders)
            moveAndCheck ("slider '" + s->getTitle() + "'", [s] { s->setValue (0.9, juce::sendNotificationSync); });

        std::vector<juce::ComboBox*> boxes;
        collect (editor, boxes);
        for (auto* b : boxes)
            moveAndCheck ("menu '" + b->getTitle() + "'", [b] { b->setSelectedItemIndex (juce::jmin (1, b->getNumItems() - 1) == b->getSelectedItemIndex() ? 2 : 1, juce::sendNotificationSync); });

        std::vector<GainStepSelector*> selectors;
        collect (editor, selectors);
        for (auto* s : selectors)
            moveAndCheck ("gain step selector", [s] { s->select (3); });

        std::vector<juce::TextButton*> buttons;
        collect (editor, buttons);
        int toggles = 0;
        for (auto* b : buttons)
        {
            if (! b->getClickingTogglesState())
                continue;
            ++toggles;
            moveAndCheck ("button '" + b->getButtonText() + "'", [b] { b->setToggleState (! b->getToggleState(), juce::sendNotificationSync); });
        }
        expect (toggles == 10, viewName + ": 10 on/off buttons (found " + juce::String (toggles) + ")");

        expect (moved == allIds, viewName + ": moving the controls reached all 21 parameters");

        // And the other way: a parameter change shows up in the control (host automation -> window)
        if (auto* knob = sliders.empty() ? nullptr : sliders.front())
        {
            resetAll (proc);
            if (auto* p = proc.getParameters()[0])
                p->setValueNotifyingHost (0.25f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            expect (std::abs (knob->getValue() - 0.25) < 1.0e-3, viewName + ": host automation moves the first knob");
        }

        resetAll (proc);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    PhenomeEonProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    // ---------------- control checks, both windows ----------------
    {
        PhenomeEonEditor editor (proc);
        editor.setSize (1080, 720);

        expect (editor.getView() == PhenomeEonEditor::View::styled, "opens on the styled panel by default");
        checkControls (proc, editor, "styled");

        editor.showView (PhenomeEonEditor::View::plain);
        expect (editor.getView() == PhenomeEonEditor::View::plain, "can switch to the plain sliders");
        checkControls (proc, editor, "plain");

        // The window choice is remembered in the saved project
        juce::MemoryBlock saved;
        proc.getStateInformation (saved);
        PhenomeEonProcessor proc2;
        proc2.setStateInformation (saved.getData(), (int) saved.getSize());
        {
            PhenomeEonEditor editor2 (proc2);
            expect (editor2.getView() == PhenomeEonEditor::View::plain, "reopens on the plain sliders after save and reload");
            editor2.showView (PhenomeEonEditor::View::styled);
        }
        juce::MemoryBlock saved2;
        proc2.getStateInformation (saved2);
        PhenomeEonProcessor proc3;
        proc3.setStateInformation (saved2.getData(), (int) saved2.getSize());
        {
            PhenomeEonEditor editor3 (proc3);
            expect (editor3.getView() == PhenomeEonEditor::View::styled, "reopens on the styled panel after switching back");
        }

        // Control values survive a save and reload in either window
        resetAll (proc);
        proc.getParameters()[1]->setValueNotifyingHost (0.8f);
        juce::MemoryBlock saved3;
        proc.getStateInformation (saved3);
        PhenomeEonProcessor proc4;
        proc4.setStateInformation (saved3.getData(), (int) saved3.getSize());
        expect (std::abs (proc4.getParameters()[1]->getValue() - 0.8f) < 1.0e-4f, "control values survive save and reload");
        resetAll (proc);
    }


    // ---------------- recording through the real processor ----------------
    {
        resetAll (proc);
        long long clock = 0;
        feed (proc, clock, 4);

        // Rec on, ten seconds of sound, Rec off
        setParameter (proc, PheParams::rec, 1.0f);
        feed (proc, clock, 938);                                    // 938 blocks of 512 = 480256 samples
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            std::vector<ReelDisplay*> reels;
            collect (editor, reels);
            save (editor, outDir.getChildFile ("styled_while_recording.png"), 1654.0f / 1200.0f);
        }
        feed (proc, clock, 1);
        const int firstLength = proc.getEngine().getReelView().lengthSamples;
        expect (proc.getEngine().getReelView().recording, "wrapper: recording while Rec is on");
        setParameter (proc, PheParams::rec, 0.0f);
        feed (proc, clock, 3);
        expect (! proc.getEngine().getReelView().recording, "wrapper: stopped when Rec goes off");
        expect (firstLength == 939 * 512, "wrapper: reel is as long as the recording (" + juce::String (firstLength) + ")");
        expect (proc.getEngine().getReelView().playing, "wrapper: the new recording loops (Play starts on)");

        // Sound On Sound up: we hear the loop. With silence in, the output is the loop.
        setParameter (proc, PheParams::soundOnSound, 1.0f);
        feed (proc, clock, 400);
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_recorded_reel.png"), 1654.0f / 1200.0f);
        }

        // Clear Reel: acts, then the wrapper switches the button back off by itself
        setParameter (proc, PheParams::clearReel, 1.0f);
        feed (proc, clock, 2);
        letWindowThreadRun (200);
        expect (proc.getEngine().getReelView().lengthSamples == 0, "wrapper: Clear Reel empties the reel");
        expect (parameterValue (proc, PheParams::clearReel) < 0.5f, "wrapper: the Clear Reel button switches itself off");

        // A project saved while Rec was on must not start recording when opened
        setParameter (proc, PheParams::rec, 1.0f);
        juce::MemoryBlock whileRecOn;
        proc.getStateInformation (whileRecOn);
        setParameter (proc, PheParams::rec, 0.0f);
        PhenomeEonProcessor reopened;
        reopened.setPlayConfigDetails (2, 2, 48000.0, 512);
        reopened.prepareToPlay (48000.0, 512);
        reopened.setStateInformation (whileRecOn.getData(), (int) whileRecOn.getSize());
        letWindowThreadRun (200);
        expect (parameterValue (reopened, PheParams::rec) < 0.5f, "wrapper: a project saved with Rec on opens with Rec off");

        // Recording until the reel is full: stops by itself and the Rec button goes off
        setParameter (proc, PheParams::rec, 1.0f);
        feed (proc, clock, 16400);                                  // 8,396,800 samples: more than the 8,352,000 the reel holds
        letWindowThreadRun (200);
        expect (proc.getEngine().getReelView().lengthSamples == proc.getEngine().getReelView().capacitySamples,
                "wrapper: a full reel is 2.9 minutes");
        expect (parameterValue (proc, PheParams::rec) < 0.5f, "wrapper: the Rec button switches itself off when the reel is full");
        setParameter (proc, PheParams::clearReel, 1.0f);
        feed (proc, clock, 2);
        letWindowThreadRun (200);
        resetAll (proc);
    }

    // ---------------- pictures ----------------
    // Knob positions roughly like the mockup, so the pictures can be compared with it
    const std::map<juce::String, float> mockLike {
        { PheParams::inputLevel, 0.70f }, { PheParams::soundOnSound, 0.75f }, { PheParams::geneSize, 0.30f },
        { PheParams::slide, 0.42f },      { PheParams::variSpeed, 0.75f },    { PheParams::morph, 0.20f },
        { PheParams::organize, 0.25f },   { PheParams::outputLevel, 0.60f } };

    auto applyMockLike = [&]
    {
        for (auto& [id, v] : mockLike)
            if (auto* p = proc.getValueTreeState().getParameter (id))
                p->setValueNotifyingHost (v);
        if (auto* p = proc.getValueTreeState().getParameter (PheParams::pitchSnap))
            p->setValueNotifyingHost (p->convertTo0to1 (2.0f));
    };

    {
        resetAll (proc);
        applyMockLike();
        PhenomeEonEditor editor (proc);
        editor.showView (PhenomeEonEditor::View::styled);
        editor.setSize (1200, 800);

        // 1654 px wide = the size of the approved mockup picture
        std::vector<ReelDisplay*> reels;
        collect (editor, reels);
        expect (reels.size() == 1, "one reel display");

        save (editor, outDir.getChildFile ("styled_empty_as_plugin.png"), 1654.0f / 1200.0f);

        editor.setSize (1080, 720);      // the size the plugin opens at
        save (editor, outDir.getChildFile ("styled_default_window.png"), 1.0f);
        editor.setSize (1200, 800);

        for (auto* r : reels)
            r->showDemoContent (true);
        save (editor, outDir.getChildFile ("styled_with_demo_reel.png"), 1654.0f / 1200.0f);

        editor.setSize (840, 560);
        save (editor, outDir.getChildFile ("styled_smallest_window.png"), 1.0f);

        editor.showView (PhenomeEonEditor::View::plain);
        editor.setSize (900, 600);
        save (editor, outDir.getChildFile ("plain_sliders.png"), 1.0f);
    }

    std::cout << "preview checks: " << (checks - failures) << " passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
