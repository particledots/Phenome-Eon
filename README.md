# PhenomeEon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack granular "reel" module (the
Make Noise Morphagene). All DSP is our own code; it is not a port or copy of anything, and it is not affiliated
with Make Noise.

Status: **milestone 7 (splices): built and checked automatically; not yet tested in Logic.**
Milestones 1 to 6 (mockup, empty build, styled panel, reel / Rec / Sound On Sound / playback, tape head and genes, Morph and
Pitch Scale) were tested in Logic on both Macs and work.

Version 0.7.0 (milestone 7, **not yet tested in Logic**): splices. Same styled panel and the same 21 control ids as 0.6.0.
Mono or stereo in, the same out. Everything from 0.6.0 works as before when there are no markers (one splice = the whole reel).

- **What a splice is.** A splice marker is a cut mark on the reel. With no markers the reel is one splice; each marker adds
  one more, up to 299 markers (300 splices). The reel display draws the markers (dashed white lines) with a number for each
  splice under the waveform. The selected splice is tinted amber.
- **Everything plays inside the selected splice.** Slide, Gene-Size, Vari-Speed, Morph and Pitch Scale now work on the
  selected splice instead of on the whole reel: Slide 0 is the start of the splice, "whole splice" is one lap of it (the
  Gene-Size read-out says "whole splice" when there are several), and a gene that runs past the end of the splice carries on
  from its start.
- **SPLICE** (button): drops a marker where the tape head is now. The splice it cuts keeps its number and becomes the part
  *before* the marker, so the loop you were hearing becomes the stretch up to the point where you pressed. A splice cannot be
  shorter than 64 samples (a press that would make one is ignored).
- **SHIFT** (button): selects the next splice (after the last, back to the first).
- **ORGANIZE** (knob): selects splices by position. The knob is cut into equal zones, one per splice, however long each splice
  is (the read-out says "splice 3 of 5"). **Organize wins over Shift:** Shift moves on from wherever Organize last put you, and
  as soon as the Organize knob moves into a different zone it takes over again. Adding or removing markers does not by
  itself change the selection.
- **A newly selected splice does not start at once.** As on the module, the gene in progress plays to its end first, and the
  next gene starts in the new splice. The status line shows "2 / 5 > 3" while splice 3 is waiting to take over from splice 2.
- **DELETE SPLICE MARKER** (button): takes away the marker at the end of the selected splice, joining it with the next one.
  On the last splice there is no marker after it, so nothing happens.
- **DELETE ALL MARKERS** (button): the whole reel becomes one splice again. (The audio is not touched.)
- **REC NEW SPLICE** (a switch, like REC): on = record a new splice onto the end of the reel; off = stop. What you hear is
  what is recorded (the Sound On Sound mix, which can include the other splices as they play), so Sound On Sound decides
  whether the new splice holds live sound, other splices, or both. The reel grows as it records; the old splices keep
  playing. When you stop, a marker is placed at the old end of the reel, the new splice is selected, and it starts looping
  straight away (the old genes fade out over about a millisecond). Pressing REC while a new splice is recording stops it.
  The take stops by itself when the reel is full (2.9 minutes). A take shorter than 64 samples is thrown away. On an empty
  reel, REC NEW SPLICE records the first take, as REC does.
- **REC** (over existing audio) now stays inside the splice the playhead is in: it records from the playhead round and round
  that splice until switched off (Time Lag Accumulation), and never touches the other splices.
- **CLEAR REEL** also takes all the markers away.
- **Starting position changed.** Organize now starts fully left (the first splice). A project saved with 0.6.0 stored 0.5
  and keeps it; with no markers it makes no difference.
- **Splice, Shift, Delete Splice Marker and Delete All Markers** act the moment they go on, then the plugin switches them
  back off by itself (like Clear Reel). A project saved with any of them on, or with Rec New Splice on, opens with them off.
- **Not working yet (stored and automatable, but no sound change):** ERASE SPLICE and UNDO; Load WAV, Save WAV, Reel folder.
  Splice markers are not saved with the project yet (they will be saved with the reel file in milestone 9), and the reel
  itself is not saved with the project, as before.
- **When the buttons act:** the buttons act while Logic is processing audio (playing). Logic does not run effects while
  stopped, so press Play in Logic, then use the buttons. Marker positions are set at the start of an audio block (a few
  milliseconds).

What has been checked automatically (before any test in Logic): the engine self-test (477 checks: everything from 0.6.0,
plus the marker list and its limits, the Organize zones, Shift and the Organize-over-Shift rule, the selection staying put
when markers are added or removed, a marker landing exactly where the head is, a new splice waiting for the gene in progress
to end, playback staying inside its splice, deleting markers, recording over a splice not touching the others, recording a
new splice (what is written, the marker, selection, looping straight away, stopping by itself when full, being refused or
thrown away when it should be, Rec stopping it), Clear Reel and a sample-rate change keeping the markers right, results not
depending on block size, and long random sequences of every button keeping the sound finite and the markers valid), also
run under a memory-error checker with no errors; the preview tool (draws the windows to pictures, moves every control, drives
the real plugin code with audio; 174 checks); and the GitHub build. Not yet tested: anything in Logic (M4 Mac mini, macOS
Tahoe, Logic 11.2.2; Intel Mac, macOS 12.7.8, Logic 10.7.8).

Earlier versions: 0.6.0 Morph and Pitch Scale (tested, works); 0.5.0 tape head, genes, Vari-Speed, Pitch Snap (tested, works);
0.4.0 reel, Rec, Sound On Sound, basic playback (tested, works); 0.3.1 styled panel on the empty build (tested, works); 0.2.0
empty build with plain sliders (tested, works).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. `engine/include/phe/core/` holds the generic parts (scales, level helpers, the tape-speed curve and Pitch Snap, the waveform picture); `Reel.h`, `GenePlayer.h` and `Engine.h` are the reel, the tape head with its genes, and the engine. `core/Morph.h` is the Morph curve and the random pitch-up / Pitch Scale choice; `core/SpliceMap.h` is the list of splice markers and the Organize / Shift selection rules (reusable). |
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
