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
        expect (allIds.size() == 27, viewName + ": 27 parameters");

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

        expect (moved == allIds, viewName + ": moving the controls reached all 27 parameters");

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
        setParameter (proc, PheParams::deleteAllMarkers, 1.0f);
        feedSilenceLoudest (proc, 2);
        letWindowThreadRun (200);
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

    std::cout << "preview checks: " << (checks - failures) << " passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
