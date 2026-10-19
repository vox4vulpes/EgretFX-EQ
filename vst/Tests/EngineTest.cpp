// Egret EQ DSP self-test: runs the engine on test signals and checks it
// against the filter design, the way the web version is checked. Returns
// non-zero on failure, so CI stops before building the release.
#include "EqDsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace eq;

namespace
{
    int failures = 0;
    void check (bool ok, const char* what, double got, double want)
    {
        std::printf ("%s %-46s got %10.4f  want %10.4f\n", ok ? "ok  " : "FAIL", what, got, want);
        if (! ok) ++failures;
    }

    // Level change in dB of a sine through the engine (left, right), steady state.
    std::pair<double, double> sineGain (const Params& p, double f, bool side = false, int solo = -1, double fs = 48000)
    {
        Engine e;
        e.prepare (fs);
        e.setParams (p, solo);
        const auto n = (int) fs;
        std::vector<float> L ((size_t) n), R ((size_t) n), oL ((size_t) n), oR ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const auto v = (float) (0.5 * std::sin (2 * 3.14159265358979 * f * i / fs));
            L[(size_t) i] = v; R[(size_t) i] = side ? -v : v;
        }
        for (int o = 0; o + 128 <= n; o += 128)
            e.process (L.data() + o, R.data() + o, oL.data() + o, oR.data() + o, 128);
        double a = 0, b = 0, c = 0;
        for (int i = n / 2; i < n - 128; ++i) { a += L[(size_t) i] * L[(size_t) i]; b += oL[(size_t) i] * oL[(size_t) i]; c += oR[(size_t) i] * oR[(size_t) i]; }
        return { 10 * std::log10 (b / a), 10 * std::log10 (c / a) };
    }

    double designedDb (const Params& p, double f, double fs = 48000)
    {
        double db = 0;
        Section s[maxSections];
        for (int i = 0; i < maxBands; ++i)
        {
            const auto& b = p.bands[(size_t) i];
            if (! b.used || ! b.on) continue;
            const auto n = designBand (b.shape, b.freq, p.effectiveGain (i), b.q, b.slope, fs, s);
            for (int k = 0; k < n; ++k) db += 20 * std::log10 (sectionMag (s[k], 2 * 3.14159265358979 * f / fs));
        }
        return db;
    }

    // Through the engine in 128-sample blocks; in Natural/Linear Phase the kernel
    // is built first, as the plugin's builder thread does.
    struct Run { std::vector<float> L, R; int latency; };
    Run runEngine (const Params& p, const std::vector<float>& inL, const std::vector<float>& inR,
                   const std::vector<float>* sc = nullptr, double fs = 48000)
    {
        Engine e;
        e.prepare (fs);
        e.setParams (p, -1, sc != nullptr);
        if (p.phase != Phase::zero) e.setKernel (buildKernel (p, fs));
        const auto n = inL.size();
        Run r { std::vector<float> (n), std::vector<float> (n), 0 };
        for (size_t o = 0; o + 128 <= n; o += 128)
            e.process (inL.data() + o, inR.data() + o, r.L.data() + o, r.R.data() + o, 128,
                       sc != nullptr ? sc->data() + o : nullptr, sc != nullptr ? sc->data() + o : nullptr);
        r.latency = e.getLatency();
        return r;
    }
    std::vector<float> sine (double f, double amp, double secs, double fs = 48000)
    {
        std::vector<float> x ((size_t) (secs * fs));
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin (2 * 3.14159265358979 * f * (double) i / fs));
        return x;
    }
    // The level of out against in over the last quarter second (both steady), in dB.
    double levelDb (const std::vector<float>& out, const std::vector<float>& in, double fs = 48000)
    {
        double a = 0, b = 0;
        for (size_t i = out.size() - (size_t) (fs / 4); i < out.size() - 128; ++i) { a += (double) out[i] * out[i]; b += (double) in[i] * in[i]; }
        return 10 * std::log10 (a / b);
    }
    double analogDb (const Params& p, double f)
    {
        double re = 1, im = 0;
        for (int i = 0; i < maxBands; ++i)
        {
            const auto& b = p.bands[(size_t) i];
            if (b.used && b.on) analogBand ({ b.shape, b.freq, p.effectiveGain (i), b.q, b.slope, p.gainQ }, &f, 1, &re, &im);
        }
        return 10 * std::log10 (re * re + im * im);
    }
}

int main()
{
    // 1. What the engine does is what the curve shows.
    {
        const auto p = presetParams ("b1=lowcut 90 0 0.71 24; b2=bell 280 -2.5 1.2; b3=bell 3200 6 0.9; b4=highshelf 11000 4 0.71 12; b5=notch 60 0 8 12");
        for (auto f : { 50.0, 100.0, 300.0, 1000.0, 3200.0, 12000.0 })
        {
            const auto got = sineGain (p, f).first, want = designedDb (p, f);
            char what[64];
            std::snprintf (what, sizeof what, "stereo bands at %.0f Hz", f);
            check (std::abs (got - want) < 0.05, what, got, want);
        }
    }
    // 2. Stereo placement.
    {
        const auto mid = presetParams ("b1=bell 1000 12 1 12 mid");
        check (std::abs (sineGain (mid, 1000).first - 12) < 0.05, "mid band, mono signal", sineGain (mid, 1000).first, 12);
        check (std::abs (sineGain (mid, 1000, true).first) < 0.05, "mid band, side signal", sineGain (mid, 1000, true).first, 0);
        const auto left = presetParams ("b1=bell 1000 12 1 12 left");
        const auto g = sineGain (left, 1000);
        check (std::abs (g.first - 12) < 0.05 && std::abs (g.second) < 0.05, "left band: left +12, right 0", g.first, 12);
        const auto off = presetParams ("b1=bell 1000 12 1 12 stereo off");
        check (std::abs (sineGain (off, 1000).first) < 0.05, "a band switched off does nothing", sineGain (off, 1000).first, 0);
    }
    // 3. SOLO plays the band's region.
    {
        const auto p = presetParams ("b1=bell 1000 12 2");
        check (std::abs (sineGain (p, 1000, false, 0).first) < 0.1, "solo: the band's centre at 0 dB", sineGain (p, 1000, false, 0).first, 0);
        check (sineGain (p, 100, false, 0).first < -20, "solo: an octave and more away is gone", sineGain (p, 100, false, 0).first, -20);
    }
    // 4. Output gain, gain scale, auto gain.
    {
        auto p = presetParams ("b1=bell 1000 6 1; gain=-3");
        check (std::abs (sineGain (p, 1000).first - 3) < 0.05, "+6 band, -3 output", sineGain (p, 1000).first, 3);
        p.gainScale = 50;
        check (std::abs (sineGain (p, 1000).first) < 0.05, "gain scale 50%: +3 band, -3 output", sineGain (p, 1000).first, 0);
        const auto a = presetParams ("b1=highshelf 3000 6 0.71 12; auto=1");
        const auto ag = autoGainDb (a, 48000);
        check (ag < -1 && ag > -6, "auto gain turns a +6 shelf down", ag, -3);
        check (std::abs (sineGain (a, 100).first - ag) < 0.05, "auto gain heard below the shelf", sineGain (a, 100).first, ag);
    }
    // 4b. Dynamics: nothing below the threshold, the full range well above it; Character.
    {
        const auto down = presetParams ("b2=bell 1000 0 1 12 stereo dyn:-8,-30,5,80");
        auto at = [&] (const Params& p, double dbfs)
        {
            Engine e; e.prepare (48000); e.setParams (p, -1);
            const int n = 48000;
            std::vector<float> x ((size_t) n), oL ((size_t) n), oR ((size_t) n);
            for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (std::pow (10.0, dbfs / 20) * std::sin (2 * 3.14159265358979 * 1000 * i / 48000));
            for (int o = 0; o + 128 <= n; o += 128) e.process (x.data() + o, x.data() + o, oL.data() + o, oR.data() + o, 128);
            double a = 0, b = 0;
            for (int i = n / 2; i < n; ++i) { a += x[(size_t) i] * x[(size_t) i]; b += oL[(size_t) i] * oL[(size_t) i]; }
            return 10 * std::log10 (b / a);
        };
        check (std::abs (at (down, -50)) < 0.05, "dynamic band below its threshold", at (down, -50), 0);
        check (std::abs (at (down, -6) + 8) < 0.05, "dynamic band well above: the full range", at (down, -6), -8);
        const auto up = presetParams ("b2=bell 1000 0 1 12 stereo dyn:6,-30,5,80");
        check (std::abs (at (up, -6) - 6) < 0.05, "dynamic band upward", at (up, -6), 6);
        const auto warm = presetParams ("b1=bell 1000 0 1; char=warm");
        check (std::abs (at (warm, -20)) < 0.1, "Warm leaves quiet sound alone", at (warm, -20), 0);
        check (at (warm, 0) < -1.5 && at (warm, 0) > -3.5, "Warm rounds a full-scale sine", at (warm, 0), -2.4);
    }
    // 5. Every setting stable, at every rate.
    {
        // Our own random numbers, the same on every platform.
        uint32_t seed = 7;
        auto u = [&seed] (auto&) { seed = seed * 1664525u + 1013904223u; return seed / 4294967296.0; };
        int rng = 0;
        int bad = 0, total = 0;
        Section s[maxSections];
        for (int i = 0; i < 1000000; ++i)
        {
            const auto shape = (Shape) (i % numShapes);
            static const double rates[] = { 44100, 48000, 88200, 96000, 192000, 22050 };
            const auto fs = rates[i % 6];
            const auto f = freqMin * std::pow (freqMax / freqMin, u (rng)), g = (u (rng) * 2 - 1) * gainMax;
            const auto q = qMin * std::pow (qMax / qMin, u (rng));
            const auto slope = slopes[(int) (u (rng) * numSlopes) % numSlopes];
            const auto n = designBand (shape, f, g, q, slope, fs, s);
            for (int k = 0; k < n; ++k)
            {
                ++total;
                const auto a1 = s[k].a1, a2 = s[k].a2;
                const auto disc = a1 * a1 - 4 * a2;
                const auto r = disc < 0 ? std::sqrt (a2) : std::max (std::abs ((-a1 + std::sqrt (disc)) / 2), std::abs ((-a1 - std::sqrt (disc)) / 2));
                if (! (r < 1) || ! std::isfinite (s[k].b0 + s[k].b1 + s[k].b2))
                {
                    if (++bad <= 5) std::printf ("     unstable: %s %.6g Hz %.4g dB Q %.5g slope %d at %g, section %d, pole radius %.12g\n", shapeKeys[(int) shape], f, g, q, slope, fs, k, r);
                }
            }
        }
        check (bad == 0, "random settings: unstable sections", bad, 0);
        std::printf ("     (%d sections)\n", total);
    }
    // 6. Moving things doesn't click: the biggest step stays below a clean sine's.
    {
        Engine e;
        e.prepare (48000);
        auto p = presetParams ("b1=bell 200 18 4");
        e.setParams (p, -1);
        const int n = 96000;
        std::vector<float> x ((size_t) n), oL ((size_t) n), oR ((size_t) n);
        for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (0.25 * std::sin (2 * 3.14159265358979 * 220 * i / 48000));
        for (int o = 0; o + 128 <= n; o += 128)
        {
            if (o == 128 * 100) { p.bands[0].freq = 8000; e.setParams (p, -1); }
            if (o == 128 * 200) { p.bands[0].shape = Shape::lowcut; p.bands[0].slope = 96; e.setParams (p, -1); }
            if (o == 128 * 300) { p.bands[0].on = false; e.setParams (p, -1); }
            if (o == 128 * 400) { p.bands[0].on = true; p.bands[0].place = Place::side; e.setParams (p, -1); }
            e.process (x.data() + o, x.data() + o, oL.data() + o, oR.data() + o, 128);
        }
        double maxStep = 0;
        for (int i = 1; i < n; ++i) maxStep = std::max (maxStep, (double) std::abs (oL[(size_t) i] - oL[(size_t) i - 1]));
        const auto clean = 2 * 3.14159265358979 * 220 / 48000 * 0.25 * std::pow (10, 18 / 20.0);
        check (maxStep < clean, "largest step while changing", maxStep, clean);
    }
    // 7. Presets, the demo, the analyzer.
    {
        int bandsTotal = 0;
        for (const auto& fp : factoryPresets())
            for (const auto& b : presetParams (fp.spec).bands) bandsTotal += b.used ? 1 : 0;
        check (bandsTotal >= 40, "factory presets parse (bands in all)", bandsTotal, 40);
        std::vector<float> L, R;
        makeDemo (48000, L, R);
        float peak = 0;
        for (size_t i = 0; i < L.size(); ++i) peak = std::max ({ peak, std::abs (L[i]), std::abs (R[i]) });
        check (std::abs (peak - 0.7f) < 1e-4 && L.size() == 205714, "demo loop: 4.29 s, peak 0.7", peak, 0.7);
        Analyzer a;
        a.setSize (8192);
        std::vector<float> sine (16384);
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = (float) std::sin (2 * 3.14159265358979 * 1000 * i / 48000.0);
        a.push (sine.data(), (int) sine.size());
        const auto& db = a.compute (0);
        float top = -200;
        for (auto v : db) top = std::max (top, v);
        check (std::abs (top - 20 * std::log10 (0.21)) < 0.5, "analyzer: full-scale sine (Blackman, 1/N)", top, 20 * std::log10 (0.21));
    }

    // 8. NATURAL and LINEAR PHASE, BRICKWALL, SPECTRAL dynamics, SIDECHAIN (as the web version is checked).
    {
        auto band = [] (Params& p, int i, Shape shape, double f, double g, double q, int slope = 12, Place place = Place::stereo)
        {
            auto& b = p.bands[(size_t) i];
            b.used = true; b.shape = shape; b.freq = f; b.gain = g; b.q = q; b.slope = slope; b.place = place;
            return &b;
        };
        for (auto mode : { Phase::natural, Phase::linear })
        {
            Params p;
            p.phase = mode;
            band (p, 0, Shape::bell, 1000, 9, 2); band (p, 1, Shape::lowcut, 80, 0, 0.71, 24); band (p, 2, Shape::highshelf, 15000, -6, 0.71);
            double worst = 0;
            for (double f : { 100.0, 300.0, 800.0, 1000.0, 1300.0, 4000.0, 12000.0, 18000.0 })
            {
                const auto x = sine (f, 0.25, 0.8);
                worst = std::max (worst, std::abs (levelDb (runEngine (p, x, x).L, x) - analogDb (p, f)));
            }
            check (worst < 0.35, mode == Phase::natural ? "Natural Phase follows the analog curve" : "Linear Phase follows the analog curve", worst, 0);
        }
        {
            Params p;
            p.phase = Phase::linear;
            band (p, 0, Shape::bell, 300, 12, 4);
            std::vector<float> x (48000, 0.0f);
            x[20000] = 1;   // after the fade-in
            const auto r = runEngine (p, x, x);
            size_t at = 0;
            for (size_t i = 0; i < r.L.size(); ++i) if (std::abs (r.L[i]) > std::abs (r.L[at])) at = i;
            double asym = 0, en = 0;
            for (size_t d = 1; d < 3000; ++d) { asym += std::abs (r.L[at + d] - r.L[at - d]); en += std::abs (r.L[at + d]); }
            check ((int) at - 20000 == r.latency && r.latency == kernelLatency (Phase::linear, PhaseRes::medium, 48000), "Linear Phase: the impulse comes out a latency later", (double) at - 20000, r.latency);
            check (asym / en < 1e-3, "Linear Phase: the impulse is symmetric", asym / en, 0);
        }
        for (auto mode : { Phase::linear, Phase::natural })
        {
            Params p;
            p.phase = mode; p.phaseRes = PhaseRes::high;
            band (p, 0, Shape::highcut, 1000, 0, 0.71, brickSlope);
            const auto x9 = sine (900, 0.25, 1.2), x15 = sine (1500, 0.25, 1.2);
            const auto pass = levelDb (runEngine (p, x9, x9).L, x9), stop = levelDb (runEngine (p, x15, x15).L, x15);
            check (std::abs (pass) < 0.5 && stop < -60, mode == Phase::linear ? "brickwall 1 kHz (Linear): 1.5 kHz gone" : "brickwall 1 kHz (Natural): 1.5 kHz gone", stop, -60);
        }
        {
            Params p;
            p.phase = Phase::linear;
            band (p, 0, Shape::bell, 1000, -12, 1, 12, Place::side);
            const auto x = sine (1000, 0.2, 1.0);
            std::vector<float> neg (x.size());
            for (size_t i = 0; i < x.size(); ++i) neg[i] = -x[i];
            const auto side = levelDb (runEngine (p, x, neg).L, x), mono = levelDb (runEngine (p, x, x).L, x);
            check (std::abs (side + 12) < 0.3 && std::abs (mono) < 0.1, "Linear Phase, side band: side -12, mono untouched", side, -12);
        }
        {
            Params p;
            auto* b = band (p, 0, Shape::bell, 2000, 0, 0.7);
            b->dyn = true; b->range = -10; b->thresh = -30; b->attack = 5; b->release = 50; b->spectral = true;
            const auto tone = sine (2500, 0.3, 1.5);
            const auto r = runEngine (p, tone, tone);
            check (levelDb (r.L, tone) < -7, "spectral: a loud tone in the band comes down", levelDb (r.L, tone), -9);
            check (r.latency == 2048, "spectral: latency 2048 at 48 kHz", r.latency, 2048);
            std::vector<float> noise (96000);
            uint32_t seed = 1;
            for (auto& v : noise) { seed = seed * 1664525u + 1013904223u; v = (float) ((seed / 4294967296.0 - 0.5) * 0.6); }
            const auto spectral = levelDb (runEngine (p, noise, noise).L, noise);
            b->spectral = false;
            const auto broad = levelDb (runEngine (p, noise, noise).L, noise);
            check (spectral > -0.5 && broad < spectral - 0.5, "spectral leaves noise alone (broadband doesn't)", spectral, 0);
        }
        {
            Params p;
            auto* b = band (p, 0, Shape::lowshelf, 200, 0, 0.71);
            b->dyn = true; b->range = -12; b->thresh = -30; b->attack = 2; b->release = 50; b->sc = true;
            const auto x = sine (60, 0.05, 1.5), key = sine (100, 0.5, 1.5);
            const std::vector<float> silence (x.size(), 0.0f);
            const auto ducked = levelDb (runEngine (p, x, x, &key).L, x), alone = levelDb (runEngine (p, x, x, &silence).L, x);
            check (ducked < -8 && std::abs (alone) < 0.3, "sidechain: the key ducks the band", ducked, -12);
            p.phase = Phase::linear;
            const auto lin = levelDb (runEngine (p, x, x, &key).L, x);
            check (lin < -8, "sidechain in Linear Phase", lin, -12);
        }
        {
            // a new kernel fades in: no step bigger than the sine's own
            Params p;
            p.phase = Phase::linear;
            band (p, 0, Shape::bell, 1000, 0, 1);
            Engine e;
            e.prepare (48000);
            e.setParams (p, -1);
            e.setKernel (buildKernel (p, 48000));
            const auto x = sine (1000, 0.3, 1.0);
            std::vector<float> oL (x.size()), oR (x.size());
            for (size_t o = 0; o + 128 <= x.size(); o += 128)
            {
                if (o == 24064) { p.bands[0].gain = 12; e.setParams (p, -1); e.setKernel (buildKernel (p, 48000)); }
                e.process (x.data() + o, x.data() + o, oL.data() + o, oR.data() + o, 128);
            }
            double maxStep = 0;
            for (size_t i = 20000; i < x.size(); ++i) maxStep = std::max (maxStep, (double) std::abs (oL[i] - oL[i - 1]));
            const auto sineStep = 0.3 * std::pow (10.0, 12 / 20.0) * 2 * 3.14159265358979 * 1000 / 48000;
            check (maxStep < sineStep * 1.05, "a new kernel fades in smoothly", maxStep, sineStep);
            int freed = 0;
            while (e.takeRetired() != nullptr) ++freed;
            check (freed >= 1, "the old kernel comes back to be freed", freed, 1);
        }
        {
            const auto lin = presetParams ("b1=lowcut 25 0 0.71 120; b2=bell 3000 0 0.5 12 stereo dyn:-6,-36,5,80 spec sc; gq=1; phase=linear; res=high");
            check (lin.phase == Phase::linear && lin.phaseRes == PhaseRes::high && lin.gainQ && lin.bands[0].slope == brickSlope
                   && lin.bands[1].spectral && lin.bands[1].sc, "presets: phase, res, gq, brickwall, spec, sc", 1, 1);
        }
    }

    std::printf (failures == 0 ? "\nAll Egret EQ engine tests passed.\n" : "\n%d test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
