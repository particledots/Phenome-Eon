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
#include "ReelFileActions.h"

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

    // Like feed(), but silent input, and tells us the loudest output sample (left channel) over those blocks.
    float feedSilenceLoudest (PhenomeEonProcessor& proc, int numBlocks)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        float loudest = 0.0f;
        for (int b = 0; b < numBlocks; ++b)
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
            loudest = juce::jmax (loudest, buffer.getMagnitude (0, 0, 512));
        }
        return loudest;
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
        expect (allIds.size() == 32, viewName + ": 32 parameters");

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

        expect (moved == allIds, viewName + ": moving the controls reached all 32 parameters");

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

    // ---------------- the tape head and genes through the real processor (milestone 5) ----------------
    {
        resetAll (proc);
        long long clock = 0;
        feed (proc, clock, 4);
        setParameter (proc, PheParams::rec, 1.0f);
        feed (proc, clock, 281);                                    // about 3 seconds
        setParameter (proc, PheParams::rec, 0.0f);
        feed (proc, clock, 3);
        const int reelLength = proc.getEngine().getReelView().lengthSamples;
        expect (reelLength > 100000, "wrapper (M5): a 3 second reel was recorded (" + juce::String (reelLength) + ")");

        setParameter (proc, PheParams::soundOnSound, 1.0f);         // hear the loop only
        expect (feedSilenceLoudest (proc, 100) > 0.05f, "wrapper (M5): with the controls as they start, the reel plays");
        expect (std::abs (proc.getEngine().getReelView().rate - 1.0f) < 1.0e-5f, "wrapper (M5): starting speed is exactly original speed");

        // Vari-Speed in the middle: stopped and silent; back again: plays
        setParameter (proc, PheParams::variSpeed, 0.5f);
        feedSilenceLoudest (proc, 150);
        expect (feedSilenceLoudest (proc, 20) < 1.0e-4f, "wrapper (M5): Vari-Speed in the middle stops the sound");
        setParameter (proc, PheParams::variSpeed, 0.75f);
        expect (feedSilenceLoudest (proc, 100) > 0.05f, "wrapper (M5): Vari-Speed back at 2:30 plays again");

        // Backwards
        setParameter (proc, PheParams::variSpeed, 0.25f);
        expect (feedSilenceLoudest (proc, 100) > 0.05f, "wrapper (M5): backwards plays");
        expect (proc.getEngine().getReelView().rate < -0.99f, "wrapper (M5): backwards is reported as a negative speed");
        setParameter (proc, PheParams::variSpeed, 0.75f);
        feedSilenceLoudest (proc, 100);

        // Gene-Size: shorter genes
        expect (proc.getEngine().getReelView().geneSeconds == 0.0f, "wrapper (M5): Gene-Size fully left is the whole reel");
        setParameter (proc, PheParams::geneSize, 0.5f);
        expect (feedSilenceLoudest (proc, 100) > 0.02f, "wrapper (M5): short genes make sound");
        {
            const auto v = proc.getEngine().getReelView();
            expect (v.geneSeconds > 0.0f && v.geneSeconds < 1.0f, "wrapper (M5): Gene-Size half way gives a short gene (" + juce::String (v.geneSeconds, 3) + " s)");
            expect (v.geneSpan > 0 && v.geneSpan < reelLength / 2, "wrapper (M5): and its window is part of the reel");
        }

        // Slide moves the gene
        setParameter (proc, PheParams::slide, 0.6f);
        feedSilenceLoudest (proc, 20);
        {
            const auto v = proc.getEngine().getReelView();
            expect (v.geneStart > (int) (0.55 * reelLength) && v.geneStart < (int) (0.65 * reelLength),
                    "wrapper (M5): Slide moves the gene's window (" + juce::String (v.geneStart) + ")");
        }

        // Pitch Snap through the menu: a knob a little above original speed lands exactly on original speed
        setParameter (proc, PheParams::geneSize, 0.0f);
        setParameter (proc, PheParams::slide, 0.0f);
        const float knobPlus04 = 0.5f + 0.5f * (0.5f + 0.4f / 24.0f);
        setParameter (proc, PheParams::variSpeed, knobPlus04);
        feedSilenceLoudest (proc, 100);
        const float smoothRate = proc.getEngine().getReelView().rate;
        expect (smoothRate > 1.0f && smoothRate < 1.05f, "wrapper (M5): Pitch Snap off: the speed is a little over 1 (" + juce::String (smoothRate, 4) + ")");
        if (auto* snap = proc.getValueTreeState().getParameter (PheParams::pitchSnap))
            snap->setValueNotifyingHost (snap->convertTo0to1 (2.0f));          // Semitones
        feedSilenceLoudest (proc, 100);
        expect (std::abs (proc.getEngine().getReelView().rate - 1.0f) < 1.0e-4f, "wrapper (M5): Pitch Snap Semitones lands on original speed");
        if (auto* snap = proc.getValueTreeState().getParameter (PheParams::pitchSnap))
            snap->setValueNotifyingHost (snap->convertTo0to1 (0.0f));

        // A picture with a real gene box
        setParameter (proc, PheParams::variSpeed, 0.75f);
        setParameter (proc, PheParams::geneSize, 0.42f);
        setParameter (proc, PheParams::slide, 0.35f);
        feedSilenceLoudest (proc, 60);
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_gene_box.png"), 1654.0f / 1200.0f);
        }
        setParameter (proc, PheParams::variSpeed, 0.5f);
        feedSilenceLoudest (proc, 60);
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_tape_stopped.png"), 1654.0f / 1200.0f);
        }

        setParameter (proc, PheParams::clearReel, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        resetAll (proc);

        // A longer reel (80 seconds) so the gene box is wide enough to see; one gene runs past the end and wraps round
        feed (proc, clock, 4);
        setParameter (proc, PheParams::rec, 1.0f);
        feed (proc, clock, 7500);
        setParameter (proc, PheParams::rec, 0.0f);
        feed (proc, clock, 3);
        setParameter (proc, PheParams::soundOnSound, 1.0f);
        setParameter (proc, PheParams::geneSize, 0.30f);
        setParameter (proc, PheParams::slide, 0.50f);
        feedSilenceLoudest (proc, 50);
        {
            const auto v = proc.getEngine().getReelView();
            expect (v.geneSpan > 0 && v.geneSeconds > 1.0f, "wrapper (M5): on a long reel the gene is seconds long (" + juce::String (v.geneSeconds, 2) + " s)");
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_gene_box_long_reel.png"), 1654.0f / 1200.0f);
        }
        setParameter (proc, PheParams::slide, 0.95f);
        feedSilenceLoudest (proc, 50);
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_gene_box_wrapping.png"), 1654.0f / 1200.0f);
        }
        setParameter (proc, PheParams::clearReel, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        resetAll (proc);
    }

    // ---------------- Morph and Pitch Scale through the real processor (milestone 6) ----------------
    {
        resetAll (proc);
        long long clock = 0;
        feed (proc, clock, 4);
        setParameter (proc, PheParams::rec, 1.0f);
        feed (proc, clock, 281);                                    // about 3 seconds
        setParameter (proc, PheParams::rec, 0.0f);
        feed (proc, clock, 3);
        setParameter (proc, PheParams::soundOnSound, 1.0f);         // hear the loop only

        expect (std::abs (parameterValue (proc, PheParams::morph) - phe::kDefaultMorph) < 1.0e-6f, "wrapper (M6): Morph starts at 1/1");
        feedSilenceLoudest (proc, 50);
        expect (proc.getEngine().getReelView().voices == 1, "wrapper (M6): at the starting positions one gene plays");

        setParameter (proc, PheParams::geneSize, 0.5f);             // a short gene, so genes come and go quickly
        feedSilenceLoudest (proc, 50);
        expect (proc.getEngine().getReelView().voices >= 1, "wrapper (M6): short genes at 1/1");

        setParameter (proc, PheParams::morph, 0.6f);                // 1:00, three at once
        expect (feedSilenceLoudest (proc, 100) > 0.02f, "wrapper (M6): overlapping genes make sound");
        {
            int most = 0;
            for (int i = 0; i < 40; ++i)
            {
                feedSilenceLoudest (proc, 1);
                most = juce::jmax (most, proc.getEngine().getReelView().voices);
            }
            expect (most >= 3 && most <= 4, "wrapper (M6): Morph at 1:00 plays three genes at once (" + juce::String (most) + ")");
        }

        setParameter (proc, PheParams::morph, 1.0f);                // fully round: four genes, random pitch and pan
        expect (feedSilenceLoudest (proc, 100) > 0.02f, "wrapper (M6): fully round Morph makes sound");
        {
            int most = 0;
            for (int i = 0; i < 40; ++i)
            {
                feedSilenceLoudest (proc, 1);
                most = juce::jmax (most, proc.getEngine().getReelView().voices);
            }
            expect (most >= 4 && most <= 5, "wrapper (M6): fully round Morph plays four genes at once (" + juce::String (most) + ")");
        }

        setParameter (proc, PheParams::pitchScale, 1.0f / 28.0f);   // Major
        expect (feedSilenceLoudest (proc, 100) > 0.02f, "wrapper (M6): Pitch Scale (Major) makes sound");
        expect (proc.getEngine().getParams().pitchScale == 1, "wrapper (M6): the Pitch Scale menu reaches the engine as scale 1");
        setParameter (proc, PheParams::pitchScale, 1.0f);           // the last scale
        feedSilenceLoudest (proc, 20);
        expect (proc.getEngine().getParams().pitchScale == 28, "wrapper (M6): the last Pitch Scale menu entry is scale 28");
        setParameter (proc, PheParams::pitchScale, 1.0f / 28.0f);

        // Far left: gaps, so some moments are silent and some are not
        setParameter (proc, PheParams::morph, 0.0f);
        feedSilenceLoudest (proc, 100);
        {
            int silentMoments = 0, soundMoments = 0;
            for (int i = 0; i < 200; ++i)
            {
                feedSilenceLoudest (proc, 1);
                if (proc.getEngine().getReelView().voices == 0) ++silentMoments; else ++soundMoments;
            }
            expect (silentMoments > 10 && soundMoments > 10, "wrapper (M6): Morph far left has gaps (" + juce::String (silentMoments) + " silent, " + juce::String (soundMoments) + " playing)");
        }

        // A picture with Morph and Pitch Scale set
        setParameter (proc, PheParams::morph, 0.8f);
        setParameter (proc, PheParams::geneSize, 0.3f);
        feedSilenceLoudest (proc, 60);
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_morph.png"), 1654.0f / 1200.0f);
        }

        setParameter (proc, PheParams::clearReel, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        resetAll (proc);
    }

    // ---------------- splices through the real processor (milestone 7) ----------------
    {
        resetAll (proc);
        long long clock = 0;
        feed (proc, clock, 4);
        setParameter (proc, PheParams::rec, 1.0f);
        feed (proc, clock, 560);                                    // about 6 seconds
        setParameter (proc, PheParams::rec, 0.0f);
        feed (proc, clock, 3);
        feedSilenceLoudest (proc, 40);
        const auto view = [&proc] { return proc.getEngine().getReelView(); };
        const int reelLength = view().lengthSamples;
        expect (reelLength > 250000 && view().numSplices == 1, "wrapper (M7): a 6 second reel, one splice (" + juce::String (reelLength) + ")");
        expect (std::abs (parameterValue (proc, PheParams::organize) - phe::kDefaultOrganize) < 1.0e-6f, "wrapper (M7): Organize starts fully left");

        // Splice: drops a marker, and the button switches itself off
        setParameter (proc, PheParams::splice, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        expect (view().numSplices == 2, "wrapper (M7): Splice drops a marker (" + juce::String (view().numSplices) + " splices)");
        expect (parameterValue (proc, PheParams::splice) < 0.5f, "wrapper (M7): the Splice button switches itself off");
        feedSilenceLoudest (proc, 300);
        setParameter (proc, PheParams::splice, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        expect (view().numSplices == 3, "wrapper (M7): a second Splice makes three splices (" + juce::String (view().numSplices) + ")");
        feedSilenceLoudest (proc, 20);

        // Shift and Organize
        expect (view().selectedSplice == 0, "wrapper (M7): splice 1 is selected");
        setParameter (proc, PheParams::shift, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        expect (view().selectedSplice == 1, "wrapper (M7): Shift selects the next splice");
        expect (parameterValue (proc, PheParams::shift) < 0.5f, "wrapper (M7): the Shift button switches itself off");
        setParameter (proc, PheParams::organize, 0.9f);
        feedSilenceLoudest (proc, 2);
        expect (view().selectedSplice == 2, "wrapper (M7): Organize at 0.9 selects the third splice");
        feedSilenceLoudest (proc, 1500);                            // long enough for the waiting splice to take over
        expect (view().playingSplice == 2, "wrapper (M7): the third splice plays (" + juce::String (view().playingSplice + 1) + ")");
        expect (feedSilenceLoudest (proc, 100) > 0.02f, "wrapper (M7): the selected splice makes sound");
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_splices.png"), 1654.0f / 1200.0f);
        }

        // Delete Splice Marker, Delete All Markers
        setParameter (proc, PheParams::organize, 0.0f);
        feedSilenceLoudest (proc, 2);
        setParameter (proc, PheParams::deleteMarker, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        expect (view().numSplices == 2, "wrapper (M7): Delete Splice Marker joins two splices (" + juce::String (view().numSplices) + ")");
        expect (parameterValue (proc, PheParams::deleteMarker) < 0.5f, "wrapper (M7): the Delete Splice Marker button switches itself off");
        setParameter (proc, PheParams::deleteAllMarkers, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        expect (view().numSplices == 1, "wrapper (M7): Delete All Markers leaves one splice");
        expect (parameterValue (proc, PheParams::deleteAllMarkers) < 0.5f, "wrapper (M7): the Delete All Markers button switches itself off");

        // Rec New Splice: a switch. On = recording a new splice; off = stop.
        setParameter (proc, PheParams::recNewSplice, 1.0f);
        feed (proc, clock, 200);
        expect (view().recording, "wrapper (M7): Rec New Splice records");
        expect (view().lengthSamples > reelLength + 90000, "wrapper (M7): the reel grows while it records (" + juce::String (view().lengthSamples) + ")");
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_recording_new_splice.png"), 1654.0f / 1200.0f);
        }
        setParameter (proc, PheParams::recNewSplice, 0.0f);
        feed (proc, clock, 3);
        expect (! view().recording && view().numSplices == 2 && view().selectedSplice == 1,
                "wrapper (M7): stopped; a second splice, selected (" + juce::String (view().numSplices) + " splices)");
        expect (feedSilenceLoudest (proc, 100) > 0.02f, "wrapper (M7): the new splice plays");

        // A project saved while a momentary button was on must not act when it is opened
        setParameter (proc, PheParams::splice, 1.0f);
        setParameter (proc, PheParams::deleteAllMarkers, 1.0f);
        setParameter (proc, PheParams::recNewSplice, 1.0f);
        juce::MemoryBlock whileOn;
        proc.getStateInformation (whileOn);
        setParameter (proc, PheParams::splice, 0.0f);
        setParameter (proc, PheParams::deleteAllMarkers, 0.0f);
        setParameter (proc, PheParams::recNewSplice, 0.0f);
        PhenomeEonProcessor reopened;
        reopened.setPlayConfigDetails (2, 2, 48000.0, 512);
        reopened.prepareToPlay (48000.0, 512);
        reopened.setStateInformation (whileOn.getData(), (int) whileOn.getSize());
        letWindowThreadRun (200);
        expect (parameterValue (reopened, PheParams::splice) < 0.5f && parameterValue (reopened, PheParams::deleteAllMarkers) < 0.5f
                    && parameterValue (reopened, PheParams::recNewSplice) < 0.5f && parameterValue (reopened, PheParams::shift) < 0.5f,
                "wrapper (M7): a project saved with the splice buttons on opens with them off");

        setParameter (proc, PheParams::clearReel, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        expect (view().numSplices == 1, "wrapper (M7): Clear Reel takes the markers away");

        // ---------------- milestone 8: Erase Splice, Undo / Redo, the option menus ----------------
        // The reel was cleared just above: record a fresh 3-second take, then work on it
        setParameter (proc, PheParams::rec, 1.0f);
        feed (proc, clock, 281);
        setParameter (proc, PheParams::rec, 0.0f);
        feed (proc, clock, 3);
        feedSilenceLoudest (proc, 400);
        setParameter (proc, PheParams::splice, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        expect (view().numSplices == 2, "wrapper (M8): setup, two splices (" + juce::String (view().numSplices) + ")");
        const int lengthBefore = view().lengthSamples;
        expect (view().undoState == 1, "wrapper (M8): the Splice can be undone");

        setParameter (proc, PheParams::undo, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        feedSilenceLoudest (proc, 20);
        expect (view().numSplices == 1 && view().undoState == 2, "wrapper (M8): Undo takes the marker away and offers Redo");
        expect (parameterValue (proc, PheParams::undo) < 0.5f, "wrapper (M8): the Undo button switches itself off");
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            std::vector<juce::TextButton*> found;
            collect (editor, found);
            bool redoShown = false;
            for (auto* b : found)
                if (b->getButtonText() == "REDO")
                    redoShown = true;
            expect (redoShown, "wrapper (M8): the Undo button reads REDO after an Undo");
        }
        setParameter (proc, PheParams::undo, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        feedSilenceLoudest (proc, 20);
        expect (view().numSplices == 2 && view().undoState == 1, "wrapper (M8): pressing it again is Redo");

        setParameter (proc, PheParams::eraseSplice, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        for (int i = 0; i < 40 && view().busy; ++i)
            feedSilenceLoudest (proc, 4);
        expect (view().numSplices == 1 && view().lengthSamples < lengthBefore, "wrapper (M8): Erase Splice removes a splice (" + juce::String (view().lengthSamples) + " < " + juce::String (lengthBefore) + ")");
        expect (parameterValue (proc, PheParams::eraseSplice) < 0.5f, "wrapper (M8): the Erase Splice button switches itself off");
        setParameter (proc, PheParams::undo, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
        for (int i = 0; i < 40 && view().busy; ++i)
            feedSilenceLoudest (proc, 4);
        expect (view().lengthSamples == lengthBefore && view().numSplices == 2, "wrapper (M8): Undo brings the erased splice back");

        // The option menus reach the engine
        setParameter (proc, PheParams::playMode, proc.getValueTreeState().getParameter (PheParams::playMode)->convertTo0to1 (2.0f));
        setParameter (proc, PheParams::morphChord, proc.getValueTreeState().getParameter (PheParams::morphChord)->convertTo0to1 (1.0f));
        setParameter (proc, PheParams::variSpeedMode, proc.getValueTreeState().getParameter (PheParams::variSpeedMode)->convertTo0to1 (1.0f));
        feedSilenceLoudest (proc, 2);
        expect (proc.getEngine().getParams().playMode == 2 && proc.getEngine().getParams().morphChord == 1
                    && proc.getEngine().getParams().variSpeedMode == 1, "wrapper (M8): the option menus reach the engine");

        // Older projects (without the new menus) open with the normal behaviour
        {
            juce::MemoryBlock block;
            proc.getStateInformation (block);
            auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
            if (xml != nullptr)
            {
                for (auto* id : { PheParams::variSpeedMode, PheParams::recordSource, PheParams::organizeMode,
                                  PheParams::geneWindow, PheParams::playMode, PheParams::morphChord })
                    for (int i = xml->getNumChildElements(); --i >= 0;)
                        if (xml->getChildElement (i)->getStringAttribute ("id") == id)
                            xml->removeChildElement (xml->getChildElement (i), true);
                juce::MemoryBlock old;
                juce::AudioProcessor::copyXmlToBinary (*xml, old);
                PhenomeEonProcessor older;
                older.setPlayConfigDetails (2, 2, 48000.0, 512);
                older.prepareToPlay (48000.0, 512);
                older.setStateInformation (old.getData(), (int) old.getSize());
                expect (parameterValue (older, PheParams::playMode) == 0.0f && parameterValue (older, PheParams::variSpeedMode) == 0.0f
                            && parameterValue (older, PheParams::morphChord) == 0.0f, "wrapper (M8): an older project opens with the normal behaviour");
            }
        }
        {
            PhenomeEonEditor editor (proc);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            std::vector<StyledPanel*> panels;
            collect (editor, panels);
            expect (panels.size() == 1, "wrapper (M8): one styled panel");
            if (! panels.empty())
            {
                panels[0]->showOptions (true);
                letWindowThreadRun (150);
                expect (panels[0]->optionsShowing(), "wrapper (M8): the Options sheet opens");
                save (editor, outDir.getChildFile ("styled_options.png"), 1654.0f / 1200.0f);
                panels[0]->showOptions (false);
                expect (! panels[0]->optionsShowing(), "wrapper (M8): and closes");
            }
        }
        resetAll (proc);
    }

    // ---------------- the clock features (milestone 10) ----------------
    {
        resetAll (proc);
        PhenomeEonProcessor fresh;
        fresh.setPlayConfigDetails (2, 2, 48000.0, 512);
        fresh.prepareToPlay (48000.0, 512);

        // A new plugin has every clock feature off, and the clock menus start where the engine expects
        expect (parameterValue (fresh, PheParams::clockMode) == 0.0f && parameterValue (fresh, PheParams::retrigger) == 0.0f
                    && parameterValue (fresh, PheParams::recSync) == 0.0f, "wrapper (M10): clock mode, retrigger and rec sync start Off");
        expect (std::abs (parameterValue (fresh, PheParams::clockRate) - fresh.getValueTreeState().getParameter (PheParams::clockRate)->convertTo0to1 (3.0f)) < 1.0e-5f
                    && std::abs (parameterValue (fresh, PheParams::clocksPerSplice) - fresh.getValueTreeState().getParameter (PheParams::clocksPerSplice)->convertTo0to1 (3.0f)) < 1.0e-5f,
                "wrapper (M10): the rate starts at 1/4 and the stretch at 4 clocks");

        // The menus reach the engine
        auto choose = [&fresh] (const char* id, float index) { setParameter (fresh, id, fresh.getValueTreeState().getParameter (id)->convertTo0to1 (index)); };
        choose (PheParams::clockMode, 3.0f);
        choose (PheParams::clockRate, 7.0f);
        choose (PheParams::clocksPerSplice, 9.0f);
        choose (PheParams::retrigger, 7.0f);
        choose (PheParams::recSync, 1.0f);
        feedSilenceLoudest (fresh, 2);
        const auto& pr = fresh.getEngine().getParams();
        expect (pr.clockMode == 3 && pr.clockRate == 7 && pr.clocksPerSplice == 9 && pr.retrigger == 7 && pr.recSync == 1,
                "wrapper (M10): the clock menus reach the engine");

        // With no host position the clock runs by itself at 120 bpm
        const auto view = fresh.getEngine().getReelView();
        expect (std::abs (view.clockBpm - 120.0f) < 0.01f && ! view.clockLocked, "wrapper (M10): without a host the clock runs at 120 bpm by itself");

        // Rec Sync through the real wrapper: a recording waits for a tick (1/16 at 120 bpm = 6000 samples, so within 12 blocks)
        choose (PheParams::clockMode, 0.0f);
        choose (PheParams::clockRate, 3.0f);                       // 1/4: every 24000 samples = 47 blocks
        setParameter (fresh, PheParams::rec, 1.0f);
        feedSilenceLoudest (fresh, 2);
        expect (fresh.getEngine().getReelView().recWait == 1 && ! fresh.getEngine().getReelView().recording, "wrapper (M10): Rec waits for the tick");
        feedSilenceLoudest (fresh, 60);
        expect (fresh.getEngine().getReelView().recording && fresh.getEngine().getReelView().recWait == 0, "wrapper (M10): then recording starts");
        setParameter (fresh, PheParams::rec, 0.0f);
        feedSilenceLoudest (fresh, 120);
        expect (! fresh.getEngine().getReelView().recording && fresh.getEngine().getReelView().lengthSamples > 0
                    && fresh.getEngine().getReelView().lengthSamples % 24000 == 0,
                "wrapper (M10): and ends on a tick (reel " + juce::String (fresh.getEngine().getReelView().lengthSamples) + " samples)");

        // An older project (without the clock menus) opens with everything off
        {
            juce::MemoryBlock block;
            fresh.getStateInformation (block);
            auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), (int) block.getSize());
            if (xml != nullptr)
            {
                for (auto* id : { PheParams::clockMode, PheParams::clockRate, PheParams::clocksPerSplice, PheParams::retrigger, PheParams::recSync })
                    for (int i = xml->getNumChildElements(); --i >= 0;)
                        if (xml->getChildElement (i)->getStringAttribute ("id") == id)
                            xml->removeChildElement (xml->getChildElement (i), true);
                juce::MemoryBlock old;
                juce::AudioProcessor::copyXmlToBinary (*xml, old);
                PhenomeEonProcessor older;
                older.setPlayConfigDetails (2, 2, 48000.0, 512);
                older.prepareToPlay (48000.0, 512);
                older.setStateInformation (old.getData(), (int) old.getSize());
                expect (parameterValue (older, PheParams::clockMode) == 0.0f && parameterValue (older, PheParams::retrigger) == 0.0f
                            && parameterValue (older, PheParams::recSync) == 0.0f, "wrapper (M10): an older project opens with every clock feature off");
            }
        }

        // The window: the Clock box shows the tempo, the sheet opens and closes, the pictures
        {
            PhenomeEonEditor editor (fresh);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            std::vector<StyledPanel*> panels;
            collect (editor, panels);
            expect (panels.size() == 1, "wrapper (M10): one styled panel");
            if (! panels.empty())
            {
                choose (PheParams::clockMode, 2.0f);
                feedSilenceLoudest (fresh, 20);
                letWindowThreadRun (150);
                save (editor, outDir.getChildFile ("styled_clock_box.png"), 1654.0f / 1200.0f);

                panels[0]->showClockSheet (true);
                letWindowThreadRun (150);
                expect (panels[0]->clockSheetShowing() && ! panels[0]->optionsShowing(), "wrapper (M10): the Clock sheet opens");
                save (editor, outDir.getChildFile ("styled_clock_sheet.png"), 1654.0f / 1200.0f);
                panels[0]->showOptions (true);
                expect (panels[0]->optionsShowing() && ! panels[0]->clockSheetShowing(), "wrapper (M10): opening Options closes the Clock sheet");
                panels[0]->showOptions (false);
                panels[0]->showClockSheet (true);
                panels[0]->showClockSheet (false);
                expect (! panels[0]->clockSheetShowing(), "wrapper (M10): and the Clock sheet closes");

                // waiting to record shows in the reel display
                setParameter (fresh, PheParams::rec, 1.0f);
                choose (PheParams::recSync, 1.0f);
                feedSilenceLoudest (fresh, 3);
                setParameter (fresh, PheParams::rec, 0.0f);
                setParameter (fresh, PheParams::rec, 1.0f);
                feedSilenceLoudest (fresh, 2);
                letWindowThreadRun (150);
                save (editor, outDir.getChildFile ("styled_rec_waiting.png"), 1654.0f / 1200.0f);
                setParameter (fresh, PheParams::rec, 0.0f);
                feedSilenceLoudest (fresh, 120);
                setParameter (fresh, PheParams::recSync, 0.0f);
            }
            editor.showView (PhenomeEonEditor::View::plain);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("plain_clock.png"), 1.0f);
        }
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
        editor.setSize (1200, 800);
        save (editor, outDir.getChildFile ("plain_sliders.png"), 1.0f);
    }


    // ---------------- reel files and project links (milestone 9) ----------------
    {
        const juce::File work = outDir.getChildFile ("reelfile_check");
        work.deleteRecursively();
        work.createDirectory();

        PhenomeEonProcessor a;
        a.setPlayConfigDetails (2, 2, 48000.0, 512);
        a.prepareToPlay (48000.0, 512);
        a.setReelFolder (work.getChildFile ("Reels"));
        expect (a.getReelFolder() == work.getChildFile ("Reels"), "reel folder is remembered");

        // Both windows: the three file buttons are live and show the reel folder / a status sentence.
        {
            PhenomeEonEditor editor (a);
            editor.setSize (1200, 800);
            letWindowThreadRun (100);
            for (auto view : { PhenomeEonEditor::View::styled, PhenomeEonEditor::View::plain })
            {
                editor.showView (view);
                letWindowThreadRun (100);
                std::vector<juce::TextButton*> buttons;
                collect (editor, buttons);
                int found = 0;
                for (auto* b : buttons)
                    if (b->getButtonText() == "Load WAV" || b->getButtonText() == "Save WAV" || b->getButtonText() == "Reel folder")
                    {
                        ++found;
                        expect (b->isEnabled() && b->isVisible(), "file button '" + b->getButtonText() + "' is live in the " + (view == PhenomeEonEditor::View::styled ? "styled" : "plain") + " window");
                    }
                expect (found == 3, "three file buttons in the window (" + juce::String (found) + ")");
            }
            editor.showView (PhenomeEonEditor::View::styled);
        }

        // Nothing recorded: nothing to say, nothing to save.
        {
            ReelFileActions actions (a);
            expect (actions.statusText().isEmpty(), "empty reel: no status sentence");
            std::string why;
            expect (! a.getReelLibrary().saveReel (work.getChildFile ("Reels/none.wav").getFullPathName().toStdString(), why), "an empty reel cannot be saved");
        }

        // Record, put in a marker, and look at the sentence.
        long long clock = 0;
        feed (a, clock, 4);
        setParameter (a, PheParams::rec, 1.0f);
        feed (a, clock, 300);
        setParameter (a, PheParams::rec, 0.0f);
        feed (a, clock, 4);
        feed (a, clock, 40);
        setParameter (a, PheParams::splice, 1.0f);
        feed (a, clock, 3);
        letWindowThreadRun (150);
        expect (a.getEngine().getReelView().numSplices == 2, "a marker was set (" + juce::String (a.getEngine().getReelView().numSplices) + " splices)");
        ReelFileActions actions (a);
        expect (actions.statusText().contains ("not saved to a file yet"), "unsaved reel says so: " + actions.statusText());

        // Save, pumping audio and the 20 Hz tick the way Logic and the window thread would.
        const juce::File file = work.getChildFile ("Reels/take.wav");
        std::string why;
        expect (a.getReelLibrary().saveReel (file.getFullPathName().toStdString(), why), "save accepted: " + juce::String (why));
        for (int i = 0; i < 400 && a.getReelLibrary().busy(); ++i)
        {
            feed (a, clock, 1);
            a.getReelLibrary().tick();
        }
        expect (file.existsAsFile() && ! a.getReelLibrary().busy(), "the file was written");
        expect (actions.statusText().startsWith ("saved take.wav"), "status after saving: " + actions.statusText());
        expect (! actions.statusIsProblem(), "saved is not a problem");
        const int savedLength = a.getEngine().getReelView().lengthSamples;

        // The project stores a link, not audio.
        juce::MemoryBlock project;
        a.getStateInformation (project);
        expect (project.getSize() < 8000, "the project holds no audio (" + juce::String ((int) project.getSize()) + " bytes)");
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary (project.getData(), (int) project.getSize());
            expect (xml != nullptr && xml->getChildByName ("REELFILE") != nullptr, "the project holds a REELFILE link");
            if (xml != nullptr && xml->getChildByName ("REELFILE") != nullptr)
                expect (xml->getChildByName ("REELFILE")->getStringAttribute ("path") == "take.wav", "stored relative to the reel folder");
        }

        // Editing says so.
        setParameter (a, PheParams::splice, 0.0f);
        feed (a, clock, 2);
        setParameter (a, PheParams::splice, 1.0f);
        feed (a, clock, 3);
        a.getReelLibrary().tick();
        letWindowThreadRun (100);
        expect (actions.statusText().startsWith ("edited since saved: take.wav"), "edited reel says so: " + actions.statusText());

        // A new instance (opening the project) brings the reel back from the file.
        {
            PhenomeEonProcessor b;
            b.setPlayConfigDetails (2, 2, 48000.0, 512);
            b.setStateInformation (project.getData(), (int) project.getSize());
            expect (b.getReelLibrary().hasBinding(), "the link is restored from the project");
            b.prepareToPlay (48000.0, 512);
            long long clock2 = 0;
            for (int i = 0; i < 400 && b.getReelLibrary().busy(); ++i)
            {
                feed (b, clock2, 1, 0.0f);
                b.getReelLibrary().tick();
            }
            expect (b.getEngine().getReelView().lengthSamples == savedLength, "the reel came back from the file (" + juce::String (b.getEngine().getReelView().lengthSamples) + " of " + juce::String (savedLength) + ")");
            expect (b.getEngine().getReelView().numSplices == 2, "with its splice marker");
            ReelFileActions actionsB (b);
            expect (actionsB.statusText().startsWith ("loaded take.wav"), "status after opening: " + actionsB.statusText());

            PhenomeEonEditor editor (b);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_reel_file_loaded.png"), 1654.0f / 1200.0f);
        }

        // The file is gone: the link is kept and the sentence is a problem.
        {
            file.deleteFile();
            PhenomeEonProcessor c;
            c.setPlayConfigDetails (2, 2, 48000.0, 512);
            c.setStateInformation (project.getData(), (int) project.getSize());
            c.prepareToPlay (48000.0, 512);
            long long clock3 = 0;
            for (int i = 0; i < 400 && c.getReelLibrary().busy(); ++i)
            {
                feed (c, clock3, 1, 0.0f);
                c.getReelLibrary().tick();
            }
            ReelFileActions actionsC (c);
            expect (actionsC.statusText().startsWith ("missing: take.wav") && actionsC.statusIsProblem(), "missing file is reported: " + actionsC.statusText());
            juce::MemoryBlock again;
            c.getStateInformation (again);
            auto xml = juce::AudioProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
            expect (xml != nullptr && xml->getChildByName ("REELFILE") != nullptr, "the link is kept in the project while the file is missing");

            PhenomeEonEditor editor (c);
            editor.showView (PhenomeEonEditor::View::styled);
            editor.setSize (1200, 800);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("styled_reel_file_missing.png"), 1654.0f / 1200.0f);
            editor.showView (PhenomeEonEditor::View::plain);
            letWindowThreadRun (150);
            save (editor, outDir.getChildFile ("plain_reel_file_missing.png"), 1.0f);
        }

        // An old project (no link) opens as before.
        {
            PhenomeEonProcessor d;
            juce::MemoryBlock plainProject;
            d.getStateInformation (plainProject);
            expect (! d.getReelLibrary().hasBinding(), "a new plugin has no link");
            PhenomeEonProcessor e;
            e.setStateInformation (plainProject.getData(), (int) plainProject.getSize());
            expect (! e.getReelLibrary().hasBinding(), "a project with no link opens with no link");
        }
        work.deleteRecursively();
    }

    // ---------------- milestone 11: the ids, the window coming and going, saving all controls, mono ----------------
    {
        // The 32 control ids Logic stores in projects and automation. None may ever be renamed or removed.
        const char* expectedIds[] = {
            "inputLevel", "soundOnSound", "geneSize", "slide", "variSpeed", "morph", "organize", "outputLevel",
            "inputGain", "pitchSnap", "pitchScale",
            "rec", "recNewSplice", "splice", "shift", "play", "deleteMarker", "deleteAllMarkers", "eraseSplice", "clearReel", "undo",
            "variSpeedMode", "recordSource", "organizeMode", "geneWindow", "playMode", "morphChord",
            "clockMode", "clockRate", "clocksPerSplice", "retrigger", "recSync" };
        PhenomeEonProcessor idProc;
        int found = 0;
        for (auto* id : expectedIds)
            if (idProc.getValueTreeState().getParameter (id) != nullptr)
                ++found;
        expect (found == 32, "M11: all 32 control ids exist (found " + juce::String (found) + ")");
        expect (idProc.getParameters().size() == 32, "M11: exactly 32 controls (" + juce::String (idProc.getParameters().size()) + ")");

        // Every control saved with a random value comes back the same from a saved project.
        {
            juce::Random rnd (11);
            for (int round = 0; round < 25; ++round)
            {
                PhenomeEonProcessor a;
                for (auto* param : a.getParameters())
                {
                    // Buttons are on or off; menus and knobs take any position.
                    const bool isButton = dynamic_cast<juce::AudioParameterBool*> (param) != nullptr;
                    param->setValueNotifyingHost (isButton ? (rnd.nextBool() ? 1.0f : 0.0f) : rnd.nextFloat());
                }
                const auto before = snapshotParams (a);
                juce::MemoryBlock project;
                a.getStateInformation (project);
                PhenomeEonProcessor b;
                b.setStateInformation (project.getData(), (int) project.getSize());
                const auto after = snapshotParams (b);
                bool same = before.size() == after.size();
                juce::String differing;
                for (auto& [id, v] : before)
                    if (std::abs (after.at (id) - v) > 1.0e-5f)
                    {
                        same = false;
                        differing << " " << id << " (" << v << " -> " << after.at (id) << ")";
                    }
                expect (same, "M11: all 32 controls survive save and reload (round " + juce::String (round) + ")" + differing);
            }
        }

        // The window opened and closed many times, at different sizes, with audio running, switching windows and sheets.
        {
            PhenomeEonProcessor live;
            live.setPlayConfigDetails (2, 2, 48000.0, 512);
            live.prepareToPlay (48000.0, 512);
            long long clock = 0;
            feed (live, clock, 6);
            const auto untouched = snapshotParams (live);
            int sheetOpens = 0;
            for (int i = 0; i < 80; ++i)
            {
                {
                    PhenomeEonEditor editor (live);
                    const int w = 840 + (i * 97) % 960;
                    editor.setSize (w, w * 2 / 3);
                    editor.showView (i % 3 == 0 ? PhenomeEonEditor::View::plain : PhenomeEonEditor::View::styled);
                    letWindowThreadRun (15);
                    feed (live, clock, 2);
                    std::vector<StyledPanel*> panels;
                    collect (editor, panels);
                    if (! panels.empty() && editor.getView() == PhenomeEonEditor::View::styled)
                    {
                        if (i % 2 == 0) { panels[0]->showOptions (true); ++sheetOpens; }
                        if (i % 5 == 0) { panels[0]->showClockSheet (true); ++sheetOpens; }
                    }
                    editor.showView (PhenomeEonEditor::View::styled);
                    letWindowThreadRun (10);
                    auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f);
                    expect (image.getWidth() > 100, "M11: the window can be drawn");
                }
                feed (live, clock, 1);
            }
            const auto afterwards = snapshotParams (live);
            expect (changedBetween (untouched, afterwards).empty(), "M11: opening and closing windows 80 times changed no control");
            expect (sheetOpens > 20, "M11: sheets were opened during the cycles");
        }

        // Mono in, mono out (the plugin accepts it): sound comes out, nothing is not-a-number.
        {
            PhenomeEonProcessor mono;
            mono.setPlayConfigDetails (1, 1, 44100.0, 256);
            mono.prepareToPlay (44100.0, 256);
            juce::AudioBuffer<float> buffer (1, 256);
            juce::MidiBuffer midi;
            bool allNumbers = true;
            float loudest = 0.0f;
            setParameter (mono, PheParams::rec, 1.0f);
            for (int b = 0; b < 400; ++b)
            {
                for (int i = 0; i < 256; ++i)
                    buffer.setSample (0, i, 0.4f * std::sin (0.05f * (float) (b * 256 + i)));
                if (b == 200)
                    setParameter (mono, PheParams::rec, 0.0f);
                mono.processBlock (buffer, midi);
                for (int i = 0; i < 256; ++i)
                {
                    const float v = buffer.getSample (0, i);
                    if (! std::isfinite (v)) allNumbers = false;
                    loudest = std::max (loudest, std::abs (v));
                }
            }
            expect (allNumbers, "M11: mono run gives only real numbers");
            expect (loudest > 0.05f, "M11: mono run gives sound (loudest " + juce::String (loudest) + ")");
        }
    }

    // ---------------- version 1.0.1: the Vari-Speed arc and read-out are green forwards, red backwards, grey stopped ----------------
    {
        const auto green = PheStyle::tapeForward, red = PheStyle::tapeBackward, grey = PheStyle::tapeStopped, orange = PheStyle::amber;

        // How many pixels of the picture are (nearly) this colour.
        auto countNear = [] (const juce::Image& img, juce::Colour c)
        {
            int n = 0;
            for (int y = 0; y < img.getHeight(); ++y)
                for (int x = 0; x < img.getWidth(); ++x)
                {
                    const auto p = img.getPixelAt (x, y);
                    if (std::abs ((int) p.getRed() - (int) c.getRed()) < 14
                        && std::abs ((int) p.getGreen() - (int) c.getGreen()) < 14
                        && std::abs ((int) p.getBlue() - (int) c.getBlue()) < 14)
                        ++n;
                }
            return n;
        };
        auto setChoice = [] (PhenomeEonProcessor& p, const char* id, int index)
        {
            if (auto* prm = p.getValueTreeState().getParameter (id))
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        };

        PhenomeEonProcessor proc;
        PhenomeEonEditor editor (proc);
        editor.showView (PhenomeEonEditor::View::styled);
        editor.setSize (1200, 800);
        letWindowThreadRun (150);

        std::vector<KnobCard*> cards;
        collect (editor, cards);
        KnobCard* vari = nullptr;
        KnobCard* morph = nullptr;
        for (auto* c : cards)
        {
            if (juce::String (c->getParameterId()) == "variSpeed") vari = c;
            if (juce::String (c->getParameterId()) == "morph") morph = c;
        }
        expect (vari != nullptr && morph != nullptr, "1.0.1: found the Vari-Speed and Morph knob cards");

        if (vari != nullptr && morph != nullptr)
        {
            // 'want' is which colour the knob must have: 0 green, 1 red, 2 grey
            auto checkKnob = [&] (const juce::String& what, float knob, int want)
            {
                setParameter (proc, PheParams::variSpeed, knob);
                letWindowThreadRun (120);
                const juce::Colour wanted = want == 0 ? green : (want == 1 ? red : grey);
                expect (vari->getAccentColour() == wanted, "1.0.1: " + what + ": colour rule gives the right colour");

                const auto picture = vari->createComponentSnapshot (vari->getLocalBounds(), true, 1.0f);
                const int g = countNear (picture, green), r = countNear (picture, red), o = countNear (picture, orange);
                expect (o < 5, "1.0.1: " + what + ": no orange left on the Vari-Speed knob (" + juce::String (o) + " pixels)");
                if (want == 0) expect (g > 15 && r < 5, "1.0.1: " + what + ": drawn green, not red (" + juce::String (g) + " green, " + juce::String (r) + " red)");
                if (want == 1) expect (r > 15 && g < 5, "1.0.1: " + what + ": drawn red, not green (" + juce::String (g) + " green, " + juce::String (r) + " red)");
                if (want == 2) expect (g < 5 && r < 5, "1.0.1: " + what + ": drawn grey, no green or red (" + juce::String (g) + " green, " + juce::String (r) + " red)");
            };

            // Classic mode
            setChoice (proc, PheParams::variSpeedMode, 0);
            checkKnob ("classic, 2:30 (1x forward)", 0.75f, 0);
            checkKnob ("classic, 9:30 (1x backward)", 0.25f, 1);
            checkKnob ("classic, middle (stopped)", 0.5f, 2);
            checkKnob ("classic, far right", 1.0f, 0);
            checkKnob ("classic, far left", 0.0f, 1);
            checkKnob ("classic, just right of middle", 0.6f, 0);
            checkKnob ("classic, just left of middle", 0.4f, 1);

            save (editor, outDir.getChildFile ("styled_vari_backward.png"), 1654.0f / 1200.0f);   // knob at 0.4: slow backwards = red
            setParameter (proc, PheParams::variSpeed, 0.75f);
            letWindowThreadRun (120);
            save (editor, outDir.getChildFile ("styled_vari_forward.png"), 1654.0f / 1200.0f);
            setParameter (proc, PheParams::variSpeed, 0.5f);
            letWindowThreadRun (120);
            save (editor, outDir.getChildFile ("styled_vari_stopped.png"), 1654.0f / 1200.0f);

            // With Pitch Snap on the colour is the same
            setChoice (proc, PheParams::pitchSnap, 2);
            checkKnob ("classic + Pitch Snap Semitones, 9:30", 0.25f, 1);
            checkKnob ("classic + Pitch Snap Semitones, 2:30", 0.75f, 0);
            setChoice (proc, PheParams::pitchSnap, 1);
            checkKnob ("classic + Pitch Snap Steps, 9:30", 0.25f, 1);
            checkKnob ("classic + Pitch Snap Steps, 2:30", 0.75f, 0);
            setChoice (proc, PheParams::pitchSnap, 0);

            // Octave scale, both ways
            setChoice (proc, PheParams::variSpeedMode, 1);
            checkKnob ("octave both ways, left of middle", 0.25f, 1);
            checkKnob ("octave both ways, middle (stopped)", 0.5f, 2);
            checkKnob ("octave both ways, right of middle", 0.75f, 0);

            // Octave scale, forward only: never red; the far left is the stop
            setChoice (proc, PheParams::variSpeedMode, 2);
            checkKnob ("forward only, far left (stopped)", 0.0f, 2);
            checkKnob ("forward only, a quarter", 0.25f, 0);
            checkKnob ("forward only, middle", 0.5f, 0);
            checkKnob ("forward only, far right", 1.0f, 0);

            // Changing the menu while the knob stays put changes the colour (the window notices by itself)
            setChoice (proc, PheParams::variSpeedMode, 0);
            checkKnob ("back to classic, 9:30", 0.25f, 1);
            setChoice (proc, PheParams::variSpeedMode, 2);
            letWindowThreadRun (150);
            {
                const auto picture = vari->createComponentSnapshot (vari->getLocalBounds(), true, 1.0f);
                expect (countNear (picture, green) > 15 && countNear (picture, red) < 5,
                        "1.0.1: switching the menu to forward only turns a red knob green without touching it");
            }
            setChoice (proc, PheParams::variSpeedMode, 0);
            letWindowThreadRun (150);
            {
                const auto picture = vari->createComponentSnapshot (vari->getLocalBounds(), true, 1.0f);
                expect (countNear (picture, red) > 15 && countNear (picture, green) < 5,
                        "1.0.1: switching the menu back to classic turns it red again");
            }

            // The other knobs stay orange
            const auto morphPicture = morph->createComponentSnapshot (morph->getLocalBounds(), true, 1.0f);
            expect (morph->getAccentColour() == orange, "1.0.1: Morph keeps its orange rule");
            expect (countNear (morphPicture, orange) > 30 && countNear (morphPicture, green) < 5 && countNear (morphPicture, red) < 5,
                    "1.0.1: Morph is still drawn orange");
            for (auto* c : cards)
                if (c != vari)
                    expect (c->getAccentColour() == orange, "1.0.1: " + juce::String (c->getParameterId()) + " is still orange");
        }
    }

    std::cout << "preview checks: " << (checks - failures) << " passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
