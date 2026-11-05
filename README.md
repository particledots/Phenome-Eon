# PhenomeEon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack granular "reel" module (the
Make Noise Morphagene). All DSP is our own code; it is not a port or copy of anything, and it is not affiliated
with Make Noise.

Status: **milestone 6 (Morph and Pitch Scale): built and checked automatically; not yet tested in Logic.**
Milestones 1 to 5 (mockup, empty build, styled panel, reel / Rec / Sound On Sound / playback, tape head and genes) were
tested in Logic on both Macs and work.

Version 0.6.0 (milestone 6, **not yet tested in Logic**): Morph and Pitch Scale now make sound. Same styled panel and the
same 21 control ids as 0.5.0. Mono or stereo in, the same out. Everything from 0.5.0 works as before.

- **Starting position changed.** A new PhenomeEon opens with Morph at 1/1 (about 8:30 to 9:00 on the knob, 0.18), so it
  still plays a plain 1:1 loop. A project saved with 0.5.0 stored Morph at 0.5 (12:00, two genes at once): reset the Morph
  knob, or add a fresh PhenomeEon.
- **MORPH** (the big knob under the gene controls): how genes follow each other. A new gene starts every so often; Morph
  sets how often, as a share of a gene's length.
  - **Far left:** a gap of silence before each next gene (up to half a gene long); the read-out says "gap 30%".
  - **About 8:30 to 9:00 (0.18): 1/1.** Genes follow each other with no gap and no overlap (a seamless loop). This is a small
    flat zone, so it is easy to find; the read-out says "1/1".
  - **Up to 12:00: overlap.** The next gene starts before the last one has ended, so copies of the same gene play together,
    each a little later than the one before. At 12:00 two play at a time ("2/1").
  - **Up to 1:00:** three at a time, equally spaced ("3/1"). Panning starts to fade in: the genes are spread across the
    stereo picture (left, right, centre in turn); it is fully in by 2:30.
  - **Past 2:30:** a fourth gene joins (spacing tightens to a quarter of a gene), and every new gene gets a **random pitch-up**
    (from none up to two octaves at the end of the knob) and a **random place** in the stereo picture, both growing as the
    knob goes round. The read-out shows the highest pitch-up (for example "3.6/1 +12st").
  - Several genes together are turned down so the level stays steady (equal power: each is divided by the square root of
    how many play at once). Genes keep their 1.5 ms fades. It works the same at any Gene-Size, up to the whole reel.
  - Our own curves, drawn through the manual's landmarks; to be tuned by ear (all in `engine/include/phe/core/Morph.h`).
- **PITCH SCALE** (menu under Morph: Off + 28 scales): decides which random pitch-ups are possible. **Off:** any amount up to
  the range. **A scale:** only notes of that scale, counted up from the note Vari-Speed is playing now (so the root is
  always the current Vari-Speed note), over as many octaves as the range reaches; the root itself is always possible. It
  changes nothing below 2:30 on Morph, where there are no pitch-ups. Pitch Snap (on Vari-Speed) and Pitch Scale (on Morph)
  work together.
- **The reel display** has a GENES count in the status line (how many genes play at this moment).
- **PLAY off** lets every gene in progress finish, then stops. A stopped tape is silent at any Morph setting.
- **Recording** still writes what you hear (the Sound On Sound mix, including overlapped and pitched genes) at constant speed.
- The random pitch-ups and places repeat the same way each time PLAY starts (the same random sequence each time).
- **Not working yet (stored and automatable, but no sound change):** Organize, REC NEW SPLICE, SPLICE, SHIFT and the four
  delete / undo buttons; Load WAV, Save WAV, Reel folder.
- **When the buttons act:** Rec, Play and Clear Reel act while Logic is processing audio (playing). Logic does not run
  effects while stopped, so press Play in Logic, then use the buttons.
- A project saved while Rec or Clear Reel was on opens with them off, so nothing starts recording or clears by itself.

What has been checked automatically (before any test in Logic): the engine self-test (334 checks: everything from 0.5.0,
plus the Morph landmarks and curve shape, gaps and exact start spacing, how many genes play at once at each setting, whole-
reel genes, backwards and double-speed playback with overlaps, steady level, panning, random pitch-up amounts and the
scales they stay in, Pitch Scale having no effect below 2:30, the Vari-Speed note being the root, results not depending on
block size, Play off with overlapping genes, a stopped tape, recording while genes overlap, random control changes staying
finite), also run under a memory-error checker with no errors; the preview tool (draws the windows to pictures, moves every
control, drives the real plugin code with audio); and the GitHub build. Not yet tested: anything in Logic (M4 Mac mini, macOS
Tahoe, Logic 11.2.2; Intel Mac, macOS 12.7.8, Logic 10.7.8).

Earlier versions: 0.5.0 tape head, genes, Vari-Speed, Pitch Snap (tested, works); 0.4.0 reel, Rec, Sound On Sound, basic
playback (tested, works); 0.3.1 styled panel on the empty build (tested, works); 0.2.0 empty build with plain sliders
(tested, works).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. `engine/include/phe/core/` holds the generic parts (scales, level helpers, the tape-speed curve and Pitch Snap, the waveform picture); `Reel.h`, `GenePlayer.h` and `Engine.h` are the reel, the tape head with its genes, and the engine. `core/Morph.h` is the Morph curve and the random pitch-up / Pitch Scale choice. |
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
