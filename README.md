# Egret EQ

**[Use it in your browser](https://vox4vulpes.github.io/EgretFX-EQ/)** ·
**[Download the VST3 plugin and standalone app](https://github.com/vox4vulpes/EgretFX-EQ/releases/latest)** ·
part of the [Egret FX Bundle](https://github.com/vox4vulpes/EgretFX)

翡翠 · Parametric Equalizer: up to 48 bands on a big interactive curve, with a
live spectrum behind it. It runs entirely in the browser via the Web Audio
API — no plugins, no install. Open `index.html` in any modern browser.

## How it works

Every band is an analog filter made digital so that its curve matches the
analog one (poles by the matched z-transform, zeros fitted at DC, at Nyquist
and at the band's own frequency, after M. Vicanek's *Matched Second Order
Digital Filters*): bells and shelves keep their shape right up to 20 kHz
instead of squeezing towards Nyquist, and cuts are exact mirror images of
boosts. The bands run in series with zero latency. Moving a band glides it;
changing its shape, slope or stereo placement crossfades, so nothing clicks.

## Controls

- **The display** — double-click to add a band (near the left edge a low cut,
  near the right a high cut, otherwise a bell where you clicked), drag a dot
  to move it (`Shift` for fine moves), scroll on it to change Q, double-click
  it to switch it off, `Alt`-click it or press `Delete` to remove it; arrow
  keys move the selected band. Hovering shows the band's numbers and note.
  Cut, notch, band-pass and all-pass dots have no gain: their height is Q.
- **Band** — **SHAPE**: bell, low/high shelf, low/high cut, notch, band pass,
  tilt shelf, flat tilt (a constant dB-per-octave tilt across the whole range)
  and all pass; **SLOPE** 6 to 96 dB/oct; **STEREO**: both channels, left,
  right, mid or side; **FREQ**, **GAIN**, **Q**; **BYPASS**, **SOLO** (hear
  only what the band works on) and **DELETE**
- **Analyzer** — the spectrum before (**PRE**) and after (**POST**) the EQ, at
  four resolutions and five speeds, 60/90/120 dB range, **TILT** (4.5 dB per
  octave, so music looks flat) and **FREEZE**; **EQ ±** sets the curve's scale
  (3, 6, 12 or 30 dB)
- **Dynamic EQ** — pull the arrow under the selected dot: the band follows the
  level of its own part of the sound (**THRESH**, **ATTACK**, **RELEASE**) and
  moves by up to its range (the diamond, or the ring around GAIN); **DYN**
  beside GAIN switches it
- **EQ Sketch** — the pencil: draw the curve you want, bands are placed to make it
- **Character** — Clean, Subtle or Warm saturation
- **Output** — **SCALE** (all gains at once, 0–200%), **GAIN**, **PAN**, and
  **AUTO GAIN**, which keeps the loudness level while you EQ
- **Presets** — factory settings and your own; **UNDO** / **REDO**
  (`Ctrl+Z`, `Ctrl+Shift+Z`) and **A** / **B** to compare two settings
- **Source** (browser and standalone app) — a demo loop, a file (or drop one on
  the display), your microphone or line in, or pink noise; `Space` plays

Click a knob's value to type one: `2.5k`, `-3`, `0.7`, `L20`.

## VST3 plugin

[`vst/`](vst/) is the same EQ as a VST3 plugin and standalone app, built with
JUCE: the filter design and the engine are the web version's, ported line for
line (the coefficients match exactly), with the same panel.

- Every band setting is an automatable parameter (48 bands); the analyzer and
  display settings and A/B are saved with the project. User presets are files
  under your user app-data folder in `Egret/Egret EQ/Presets`.
- In a DAW it works on its track (stereo or mono); the standalone app adds the
  demo loop, files and pink noise.
- The window can be resized; the panel scales with it.
- When it starts, the plugin looks on GitHub for a newer version (one request
  for the latest release's public details) and offers to update; **Help**
  turns that off.

To build it yourself (C++17; on Linux a few dev packages, see
[`.github/workflows/build-vst.yml`](.github/workflows/build-vst.yml)):

```sh
cmake -S vst -B vst/build -DCMAKE_BUILD_TYPE=Release
cmake --build vst/build --config Release
```

`vst/Tests/EngineTest.cpp` checks the engine against its own filter design
(levels, stereo placement, solo, gains, stability, clicks) without JUCE
(`-DEGRET_BUILD_PLUGIN=OFF`).

## Releases

What changed in each version is in [CHANGELOG.md](CHANGELOG.md). To cut a
release, add a `## [x.y.z]` section there and push a version tag; the
[Release workflow](.github/workflows/release.yml) builds the VST3 plugin on
Windows, macOS and Linux and publishes the zips and the installer.

## License

[GNU AGPLv3](LICENSE). Copyright (c) 2026 vox4vulpes
