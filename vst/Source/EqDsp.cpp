#include "EqDsp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace eq
{
    namespace
    {
        constexpr double pi = 3.14159265358979323846;
        constexpr double sqrt1_2 = 0.70710678118654752440;
        constexpr double flatTiltLo = 20, flatTiltHi = 20000;
        constexpr int subBlock = 32;
        constexpr double fadeSeconds = 0.01, glideSeconds = 0.015;

        // An analog section n2 s^2 + n1 s + n0 over d2 s^2 + d1 s + d0, normalised to its frequency.
        struct Proto { double n2, n1, n0, d2, d1, d0; };

        double analogMag2 (const Proto& c, double x)
        {
            const auto nr = c.n0 - c.n2 * x * x, ni = c.n1 * x, dr = c.d0 - c.d2 * x * x, di = c.d1 * x;
            return (nr * nr + ni * ni) / (dr * dr + di * di);
        }

        void setSection (Section& s, double b0, double b1, double b2, double a1, double a2) { s = { b0, b1, b2, a1, a2 }; }
        void scaleSection (Section& s, double k) { s.b0 *= k; s.b1 *= k; s.b2 *= k; }
        void invertSection (Section& s) { const auto b0 = s.b0; setSection (s, 1 / b0, s.a1 / b0, s.a2 / b0, s.b1 / b0, s.b2 / b0); }

        // First order: the pole matched, the zero fitted at DC and Nyquist.
        void firstOrderSection (const Proto& c, double w0, Section& out)
        {
            const auto a1 = -std::exp (-c.d0 / c.d1 * w0);
            const auto g0 = std::sqrt (analogMag2 (c, 0)), gn = std::sqrt (analogMag2 (c, pi / w0));
            const auto sB0 = (1 + a1) * g0, sB1 = (1 - a1) * gn;
            setSection (out, (sB0 + sB1) / 2, (sB0 - sB1) / 2, 0, a1, 0);
        }

        // The largest root of b0 z^2 + b1 z + b2.
        double zeroRadius (double b0, double b1, double b2)
        {
            const auto d = b1 * b1 - 4 * b0 * b2;
            if (d < 0) return std::sqrt (b2 / b0);
            const auto sd = std::sqrt (d);
            return std::max (std::abs ((-b1 + sd) / (2 * b0)), std::abs ((-b1 - sd) / (2 * b0)));
        }

        // The web's matchSection: poles by the matched z-transform, zeros fitted
        // at DC, Nyquist and the section's own frequency (after M. Vicanek).
        void matchSection (const Proto& c, double w0, Section& out)
        {
            if (c.d2 == 0) { firstOrderSection (c, w0, out); return; }
            const auto dDisc = c.d1 * c.d1 - 4 * c.d2 * c.d0, nDisc = c.n1 * c.n1 - 4 * c.n2 * c.n0;
            if (c.n2 > 0 && dDisc >= 0 && nDisc >= 0)
            {
                // Real poles and zeros: two first-order sections multiplied back together.
                const auto dq = std::sqrt (dDisc), nq = std::sqrt (nDisc), k = c.n2 / c.d2;
                Section A, B;
                firstOrderSection ({ 0, k, k * (c.n1 - nq) / (2 * c.n2), 0, 1, (c.d1 - dq) / (2 * c.d2) }, w0, A);
                firstOrderSection ({ 0, 1, (c.n1 + nq) / (2 * c.n2), 0, 1, (c.d1 + dq) / (2 * c.d2) }, w0, B);
                setSection (out, A.b0 * B.b0, A.b0 * B.b1 + A.b1 * B.b0, A.b1 * B.b1, A.a1 + B.a1, A.a1 * B.a1);
                return;
            }
            const auto wn = std::sqrt (c.d0 / c.d2), zeta = c.d1 / c.d2 / (2 * wn), W = wn * w0;
            const auto r = std::exp (-zeta * W), a2 = r * r;
            const auto a1 = zeta < 1 ? -2 * r * std::cos (std::min (pi, W * std::sqrt (1 - zeta * zeta)))
                                     : -2 * r * std::cosh (W * std::sqrt (zeta * zeta - 1));
            const auto x = pi / w0;
            if (c.n1 == 0 && c.n0 == 0)
            {
                // Highpass: both zeros at DC, the level fitted at Nyquist.
                const auto bh = std::abs (1 - a1 + a2) * std::sqrt (analogMag2 (c, x)) / 4;
                setSection (out, bh, -2 * bh, bh, a1, a2);
                return;
            }
            if (c.n2 == 0 && c.n1 == 0 && W < 0.05 * pi)
            {
                // A lowpass far below Nyquist: the fit would cancel itself out; both
                // zeros at Nyquist (the analog curve is long gone there), unity at DC.
                const auto bl = (1 + a1 + a2) / 4;
                setSection (out, bl, 2 * bl, bl, a1, a2);
                return;
            }
            const auto A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
            const auto B0 = A0 * analogMag2 (c, 0), B1 = A1 * analogMag2 (c, x);
            const auto wm = std::min (W, 0.9 * pi), sm = std::sin (wm / 2), phi1 = sm * sm, phi0 = 1 - phi1, phi2 = 4 * phi0 * phi1;
            auto B2 = ((A0 * phi0 + A1 * phi1 + A2 * phi2) * analogMag2 (c, wm / w0) - B0 * phi0 - B1 * phi1) / phi2;
            const auto sq0 = std::sqrt (B0), sq1 = std::sqrt (B1), Wv = (sq0 + sq1) / 2;
            // The fitted zeros, or the matched ones when the fit has none or puts
            // them (almost) on the unit circle, where turning the section over
            // would make it unstable.
            auto fitted = Wv * Wv + B2 >= 0;
            if (fitted && c.n2 > 0)
            {
                const auto fb0 = (Wv + std::sqrt (Wv * Wv + B2)) / 2, fb1 = (sq0 - sq1) / 2, fb2 = fb0 > 0 ? -B2 / (4 * fb0) : 0;
                fitted = fb0 > 0 && zeroRadius (fb0, fb1, fb2) < 1 - 1e-6;
            }
            if (! fitted)
            {
                if (c.n2 > 0)
                {
                    // No fit with these poles: the zeros by the matched z-transform too
                    // (always inside the unit circle, so the section can be turned over),
                    // the level matched at DC.
                    const auto wz = std::sqrt (c.n0 / c.n2), zz = c.n1 / c.n2 / (2 * wz), Wz = wz * w0;
                    const auto rz = std::exp (-zz * Wz), c2 = rz * rz;
                    const auto c1 = zz < 1 ? -2 * rz * std::cos (std::min (pi, Wz * std::sqrt (1 - zz * zz)))
                                           : -2 * rz * std::cosh (Wz * std::sqrt (zz * zz - 1));
                    const auto k = std::sqrt (analogMag2 (c, 0)) * (1 + a1 + a2) / (1 + c1 + c2);
                    setSection (out, k, k * c1, k * c2, a1, a2);
                    return;
                }
                B2 = -Wv * Wv;   // as close as these poles allow
            }
            const auto b0 = (Wv + std::sqrt (Wv * Wv + B2)) / 2;
            setSection (out, b0, (sq0 - sq1) / 2, b0 > 0 ? -B2 / (4 * b0) : 0, a1, a2);
        }

        // Notch: zeros on the unit circle (a true null), matched poles, unity at DC.
        void notchSection (double w0, double q, Section& out)
        {
            const auto zeta = 1 / (2 * q), r = std::exp (-zeta * w0), a2 = r * r;
            const auto a1 = zeta < 1 ? -2 * r * std::cos (w0 * std::sqrt (1 - zeta * zeta))
                                     : -2 * r * std::cosh (w0 * std::sqrt (zeta * zeta - 1));
            const auto cw = std::cos (w0), k = (1 + a1 + a2) / (2 - 2 * cw);
            setSection (out, k, -2 * cw * k, k, a1, a2);
        }

        Proto bellProto (double g, double q)
        {
            const auto A = std::pow (10.0, std::abs (g) / 40);
            return { 1, A / q, 1, 1, 1 / (A * q), 1 };
        }
        double bellDb (double x, double g, double q)
        {
            const auto d = 10 * std::log10 (analogMag2 (bellProto (g, q), x));
            return g < 0 ? -d : d;
        }

        // Butterworth section Qs for each order, the most resonant last.
        const std::vector<double>& butterQs (int order)
        {
            static const auto table = []
            {
                std::array<std::vector<double>, 17> t;
                for (int o = 0; o <= 16; ++o)
                    for (int k = 1; k <= o / 2; ++k)
                        t[(size_t) o].push_back (1 / (2 * std::cos ((2 * k - 1) * pi / (2 * o))));
                return t;
            }();
            return table[(size_t) std::clamp (order, 0, 16)];
        }

        int roundJs (double v) { return (int) std::floor (v + 0.5); }
    }

    const char* const shapeKeys[numShapes] = { "bell", "lowshelf", "lowcut", "highshelf", "highcut", "notch", "bandpass", "tiltshelf", "flattilt", "allpass" };
    const char* const shapeLabels[numShapes] = { "BELL", "LO SHELF", "LO CUT", "HI SHELF", "HI CUT", "NOTCH", "BAND PASS", "TILT", "FLAT TILT", "ALL PASS" };
    const char* const placeKeys[numPlaces] = { "stereo", "left", "right", "mid", "side" };
    const char* const placeLabels[numPlaces] = { "STEREO", "L", "R", "MID", "SIDE" };

    const char* const phaseKeys[numPhases] = { "zero", "natural", "linear" };
    const char* const phaseLabels[numPhases] = { "Zero Latency", "Natural Phase", "Linear Phase" };
    const char* const phaseResKeys[numPhaseRes] = { "low", "medium", "high", "max" };
    const char* const phaseResLabels[numPhaseRes] = { "Low", "Medium", "High", "Maximum" };

    bool hasGain (Shape s) { return s == Shape::bell || s == Shape::lowshelf || s == Shape::highshelf || s == Shape::tiltshelf || s == Shape::flattilt; }
    bool hasQ (Shape s) { return s != Shape::flattilt; }
    bool hasSlope (Shape s) { return s != Shape::bell && s != Shape::flattilt; }
    bool canSolo (Shape s) { return s != Shape::tiltshelf && s != Shape::flattilt && s != Shape::allpass; }
    bool isCut (Shape s) { return s == Shape::lowcut || s == Shape::highcut; }
    bool isBrick (Shape s, int slope) { return isCut (s) && slope >= brickSlope; }

    bool Band::operator== (const Band& o) const
    {
        return used == o.used && on == o.on && shape == o.shape && freq == o.freq && gain == o.gain && q == o.q && slope == o.slope && place == o.place
            && dyn == o.dyn && range == o.range && thresh == o.thresh && attack == o.attack && release == o.release
            && sc == o.sc && spectral == o.spectral;
    }
    bool Params::isDynamic (int i) const { return bands[(size_t) i].dyn && hasGain (bands[(size_t) i].shape); }
    const char* const characterKeys[numCharacters] = { "clean", "subtle", "warm" };
    const char* const characterLabels[numCharacters] = { "Clean", "Subtle", "Warm" };
    bool Params::operator== (const Params& o) const
    {
        return bands == o.bands && outGain == o.outGain && outPan == o.outPan && gainScale == o.gainScale && autoGain == o.autoGain && power == o.power
            && character == o.character && gainQ == o.gainQ && phase == o.phase && phaseRes == o.phaseRes;
    }
    double Params::effectiveGain (int i) const
    {
        const auto& b = bands[(size_t) i];
        return hasGain (b.shape) ? b.gain * gainScale / 100 : 0;
    }

    double sectionMag (const Section& s, double w)
    {
        const auto c1 = std::cos (w), c2 = std::cos (2 * w), s1 = std::sin (w), s2 = std::sin (2 * w);
        const auto nr = s.b0 + s.b1 * c1 + s.b2 * c2, ni = -(s.b1 * s1 + s.b2 * s2);
        const auto dr = 1 + s.a1 * c1 + s.a2 * c2, di = -(s.a1 * s1 + s.a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    int designBand (Shape shape, double freq, double g, double q, int slope, double fs, Section* secs, bool gainQ)
    {
        const auto f = std::min (freq, freqMax);
        auto w0 = 2 * pi * f / fs;
        int n = 0;
        // GAIN-Q: like an analog desk, a bell narrows as it boosts or cuts more (Q x2 at 15 dB)
        if (gainQ && shape == Shape::bell) q = std::min (qMax, q * std::pow (2.0, std::abs (g) / 15));
        // BRICKWALL in Zero Latency: the steepest Butterworth there is (96 dB/oct)
        const auto order = std::min (16, std::max (1, roundJs (slope / 6.0))), pairs = std::min (maxSections, std::max (1, roundJs (slope / 12.0)));
        const auto fLim = 0.45 * fs;   // a resonance above this can't sit at its own frequency

        switch (shape)
        {
            case Shape::bell:
            {
                if (f > fLim)
                {
                    // Only the lower skirt is heard: a bell at the limit with the analog
                    // level there, as wide as the skirt (same level an octave down).
                    const auto gl = bellDb (fLim / f, g, q), want = std::abs (bellDb (fLim / f / 2, g, q));
                    double qlo = qMin, qhi = qMax;
                    for (int i = 0; i < 40; ++i)
                    {
                        const auto qm = std::sqrt (qlo * qhi);
                        if (std::abs (bellDb (0.5, gl, qm)) > want) qlo = qm; else qhi = qm;
                    }
                    g = gl; q = std::sqrt (qlo * qhi); w0 = 2 * pi * fLim / fs;
                }
                matchSection (bellProto (g, q), w0, secs[n]);
                if (g < 0) invertSection (secs[n]);
                ++n;
                break;
            }
            case Shape::lowshelf:
            case Shape::highshelf:
            case Shape::tiltshelf:
            {
                // A Butterworth shelf (Holters & Zoelzer), each section designed as a
                // low-shelf boost: a cut is its inverse, a high shelf G / H_low.
                const auto high = shape != Shape::lowshelf, flip = high == (g > 0);
                const auto& qs = butterQs (order);
                const auto As = std::pow (10.0, std::abs (g) * 2 / order / 40), G2 = As * As;
                for (size_t i = 0; i < qs.size(); ++i)
                {
                    const auto qk = i == qs.size() - 1 ? qs[i] * q / sqrt1_2 : qs[i], k = std::sqrt (As) / qk;
                    matchSection ({ As, As * k, G2, As, k, 1 }, w0, secs[n]);
                    if (flip) invertSection (secs[n]);
                    if (high) scaleSection (secs[n], g > 0 ? G2 : 1 / G2);
                    ++n;
                }
                if (order % 2)
                {
                    const auto G1 = std::pow (10.0, std::abs (g) / order / 20), r1 = std::sqrt (G1);
                    matchSection ({ 0, 1, r1, 0, 1, 1 / r1 }, w0, secs[n]);
                    if (flip) invertSection (secs[n]);
                    if (high) scaleSection (secs[n], g > 0 ? G1 : 1 / G1);
                    ++n;
                }
                if (shape == Shape::tiltshelf) scaleSection (secs[0], std::pow (10.0, -g / 40));
                break;
            }
            case Shape::lowcut:
            case Shape::highcut:
            {
                const auto hp = shape == Shape::lowcut;
                const auto& qs = butterQs (order);
                for (size_t i = 0; i < qs.size(); ++i)
                {
                    const auto qn = i == qs.size() - 1 ? qs[i] * q / sqrt1_2 : qs[i];
                    matchSection (hp ? Proto { 1, 0, 0, 1, 1 / qn, 1 } : Proto { 0, 0, 1, 1, 1 / qn, 1 }, w0, secs[n++]);
                }
                if (order % 2) matchSection (hp ? Proto { 0, 1, 0, 0, 1, 1 } : Proto { 0, 0, 1, 0, 1, 1 }, w0, secs[n++]);
                break;
            }
            case Shape::notch:
            case Shape::bandpass:
            {
                w0 = 2 * pi * std::min (f, fLim) / fs;
                for (int i = 0; i < pairs; ++i)
                {
                    if (shape == Shape::bandpass) matchSection ({ 0, 1 / q, 0, 1, 1 / q, 1 }, w0, secs[n++]);
                    else notchSection (w0, q, secs[n++]);
                }
                break;
            }
            case Shape::flattilt:
            {
                // A constant dB-per-octave slope (GAIN: the tilt from 20 Hz to 20 kHz) from
                // first-order high shelves spread in log frequency, 0 dB at the band frequency.
                const auto lo = std::log2 (flatTiltLo / 2), step = (std::log2 (flatTiltHi * 2) - lo) / maxSections;
                const auto Gt = std::pow (10.0, g / std::log2 (flatTiltHi / flatTiltLo) * step / 20), rt = std::sqrt (Gt);
                for (int i = 0; i < maxSections; ++i)
                {
                    const auto fc = std::pow (2.0, lo + (i + 0.5) * step);
                    matchSection ({ 0, Gt, rt, 0, 1, rt }, 2 * pi * fc / fs, secs[n++]);
                }
                double at = 1;
                for (int i = 0; i < n; ++i) at *= sectionMag (secs[i], std::min (w0, pi));
                scaleSection (secs[0], 1 / at);
                break;
            }
            case Shape::allpass:
            {
                // Phase only, so the plain bilinear transform is exact.
                const auto wa = std::min (w0, 0.98 * pi);
                if (order == 1)
                {
                    const auto tn = std::tan (wa / 2), ca = (tn - 1) / (tn + 1);
                    setSection (secs[n++], ca, 1, 0, ca, 0);
                }
                else
                {
                    const auto al = std::sin (wa) / (2 * q), cs = std::cos (wa), a0 = 1 + al;
                    for (int i = 0; i < pairs; ++i) setSection (secs[n++], (1 - al) / a0, -2 * cs / a0, 1, -2 * cs / a0, (1 - al) / a0);
                }
                break;
            }
        }
        return n;
    }

    void designSolo (Shape shape, double freq, double q, double fs, Section& out)
    {
        const auto w0 = 2 * pi * std::min (freq, 0.45 * fs) / fs;
        if (shape == Shape::lowshelf || shape == Shape::lowcut) matchSection ({ 0, 0, 1, 1, std::sqrt (2.0), 1 }, w0, out);
        else if (shape == Shape::highshelf || shape == Shape::highcut) matchSection ({ 1, 0, 0, 1, std::sqrt (2.0), 1 }, w0, out);
        else { const auto qq = std::max (0.3, q); matchSection ({ 0, 1 / qq, 0, 1, 1 / qq, 1 }, w0, out); }
    }

    // AUTO GAIN: the level change on pink, A-weighted sound, measured on the curve, turned around.
    namespace { constexpr int points = 64; }

    double autoGainDb (const Params& p, double fs)
    {
        static const auto table = []
        {
            std::array<std::array<double, points>, 2> t {};
            double sum = 0;
            for (int i = 0; i < points; ++i)
            {
                const auto f = 20 * std::pow (1000.0, (i + 0.5) / points), f2 = f * f;
                const auto ra = 12194.0 * 12194.0 * f2 * f2 / ((f2 + 20.6 * 20.6) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
                t[0][(size_t) i] = f; t[1][(size_t) i] = ra * ra; sum += ra * ra;
            }
            for (auto& w : t[1]) w /= sum;
            return t;
        }();
        // This runs on the audio thread whenever a setting changes: the sines and
        // cosines of the 64 points once per call (not once per section), and each
        // section's power straight from them, without a square root.
        std::array<double, points> c1, c2, s1, s2;
        for (size_t k = 0; k < (size_t) points; ++k)
        {
            const auto w = 2 * pi * table[0][k] / fs;
            c1[k] = std::cos (w); c2[k] = std::cos (2 * w); s1[k] = std::sin (w); s2[k] = std::sin (2 * w);
        }
        std::array<double, points> pow;
        pow.fill (1);
        Section secs[maxSections];
        for (int i = 0; i < maxBands; ++i)
        {
            const auto& b = p.bands[(size_t) i];
            if (! b.used || ! b.on || b.place == Place::side) continue;
            const auto n = designBand (b.shape, b.freq, p.effectiveGain (i), b.q, b.slope, fs, secs, p.gainQ);
            const auto full = b.place == Place::stereo || b.place == Place::mid;
            for (size_t k = 0; k < (size_t) points; ++k)
            {
                if (table[0][k] >= fs / 2) continue;
                double m2 = 1;
                for (int s = 0; s < n; ++s)
                {
                    const auto& q = secs[s];
                    const auto nr = q.b0 + q.b1 * c1[k] + q.b2 * c2[k], ni = -(q.b1 * s1[k] + q.b2 * s2[k]);
                    const auto dr = 1 + q.a1 * c1[k] + q.a2 * c2[k], di = -(q.a1 * s1[k] + q.a2 * s2[k]);
                    m2 *= (nr * nr + ni * ni) / (dr * dr + di * di);
                }
                pow[k] *= full ? m2 : (1 + m2) / 2;
            }
        }
        double tot = 0;
        for (int k = 0; k < points; ++k) tot += table[1][(size_t) k] * pow[(size_t) k];
        return std::clamp (-10 * std::log10 (tot), -24.0, 24.0);
    }

    //==============================================================================
    // The analog curves. A brickwall is 120 dB down within a semitone, its
    // edge a raised cosine.
    double brickDb (Shape shape, double bandFreq, double f)
    {
        constexpr double floorDb = -120;
        const auto t = 12 * std::log2 (shape == Shape::lowcut ? f / bandFreq : bandFreq / f);   // semitones into the open side
        if (! (t < 0)) return 0;
        if (t <= -1) return floorDb;
        return floorDb * (0.5 - 0.5 * std::cos (pi * -t));
    }

    namespace
    {
        void mulSection (const Proto& c0, double f0, const double* freqs, int n, double* re, double* im, bool invert, double scale)
        {
            auto c = c0;
            if (invert) { std::swap (c.n2, c.d2); std::swap (c.n1, c.d1); std::swap (c.n0, c.d0); }
            for (int k = 0; k < n; ++k)
            {
                const auto x = freqs[k] / f0, nr = c.n0 - c.n2 * x * x, ni = c.n1 * x, dr = c.d0 - c.d2 * x * x, di = c.d1 * x;
                const auto den = std::max (1e-300, dr * dr + di * di);
                const auto hr = (nr * dr + ni * di) / den * scale, hi = (ni * dr - nr * di) / den * scale;
                const auto r = re[k], m = im[k];
                re[k] = r * hr - m * hi; im[k] = r * hi + m * hr;
            }
        }
    }

    bool analogBand (const AnalogBand& band, const double* freqs, int n, double* re, double* im, bool brickApart)
    {
        const auto shape = band.shape;
        const auto f = std::min (band.freq, freqMax), g = band.gain;
        auto q = band.q;
        if (band.gainQ && shape == Shape::bell) q = std::min (qMax, q * std::pow (2.0, std::abs (g) / 15));
        const auto order = std::min (16, std::max (1, roundJs (band.slope / 6.0))), pairs = std::min (maxSections, std::max (1, roundJs (band.slope / 12.0)));
        switch (shape)
        {
            case Shape::bell:
                mulSection (bellProto (g, q), f, freqs, n, re, im, g < 0, 1);
                break;
            case Shape::lowshelf:
            case Shape::highshelf:
            case Shape::tiltshelf:
            {
                const auto high = shape != Shape::lowshelf, flip = high == (g > 0);
                const auto& qs = butterQs (order);
                const auto As = std::pow (10.0, std::abs (g) * 2 / order / 40), G2 = As * As;
                for (size_t i = 0; i < qs.size(); ++i)
                {
                    const auto qk = i == qs.size() - 1 ? qs[i] * q / sqrt1_2 : qs[i], kk = std::sqrt (As) / qk;
                    mulSection ({ As, As * kk, G2, As, kk, 1 }, f, freqs, n, re, im, flip, high ? (g > 0 ? G2 : 1 / G2) : 1);
                }
                if (order % 2)
                {
                    const auto G1 = std::pow (10.0, std::abs (g) / order / 20), r1 = std::sqrt (G1);
                    mulSection ({ 0, 1, r1, 0, 1, 1 / r1 }, f, freqs, n, re, im, flip, high ? (g > 0 ? G1 : 1 / G1) : 1);
                }
                if (shape == Shape::tiltshelf)
                {
                    const auto ts = std::pow (10.0, -g / 40);
                    for (int k = 0; k < n; ++k) { re[k] *= ts; im[k] *= ts; }
                }
                break;
            }
            case Shape::lowcut:
            case Shape::highcut:
            {
                if (isBrick (shape, band.slope))
                {
                    if (brickApart) return true;
                    for (int k = 0; k < n; ++k) { const auto m = std::pow (10.0, brickDb (shape, band.freq, freqs[k]) / 20); re[k] *= m; im[k] *= m; }
                    return false;
                }
                const auto hp = shape == Shape::lowcut;
                const auto& qs = butterQs (order);
                for (size_t i = 0; i < qs.size(); ++i)
                {
                    const auto qn = i == qs.size() - 1 ? qs[i] * q / sqrt1_2 : qs[i];
                    mulSection (hp ? Proto { 1, 0, 0, 1, 1 / qn, 1 } : Proto { 0, 0, 1, 1, 1 / qn, 1 }, f, freqs, n, re, im, false, 1);
                }
                if (order % 2) mulSection (hp ? Proto { 0, 1, 0, 0, 1, 1 } : Proto { 0, 0, 1, 0, 1, 1 }, f, freqs, n, re, im, false, 1);
                break;
            }
            case Shape::notch:
            case Shape::bandpass:
                for (int i = 0; i < pairs; ++i)
                    mulSection (shape == Shape::bandpass ? Proto { 0, 1 / q, 0, 1, 1 / q, 1 } : Proto { 1, 0, 1, 1, 1 / q, 1 }, f, freqs, n, re, im, false, 1);
                break;
            case Shape::flattilt:
            {
                const auto lo = std::log2 (flatTiltLo / 2), step = (std::log2 (flatTiltHi * 2) - lo) / maxSections;
                const auto Gt = std::pow (10.0, g / std::log2 (flatTiltHi / flatTiltLo) * step / 20), rt = std::sqrt (Gt);
                double ar = 1, ai = 0;
                for (int i = 0; i < maxSections; ++i)
                {
                    const auto fc = std::pow (2.0, lo + (i + 0.5) * step);
                    mulSection ({ 0, Gt, rt, 0, 1, rt }, fc, freqs, n, re, im, false, 1);
                    mulSection ({ 0, Gt, rt, 0, 1, rt }, fc, &f, 1, &ar, &ai, false, 1);
                }
                const auto at = 1 / std::sqrt (ar * ar + ai * ai);   // pinned to 0 dB at the band frequency
                for (int k = 0; k < n; ++k) { re[k] *= at; im[k] *= at; }
                break;
            }
            case Shape::allpass:
                if (order == 1) mulSection ({ 0, -1, 1, 0, 1, 1 }, f, freqs, n, re, im, false, 1);
                else for (int i = 0; i < pairs; ++i) mulSection ({ 1, -1 / q, 1, 1, 1 / q, 1 }, f, freqs, n, re, im, false, 1);
                break;
        }
        return false;
    }

    //==============================================================================
    void FftTable::make (int size)
    {
        n = size;
        c.resize ((size_t) size / 2); s.resize ((size_t) size / 2);
        for (int i = 0; i < size / 2; ++i) { c[(size_t) i] = std::cos (2 * pi * i / size); s[(size_t) i] = std::sin (2 * pi * i / size); }
    }

    void fftWith (const FftTable& t, double* re, double* im, bool inverse)
    {
            const auto n = t.n;
            for (int i = 1, j = 0; i < n; ++i)
            {
                int bit = n >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
            }
            const auto sg = inverse ? 1.0 : -1.0;
            for (int size = 2; size <= n; size <<= 1)
            {
                const auto half = size >> 1, step = n / size;
                for (int i = 0; i < n; i += size)
                    for (int j = 0; j < half; ++j)
                    {
                        const auto wr = t.c[(size_t) (j * step)], wi = sg * t.s[(size_t) (j * step)];
                        const auto a = i + j, b = a + half;
                        const auto tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                    }
            }
            if (inverse) for (int i = 0; i < n; ++i) { re[i] /= n; im[i] /= n; }
    }

    namespace
    {
        // NATURAL / LINEAR PHASE: each mode's kernel length (Linear Phase's by RES).
        constexpr double linSeconds[numPhaseRes] = { 0.085, 0.17, 0.34, 0.68 };
        constexpr double naturalSeconds = 0.34;
        constexpr int naturalPre = 256;   // Natural Phase: room before the analog response starts
        int pow2Round (double x) { return 1 << (int) std::lround (std::log2 (x)); }
        int kernelLength (Phase m, PhaseRes r, double fs) { return pow2Round (fs * (m == Phase::linear ? linSeconds[(int) r] : naturalSeconds)); }
        int specSize (double fs) { return fs > 60000 ? 4096 : 2048; }
        bool needsSpec (const Params& p)
        {
            for (int i = 0; i < maxBands; ++i)
            {
                const auto& b = p.bands[(size_t) i];
                if (b.used && b.on && p.isDynamic (i) && (b.spectral || p.phase != Phase::zero)) return true;
            }
            return false;
        }

        // The minimum-phase spectrum with the magnitude db[0..N/2] (dB) on an N-point grid, by the cepstrum.
        void minPhase (const double* db, int N, double* outRe, double* outIm)
        {
            const auto K = N / 2;
            std::vector<double> re ((size_t) N, 0.0), im ((size_t) N, 0.0);
            FftTable t;
            t.make (N);
            for (int k = 0; k <= K; ++k) { re[(size_t) k] = db[k] * std::log (10.0) / 20; if (k > 0 && k < K) re[(size_t) (N - k)] = re[(size_t) k]; }
            fftWith (t, re.data(), im.data(), true);
            for (int k = 1; k < K; ++k) re[(size_t) k] *= 2;
            for (int k = K + 1; k < N; ++k) re[(size_t) k] = 0;
            std::fill (im.begin(), im.end(), 0.0);
            fftWith (t, re.data(), im.data(), false);
            for (int k = 0; k <= K; ++k) { const auto mg = std::exp (re[(size_t) k]); outRe[k] = mg * std::cos (im[(size_t) k]); outIm[k] = mg * std::sin (im[(size_t) k]); }
        }
    }

    void fft (double* re, double* im, int n, bool inverse)
    {
        FftTable t;
        t.make (n);
        fftWith (t, re, im, inverse);
    }

    int kernelLatency (Phase m, PhaseRes r, double fs)
    {
        if (m == Phase::zero) return 0;
        return kernelBlock + (m == Phase::linear ? kernelLength (m, r, fs) / 2 : naturalPre);
    }

    int expectedLatency (const Params& p, double fs)
    {
        return kernelLatency (p.phase, p.phaseRes, fs) + (needsSpec (p) ? specSize (fs) : 0);
    }

    bool kernelChanged (const Params& a, const Params& b)
    {
        if (a.phase != b.phase || a.phaseRes != b.phaseRes || a.gainScale != b.gainScale || a.gainQ != b.gainQ) return true;
        for (size_t i = 0; i < (size_t) maxBands; ++i)
        {
            const auto& x = a.bands[i];
            const auto& y = b.bands[i];
            if (x.used != y.used || x.on != y.on) return true;
            if (! x.used || ! x.on) continue;
            if (x.shape != y.shape || x.freq != y.freq || x.gain != y.gain || x.q != y.q || x.slope != y.slope || x.place != y.place) return true;
        }
        return false;
    }

    // The whole EQ as four filters from the bands' analog curves:
    //  Linear Phase: the magnitude only, symmetric around half the kernel (no
    //    phase change at all; latency half the kernel); RES sets the length.
    //  Natural Phase: the analog magnitude and phase, delayed a little so the
    //    part the analog filter would have past Nyquist can be made.
    std::unique_ptr<Kernel> buildKernel (const Params& p, double fs)
    {
        auto kernel = std::make_unique<Kernel>();
        kernel->mode = p.phase; kernel->res = p.phaseRes; kernel->fs = fs;
        if (p.phase == Phase::zero) return kernel;
        const auto lin = p.phase == Phase::linear;
        const auto K = kernelLength (p.phase, p.phaseRes, fs), N = 2 * K, nb = K + 1;
        std::vector<double> freqs ((size_t) nb);
        for (int k = 0; k < nb; ++k) freqs[(size_t) k] = k * fs / N;
        struct Spectrum { std::vector<double> re, im; };
        std::array<Spectrum, 4> groups;   // left, right, mid, side (empty: nothing there)
        auto group = [&] (int gi) -> Spectrum&
        {
            auto& g = groups[(size_t) gi];
            if (g.re.empty()) { g.re.assign ((size_t) nb, 1.0); g.im.assign ((size_t) nb, 0.0); }
            return g;
        };
        std::vector<double> tRe ((size_t) nb), tIm ((size_t) nb), db ((size_t) nb);
        auto oneSided = false;
        for (int i = 0; i < maxBands; ++i)
        {
            const auto& b = p.bands[(size_t) i];
            if (! b.used || ! b.on) continue;
            oneSided = oneSided || b.place != Place::stereo;
            std::fill (tRe.begin(), tRe.end(), 1.0); std::fill (tIm.begin(), tIm.end(), 0.0);
            if (analogBand ({ b.shape, b.freq, p.effectiveGain (i), b.q, b.slope, p.gainQ }, freqs.data(), nb, tRe.data(), tIm.data(), ! lin))
            {
                // Natural Phase: the brickwall minimum-phase
                for (int k = 0; k < nb; ++k) db[(size_t) k] = brickDb (b.shape, b.freq, freqs[(size_t) k]);
                minPhase (db.data(), N, tRe.data(), tIm.data());
            }
            auto apply = [&] (int gi)
            {
                auto& g = group (gi);
                for (size_t k = 0; k < (size_t) nb; ++k)
                {
                    const auto r = g.re[k], m = g.im[k];
                    g.re[k] = r * tRe[k] - m * tIm[k]; g.im[k] = r * tIm[k] + m * tRe[k];
                }
            };
            switch (b.place)
            {
                case Place::stereo: apply (0); apply (1); break;
                case Place::left: apply (0); break;
                case Place::right: apply (1); break;
                case Place::mid: apply (2); break;
                case Place::side: apply (3); break;
            }
        }
        Spectrum one { std::vector<double> ((size_t) nb, 1.0), std::vector<double> ((size_t) nb, 0.0) };
        auto get = [&] (int gi) -> const Spectrum& { return groups[(size_t) gi].re.empty() ? one : groups[(size_t) gi]; };
        auto mul = [nb] (const Spectrum& a, const Spectrum& b, double sc)
        {
            Spectrum o { std::vector<double> ((size_t) nb), std::vector<double> ((size_t) nb) };
            for (size_t k = 0; k < (size_t) nb; ++k)
            {
                o.re[k] = (a.re[k] * b.re[k] - a.im[k] * b.im[k]) * sc;
                o.im[k] = (a.re[k] * b.im[k] + a.im[k] * b.re[k]) * sc;
            }
            return o;
        };
        Spectrum ll = get (0), rr = get (1), lr, rl;
        const auto ms = ! groups[2].re.empty() || ! groups[3].re.empty();
        if (ms)
        {
            const auto& M = get (2);
            const auto& S = get (3);
            Spectrum sum { std::vector<double> ((size_t) nb), std::vector<double> ((size_t) nb) }, dif = sum;
            for (size_t k = 0; k < (size_t) nb; ++k)
            {
                sum.re[k] = M.re[k] + S.re[k]; sum.im[k] = M.im[k] + S.im[k];
                dif.re[k] = M.re[k] - S.re[k]; dif.im[k] = M.im[k] - S.im[k];
            }
            const auto L = get (0), R = get (1);
            ll = mul (L, sum, 0.5); rl = mul (L, dif, 0.5); lr = mul (R, dif, 0.5); rr = mul (R, sum, 0.5);
        }
        if (lin)
            for (auto* h : { &ll, &lr, &rl, &rr })
                for (size_t k = 0; k < h->re.size(); ++k) { h->re[k] = std::sqrt (h->re[k] * h->re[k] + h->im[k] * h->im[k]); h->im[k] = 0; }
        const auto D = lin ? K / 2 : naturalPre, P = kernelBlock, nparts = K / P;
        FftTable big, small;
        big.make (N); small.make (2 * P);
        auto parts = [&] (const Spectrum& H)
        {
            std::vector<float> out;
            if (H.re.empty()) return out;
            std::vector<double> re ((size_t) N, 0.0), im ((size_t) N, 0.0);
            for (int j = 0; j <= K; ++j)
            {
                const auto ph = -2 * pi * j * D / N, c = std::cos (ph), sn = std::sin (ph);
                const auto xr = H.re[(size_t) j] * c - H.im[(size_t) j] * sn, xi = H.re[(size_t) j] * sn + H.im[(size_t) j] * c;
                re[(size_t) j] = xr; im[(size_t) j] = j == K ? 0 : xi;
                if (j > 0 && j < K) { re[(size_t) (N - j)] = xr; im[(size_t) (N - j)] = -xi; }
            }
            fftWith (big, re.data(), im.data(), true);
            for (int n = 0; n < K; ++n)
            {
                double w;
                if (lin) w = 0.5 - 0.5 * std::cos (2 * pi * (n + 0.5) / K);
                else w = n < D ? 0.5 - 0.5 * std::cos (pi * n / D) : n > K * 0.75 ? 0.5 + 0.5 * std::cos (pi * (n - K * 0.75) / (K * 0.25)) : 1.0;
                re[(size_t) n] *= w;
            }
            out.assign ((size_t) (nparts * (P + 1) * 2), 0.0f);
            std::vector<double> fr ((size_t) (2 * P)), fi ((size_t) (2 * P));
            for (int j = 0; j < nparts; ++j)
            {
                std::fill (fr.begin(), fr.end(), 0.0); std::fill (fi.begin(), fi.end(), 0.0);
                for (int n = 0; n < P; ++n) fr[(size_t) n] = re[(size_t) (j * P + n)];
                fftWith (small, fr.data(), fi.data(), false);
                for (int n = 0; n <= P; ++n)
                {
                    out[(size_t) ((j * (P + 1) + n) * 2)] = (float) fr[(size_t) n];
                    out[(size_t) ((j * (P + 1) + n) * 2 + 1)] = (float) fi[(size_t) n];
                }
            }
            return out;
        };
        kernel->parts = nparts;
        kernel->latency = P + D;
        kernel->ll = parts (ll);
        if (ms) { kernel->lr = parts (lr); kernel->rl = parts (rl); }
        if (oneSided) kernel->rr = parts (rr);   // nothing only on one side: the right channel's filter is the left's
        return kernel;
    }

    //==============================================================================
    Engine::Engine() { prepare (48000); }
    Engine::~Engine() = default;

    void Engine::prepare (double sampleRate)
    {
        fs = sampleRate;
        fadeStep = 1 / (fadeSeconds * fs);
        glide = 1 - std::exp (-subBlock / (glideSeconds * fs));
        for (auto& b : bands) { b.z.fill (0); b.dirty = true; }
        soloZ.fill (0);
        // NATURAL / LINEAR PHASE: room for the longest kernel at this rate
        const auto P = kernelBlock, N = 2 * P;
        maxParts = kernelLength (Phase::linear, PhaseRes::max, fs) / P;
        for (auto* v : { &fir.inL, &fir.inR, &fir.re, &fir.im }) v->assign ((size_t) N, 0.0);
        for (auto* v : { &fir.outL, &fir.outR, &fir.out2L, &fir.out2R }) v->assign ((size_t) P, 0.0);
        for (auto* v : { &fir.yl, &fir.yr }) v->assign ((size_t) (2 * (P + 1)), 0.0);
        fir.fdlL.assign ((size_t) maxParts, std::vector<double> ((size_t) (2 * (P + 1)), 0.0));
        fir.fdlR = fir.fdlL;
        firTable.make (N);
        // SPECTRAL: the short-time FFT
        spec.N = specSize (fs); spec.H = spec.N / 4;
        const auto nb = spec.N / 2 + 1;
        for (auto* v : { &spec.win, &spec.inL, &spec.inR, &spec.dI, &spec.dS, &spec.accL, &spec.accR, &spec.re, &spec.im, &spec.dre, &spec.dim })
            v->assign ((size_t) spec.N, 0.0);
        for (auto* v : { &spec.outL, &spec.outR }) v->assign ((size_t) spec.H, 0.0);
        for (auto* v : { &spec.gdb, &spec.freqs, &spec.wRe, &spec.wIm }) v->assign ((size_t) nb, 0.0);
        for (int i = 0; i < spec.N; ++i) spec.win[(size_t) i] = std::sin (pi * i / spec.N);   // square-root Hann
        for (int i = 0; i < nb; ++i) spec.freqs[(size_t) i] = i * fs / spec.N;
        spec.cal = 20 * std::log10 (pi / spec.N);   // a sine's bin reads its peak level
        specTable.make (spec.N);
        for (auto& b : bands) { b.specW.assign ((size_t) nb, 0.0); b.specEnv.assign ((size_t) nb, 0.0); b.wF = -1; }
        // the delays: as long as the longest latency
        int size = 1;
        while (size < kernelLatency (Phase::linear, PhaseRes::max, fs) + spec.N + subBlock + 1) size <<= 1;
        for (auto* d : { &dry, &detDelay }) { d->a.assign ((size_t) size, 0.0); d->b.assign ((size_t) size, 0.0); d->mask = size - 1; d->w = 0; }
        // kernels made for another rate are no use (this isn't the audio thread)
        fir.kern.reset(); fir.next.reset(); waiting.reset();
        for (auto& r : retired) r.reset();
        firReset(); specReset();
        if (mode != Phase::zero) gate = 0;
    }

    void Engine::setParams (const Params& p, int soloBand, bool scConnected)
    {
        for (int i = 0; i < maxBands; ++i)
        {
            auto& b = bands[(size_t) i];
            const auto& src = p.bands[(size_t) i];
            b.tShape = src.shape; b.tSlope = src.slope; b.tPlace = src.place;
            b.tActive = src.used && src.on;
            b.tf = src.freq; b.tq = src.q;
            b.staticG = p.effectiveGain (i);
            const auto dyn = b.tActive && p.isDynamic (i);
            if (dyn != b.dyn) { b.env = 0; b.dynDb = 0; b.detZ.fill (0); }
            b.dyn = dyn;
            b.range = src.range * p.gainScale / 100; b.thresh = src.thresh; b.att = src.attack; b.rel = src.release;
            b.sc = src.sc; b.spectral = src.spectral;
            // through the short-time FFT: spectral bands, and every dynamic band in Natural/Linear Phase
            b.viaStft = b.dyn && (b.spectral || p.phase != Phase::zero);
            if (b.viaStft && b.spectral) b.dynDb = 0;
            b.tg = b.staticG + (b.dyn && ! b.viaStft ? b.dynDb : 0);
        }
        if (p.gainQ != gainQ) { gainQ = p.gainQ; for (auto& b : bands) b.dirty = true; }
        tMode = p.phase; tRes = p.phaseRes;
        tSpec = false;
        for (const auto& b : bands) tSpec = tSpec || b.viaStft;
        scOn = scConnected;
        tCharacter = p.character;
        tSolo = soloBand >= 0 && soloBand < maxBands && p.bands[(size_t) soloBand].used && canSolo (p.bands[(size_t) soloBand].shape) ? soloBand : -1;
        autoDb = p.autoGain ? autoGainDb (p, fs) : 0;
        tGain = std::pow (10.0, (p.outGain + autoDb) / 20);
        tPan = p.outPan / 100;
        tPower = p.power ? 1 : 0;
        if (! started)
        {
            // The first settings are where we start, not something to glide to.
            started = true;
            for (auto& b : bands)
            {
                b.shape = b.tShape; b.slope = b.tSlope; b.place = b.tPlace;
                b.f = b.tf; b.g = b.tg; b.q = b.tq; b.mix = b.mixTarget = b.tActive ? 1 : 0; b.dirty = true;
            }
            gain = tGain; pan = tPan; power = tPower;
            character = tCharacter; charMix = character == Character::clean ? 0 : 1;
            if (tMode != Phase::zero || tSpec) gate = 0;   // the mode below switches in silence, then fades in
        }
    }

    // PHASE and the spectral stage: switch in silence, fade back in. Returns the gate before this sub-block.
    double Engine::updateMode (int len)
    {
        auto want = tMode != mode || (tMode == Phase::linear && tRes != res) || tSpec != specOn;
        if (want && gate == 0)
        {
            mode = tMode; res = tRes; specOn = tSpec;
            firReset(); specReset(); dry.reset(); detDelay.reset();
            if (kernelFits (waiting.get(), mode, res)) firSetKernel (std::move (waiting));
            for (auto& b : bands) std::fill (b.specEnv.begin(), b.specEnv.end(), 0.0);
            want = false;
        }
        firLatency = mode != Phase::zero && fir.kern != nullptr ? fir.kern->latency : 0;
        latency = firLatency + (specOn ? spec.N : 0);
        latencyNow.store (latency, std::memory_order_relaxed);
        const double target = want || (mode != Phase::zero && fir.kern == nullptr) ? 0 : 1, g0 = gate;
        gate = target > g0 ? std::min (1.0, g0 + len * fadeStep) : target < g0 ? std::max (0.0, g0 - len * fadeStep) : g0;
        return g0;
    }

    bool Engine::kernelFits (const Kernel* k, Phase m, PhaseRes r) const
    {
        return k != nullptr && m != Phase::zero && k->fs == fs && k->mode == m && (m != Phase::linear || k->res == r);
    }

    void Engine::setKernel (std::unique_ptr<Kernel> k)
    {
        if (k == nullptr) return;
        if (k->fs != fs) { retire (std::move (k)); return; }
        if (kernelFits (k.get(), mode, res)) firSetKernel (std::move (k));
        else { retire (std::move (waiting)); waiting = std::move (k); }
    }

    void Engine::retire (std::unique_ptr<Kernel> k)
    {
        if (k == nullptr) return;
        for (auto& r : retired)
            if (r == nullptr) { r = std::move (k); return; }
        // all full (the caller isn't collecting): freed here, as a last resort
    }

    std::unique_ptr<Kernel> Engine::takeRetired()
    {
        for (auto& r : retired)
            if (r != nullptr) return std::move (r);
        return {};
    }

    void Engine::updateBand (BandState& b)
    {
        const auto changing = b.tShape != b.shape || b.tSlope != b.slope || b.tPlace != b.place;
        b.mixTarget = changing ? 0 : b.tActive ? 1 : 0;
        if (b.mix == 0)
        {
            if (changing) { b.shape = b.tShape; b.slope = b.tSlope; b.place = b.tPlace; b.mixTarget = b.tActive ? 1 : 0; b.dirty = true; }
            if (b.f != b.tf || b.g != b.tg || b.q != b.tq) { b.f = b.tf; b.g = b.tg; b.q = b.tq; b.dirty = true; }
            if (b.mixTarget == 0) return;
            b.z.fill (0);   // fading in from silence: start clean
        }
        else
        {
            const auto k = glide;
            auto moved = false;
            if (b.f != b.tf) { b.f *= std::pow (b.tf / b.f, k); if (std::abs (std::log (b.tf / b.f)) < 1e-4) b.f = b.tf; moved = true; }
            if (b.q != b.tq) { b.q *= std::pow (b.tq / b.q, k); if (std::abs (std::log (b.tq / b.q)) < 1e-4) b.q = b.tq; moved = true; }
            if (b.g != b.tg)
            {
                if (b.dyn) b.g = b.tg;   // the detector's attack and release already shape it
                else { b.g += (b.tg - b.g) * k; if (std::abs (b.tg - b.g) < 1e-3) b.g = b.tg; }
                moved = true;
            }
            if (moved) b.dirty = true;
        }
        if (b.dirty)
        {
            b.n = designBand (b.shape, b.f, b.g, b.q, b.slope, fs, b.secs.data(), gainQ);
            b.dirty = false;
        }
    }

    namespace
    {
        // One sample through a band's sections (transposed direct form II), channel ch.
        inline double runSections (const Section* secs, int n, double* z, int ch, double x)
        {
            for (int s = 0; s < n; ++s)
            {
                const auto& c = secs[s];
                auto* zz = z + s * 4 + ch * 2;
                const auto y = c.b0 * x + zz[0];
                zz[0] = c.b1 * x - c.a1 * y + zz[1];
                zz[1] = c.b2 * x - c.a2 * y;
                x = y;
            }
            return x;
        }

        // A whole sub-block through a band's sections, one section at a time with
        // its state held in registers: the same sums in the same order as
        // runSections, sample by sample, so the result is identical. z: the
        // channel's state in the first section (the right channel's is z + 2).
        inline void runSectionsBlock (const Section* secs, int n, double* z, double* x, int len)
        {
            for (int s = 0; s < n; ++s)
            {
                const auto b0 = secs[s].b0, b1 = secs[s].b1, b2 = secs[s].b2, a1 = secs[s].a1, a2 = secs[s].a2;
                auto* zz = z + s * 4;
                auto z0 = zz[0], z1 = zz[1];
                for (int i = 0; i < len; ++i)
                {
                    const auto in = x[i], y = b0 * in + z0;
                    z0 = b1 * in - a1 * y + z1;
                    z1 = b2 * in - a2 * y;
                    x[i] = y;
                }
                zz[0] = z0; zz[1] = z1;
            }
        }
    }

    // One band over a sub-block: a band fading in or out (or changing shape), or
    // on one side only. Settled stereo bands go through runSettled instead.
    void Engine::runBand (BandState& b, double* L, double* R, int len)
    {
        // The fade for each sample, first.
        double mixes[subBlock];
        auto mix = b.mix;
        const auto target = b.mixTarget, step = fadeStep;
        for (int i = 0; i < len; ++i)
        {
            if (mix != target) mix = mix < target ? std::min (target, mix + step) : std::max (target, mix - step);
            mixes[i] = mix;
        }
        b.mix = mix;
        const auto* secs = b.secs.data();
        auto* z = b.z.data();
        const auto n = b.n;
        double wetL[subBlock], wetR[subBlock];
        switch (b.place)
        {
            case Place::stereo:
                std::copy (L, L + len, wetL); std::copy (R, R + len, wetR);
                runSectionsBlock (secs, n, z, wetL, len);
                runSectionsBlock (secs, n, z + 2, wetR, len);
                for (int i = 0; i < len; ++i) { L[i] = L[i] + mixes[i] * (wetL[i] - L[i]); R[i] = R[i] + mixes[i] * (wetR[i] - R[i]); }
                break;
            case Place::left:
            case Place::right:
            {
                auto* x = b.place == Place::left ? L : R;
                std::copy (x, x + len, wetL);
                runSectionsBlock (secs, n, z, wetL, len);
                for (int i = 0; i < len; ++i) x[i] = x[i] + mixes[i] * (wetL[i] - x[i]);
                break;
            }
            case Place::mid:
            case Place::side:
            {
                double m[subBlock], sd[subBlock];
                for (int i = 0; i < len; ++i) { m[i] = (L[i] + R[i]) * 0.5; sd[i] = (L[i] - R[i]) * 0.5; }
                auto* x = b.place == Place::mid ? m : sd;
                std::copy (x, x + len, wetL);
                runSectionsBlock (secs, n, z, wetL, len);
                for (int i = 0; i < len; ++i) { x[i] += mixes[i] * (wetL[i] - x[i]); L[i] = m[i] + sd[i]; R[i] = m[i] - sd[i]; }
                break;
            }
        }
    }

    // Each sample through every section in the list, left and right. A band
    // that has settled fades at 1, and l + 1 * (y - l) is not always exactly y,
    // so that sum stays: the sound is the same, bit for bit, as band by band.
    void Engine::runSettled (int count, double* L, double* R, int len)
    {
        const auto* const* secs = runSecs.data();
        auto* const* zs = runZ.data();
        const auto* ends = runEnd.data();
        for (int i = 0; i < len; ++i)
        {
            auto l = L[i], r = R[i], bandL = l, bandR = r;
            for (int k = 0; k < count; ++k)
            {
                const auto& c = *secs[k];
                auto* zz = zs[k];
                const auto yl = c.b0 * l + zz[0];
                zz[0] = c.b1 * l - c.a1 * yl + zz[1];
                zz[1] = c.b2 * l - c.a2 * yl;
                const auto yr = c.b0 * r + zz[2];
                zz[2] = c.b1 * r - c.a1 * yr + zz[3];
                zz[3] = c.b2 * r - c.a2 * yr;
                l = yl; r = yr;
                if (ends[k]) { l = bandL + (l - bandL); r = bandR + (r - bandR); bandL = l; bandR = r; }
            }
            L[i] = l; R[i] = r;
        }
    }

    void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n, const float* scL, const float* scR)
    {
        float peaks[4] = {};
        const auto hasSc = scL != nullptr && scR != nullptr;
        for (int off = 0; off < n; off += subBlock)
        {
            const auto len = std::min (subBlock, n - off);
            auto* L = bufL.data();
            auto* R = bufR.data();
            const auto gate0 = updateMode (len);
            // il/ir: the input now; dl/dr: the dry sound, delayed as much as the EQ (BYPASS, SOLO)
            double il[subBlock], ir[subBlock], sl[subBlock], sr[subBlock];
            auto* dl = dryL.data();
            auto* dr = dryR.data();
            for (int i = 0; i < len; ++i)
            {
                il[i] = inL[off + i]; ir[i] = inR[off + i];
                L[i] = dl[i] = il[i]; R[i] = dr[i] = ir[i];
                peaks[0] = std::max (peaks[0], std::abs (inL[off + i]));
                peaks[1] = std::max (peaks[1], std::abs (inR[off + i]));
            }
            if (hasSc) for (int i = 0; i < len; ++i) { sl[i] = scL[off + i]; sr[i] = scR[off + i]; }
            for (int j = 0; j < maxBands; ++j)
            {
                auto& bj = bands[(size_t) j];
                if (bj.dyn && ! (bj.viaStft && bj.spectral)) detect (bj, il, ir, hasSc ? sl : nullptr, hasSc ? sr : nullptr, len, j);
            }
            // The bands in order. Settled stereo bands next to each other (most of
            // them, most of the time) go through together, sample by sample: the
            // processor then has every section's filter to work on at once instead
            // of waiting on one band's at a time. The rest (fading, or on one side)
            // go one band at a time.
            for (auto& b : bands) updateBand (b);
            int count = 0;
            if (mode != Phase::zero) firProcess (L, R, len);   // Natural/Linear Phase: the kernel does every band
            else for (auto& b : bands)
            {
                if (b.mix == 0 && b.mixTarget == 0) continue;
                if (b.place == Place::stereo && b.mix == 1 && b.mixTarget == 1)
                {
                    for (int s = 0; s < b.n; ++s)
                    {
                        runSecs[(size_t) count] = &b.secs[(size_t) s];
                        runZ[(size_t) count] = b.z.data() + s * 4;
                        runEnd[(size_t) count] = s == b.n - 1;
                        ++count;
                    }
                    continue;
                }
                if (count > 0) { runSettled (count, L, R, len); count = 0; }
                runBand (b, L, R, len);
            }
            if (count > 0) runSettled (count, L, R, len);
            if (specOn)
            {
                auto* di = detI.data();
                auto* ds = detS.data();
                for (int i = 0; i < len; ++i) { di[i] = (il[i] + ir[i]) * 0.5; ds[i] = hasSc ? (sl[i] + sr[i]) * 0.5 : 0.0; }
                detDelay.process (di, ds, len, firLatency);   // lined up with what comes out of the EQ
                specProcess (L, R, di, ds, len);
            }
            dry.process (dl, dr, len, latency);
            // SOLO: the soloed band's part of the spectrum, from the input.
            double soloTarget = tSolo >= 0 && tSolo == solo ? 1 : 0;
            if (soloMix == 0 && tSolo != solo) { solo = tSolo; soloZ.fill (0); soloTarget = solo >= 0 ? 1 : 0; }
            if (soloMix > 0 || soloTarget > 0)
            {
                const auto& sb = bands[(size_t) solo];
                designSolo (sb.tShape, sb.f, sb.q, fs, soloSec);
                auto sm = soloMix;
                for (int i = 0; i < len; ++i)
                {
                    if (sm != soloTarget) sm = sm < soloTarget ? std::min (1.0, sm + fadeStep) : std::max (0.0, sm - fadeStep);
                    const auto yl = runSections (&soloSec, 1, soloZ.data(), 0, dl[i]), yr = runSections (&soloSec, 1, soloZ.data(), 1, dr[i]);
                    L[i] += sm * (yl - L[i]); R[i] += sm * (yr - R[i]);
                }
                soloMix = sm;
            }
            runCharacter (L, R, len);
            // Output: gain (with AUTO GAIN), balance, and the bypass switch.
            const auto g0 = gain, p0 = pan, w0 = power, k = glide;
            gain += (tGain - gain) * k; pan += (tPan - pan) * k;
            if (std::abs (tGain - gain) < 1e-6) gain = tGain;
            if (std::abs (tPan - pan) < 1e-6) pan = tPan;
            const auto w1 = power == tPower ? power : power < tPower ? std::min (1.0, power + len * fadeStep) : std::max (0.0, power - len * fadeStep);
            power = w1;
            for (int i = 0; i < len; ++i)
            {
                const auto t = (i + 1) / (double) len, g = g0 + (gain - g0) * t, pn = p0 + (pan - p0) * t, w = w0 + (w1 - w0) * t;
                auto ol = L[i] * g * (pn > 0 ? 1 - pn : 1), orr = R[i] * g * (pn < 0 ? 1 + pn : 1);
                ol = dl[i] + w * (ol - dl[i]); orr = dr[i] + w * (orr - dr[i]);
                const auto gt = gate0 + (gate - gate0) * t;
                ol *= gt; orr *= gt;
                outL[off + i] = (float) ol; outR[off + i] = (float) orr;
                peaks[2] = std::max (peaks[2], (float) std::abs (ol));
                peaks[3] = std::max (peaks[3], (float) std::abs (orr));
            }
        }
        for (int i = 0; i < 4; ++i)
        {
            auto prev = meters[(size_t) i].load (std::memory_order_relaxed);
            while (peaks[i] > prev && ! meters[(size_t) i].compare_exchange_weak (prev, peaks[i])) {}
        }
    }

    // DYNAMIC: the band's part of the input (as SOLO hears it), its level in dB
    // (RMS, +3 dB so a sine reads its peak), attack and release; above
    // THRESHOLD the gain moves 3/4 dB per dB (like a 4:1 compressor), at most RANGE.
    // SC: the detector listens to the sidechain instead of the input.
    void Engine::detect (BandState& b, const double* dl, const double* dr, const double* sl, const double* sr, int len, int index)
    {
        if (b.tShape != b.detShape || b.tf != b.detF || b.tq != b.detQ)
        {
            b.detShape = b.tShape; b.detF = b.tf; b.detQ = b.tq;
            designSolo (b.tShape, b.tf, b.tq, fs, b.det);
        }
        if (b.sc && scOn && sl != nullptr) { dl = sl; dr = sr; }
        double ms = 0;
        for (int i = 0; i < len; ++i)
        {
            const auto y = runSections (&b.det, 1, b.detZ.data(), 0, (dl[i] + dr[i]) * 0.5);
            ms += y * y;
        }
        ms /= len;
        const auto time = ms > b.env ? b.att : b.rel;
        const auto c = 1 - std::exp (-len / (std::max (0.1, time) * 0.001 * fs));
        b.env += (ms - b.env) * c;
        const auto level = 10 * std::log10 (b.env + 1e-12) + 3.0103, over = level - b.thresh;
        double change = 0;
        if (over > 0) change = (b.range < 0 ? -1 : 1) * std::min (std::abs (b.range), over * 0.75);
        b.dynDb = change;
        if (! b.viaStft) b.tg = b.staticG + change;   // in the STFT the change is spread over the bins instead
        dynShown[(size_t) index].store ((float) change, std::memory_order_relaxed);
    }

    // CHARACTER: Subtle and Warm are soft saturation (unity gain for quiet sound,
    // rounded peaks, a touch of even harmonics); Warm drives harder and rolls off
    // the very top. A DC blocker follows; changes fade.
    void Engine::runCharacter (double* L, double* R, int len)
    {
        if (charMix == 0 && tCharacter != character) { character = tCharacter; dcX.fill (0); dcY.fill (0); lpZ.fill (0); }
        const double target = character == tCharacter && character != Character::clean ? 1 : 0;
        if (charMix == 0 && target == 0) return;
        const auto warm = character == Character::warm;
        const auto d = warm ? 1.2 : 0.7, ev = warm ? 0.08 : 0.03;
        const auto dcA = std::exp (-2 * pi * 8 / fs), lpA = warm ? std::exp (-2 * pi * 16000 / fs) : 0.0;
        auto mix = charMix;
        double* chans[2] = { L, R };
        for (int i = 0; i < len; ++i)
        {
            if (mix != target) mix = mix < target ? std::min (1.0, mix + fadeStep) : std::max (0.0, mix - fadeStep);
            for (size_t c = 0; c < 2; ++c)
            {
                const auto x = chans[c][i], t = std::tanh (d * x) / d, y = t + ev * t * t;
                auto hp = y - dcX[c] + dcA * dcY[c];   // DC blocker
                dcX[c] = y; dcY[c] = hp;
                if (lpA > 0) { lpZ[c] = hp + lpA * (lpZ[c] - hp); hp = lpZ[c]; }
                chans[c][i] = x + mix * (hp - x);
            }
        }
        charMix = mix;
    }

    //==============================================================================
    // NATURAL and LINEAR PHASE: uniformly partitioned convolution (overlap-save,
    // blocks of kernelBlock samples) of left and right through the four kernels.
    // A new kernel fades in over one block. Latency: a block plus the kernel's own.
    void Engine::firReset()
    {
        for (auto* v : { &fir.inL, &fir.inR, &fir.outL, &fir.outR }) std::fill (v->begin(), v->end(), 0.0);
        for (auto& v : fir.fdlL) std::fill (v.begin(), v.end(), 0.0);
        for (auto& v : fir.fdlR) std::fill (v.begin(), v.end(), 0.0);
        fir.pos = 0; fir.head = 0;
        retire (std::move (fir.kern));
        retire (std::move (fir.next));
    }

    void Engine::firSetKernel (std::unique_ptr<Kernel> k)
    {
        if (k->parts > maxParts) { retire (std::move (k)); return; }
        if (fir.kern == nullptr) fir.kern = std::move (k);
        else { retire (std::move (fir.next)); fir.next = std::move (k); }
    }

    namespace
    {
        void cmac (double* y, const double* x, const float* h, int n2)
        {
            for (int b = 0; b < n2; b += 2)
            {
                const auto xr = x[b], xi = x[b + 1], hr = (double) h[b], hi = (double) h[b + 1];
                y[b] += xr * hr - xi * hi; y[b + 1] += xr * hi + xi * hr;
            }
        }
    }

    // The output spectra for kernel k, then back to time: the last block's worth of samples.
    void Engine::firRender (const Kernel& k, double* oL, double* oR)
    {
        const auto P = kernelBlock, N = 2 * P, n2 = 2 * (P + 1), nf = maxParts;
        auto* yl = fir.yl.data();
        auto* yr = fir.yr.data();
        auto* re = fir.re.data();
        auto* im = fir.im.data();
        std::fill (fir.yl.begin(), fir.yl.end(), 0.0);
        std::fill (fir.yr.begin(), fir.yr.end(), 0.0);
        const auto& rr = k.rr.empty() ? k.ll : k.rr;
        for (int j = 0; j < k.parts; ++j)
        {
            const auto slot = (size_t) ((fir.head - j + nf) % nf);
            const auto* xl = fir.fdlL[slot].data();
            const auto* xr = fir.fdlR[slot].data();
            const auto o = (size_t) (j * n2);
            cmac (yl, xl, k.ll.data() + o, n2);
            if (! k.lr.empty()) cmac (yl, xr, k.lr.data() + o, n2);
            cmac (yr, xr, rr.data() + o, n2);
            if (! k.rl.empty()) cmac (yr, xl, k.rl.data() + o, n2);
        }
        // both channels in one inverse transform: YL + j YR
        for (int b = 0; b <= P; ++b)
        {
            const auto lr = yl[2 * b], li = yl[2 * b + 1], rre = yr[2 * b], ri = yr[2 * b + 1];
            re[b] = lr - ri; im[b] = li + rre;
            if (b > 0 && b < P) { re[N - b] = lr + ri; im[N - b] = rre - li; }
        }
        fftWith (firTable, re, im, true);
        for (int i = 0; i < P; ++i) { oL[i] = re[P + i]; oR[i] = im[P + i]; }
    }

    void Engine::firBlock()
    {
        const auto P = kernelBlock, N = 2 * P;
        auto* re = fir.re.data();
        auto* im = fir.im.data();
        std::copy (fir.inL.begin(), fir.inL.end(), re);
        std::copy (fir.inR.begin(), fir.inR.end(), im);
        fftWith (firTable, re, im, false);
        fir.head = (fir.head + 1) % maxParts;
        auto* xl = fir.fdlL[(size_t) fir.head].data();
        auto* xr = fir.fdlR[(size_t) fir.head].data();
        for (int k = 0; k <= P; ++k)
        {
            const auto nk = (N - k) % N;
            const auto ar = re[k], ai = im[k], br = re[nk], bi = -im[nk];
            xl[2 * k] = (ar + br) / 2; xl[2 * k + 1] = (ai + bi) / 2;
            xr[2 * k] = (ai - bi) / 2; xr[2 * k + 1] = -(ar - br) / 2;
        }
        firRender (*fir.kern, fir.outL.data(), fir.outR.data());
        if (fir.next != nullptr)
        {
            firRender (*fir.next, fir.out2L.data(), fir.out2R.data());
            for (int i = 0; i < P; ++i)
            {
                const auto t = (i + 0.5) / P;
                fir.outL[(size_t) i] += (fir.out2L[(size_t) i] - fir.outL[(size_t) i]) * t;
                fir.outR[(size_t) i] += (fir.out2R[(size_t) i] - fir.outR[(size_t) i]) * t;
            }
            retire (std::move (fir.kern));
            fir.kern = std::move (fir.next);
        }
        std::copy (fir.inL.begin() + P, fir.inL.end(), fir.inL.begin());
        std::copy (fir.inR.begin() + P, fir.inR.end(), fir.inR.begin());
    }

    void Engine::firProcess (double* L, double* R, int len)
    {
        const auto P = kernelBlock;
        for (int i = 0; i < len; ++i)
        {
            fir.inL[(size_t) (P + fir.pos)] = L[i]; fir.inR[(size_t) (P + fir.pos)] = R[i];
            L[i] = fir.outL[(size_t) fir.pos]; R[i] = fir.outR[(size_t) fir.pos];
            if (++fir.pos == P)
            {
                fir.pos = 0;
                if (fir.kern != nullptr) firBlock();
                else
                {
                    std::fill (fir.outL.begin(), fir.outL.end(), 0.0); std::fill (fir.outR.begin(), fir.outR.end(), 0.0);
                    std::copy (fir.inL.begin() + P, fir.inL.end(), fir.inL.begin());
                    std::copy (fir.inR.begin() + P, fir.inR.end(), fir.inR.begin());
                }
            }
        }
    }

    //==============================================================================
    // SPECTRAL dynamics (and every dynamic band in Natural/Linear Phase): a
    // short-time FFT (2048 points at 44.1/48 kHz, hops of a quarter, square-root
    // Hann in and out), each dynamic band's gain change spread over the bins by
    // the band's own shape. A SPECTRAL band reads its detector bin by bin, so only
    // the frequencies that pass THRESHOLD move (a resonance, not the whole band);
    // the others move as a whole with the band's level. Latency: the FFT size.
    void Engine::specReset()
    {
        for (auto* v : { &spec.inL, &spec.inR, &spec.dI, &spec.dS, &spec.accL, &spec.accR, &spec.outL, &spec.outR })
            std::fill (v->begin(), v->end(), 0.0);
        spec.c = 0;
    }

    // The band's shape, 0..1 (1 where its gain change is whole).
    const std::vector<double>& Engine::specWeights (BandState& b)
    {
        if (b.wShape == b.tShape && b.wF == b.tf && b.wQ == b.tq && b.wSlope == b.tSlope && b.wRange == b.range && b.wGainQ == gainQ)
            return b.specW;
        b.wShape = b.tShape; b.wF = b.tf; b.wQ = b.tq; b.wSlope = b.tSlope; b.wRange = b.range; b.wGainQ = gainQ;
        const auto nb = spec.N / 2 + 1;
        const auto g = std::abs (b.range) > 0.01 ? b.range : 0.0;
        if (g != 0)
        {
            std::fill (spec.wRe.begin(), spec.wRe.end(), 1.0); std::fill (spec.wIm.begin(), spec.wIm.end(), 0.0);
            analogBand ({ b.tShape, b.tf, g, b.tq, b.tSlope, gainQ }, spec.freqs.data(), nb, spec.wRe.data(), spec.wIm.data());
            for (size_t k = 0; k < (size_t) nb; ++k)
                b.specW[k] = std::clamp (10 * std::log10 (spec.wRe[k] * spec.wRe[k] + spec.wIm[k] * spec.wIm[k] + 1e-30) / g, 0.0, 1.0);
        }
        else std::fill (b.specW.begin(), b.specW.end(), 0.0);
        return b.specW;
    }

    void Engine::specProcess (double* L, double* R, const double* dI, const double* dS, int len)
    {
        const auto N = spec.N, H = spec.H;
        for (int i = 0; i < len; ++i)
        {
            const auto k = (size_t) (N - H + spec.c);
            spec.inL[k] = L[i]; spec.inR[k] = R[i]; spec.dI[k] = dI[i]; spec.dS[k] = dS[i];
            L[i] = spec.outL[(size_t) spec.c]; R[i] = spec.outR[(size_t) spec.c];
            if (++spec.c == H) { spec.c = 0; specFrame(); }
        }
    }

    void Engine::specFrame()
    {
        const auto N = spec.N, H = spec.H, nb = N / 2 + 1;
        auto* re = spec.re.data();
        auto* im = spec.im.data();
        auto* dre = spec.dre.data();
        auto* dim = spec.dim.data();
        auto* gdb = spec.gdb.data();
        const auto* win = spec.win.data();
        for (int n = 0; n < N; ++n)
        {
            re[n] = spec.inL[(size_t) n] * win[n]; im[n] = spec.inR[(size_t) n] * win[n];
            dre[n] = spec.dI[(size_t) n] * win[n]; dim[n] = spec.dS[(size_t) n] * win[n];
        }
        fftWith (specTable, re, im, false);
        fftWith (specTable, dre, dim, false);
        std::fill (spec.gdb.begin(), spec.gdb.end(), 0.0);
        auto any = false;
        for (int j = 0; j < maxBands; ++j)
        {
            auto& b = bands[(size_t) j];
            if (! b.viaStft || ! b.tActive) continue;
            const auto& W = specWeights (b);
            if (b.spectral)
            {
                auto* env = b.specEnv.data();
                const auto useSc = b.sc && scOn;
                const auto mag = std::abs (b.range), sg = b.range < 0 ? -1.0 : 1.0;
                const auto ca = 1 - std::exp (-H / (std::max (0.1, b.att) * 0.001 * fs)), cr = 1 - std::exp (-H / (std::max (0.1, b.rel) * 0.001 * fs));
                double shown = 0;
                for (int k = 0; k < nb; ++k)
                {
                    const auto w = W[(size_t) k];
                    if (w < 1e-3) { env[k] = 0; continue; }
                    const auto nk = (N - k) % N;
                    const auto ar = dre[k], ai = dim[k], br = dre[nk], bi = -dim[nk];
                    // the input is in the real part, the sidechain in the imaginary
                    const auto pr = useSc ? (ai - bi) / 2 : (ar + br) / 2, pim = useSc ? -(ar - br) / 2 : (ai + bi) / 2;
                    const auto pw = (pr * pr + pim * pim) * w * w;
                    env[k] += (pw - env[k]) * (pw > env[k] ? ca : cr);
                    if (env[k] < 1e-30) env[k] = 0;
                    const auto over = 10 * std::log10 (env[k] + 1e-30) + spec.cal - b.thresh;
                    if (over > 0 && mag > 0)
                    {
                        const auto ch = w * sg * std::min (mag, over * 0.75);
                        gdb[k] += ch; any = true;
                        if (std::abs (ch) > std::abs (shown)) shown = ch;
                    }
                }
                dynShown[(size_t) j].store ((float) shown, std::memory_order_relaxed);
            }
            else if (b.dynDb != 0)
            {
                for (int k = 0; k < nb; ++k) gdb[k] += W[(size_t) k] * b.dynDb;
                any = true;
            }
        }
        if (any)
            for (int k = 0; k < nb; ++k)
            {
                if (gdb[k] == 0) continue;
                const auto g = std::pow (10.0, gdb[k] / 20);
                re[k] *= g; im[k] *= g;
                if (k > 0 && k < N / 2) { re[N - k] *= g; im[N - k] *= g; }
            }
        fftWith (specTable, re, im, true);
        // square-root Hann twice is Hann; Hann at a quarter hop adds up to 2
        for (int n = 0; n < N; ++n) { spec.accL[(size_t) n] += re[n] * win[n] * 0.5; spec.accR[(size_t) n] += im[n] * win[n] * 0.5; }
        std::copy (spec.accL.begin(), spec.accL.begin() + H, spec.outL.begin());
        std::copy (spec.accR.begin(), spec.accR.begin() + H, spec.outR.begin());
        for (auto* v : { &spec.accL, &spec.accR })
        {
            std::copy (v->begin() + H, v->end(), v->begin());
            std::fill (v->end() - H, v->end(), 0.0);
        }
        for (auto* v : { &spec.inL, &spec.inR, &spec.dI, &spec.dS }) std::copy (v->begin() + H, v->end(), v->begin());
    }

    //==============================================================================
    void Engine::Delay::reset() { std::fill (a.begin(), a.end(), 0.0); std::fill (b.begin(), b.end(), 0.0); w = 0; }

    void Engine::Delay::process (double* x, double* y, int len, int d)
    {
        if (d <= 0) return;
        for (int i = 0; i < len; ++i)
        {
            a[(size_t) w] = x[i]; b[(size_t) w] = y[i];
            const auto r = (size_t) ((w - d) & mask);
            x[i] = a[r]; y[i] = b[r];
            w = (w + 1) & mask;
        }
    }

    void Engine::processBypassed (float* L, float* R, int n)
    {
        for (int off = 0; off < n; off += subBlock)
        {
            const auto len = std::min (subBlock, n - off);
            for (int i = 0; i < len; ++i) { dryL[(size_t) i] = L[off + i]; dryR[(size_t) i] = R[off + i]; }
            dry.process (dryL.data(), dryR.data(), len, latency);
            for (int i = 0; i < len; ++i) { L[off + i] = (float) dryL[(size_t) i]; R[off + i] = (float) dryR[(size_t) i]; }
        }
    }

    std::array<float, 4> Engine::takeMeters()
    {
        std::array<float, 4> m;
        for (size_t i = 0; i < 4; ++i) m[i] = meters[i].exchange (0.0f);
        return m;
    }

    //==============================================================================
    const std::vector<FactoryPreset>& factoryPresets()
    {
        static const std::vector<FactoryPreset> list {
            { "Flat", "" },
            { "Vocal Presence", "b1=lowcut 90 0 0.71 24; b2=bell 280 -2.5 1.2; b3=bell 3200 2.5 0.9; b4=highshelf 11000 2 0.71 12" },
            { "Vocal De-Harsh", "b1=lowcut 80 0 0.71 18; b2=bell 2800 -2 2.5; b3=bell 6500 -3 4; b4=highshelf 12000 1.5 0.71 6" },
            { "Kick Punch", "b1=lowcut 28 0 0.71 24; b2=bell 60 3 1.4; b3=bell 380 -4 1.6; b4=bell 3500 3 1.2" },
            { "Snare Crack", "b1=lowcut 70 0 0.71 24; b2=bell 200 2 1.5; b3=bell 900 -3 1.8; b4=bell 5000 3 1" },
            { "Bass Tighten", "b1=lowcut 35 0 0.9 18; b2=lowshelf 100 2 0.71 12; b3=bell 250 -3 1.5; b4=highcut 6000 0 0.71 12" },
            { "Guitar Fit", "b1=lowcut 90 0 0.71 24; b2=bell 220 -2 1.2; b3=bell 2500 2 1; b4=highcut 12000 0 0.71 12" },
            { "Drum Bus", "b1=lowshelf 80 2 0.71 12; b2=bell 400 -2.5 1; b3=highshelf 8000 2.5 0.71 6" },
            { "De-Mud", "b1=bell 300 -3.5 1.1; b2=bell 550 -1.5 1.8" },
            { "Air Lift", "b1=highshelf 9000 3.5 0.6 6; b2=bell 4000 -1 2" },
            { "Smile", "b1=lowshelf 120 4 0.71 12; b2=bell 1000 -2 0.6; b3=highshelf 6000 4 0.71 12" },
            { "Warm Tilt", "b1=flattilt 1000 -4 1" },
            { "Bright Tilt", "b1=tiltshelf 1500 4 0.6 6" },
            { "Telephone", "b1=lowcut 400 0 0.9 48; b2=highcut 3000 0 1.2 48; b3=bell 1500 4 1" },
            { "Hum Remover", "b1=notch 50 0 12 12; b2=notch 100 0 12 12; b3=notch 150 0 12 12; b4=notch 200 0 12 12" },
            { "Wide Sides", "b1=lowcut 180 0 0.71 24 side; b2=highshelf 8000 3 0.71 12 side; b3=bell 200 1 1 12 mid" },
            { "Master Polish", "b1=lowcut 20 0 0.71 36; b2=lowshelf 90 1 0.71 12; b3=bell 300 -1 0.8; b4=highshelf 12000 1.5 0.71 6" },
            { "De-Esser", "b1=lowcut 70 0 0.71 18; b2=bell 6500 0 3 12 stereo dyn:-8,-34,2,60" },
            { "Bass Control", "b1=lowcut 30 0 0.71 24; b2=lowshelf 120 0 0.71 12 stereo dyn:-6,-22,15,180" },
            { "Dynamic Air", "b1=highshelf 10000 0 0.71 12 stereo dyn:4,-40,20,300" },
            { "Mud Tamer", "b1=bell 280 0 1.2 12 stereo dyn:-5,-26,10,150; b2=bell 4000 1 0.8" },
            { "Tape Warmth", "b1=lowcut 25 0 0.71 12; b2=lowshelf 100 1.5 0.71 12; b3=highshelf 12000 -1.5 0.71 6; char=warm" },
            { "Resonance Tamer", "b1=bell 3000 0 0.5 12 stereo dyn:-6,-36,5,80 spec" },
            { "Sidechain Duck", "b1=lowshelf 150 0 0.71 12 stereo dyn:-9,-30,3,120 sc" },
            { "Master Linear", "b1=lowcut 25 0 0.71 120; b2=lowshelf 90 1 0.71 12; b3=highshelf 12000 1.5 0.71 6; phase=linear; res=high" },
        };
        return list;
    }

    Params presetParams (const std::string& spec)
    {
        Params p;
        std::stringstream items (spec);
        std::string item;
        auto trim = [] (std::string s)
        {
            const auto a = s.find_first_not_of (" \t"), b = s.find_last_not_of (" \t");
            return a == std::string::npos ? std::string() : s.substr (a, b - a + 1);
        };
        while (std::getline (items, item, ';'))
        {
            const auto eqPos = item.find ('=');
            if (eqPos == std::string::npos) continue;
            const auto key = trim (item.substr (0, eqPos));
            std::stringstream vs (trim (item.substr (eqPos + 1)));
            std::vector<std::string> v;
            for (std::string w; vs >> w;) v.push_back (w);
            if (v.empty()) continue;
            if (key.size() > 1 && key[0] == 'b' && std::isdigit ((unsigned char) key[1]))
            {
                const auto i = std::stoi (key.substr (1)) - 1;
                if (i < 0 || i >= maxBands || v.size() < 4) continue;
                const auto s = std::find_if (std::begin (shapeKeys), std::end (shapeKeys), [&] (const char* k) { return v[0] == k; });
                if (s == std::end (shapeKeys)) continue;
                auto& b = p.bands[(size_t) i];
                b.used = true; b.shape = (Shape) (s - std::begin (shapeKeys));
                b.freq = std::stod (v[1]); b.gain = std::stod (v[2]); b.q = std::stod (v[3]);
                if (v.size() > 4) b.slope = std::stoi (v[4]);
                if (v.size() > 5)
                    for (int k = 0; k < numPlaces; ++k)
                        if (v[5] == placeKeys[k]) b.place = (Place) k;
                for (size_t t = 6; t < v.size(); ++t)
                {
                    if (v[t] == "off") b.on = false;
                    if (v[t] == "sc") b.sc = true;
                    if (v[t] == "spec") b.spectral = true;
                    if (v[t].rfind ("dyn:", 0) == 0)
                    {
                        double r, th, at, re;
                        if (std::sscanf (v[t].c_str() + 4, "%lf,%lf,%lf,%lf", &r, &th, &at, &re) == 4)
                        { b.dyn = true; b.range = r; b.thresh = th; b.attack = at; b.release = re; }
                    }
                }
            }
            else if (key == "scale") p.gainScale = std::stod (v[0]);
            else if (key == "gain") p.outGain = std::stod (v[0]);
            else if (key == "pan") p.outPan = std::stod (v[0]);
            else if (key == "auto") p.autoGain = v[0] == "1";
            else if (key == "char")
            {
                for (int k = 0; k < numCharacters; ++k)
                    if (v[0] == characterKeys[k]) p.character = (Character) k;
            }
            else if (key == "gq") p.gainQ = v[0] == "1";
            else if (key == "phase")
            {
                for (int k = 0; k < numPhases; ++k)
                    if (v[0] == phaseKeys[k]) p.phase = (Phase) k;
            }
            else if (key == "res")
            {
                for (int k = 0; k < numPhaseRes; ++k)
                    if (v[0] == phaseResKeys[k]) p.phaseRes = (PhaseRes) k;
            }
        }
        return p;
    }

    //==============================================================================
    namespace
    {
        struct Rng
        {
            uint32_t seed;
            double next() { seed = seed * 1664525u + 1013904223u; return seed / 2147483648.0 - 1; }
        };
        // A band-limited (polyBLEP) saw.
        struct Saw
        {
            double ph = 0, dt;
            Saw (double f, double fs) : dt (f / fs) {}
            double next()
            {
                const auto t = ph;
                auto v = 2 * t - 1;
                if (t < dt) { const auto a = t / dt; v -= a + a - a * a - 1; }
                else if (t > 1 - dt) { const auto b = (t - 1) / dt; v -= b * b + b + b + 1; }
                ph += dt; if (ph >= 1) ph -= 1;
                return v;
            }
        };
        double mtof (int m) { return 440 * std::pow (2.0, (m - 69) / 12.0); }
    }

    // The web page's makeDemo, step for step (same random numbers, same order).
    void makeDemo (double fs, std::vector<float>& L, std::vector<float>& R)
    {
        const double bpm = 112, beat = 60 / bpm;
        const auto len = roundJs (beat * 8 * fs);
        L.assign ((size_t) len, 0.0f); R.assign ((size_t) len, 0.0f);
        Rng rng { 22222 };
        auto add = [&] (double start, double dur, auto&& fn)
        {
            const auto s0 = roundJs (start * fs), n = std::min (len - s0, roundJs (dur * fs));
            for (int i = 0; i < n; ++i)
            {
                const auto v = fn (i / fs);
                L[(size_t) (s0 + i)] += (float) v.first; R[(size_t) (s0 + i)] += (float) v.second;
            }
        };
        for (int bt = 0; bt < 8; ++bt)
        {
            const auto t0 = bt * beat;
            {
                double ph = 0;
                add (t0, 0.45, [&] (double t) { ph += (45 + 75 * std::exp (-t * 30)) / fs; const auto v = std::sin (2 * pi * ph) * std::exp (-t * 7) * 0.8; return std::pair { v, v }; });
            }
            if (bt % 2 == 1)
            {
                double hp = 0, prev = 0;
                add (t0, 0.3, [&] (double t)
                {
                    const auto w = rng.next();
                    hp = 0.85 * (hp + w - prev); prev = w;
                    const auto v = std::sin (2 * pi * 185 * t) * std::exp (-t * 20) * 0.28 + hp * std::exp (-t * 14) * 0.3;
                    return std::pair { v, v };
                });
            }
            for (int h = 0; h < 2; ++h)
            {
                const auto open = bt == 7 && h == 1;
                double lp = 0;
                add (t0 + h * beat / 2, open ? 0.35 : 0.08, [&] (double t)
                {
                    const auto w = rng.next();
                    lp += 0.35 * (w - lp);
                    const auto v = (w - lp) * std::exp (-t * (open ? 11 : 60)) * 0.16;
                    return std::pair { v * 0.8, v };
                });
            }
            {
                static const int notes[] = { 33, 33, 36, 36, 29, 29, 31, 31 };
                Saw s (mtof (notes[bt]), fs);
                double lp = 0;
                add (t0, beat * 0.9, [&] (double t)
                {
                    const auto env = std::min (1.0, t / 0.005) * (t > beat * 0.8 ? std::max (0.0, 1 - (t - beat * 0.8) / (beat * 0.1)) : 1.0);
                    lp += 0.05 * (s.next() - lp);
                    const auto v = lp * env * 0.55;
                    return std::pair { v, v };
                });
            }
        }
        const int chords[2][4] = { { 57, 60, 64, 67 }, { 53, 57, 60, 64 } };
        for (int bar = 0; bar < 2; ++bar)
            for (auto m : chords[bar])
            {
                Saw sl (mtof (m) * 1.0035, fs), sr (mtof (m) / 1.0035, fs);
                double ll = 0, lr = 0;
                add (bar * beat * 4, beat * 4, [&] (double t)
                {
                    const auto env = std::min (1.0, t / 0.25) * std::min (1.0, (beat * 4 - t) / 0.05);
                    ll += 0.12 * (sl.next() - ll); lr += 0.12 * (sr.next() - lr);
                    return std::pair { ll * env * 0.07, lr * env * 0.07 };
                });
            }
        float peak = 0;
        for (int i = 0; i < len; ++i) peak = std::max ({ peak, std::abs (L[(size_t) i]), std::abs (R[(size_t) i]) });
        for (int i = 0; i < len; ++i) { L[(size_t) i] *= 0.7f / peak; R[(size_t) i] *= 0.7f / peak; }
    }

    // Pink noise (Paul Kellet's filter), 4 seconds, different left and right.
    void makeNoise (double fs, std::vector<float>& L, std::vector<float>& R)
    {
        const auto len = (size_t) (fs * 4);
        Rng rng { 777 };
        for (auto* x : { &L, &R })
        {
            x->assign (len, 0.0f);
            double b0 = 0, b1 = 0, b2 = 0;
            for (size_t i = 0; i < len; ++i)
            {
                const auto w = rng.next();
                b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
                (*x)[i] = (float) ((b0 + b1 + b2 + w * 0.1848) * 0.06);
            }
        }
    }

    //==============================================================================
    Analyzer::Analyzer() : ring ((size_t) maxSize, 0.0f) { setSize (8192); }

    void Analyzer::push (const float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) write] = x[i];
            write = (write + 1) % maxSize;
        }
    }

    void Analyzer::setSize (int fftSize)
    {
        size = std::clamp (fftSize, 32, maxSize);
        re.assign ((size_t) size, 0.0f); im.assign ((size_t) size, 0.0f);
        smoothed.assign ((size_t) size / 2, 0.0f);
        db.assign ((size_t) size / 2, -200.0f);
    }

    void Analyzer::fft (std::vector<float>& real, std::vector<float>& imag, int n)
    {
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (real[(size_t) i], real[(size_t) j]); std::swap (imag[(size_t) i], imag[(size_t) j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const auto ang = -2 * pi / len;
            const auto wr = std::cos (ang), wi = std::sin (ang);
            for (int i = 0; i < n; i += len)
            {
                double cr = 1, ci = 0;
                for (int k = 0; k < len / 2; ++k)
                {
                    const auto a = (size_t) (i + k), b = (size_t) (i + k + len / 2);
                    const auto tr = real[b] * cr - imag[b] * ci, ti = real[b] * ci + imag[b] * cr;
                    real[b] = (float) (real[a] - tr); imag[b] = (float) (imag[a] - ti);
                    real[a] = (float) (real[a] + tr); imag[a] = (float) (imag[a] + ti);
                    const auto nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr; cr = nr;
                }
            }
        }
    }

    const std::vector<float>& Analyzer::compute (double smoothing)
    {
        const auto n = size;
        for (int i = 0; i < n; ++i)
        {
            const auto x = ring[(size_t) ((write - n + i + maxSize) % maxSize)];
            const auto w = 0.42 - 0.5 * std::cos (2 * pi * i / n) + 0.08 * std::cos (4 * pi * i / n);   // Blackman
            re[(size_t) i] = (float) (x * w); im[(size_t) i] = 0.0f;
        }
        fft (re, im, n);
        for (int k = 0; k < n / 2; ++k)
        {
            const auto mag = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) / n;
            smoothed[(size_t) k] = (float) (smoothing * smoothed[(size_t) k] + (1 - smoothing) * mag);
            db[(size_t) k] = smoothed[(size_t) k] > 0 ? 20.0f * std::log10 (smoothed[(size_t) k]) : -200.0f;
        }
        return db;
    }
}
