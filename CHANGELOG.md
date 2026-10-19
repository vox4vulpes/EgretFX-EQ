# Changelog

Each release's notes on GitHub start with its section from this file — the
release workflow refuses to publish a version that has no section here.

## [1.1.0]

### Added
- **Dynamic EQ** per band: the band follows the level of its own part of the
  sound (attack, release) and above the threshold moves by up to its range,
  down like a compressor or up like an expander. Pull the arrow under the
  selected dot to make a band dynamic; drag the diamond, or the ring around
  GAIN, to set the range; the DYN switch beside GAIN turns it off and on;
  THRESH, ATTACK and RELEASE appear in the band panel. The display shades the
  range and moves the curve with what's heard. New presets: De-Esser, Bass
  Control, Dynamic Air, Mud Tamer.
- **EQ Sketch**: the pencil at the bottom left; draw the curve you want and
  bands are placed to make it.
- **Character**: Clean, Subtle or Warm (a little analog saturation), in the
  bottom bar; preset Tape Warmth.
- The band panel can be dragged aside by its top edge (double-click puts it
  back) and made see-through.
- A list of all the bands under the display, left to right as on the curve:
  click to select, double-click to switch off, + to add; Bands shows or hides it.
- **Updates**: when the plugin starts it looks on GitHub for a newer version
  and offers to update: on Windows it downloads the installer and starts it,
  elsewhere it opens the release page. Help in the top bar checks now or
  turns the automatic check off.

The VST now has everything the browser version has.

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
