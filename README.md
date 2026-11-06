# PhenomeEon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack granular "reel" module (the
Make Noise Morphagene). All DSP is our own code; it is not a port or copy of anything, and it is not affiliated
with Make Noise.

Status: **milestone 10 (optional clock features from Logic's tempo): tested in Logic on both Macs, works as described.**
Milestones 1 to 10 were tested in Logic on both Macs and work.

Version 0.10.0 (milestone 10, tested, works). Five new controls (32 in all; older projects open with all of them Off, sounding exactly as 0.9.0):

- **Clock Mode:** Off / Hybrid / Gene Shift / Time Stretch. Gene Shift: every tick jumps to the next gene. Time Stretch: the clock
  sets how long one trip through the splice takes, the pitch stays Vari-Speed's. Hybrid: Gene Shift while Morph is at 2/1 or lower,
  Time Stretch above. These need a Gene-Size shorter than the whole splice (the Clock box says SET GENE if not).
- **Clock Rate:** 2 bars down to 1/32, with triplets. Follows Logic's song position while Logic plays; keeps running at Logic's tempo
  when Logic is stopped (120 BPM if no tempo is given).
- **Clocks per Splice** (Time Stretch), **Play Retrigger** (pull the loop back to Slide every N ticks), **Rec Sync** (Rec and Rec New
  Splice start and stop on the tick; the display says REC WAIT).
- In the styled panel: a Clock box in the header (mode, rate, tick light, tempo) and a MORE sheet. In the plain window: five menus.

Earlier notes (version 0.9.0):

Load WAV, Save WAV and Reel folder buttons now work, in the styled panel and in the plain-slider window. Everything from 0.8.0
is unchanged.

- **SAVE WAV:** writes the whole reel as a stereo 32-bit float WAV at the project's sample rate, with the splice markers
  inside the file (as standard WAV "cue" markers, so other audio programs show them too). The audio is copied a piece at a time
  while the reel keeps playing; a file is written under a temporary name and renamed, so a failed save never damages an old file.
- **LOAD WAV:** loads a WAV as the reel (8 / 16 / 24 / 32-bit whole-number or 32 / 64-bit float, mono or stereo, any sample
  rate; files with more than two channels use the first two). Cue markers in the file become splice markers. A file at another
  sample rate is converted (markers move with it); a file longer than the reel (2:54) is cut and the status line says so; markers
  closer than 64 samples are skipped. Loading is one undoable edit (Undo brings the old reel back, Redo the loaded one).
- **REEL FOLDER:** where Save starts, and where moved files are looked for. Default `~/Music/PhenomeEon`; remembered per computer.
- **Project links:** after a save or load the reel is linked to its file. The Logic project stores only the link (path, size,
  fingerprint), never audio. Opening the project loads the reel from the file. If the file was moved, the reel folder (and the
  folders inside it) is searched for a file of the same size and fingerprint. If the file changed, the new one is loaded and the
  status line says so. If it is missing the reel stays empty, the link is kept, and the status line says so (in amber). Editing the
  reel keeps the link and shows "edited since saved: name - Save WAV to keep the changes". A reel that was never saved shows
  "reel not saved to a file yet - Save WAV to keep it". Clear Reel (an empty reel) drops the link.
- **Status line:** the footer of the styled panel (and under the title in the plain window) shows what the reel file is doing.
- **While Logic is stopped:** a save or load waits for audio to run ("waiting for audio to run (press Play in Logic)...").

Tested in Logic on the M4 Mac mini (macOS Tahoe, Logic 11.2.2) and the Intel Mac (macOS 12.7.8, Logic 10.7.8): everything works as described. What was also checked automatically beforehand: the engine self-tests (638 checks as before, plus 10,400 new
checks: WAV writing and reading in every format and with damaged files, markers, sample-rate conversion, the hand-over of the
reel to and from the audio thread at several block sizes with the sound proven identical while a copy is made, Undo / Redo of a
load, loading and saving a 100-second reel, a sample-rate change in the middle of a job, long random sequences of buttons,
saves and loads, and the file library with real files: moved, changed and missing files, unwritable folders), also run under a
memory-error checker with no errors; the preview tool (245 checks, drives the real plugin code: save, project link, reopening,
missing file); and the GitHub build. The file chooser dialogs were checked by hand in Logic.

Version 0.8.0 (milestone 8, tested, works). Same styled panel and the same 21 control ids as 0.7.0, plus six
new menu ids at the end (27 in all). Everything from 0.7.0 works as before while the new menus are left on their first choice.

- **ERASE SPLICE** (button): cuts the selected splice out of the reel and closes the gap (the audio after it moves up, the
  later markers move with it). The splice after the erased one becomes the selected one (after the last splice, the new last
  one). On a reel of one splice it empties the reel. Refused while recording. Long reels are edited in small steps over a
  few audio blocks ("BUSY" shows in the status line, silent meanwhile) so Logic never stalls.
- **UNDO** (button): undoes the newest edit: a recording (first take, Time Lag Accumulation pass or new splice), Splice,
  Delete Splice Marker, Delete All Markers, Erase Splice, Clear Reel. **One level only**, and pressing it again redoes (the
  button then reads REDO). Starting a new edit forgets the one before it. Refused while recording; changing the sample rate
  forgets the history. The undo copy is made a piece at a time just before audio is changed, so recording costs nothing extra.
- **Time Lag Accumulation polish:** when a pass is laid over audio already on the reel, what is written goes through a gentle
  limiter (untouched below 0.8, bends towards but never reaches full scale), so many passes pile up without running away.
  The status line says "REC TLA" while that is happening.
- **OPTIONS** (button, bottom right of the styled panel; the same six menus are in the plain-slider window). The first choice
  of each is the normal behaviour:
  - *Vari-Speed Mode:* Classic / Octave scale (both ways) / Octave scale (forward only): speeds 1/128x to 2x in even
    musical steps; Pitch Snap still works on top.
  - *Record Source:* Mix (as set by Sound On Sound) / Input only (always records just the live input).
  - *Organize Timing:* At end of gene / Immediately.
  - *Gene Window:* Short fades / Smooth windows (each gene swells up and dies away).
  - *Play Mode:* Classic / Momentary (sound only while Play is held) / Trigger loop (keeps looping, Play jumps back to the start).
  - *Morph Chord:* Random pitch-ups / Octave chord / Major / Minor / Fifths (what Morph past 2:30 adds).
- **When the buttons act:** while Logic is processing audio (playing).

What has been checked automatically (before any test in Logic): the engine self-test (638 checks: everything from 0.7.0,
plus the undo store, erasing in every position and on a long reel in small steps at several block sizes, Undo / Redo of every
kind of edit and exact restoring of audio and markers, the limiter, each option menu, and long random sequences of every
button), also run under a memory-error checker with no errors; the preview tool (draws the windows, moves every control,
drives the real plugin code with audio); and the GitHub build. Tested in Logic on the M4 Mac mini (macOS Tahoe, Logic 11.2.2) and the Intel Mac (macOS 12.7.8, Logic 10.7.8).

Version 0.7.0 (milestone 7, tested, works): splices (Splice, Shift, Organize, Delete Splice Marker, Delete All Markers, Rec New
Splice, and recording over a splice staying inside it). Earlier versions: 0.6.0 Morph and Pitch Scale (tested, works); 0.5.0 tape head, genes, Vari-Speed, Pitch Snap (tested, works);
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
