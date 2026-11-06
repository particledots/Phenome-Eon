# PhenomeEon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack granular "reel" module (the
Make Noise Morphagene): record sound onto a reel, cut it into splices, and play it back as tiny overlapping
slices ("genes") at any speed, forwards or backwards. All DSP is our own code; it is not a port or copy of
anything, and it is not affiliated with Make Noise.

**Status: version 1.0.0 (milestone 11: polish, CPU and long-session checks, README).** Built and checked
automatically, and **tested in Logic on both test Macs (M4 Mac mini and Intel Mac): it works.** Milestones 1 to 10
were each tested in Logic on both Macs and work too.

## First five minutes

1. Install it (see *Getting the plugin* below) and put **PhenomeEon** on an audio track: Audio FX > Audio Units >
   particledots > PhenomeEon. Press Play in Logic: the plugin only runs while Logic is playing.
2. Press **REC**, play or sing something into the track, press **REC** again. That first recording becomes the reel
   (up to 2 minutes 54 seconds) and starts looping. A new PhenomeEon opens with Play on.
3. Turn **Vari-Speed** (the big knob on the left): the 2:30 position is normal speed, the middle stops the tape, turning
   left plays backwards. Turn **Gene-Size** to the right to loop a shorter and shorter slice, and **Slide** to move where
   that slice starts.
4. Turn **Morph** (the big knob on the right): left of 1/1 leaves gaps between genes, right of it makes them overlap
   (up to four at once); past 2:30 the genes also jump up in pitch and spread across the stereo picture.
5. While it loops press **SPLICE** to drop a marker. **SHIFT** or **ORGANIZE** then choose which splice plays.
   When you like the reel, press **SAVE WAV**: the Logic project stores only a link to that file, never the audio.

## The controls

| Control | What it does |
|---|---|
| **Input Level** (+ gain step -3 / 0 / +6 / +12 dB) | How loud the live input is. 0.0 dB is the starting position. |
| **Sound On Sound** | Blend of live input and the reel loop, and also what is recorded. Left: live only. Middle: both at full level. Right: loop only. |
| **Gene-Size** | Left: a gene is the whole splice. Turning right: shorter and shorter genes (the length is a length of time, down to 1 millisecond). |
| **Slide** | Where in the splice a gene starts (left = the start). |
| **Vari-Speed** (+ **Pitch Snap**: Off / Steps / Semitones) | Tape speed and direction. 2:30 = 1x forwards, 9:30 = 1x backwards, middle = stopped, about +12 semitones at the far ends and down to -26 on the slow side. Pitch Snap makes it land on notes. |
| **Morph** (+ **Pitch Scale**: Off + 28 scales) | How genes follow each other: gaps, seamless 1/1, overlaps of two, three and four. Past 2:30 each new gene gets a random pitch-up and stereo place; Pitch Scale keeps those pitch-ups in a scale. |
| **Organize** | Picks a splice by knob position (the knob is cut into equal zones, one per splice). |
| **Output Level** | How loud the output is. 0.0 dB is the starting position. |
| **REC** | Switch. On an empty reel the first recording makes the reel. On a reel with sound it records over the splice you are in, round and round, until switched off (Time Lag Accumulation). |
| **REC NEW SPLICE** | Records a brand-new splice on the end of the reel; when it stops, the new splice loops. |
| **SPLICE / SHIFT** | Splice drops a marker at the playhead. Shift selects the next splice (and wraps round). |
| **PLAY** | The reel loops. Turned off, the gene in progress finishes and it stops. |
| **DELETE SPLICE MARKER / DELETE ALL MARKERS** | Join the selected splice with the next / make the reel one splice. |
| **ERASE SPLICE** | Cuts the selected splice out of the reel and closes the gap. |
| **CLEAR REEL** | Empties the whole reel. |
| **UNDO / REDO** | One level: undoes the newest edit; pressed again it redoes it. |
| **LOAD WAV / SAVE WAV / REEL FOLDER** | Reel files: a WAV with the splice markers stored inside (other audio programs show them too). The default folder is `~/Music/PhenomeEon`. |
| **CLOCK box** and **MORE** | Optional features that follow Logic's tempo (Gene Shift, Time Stretch, Play Retrigger, Rec Sync). Everything starts Off. |
| **OPTIONS** | Six optional behaviours (Vari-Speed Mode, Record Source, Organize Timing, Gene Window, Play Mode, Morph Chord). The top choice of each is the normal one. |
| **PLAIN SLIDERS** | A plain slider window with every control, as a fallback. |

All 32 controls can be automated in Logic. The window can be resized (the shape stays 3:2).

## Things worth knowing

- **Buttons act while Logic is playing.** Logic does not run effects while it is stopped, so a button pressed then acts when playback starts.
- **One reel, up to 2 minutes 54 seconds**, kept in memory at Logic's sample rate (about 67 MB when full at 48 kHz). It is not stored in the Logic project; save it with SAVE WAV (the project keeps a link and finds the file again if you move it into the reel folder). A reel you never saved is gone when the project closes, and the panel says so.
- **Undo goes one step back**, and a new edit forgets the older step. Undo is refused while recording.
- **The curves are our own.** Vari-Speed, Gene-Size, Slide and Morph follow the landmarks the Morphagene manual describes (for example 2:30 = normal speed, 1/1 near 9:00) but were drawn and tuned by ear, so they feel similar rather than identical.
- **Not included:** the hardware's CV inputs and outputs (Logic automation replaces them), MIDI clock or any clock but Logic's tempo, swing, more than one reel, audio stored inside the project.
- **Sample rates:** tested at 44.1, 48 and 88.2 kHz (and changing between them while running). 96 kHz should work but was not tested and would use about 134 MB for a full reel.
- Mono and stereo tracks are both supported.

## What has been checked

Everything below was run before this version was handed over; "tested in Logic" is listed separately because only that
is done by hand on the real Macs.

- **Engine self-tests:** 743 checks (the reel, recording, Sound On Sound, tape speed and Pitch Snap, genes, Morph and Pitch Scale, splices, Erase and Undo, the option menus, the tempo clock) and 10,400 checks of the reel files (WAV formats, damaged files, markers, sample-rate conversion, saving and loading while the sound runs on). All pass, also under a memory-error checker.
- **Long-session test** (`engine/tests/stress_test.cpp`): simulated audio run faster than real time, with the controls and every button used at random the whole time (including saves, loads, sample-rate changes between 44.1, 48 and 88.2 kHz, buffers from 1 to 65,536 samples, loud and not-a-number input). Two runs of 24 hours, one stereo and one mono, plus several shorter ones, all passed: the output and the reel stayed real numbers, the output never ran away, and memory use stayed level (it moves between about 310 and 440 MB as the sample rate changes, with no upward trend). Five more hours of the same test under a memory-error checker found no errors.
- **CPU:** on the Linux test computer the engine used 0.15% of one processor core for a plain loop and at most about 0.7% with everything switched on at once (full reel, four genes, random pitch-ups, smooth windows, Time Stretch, recording over the reel). Big edits (Erase, Undo, Save, Load) are done a little at a time sized to the buffer, so no single block uses more than about a quarter of its own time even at a 32-sample buffer. These numbers come from a Linux computer, not from the M4 or the Intel Mac.
- **Plugin side (preview tool, 392 checks):** all 32 control ids present and unchanged, every control surviving save and reload, the window opened and closed 80 times at different sizes while audio ran, older projects opening with the new features Off, mono, and both windows drawing correctly.
- **Apple's validator and pluginval:** the build runs `auval` and `pluginval` (strictness level 5, AU and VST3) on every build. Version 1.0.0 passed all of them (the results are in the build's log).
- **Tested in Logic on both Macs** (M4 Mac mini, macOS Tahoe, Logic 11.2.2; Intel Mac, macOS 12.7.8, Logic 10.7.8): milestones 1 to 11. For version 1.0.0 on both Macs: it installs and the footer reads v1.0.0; an old 0.10.0 project opens and sounds the same; the new Options and Clock wording is clearer; there is no crackle from Erase, Undo, Save or Load at small buffers; a one-hour loop stays at about 7% on Logic's CPU meter with steady memory; and a real Morphagene reel from freesound.org loads with all its splices.

## What changed in 1.0.0

No change to how it sounds: with the controls where they were, the audio is the same as 0.10.0.

- **Options and Clock sheets rewritten in plainer words:** each row now has a one-line "what this is for" followed by what each choice does.
- **Big edits sized to the buffer:** Erase Splice, Undo, Save and Load now take smaller slices on small audio buffers (32 or 64 samples), so they cannot overrun a block. Before, one slice cost about a millisecond whatever the buffer size.
- **New checks:** the long-session test above is part of the build (a short version runs every time), and the preview tool gained the plugin-side checks.
- Earlier versions' detailed notes are in `CHANGELOG.md`.

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. `engine/include/phe/core/` holds the generic parts (scales, level helpers, the tape-speed curve and Pitch Snap, the waveform picture, the undo store, the splice list, the tempo clock, reel files); `Reel.h`, `GenePlayer.h` and `Engine.h` are the reel, the tape head with its genes, and the engine. `engine/tests/` holds the self-tests and the long-session test. |
| `plugin/` | The plugin wrapper: what Logic sees, the list of controls, and the windows (styled panel and plain sliders). `plugin/Fonts/` holds the panel fonts and their licences; `plugin/preview/` is an optional tool that draws the panel to pictures and checks the controls (not part of the plugin). |
| `tools/install/` | The generic installer used by all the "Unofficial" plugins. |
| `.github/workflows/` | The automatic build. |

The wrapper hands the engine the control values (one plain `Params` list) and the audio; the engine hands audio back.
Nothing in `engine/` knows about sliders or Logic.

To run the long-session test yourself: build the engine (`cmake -S . -B build -DPHE_BUILD_PLUGIN=OFF && cmake --build build`), then
`build/engine/phe_stress_test bench` (CPU) or `build/engine/phe_stress_test soak 24` (24 hours of simulated audio).

## Getting the plugin

1. Open the **Actions** tab of this repo and click the latest **Build PhenomeEon** run.
2. At the bottom, under **Artifacts**, download **PhenomeEon-macOS** and unzip it (twice: the download is a zip containing `PhenomeEon-macOS.zip`).
3. Inside the `PhenomeEon-macOS` folder are `PhenomeEon.component`, `PhenomeEon.vst3`, `INSTALL.txt`, `auval.log` and
   `Install-Unofficial-Plugin.command`.
4. In Terminal, once, clear the download block: `xattr -dr com.apple.quarantine ~/Downloads/PhenomeEon-macOS/*`
   (adjust the path if you unzipped elsewhere).
5. **Easy install (Logic, Audio Unit):** double-click `Install-Unofficial-Plugin.command`. It asks for your Mac
   password once, installs the AU for all users and restarts the AU registrar.
   Or by hand: copy `PhenomeEon.component` to `~/Library/Audio/Plug-Ins/Components/` and `PhenomeEon.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
6. Restart Logic. If it was already scanned, Logic Pro > Settings > Plug-In Manager > Reset & Rescan Selection.

The plugin appears under **Audio Units > particledots > PhenomeEon**.

## Notes

- Built as a universal binary (Apple Silicon and Intel), macOS 11 or newer.
- Signed ad-hoc only (no Apple Developer account), hence the quarantine step above.
- The first build of Ceo's skeleton is the model for this one; the build recipe, installer and window scaling are the same.
