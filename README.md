# PhenomeEon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack granular "reel" module (the
Make Noise Morphagene). All DSP is our own code; it is not a port or copy of anything, and it is not affiliated
with Make Noise.

Status: **milestone 4 (the reel, Rec, Sound On Sound and basic playback): built and checked automatically; not yet
tested in Logic.** Milestones 1 to 3 (mockup, empty build, styled panel) were tested in Logic on both Macs and work.

Version 0.4.0 (milestone 4, **not yet tested in Logic**): the first version that makes sound of its own. Same styled
panel and the same 21 control ids as 0.3.1. Mono or stereo in, the same out.

- **The reel:** one stereo strip of audio memory, up to **2.9 minutes**, kept at Logic's sample rate. It is in memory only:
  it is lost when the project closes (saving reels to files is milestone 9). If Logic changes the sample rate, the reel
  is converted so it keeps its pitch and length.
- **REC** (a switch): on an **empty reel** the first recording makes the reel; it is as long as the recording (it stops by
  itself when the reel is full at 2.9 minutes, and the REC button switches itself off). On a reel that **already has
  audio**, REC records over it, starting at the playhead and going round and round the loop for as long as it is on
  (the Morphagene's Time Lag Accumulation / Sound On Sound). A take shorter than about a thousandth of a second is thrown away.
- **SOUND ON SOUND:** one knob that blends the live input with the reel loop, for what you hear *and* what is recorded
  ("what you hear is what you get"). Fully left: live only. In the middle: live and loop both at full level (so a loop can
  be layered without fading). Fully right: loop only. The read-out shows live % / loop %. Our own curve; to be tuned by ear.
- **PLAY** (a switch, starts **on**): the reel loops at normal speed from the start. Turning it off lets the pass in
  progress finish and then stops; turning it on starts from the beginning. A very short (1.5 ms) fade at the loop point stops it clicking.
- **INPUT LEVEL / OUTPUT LEVEL:** work in decibels; 0.0 dB (no change) is where they start, minus infinity at the far
  left, +6 dB at the far right. **INPUT GAIN STEP** (-3 / 0 / +6 / +12 dB) also works (a small piece of milestone 8, because it is one line on the same signal path).
- **CLEAR REEL:** empties the reel at once. **No undo yet** (Undo comes in milestone 8). The button switches itself back off.
- **The reel display** now shows the real reel: the recorded length, a waveform picture (left and right), the playhead,
  a red record head while recording, and a STATE read-out (EMPTY / REC / PLAY / STOP). The whole reel counts as one splice
  until splice markers arrive in milestone 7.
- **Not working yet (stored and automatable, but no sound change):** Gene-Size, Slide, Vari-Speed, Morph, Organize, the Pitch
  Snap and Pitch Scale menus, REC NEW SPLICE, SPLICE, SHIFT and the four delete / undo buttons; Load WAV, Save WAV, Reel folder.
- **When the buttons act:** Rec, Play and Clear Reel act while Logic is processing audio (playing). Logic does not run
  effects while stopped, so press Play in Logic, then use the buttons.
- A project saved while Rec or Clear Reel was on opens with them off, so nothing starts recording or clears by itself.

What has been checked automatically (before any test in Logic): the engine self-test (198 checks: pass-through when
empty, first recording, exact playback and loop, same sound at every block size, overdub and punch-out, recording round
the loop, auto-stop when full, Clear Reel, Play on/off, sample-rate change, bad input, waveform picture, mono, random
control changes), also run under a memory-error checker with no errors; a one-off run on Linux that draws the windows to
pictures, moves every control in both windows and drives the real plugin code with audio: recording, Clear Reel and Rec
switching themselves off, a full reel, a project saved with Rec on (120 checks); and the GitHub build.
Not yet tested: anything in Logic (M4 Mac mini, macOS Tahoe, Logic 11.2.2; Intel Mac, macOS 12.7.8, Logic 10.7.8).

Earlier versions: 0.3.1 styled panel on the empty build (tested on both Macs, works); 0.2.0 empty build with plain sliders (tested, works).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. `engine/include/phe/core/` holds the generic parts (scales, level helpers, the waveform picture); `Reel.h` and `Engine.h` are the reel and the engine (genes come in milestones 5 to 6). |
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
