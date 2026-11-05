# PhenomeEon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack granular "reel" module (the
Make Noise Morphagene). All DSP is our own code; it is not a port or copy of anything, and it is not affiliated
with Make Noise.

Status: **milestone 5 (the tape head and genes: Vari-Speed, Gene-Size, Slide, Pitch Snap): built and checked
automatically; not yet tested in Logic.** Milestones 1 to 4 (mockup, empty build, styled panel, reel / Rec / Sound On
Sound / playback) were tested in Logic on both Macs and work.

Version 0.5.0 (milestone 5, **not yet tested in Logic**): the reel now plays through a tape head. Same styled panel and
the same 21 control ids as 0.4.0. Mono or stereo in, the same out. Everything from 0.4.0 (reel, REC, SOUND ON SOUND,
PLAY, levels, CLEAR REEL) works as before.

- **Starting positions changed.** A new PhenomeEon now opens with Vari-Speed at 2:30 (original speed), Gene-Size fully left
  and Slide fully left, so it plays a plain 1:1 loop. A project saved with 0.4.0 keeps the old middle positions it stored
  (Vari-Speed in the middle means **stopped**): reset those three knobs, or add a fresh PhenomeEon.
- **VARI-SPEED** (the big knob): speed and direction of the tape head. Middle = **stopped**. 2:30 (three quarters round)
  = exactly original speed forwards; 9:30 (a quarter round) = exactly original speed backwards. Further out = faster, up to
  one octave (+12 semitones) at each end. Between the middle and original speed it is slower: the pitch falls to 26
  semitones below original, then the tape brakes smoothly to a stop. More knob travel is spent on the slow speeds, so
  braking by hand feels smooth. The tape has a little inertia (a speed change glides over a few thousandths of a second).
  When stopped nothing plays (no hum), and starting again carries on from where it halted. Vari-Speed changes only
  playback, never the speed at which recording writes onto the reel. Our own curve, drawn through those landmarks; to be
  tuned by ear.
- **PITCH SNAP** (menu under Vari-Speed: Off / Steps / Semitones): makes Vari-Speed land on notes. **Semitones** lands on
  whole semitones. **Steps** lands on octaves and fifths (0, 7, 12, 19, 24 semitones; on this knob the resting places are
  -24 -19 -12 -7 0 +7 +12), the same list as Ceo. It works across the pitched range (+12 down to -26 semitones) and lets go in
  the last stretch before the stop position, so braking stays smooth. With Pitch Snap on the knob's read-out shows the
  note (for example "fwd +7 st"); with it off it shows the speed (for example "+1.50x"). Off by default.
- **GENE-SIZE:** how long the playing piece of the reel (a "gene") is. Fully left = the whole reel (one lap, as in 0.4.0).
  Turning it up shortens the gene smoothly, down to a thousandth of a second. A gene's length is a length of **time**, so
  changing the speed does not change it (a faster tape covers more of the reel inside the same time). When a gene ends the
  next one starts at once from the same place, so a short gene repeats like a grain loop. Each gene fades in and out over
  1.5 thousandths of a second so the joins do not click. The read-out shows the length ("whole reel", "340 ms", "1.20 s").
  How the knob maps to a time depends on the length of the reel: the first part of the travel is a fraction of the reel.
- **SLIDE:** where the gene starts in the reel (fully left = the start of the reel). It moves the playing gene straight
  away (with a tiny 3 ms glide against zipper noise), so turning it scrubs. It scrubs even while the tape is stopped, and
  goes quiet again when it stops moving. Going backwards, a gene is the stretch of reel just before the Slide position.
  With Gene-Size fully left it still sets where the lap starts. Turning PLAY on (or off and on) starts a fresh gene from the
  Slide position.
- **PLAY off** now lets the gene in progress finish, then stops (with Gene-Size fully left, that is the same as before).
- **The reel display** shows the current gene as an amber box over the waveform (not drawn when the gene is the whole
  reel; it can run past the end of the reel and carry on from the start), the GENE length in the status line, and STATE
  reads STOP while the tape is stopped. A short reel is a sliver on the fixed 2:54 ruler, and a short gene a thin box.
- **Recording and the tape head:** what is recorded back onto the reel is the mix you hear, as before: so recording
  while Vari-Speed is not at original speed, or with short genes, records those changes (the Morphagene-style time lag
  accumulation; the fade at the end of a whole-reel lap is still never recorded). Recording speed is always constant.
- **Not working yet (stored and automatable, but no sound change):** Morph, Organize, the Pitch Scale menu, REC NEW SPLICE,
  SPLICE, SHIFT and the four delete / undo buttons; Load WAV, Save WAV, Reel folder.
- **When the buttons act:** Rec, Play and Clear Reel act while Logic is processing audio (playing). Logic does not run
  effects while stopped, so press Play in Logic, then use the buttons.
- A project saved while Rec or Clear Reel was on opens with them off, so nothing starts recording or clears by itself.

What has been checked automatically (before any test in Logic): the engine self-test (266 checks: everything from 0.4.0,
plus the Vari-Speed landmarks and curve shape, Pitch Snap, the Gene-Size curve, exact backwards and double-speed playback,
a stopped tape being silent and resuming where it halted, Slide scrubbing, genes lasting the same time at any speed,
genes starting at the Slide position, gene fades, Play off, Pitch Snap through the engine, recording speed staying
constant, random control changes staying finite), also run under a memory-error checker with no errors; a one-off run on
Linux that draws the windows to pictures, moves every control in both windows and drives the real plugin code with audio
(139 checks, including Vari-Speed stop and reverse, Gene-Size, Slide and Pitch Snap through the wrapper); and the GitHub build.
Not yet tested: anything in Logic (M4 Mac mini, macOS Tahoe, Logic 11.2.2; Intel Mac, macOS 12.7.8, Logic 10.7.8).

Earlier versions: 0.4.0 reel, Rec, Sound On Sound, basic playback (tested, works); 0.3.1 styled panel on the empty build
(tested, works); 0.2.0 empty build with plain sliders (tested, works).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. `engine/include/phe/core/` holds the generic parts (scales, level helpers, the tape-speed curve and Pitch Snap, the waveform picture); `Reel.h`, `GenePlayer.h` and `Engine.h` are the reel, the tape head with its genes, and the engine (Morph comes in milestone 6). |
| `plugin/` | The plugin wrapper: what Logic sees, the list of controls, and the windows (styled panel and plain sliders). `plugin/Fonts/` holds the panel fonts and their licences; `plugin/preview/` is an optional tool that draws the panel to pictures and checks the controls (not part of the plugin). |
| `tools/install/` | The generic installer used by all the "Unofficial" plugins. |
| `.github/workflows/` | The automatic build. |

The wrapper hands the engine the control values (one plain `Params` list) and the audio; the engine hands audio back.
Nothing in `engine/` knows about sliders or Logic.

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
