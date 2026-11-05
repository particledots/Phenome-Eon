# PhenomeEon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack granular "reel" module (the
Make Noise Morphagene). All DSP is our own code; it is not a port or copy of anything, and it is not affiliated
with Make Noise.

Status: **milestone 2 (empty build with plain sliders; waiting for a test in Logic).**

Version 0.2.0 (milestone 2, waiting for a test in Logic): an **empty build**. It loads in Logic as an audio effect and
passes the sound straight through, untouched. It has a plain, resizable window with one slider, menu or button for
every control in the approved panel plan. **None of the controls change the sound yet**; each gets its function in a
later milestone.

- **Controls (21, all visible to Logic and automatable):**
  - Eight knobs, 0 to 1: Input Level, Sound On Sound, Gene-Size, Slide, Vari-Speed, Morph, Organize, Output Level.
  - Three menus: Input Gain Step (-3 / 0 / +6 / +12 dB, starts at 0 dB), Pitch Snap (Off / Steps / Semitones),
    Pitch Scale (Off + 28 scales, the same list as the other particledots plugins).
  - Ten buttons: REC, REC NEW SPLICE, SPLICE, SHIFT, PLAY, DELETE SPLICE MARKER, DELETE ALL MARKERS, ERASE SPLICE,
    CLEAR REEL, UNDO. (They are plain on/off switches for now.)
  - The knob starting positions are placeholders; each is set properly when its control gets its sound.
  - Not controls yet: Load WAV, Save WAV, Reel folder (milestone 9) and the optional clock features (milestone 10).
- **Window:** resizable, the same way as Ceo: laid out in a fixed 900 x 600 space and scaled to the window, keeping its
  shape, smallest about 60%, largest 180%. The styled panel (milestone 3) replaces it and keeps the same control ids.
- **Sound:** mono or stereo in, the same out, bit for bit. No delay (latency 0), no tail.
- **Names and codes:** shown as "PhenomeEon" under Audio Units > particledots; AU type Effect, manufacturer code `Pdot`,
  plugin code `Phe1`, bundle id `com.particledots.phenomeeon`.

What has been checked automatically by the build: the engine self-test (121 checks: audio identical in and out for mono
and stereo, several sample rates and block sizes, with every control at an extreme; the scale list and gain steps);
the plugin compiles, is a universal binary (Apple Silicon + Intel), is signed ad-hoc, and is run through Apple's
validator (`auval`) and pluginval (results in the package).
Not yet tested: loading in Logic on the M4 Mac mini (macOS Tahoe, Logic 11.2.2) and on the Intel Mac (macOS 12.7.8,
Logic 10.7.8).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. `engine/include/phe/core/` holds the generic parts (scales so far); the reel and gene engine goes next to it. |
| `plugin/` | The plugin wrapper: what Logic sees, the list of controls, and the window. |
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
