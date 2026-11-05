# PhenomeEon

An original AU / VST3 effect for Logic Pro, inspired by the feel of a Eurorack granular "reel" module (the
Make Noise Morphagene). All DSP is our own code; it is not a port or copy of anything, and it is not affiliated
with Make Noise.

Status: **milestone 3 (styled panel on the empty build; waiting for a test in Logic).**

Version 0.3.1 (milestone 3, waiting for a re-test in Logic; 0.3.0 tested OK except that small text was too small, now enlarged): the **styled panel**, laid out like the approved mockup,
on the same **empty build** as 0.2.0. It still loads in Logic as an audio effect and passes the sound straight
through, untouched. **None of the controls change the sound yet**; each gets its function in a later milestone.

- **Styled panel (the window you get when you open the plugin):** the title and reel-file strip, the reel display,
  the eight knobs in one row, the three menus and the 4-step input gain selector, and the ten buttons, in the
  approved colours (charcoal, amber, teal) with Space Grotesk and IBM Plex Mono (free open-licence fonts, built in,
  licence texts in `plugin/Fonts/`).
  - Knobs: drag up/down to turn, double-click to return to the starting position. Read-outs under the knobs are plain
    percentages for now; each knob gets its real unit (dB, seconds, speed ...) when it gets its sound.
  - The reel display shows an **empty reel** (ruler to 2:54, all of it free reel time, status line, audio format).
    Waveforms, splice markers, gene box and playhead are drawn by the same code but only appear once there is a reel
    (milestone 4 on).
  - **Load WAV, Save WAV and Reel folder are shown but greyed out**; they come alive in milestone 9. The dashed
    Clock box is space kept for the optional clock features (milestone 10).
  - The buttons are still plain on/off switches (as in 0.2.0); how each one behaves (held, toggled, or click) is decided
    when it gets its function.
- **Plain slider window (the fallback):** still there. Press **PLAIN SLIDERS** at the bottom right of the styled panel
  to switch to it, and **Styled panel** at its top right to switch back. Which one was showing is remembered in the
  saved project. Both windows use the same 21 control ids, so automation and saved projects behave the same in either.
- **Window size:** opens at 1080 x 720, resizable from 840 x 560 to 1800 x 1200, shape kept (3:2). The panel is laid out in
  a fixed 1200 x 800 space (the plain window in 900 x 600) and scaled to fit.
- **Controls (21, all visible to Logic and automatable):** unchanged from 0.2.0: eight knobs (0 to 1), three menus
  (Input Gain Step, Pitch Snap, Pitch Scale), ten buttons.
- **Sound:** mono or stereo in, the same out, bit for bit. No delay (latency 0), no tail.
- **Names and codes:** shown as "PhenomeEon" under Audio Units > particledots; AU type Effect, manufacturer code `Pdot`,
  plugin code `Phe1`, bundle id `com.particledots.phenomeeon`.

What has been checked automatically (before any test in Logic): the engine self-test (121 checks); a one-off run on
Linux that draws both windows to pictures (compared with the approved mockup) and moves every control in both windows,
checking that each changes exactly one parameter, that all 21 parameters have exactly one control, that host automation
moves the control, and that the choice of window survives save and reload (108 checks); and the GitHub build (compiles,
universal Apple Silicon + Intel, ad-hoc signed, `auval`, pluginval).
Not yet tested: the styled panel in Logic on the M4 Mac mini (macOS Tahoe, Logic 11.2.2) and on the Intel Mac
(macOS 12.7.8, Logic 10.7.8).

## Folders

| Folder | What it is |
|---|---|
| `engine/` | The audio engine. Plain C++, no plugin code. This is the part reused for later plugins. `engine/include/phe/core/` holds the generic parts (scales so far); the reel and gene engine goes next to it. |
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
