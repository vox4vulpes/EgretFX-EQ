# Changelog

Each release's notes on GitHub start with its section from this file — the
release workflow refuses to publish a version that has no section here.

## [1.0.0]

### Added
- **Egret EQ** (VST3 and standalone app, and in the browser): 翡翠 · Parametric
  Equalizer with up to 48 bands — bell, low/high shelf, low/high cut, notch,
  band pass, tilt shelf, flat tilt and all pass; slopes from 6 to 96 dB/oct;
  each band on both channels, left, right, mid or side. Past 24 bands a note
  says the EQ may use noticeably more CPU.
- Analog-matched filters: curves keep their analog shape up to Nyquist, cuts
  mirror boosts; zero latency; gliding, click-free changes.
- A panel in the style of Pro-Q: the display fills the window, a floating band
  panel under the selected band (shape, slope, stereo, frequency, gain, Q,
  on/off, solo, previous/next, remove), the scales and the output meter on the
  right; the top bar has undo/redo, A/B with copy, presets, the theme and bypass;
  the bottom bar the processing mode, the analyzer, auto gain, gain scale,
  output gain and pan. Light theme by default, dark on a click.
- Spectrum analyzer before and after the EQ: resolution, speed, range, tilt,
  freeze; display range ±3/6/12/30 dB; output level meter with peak.
- Band solo, gain scale, output gain and pan, auto gain.
- Factory presets and your own, undo/redo, A/B comparison.
- The standalone app plays a demo loop, a file, the audio input or pink noise.
- In the browser, ahead of the plugin (they come to it in a later version):
  dynamic EQ per band, EQ Sketch, Character (Clean, Subtle, Warm), a
  movable and see-through band panel, a list of all the bands, and a player
  beside the panel with free tracks to try settings on.
