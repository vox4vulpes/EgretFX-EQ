# Changelog

Each release's notes on GitHub start with its section from this file — the
release workflow refuses to publish a version that has no section here.

## [1.0.0]

### Added
- **Egret EQ** (web and VST3/standalone): 翡翠 · Parametric Equalizer with up
  to 24 bands — bell, low/high shelf, low/high cut, notch, band pass, tilt
  shelf, flat tilt and all pass; slopes from 6 to 96 dB/oct; each band on both
  channels, left, right, mid or side.
- Analog-matched filters: curves keep their analog shape up to Nyquist, cuts
  mirror boosts; zero latency; gliding, click-free changes.
- The interactive display: add, drag, scroll for Q, switch off, delete; the
  hovered band's numbers and note.
- Spectrum analyzer before and after the EQ: resolution, speed, range, tilt,
  freeze; display range ±3/6/12/30 dB; in and out level meters.
- Band solo, gain scale, output gain and pan, auto gain.
- Factory presets and your own, undo/redo, A/B comparison.
- Sources in the browser and the standalone app: a demo loop, a file, the
  audio input, pink noise.
