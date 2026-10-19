#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

// Egret EQ's sound, shared by the plugin and its self-test: the filter design,
// the engine, AUTO GAIN, the factory presets, the standalone app's demo loop
// and pink noise, and the analyzer's maths. A line-by-line port of the web
// version's scripts (dsp-shared and dsp-engine in index.html).
namespace eq
{
    constexpr int maxBands = 24, maxSections = 8;
    constexpr double freqMin = 10, freqMax = 30000, gainMax = 30, qMin = 0.025, qMax = 40;

    enum class Shape { bell, lowshelf, lowcut, highshelf, highcut, notch, bandpass, tiltshelf, flattilt, allpass };
    enum class Place { stereo, left, right, mid, side };
    constexpr int numShapes = 10, numSlopes = 9, numPlaces = 5;
    constexpr int slopes[numSlopes] = { 6, 12, 18, 24, 30, 36, 48, 72, 96 };
    extern const char* const shapeKeys[numShapes];    // "bell", "lowshelf", ... as in the web presets
    extern const char* const shapeLabels[numShapes];  // "BELL", "LO SHELF", ...
    extern const char* const placeKeys[numPlaces];
    extern const char* const placeLabels[numPlaces];

    bool hasGain (Shape);
    bool hasQ (Shape);
    bool hasSlope (Shape);
    bool canSolo (Shape);

    struct Band
    {
        bool used = false, on = true;
        Shape shape = Shape::bell;
        double freq = 1000, gain = 0, q = 1;
        int slope = 12;
        Place place = Place::stereo;
        bool operator== (const Band& o) const;
    };

    struct Params
    {
        std::array<Band, maxBands> bands {};
        double outGain = 0, outPan = 0, gainScale = 100;
        bool autoGain = false, power = true;
        bool operator== (const Params& o) const;
        bool operator!= (const Params& o) const { return ! (*this == o); }
        double effectiveGain (int band) const;   // the band's gain after GAIN SCALE (0 for shapes without gain)
    };

    // A biquad, a0 = 1.
    struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    // A band's biquads (up to maxSections). gain: after GAIN SCALE.
    int designBand (Shape, double freq, double gain, double q, int slope, double fs, Section* secs);
    // What SOLO plays: the part of the spectrum the band works on.
    void designSolo (Shape, double freq, double q, double fs, Section& out);
    double sectionMag (const Section&, double w);
    double autoGainDb (const Params&, double fs);

    //==============================================================================
    class Engine
    {
    public:
        void prepare (double sampleRate);
        void setParams (const Params&, int soloBand);
        // Stereo in, stereo out (they may alias).
        void process (const float* inL, const float* inR, float* outL, float* outR, int n);
        double getAutoGainDb() const { return autoDb; }
        // Peaks since the last call: in L, in R, out L, out R.
        std::array<float, 4> takeMeters();

    private:
        struct BandState
        {
            std::array<Section, maxSections> secs {};
            std::array<double, maxSections * 4> z {};
            int n = 0;
            Shape shape = Shape::bell, tShape = Shape::bell;
            int slope = 12, tSlope = 12;
            Place place = Place::stereo, tPlace = Place::stereo;
            bool tActive = false, dirty = true;
            double f = 1000, g = 0, q = 1, tf = 1000, tg = 0, tq = 1, mix = 0, mixTarget = 0;
        };
        void updateBand (BandState&);
        void runBand (BandState&, double* L, double* R, int len);

        double fs = 48000, fadeStep = 0, glide = 0;
        std::array<BandState, maxBands> bands {};
        int solo = -1, tSolo = -1;
        double soloMix = 0;
        Section soloSec;
        std::array<double, 4> soloZ {};
        double gain = 1, tGain = 1, pan = 0, tPan = 0, power = 1, tPower = 1, autoDb = 0;
        bool started = false;
        std::array<std::atomic<float>, 4> meters {};
        std::array<double, 32> bufL {}, bufR {};
    };

    //==============================================================================
    struct FactoryPreset { const char* name; const char* spec; };
    const std::vector<FactoryPreset>& factoryPresets();
    // "key=value; ..." with bands as "bN=shape freq gain q [slope] [placement] [off]".
    Params presetParams (const std::string& spec);

    // The standalone app's sources, as the web page makes them.
    void makeDemo (double fs, std::vector<float>& left, std::vector<float>& right);
    void makeNoise (double fs, std::vector<float>& left, std::vector<float>& right);

    //==============================================================================
    // Web Audio's AnalyserNode maths: the newest fftSize samples, a Blackman
    // window, |X[k]| / N, smoothed across calls, in dB.
    class Analyzer
    {
    public:
        Analyzer();
        void push (const float* x, int n);
        void setSize (int fftSize);
        int getSize() const { return size; }
        // One frame (the web page takes one per screen refresh); smoothing 0..1.
        const std::vector<float>& compute (double smoothing);
        const std::vector<float>& getDb() const { return db; }

    private:
        static constexpr int maxSize = 16384;
        std::vector<float> ring, re, im, smoothed, db;
        int write = 0, size = 8192;
        void fft (std::vector<float>& real, std::vector<float>& imag, int n);
    };
}
