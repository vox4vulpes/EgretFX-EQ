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

    std::printf (failures == 0 ? "\nAll Egret EQ engine tests passed.\n" : "\n%d test(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
