#include "EqDsp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
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
            const auto A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
            const auto B0 = A0 * analogMag2 (c, 0), B1 = A1 * analogMag2 (c, x);
            const auto wm = std::min (W, 0.9 * pi), sm = std::sin (wm / 2), phi1 = sm * sm, phi0 = 1 - phi1, phi2 = 4 * phi0 * phi1;
            auto B2 = ((A0 * phi0 + A1 * phi1 + A2 * phi2) * analogMag2 (c, wm / w0) - B0 * phi0 - B1 * phi1) / phi2;
            const auto sq0 = std::sqrt (B0), sq1 = std::sqrt (B1), Wv = (sq0 + sq1) / 2;
            if (Wv * Wv + B2 < 0) B2 = -Wv * Wv;   // as close as these poles allow
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

    bool hasGain (Shape s) { return s == Shape::bell || s == Shape::lowshelf || s == Shape::highshelf || s == Shape::tiltshelf || s == Shape::flattilt; }
    bool hasQ (Shape s) { return s != Shape::flattilt; }
    bool hasSlope (Shape s) { return s != Shape::bell && s != Shape::flattilt; }
    bool canSolo (Shape s) { return s != Shape::tiltshelf && s != Shape::flattilt && s != Shape::allpass; }

    bool Band::operator== (const Band& o) const
    {
        return used == o.used && on == o.on && shape == o.shape && freq == o.freq && gain == o.gain && q == o.q && slope == o.slope && place == o.place;
    }
    bool Params::operator== (const Params& o) const
    {
        return bands == o.bands && outGain == o.outGain && outPan == o.outPan && gainScale == o.gainScale && autoGain == o.autoGain && power == o.power;
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

    int designBand (Shape shape, double freq, double g, double q, int slope, double fs, Section* secs)
    {
        const auto f = std::min (freq, freqMax);
        auto w0 = 2 * pi * f / fs;
        int n = 0;
        const auto order = std::max (1, roundJs (slope / 6.0)), pairs = std::max (1, roundJs (slope / 12.0));
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
    double autoGainDb (const Params& p, double fs)
    {
        constexpr int points = 64;
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
        std::array<double, points> pow;
        pow.fill (1);
        Section secs[maxSections];
        for (int i = 0; i < maxBands; ++i)
        {
            const auto& b = p.bands[(size_t) i];
            if (! b.used || ! b.on || b.place == Place::side) continue;
            const auto n = designBand (b.shape, b.freq, p.effectiveGain (i), b.q, b.slope, fs, secs);
            const auto full = b.place == Place::stereo || b.place == Place::mid;
            for (int k = 0; k < points; ++k)
            {
                if (table[0][(size_t) k] >= fs / 2) continue;
                double m = 1;
                for (int s = 0; s < n; ++s) m *= sectionMag (secs[s], 2 * pi * table[0][(size_t) k] / fs);
                pow[(size_t) k] *= full ? m * m : (1 + m * m) / 2;
            }
        }
        double tot = 0;
        for (int k = 0; k < points; ++k) tot += table[1][(size_t) k] * pow[(size_t) k];
        return std::clamp (-10 * std::log10 (tot), -24.0, 24.0);
    }

    //==============================================================================
    void Engine::prepare (double sampleRate)
    {
        fs = sampleRate;
        fadeStep = 1 / (fadeSeconds * fs);
        glide = 1 - std::exp (-subBlock / (glideSeconds * fs));
        for (auto& b : bands) { b.z.fill (0); b.dirty = true; }
        soloZ.fill (0);
    }

    void Engine::setParams (const Params& p, int soloBand)
    {
        for (int i = 0; i < maxBands; ++i)
        {
            auto& b = bands[(size_t) i];
            const auto& src = p.bands[(size_t) i];
            b.tShape = src.shape; b.tSlope = src.slope; b.tPlace = src.place;
            b.tActive = src.used && src.on;
            b.tf = src.freq; b.tq = src.q; b.tg = p.effectiveGain (i);
        }
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
        }
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
            if (b.g != b.tg) { b.g += (b.tg - b.g) * k; if (std::abs (b.tg - b.g) < 1e-3) b.g = b.tg; moved = true; }
            if (moved) b.dirty = true;
        }
        if (b.dirty)
        {
            b.n = designBand (b.shape, b.f, b.g, b.q, b.slope, fs, b.secs.data());
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
    }

    void Engine::runBand (BandState& b, double* L, double* R, int len)
    {
        auto mix = b.mix;
        const auto target = b.mixTarget, step = fadeStep;
        const auto* secs = b.secs.data();
        auto* z = b.z.data();
        const auto n = b.n;
        for (int i = 0; i < len; ++i)
        {
            if (mix != target) mix = mix < target ? std::min (target, mix + step) : std::max (target, mix - step);
            const auto l = L[i], r = R[i];
            double y;
            switch (b.place)
            {
                case Place::stereo:
                    y = runSections (secs, n, z, 0, l); L[i] = l + mix * (y - l);
                    y = runSections (secs, n, z, 1, r); R[i] = r + mix * (y - r);
                    break;
                case Place::left:
                    y = runSections (secs, n, z, 0, l); L[i] = l + mix * (y - l);
                    break;
                case Place::right:
                    y = runSections (secs, n, z, 0, r); R[i] = r + mix * (y - r);
                    break;
                case Place::mid:
                case Place::side:
                {
                    auto m = (l + r) * 0.5, sd = (l - r) * 0.5;
                    if (b.place == Place::mid) { y = runSections (secs, n, z, 0, m); m += mix * (y - m); }
                    else { y = runSections (secs, n, z, 0, sd); sd += mix * (y - sd); }
                    L[i] = m + sd; R[i] = m - sd;
                    break;
                }
            }
        }
        b.mix = mix;
    }

    void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        float peaks[4] = {};
        for (int off = 0; off < n; off += subBlock)
        {
            const auto len = std::min (subBlock, n - off);
            auto* L = bufL.data();
            auto* R = bufR.data();
            double dl[subBlock], dr[subBlock];
            for (int i = 0; i < len; ++i)
            {
                dl[i] = inL[off + i]; dr[i] = inR[off + i];
                L[i] = dl[i]; R[i] = dr[i];
                peaks[0] = std::max (peaks[0], std::abs (inL[off + i]));
                peaks[1] = std::max (peaks[1], std::abs (inR[off + i]));
            }
            for (auto& b : bands)
            {
                updateBand (b);
                if (b.mix > 0 || b.mixTarget > 0) runBand (b, L, R, len);
            }
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
            // Output: gain (with AUTO GAIN), balance, and the power switch (bypass).
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
                if (v.size() > 6 && v[6] == "off") b.on = false;
            }
            else if (key == "scale") p.gainScale = std::stod (v[0]);
            else if (key == "gain") p.outGain = std::stod (v[0]);
            else if (key == "pan") p.outPan = std::stod (v[0]);
            else if (key == "auto") p.autoGain = v[0] == "1";
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
