#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Egret EQ's sound, shared by the plugin and its self-test: the filter design,
// the engine, AUTO GAIN, the factory presets, the standalone app's demo loop
// and pink noise, and the analyzer's maths. A line-by-line port of the web
// version's scripts (dsp-shared, dsp-kernel and dsp-engine in index.html).
namespace eq
{
    constexpr int maxBands = 48, maxSections = 8;
    constexpr int bandsLight = 24;   // past this the editor warns about the extra load
    constexpr double freqMin = 10, freqMax = 30000, gainMax = 30, qMin = 0.025, qMax = 40;
    constexpr int brickSlope = 120;   // BRICKWALL: a cut's slope, a wall in Natural and Linear Phase

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
    bool isCut (Shape);
    bool isBrick (Shape, int slope);

    struct Band
    {
        bool used = false, on = true;
        Shape shape = Shape::bell;
        double freq = 1000, gain = 0, q = 1;
        int slope = 12;   // brickSlope for a BRICKWALL cut
        Place place = Place::stereo;
        // DYNAMIC: the gain moves by up to RANGE when the band's part of the input passes THRESHOLD.
        bool dyn = false;
        double range = -6, thresh = -24, attack = 10, release = 120;
        // SC: the detector listens to the sidechain; SPECTRAL: frequency by frequency.
        bool sc = false, spectral = false;
        bool operator== (const Band& o) const;
        bool operator!= (const Band& o) const { return ! (*this == o); }
    };

    enum class Character { clean, subtle, warm };
    constexpr int numCharacters = 3;
    extern const char* const characterKeys[numCharacters];    // "clean", "subtle", "warm"
    extern const char* const characterLabels[numCharacters];  // "Clean", "Subtle", "Warm"

    // PHASE: Zero Latency (analog-matched filters), Natural Phase (the analog
    // curves, magnitude and phase, a little latency), Linear Phase (no phase
    // change at all; RES sets the filter's length, so detail down low, and latency).
    enum class Phase { zero, natural, linear };
    enum class PhaseRes { low, medium, high, max };
    constexpr int numPhases = 3, numPhaseRes = 4;
    extern const char* const phaseKeys[numPhases];       // "zero", "natural", "linear"
    extern const char* const phaseLabels[numPhases];     // "Zero Latency", ...
    extern const char* const phaseResKeys[numPhaseRes];  // "low", "medium", "high", "max"
    extern const char* const phaseResLabels[numPhaseRes];

    struct Params
    {
        std::array<Band, maxBands> bands {};
        double outGain = 0, outPan = 0, gainScale = 100;
        bool autoGain = false, power = true;
        Character character = Character::clean;
        bool gainQ = false;   // GAIN-Q: bells narrow as they boost or cut more, like an analog desk
        Phase phase = Phase::zero;
        PhaseRes phaseRes = PhaseRes::medium;
        bool operator== (const Params& o) const;
        bool isDynamic (int band) const;   // dynamic, and a shape with gain
        bool operator!= (const Params& o) const { return ! (*this == o); }
        double effectiveGain (int band) const;   // the band's gain after GAIN SCALE (0 for shapes without gain)
    };

    // A biquad, a0 = 1.
    struct Section { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    // A band's biquads (up to maxSections). gain: after GAIN SCALE.
    int designBand (Shape, double freq, double gain, double q, int slope, double fs, Section* secs, bool gainQ = false);
    // What SOLO plays: the part of the spectrum the band works on.
    void designSolo (Shape, double freq, double q, double fs, Section& out);
    double sectionMag (const Section&, double w);
    double autoGainDb (const Params&, double fs);

    // The analog curves (Natural and Linear Phase, their display, the spectral
    // dynamics): the same prototypes as designBand as analog filters, nothing
    // squeezed towards Nyquist. Multiplies the band's H(j 2 pi f) into re/im at
    // freqs[0..n). brickApart: leave a brickwall out and return true.
    struct AnalogBand { Shape shape; double freq, gain, q; int slope; bool gainQ; };
    bool analogBand (const AnalogBand&, const double* freqs, int n, double* re, double* im, bool brickApart = false);
    double brickDb (Shape, double bandFreq, double f);   // a brickwall's level at f

    // An FFT (radix 2, in place, double); the inverse is scaled by 1/n. fft
    // makes its table each time; fftWith uses one made ahead (no allocation).
    void fft (double* re, double* im, int n, bool inverse);
    struct FftTable
    {
        int n = 0;
        std::vector<double> c, s;
        void make (int size);
    };
    void fftWith (const FftTable&, double* re, double* im, bool inverse);

    //==============================================================================
    // NATURAL and LINEAR PHASE: the whole EQ as one long filter, cut in blocks of
    // kernelBlock samples and transformed, four ways (left to left, right to left,
    // left to right, right to right) so mid/side and left/right bands work as one.
    constexpr int kernelBlock = 256;
    struct Kernel
    {
        Phase mode = Phase::zero;
        PhaseRes res = PhaseRes::medium;
        double fs = 0;
        int parts = 0, latency = 0;
        // parts * (kernelBlock + 1) complex values (re, im); lr and rl empty
        // without mid/side bands, rr empty when it's the same as ll
        std::vector<float> ll, lr, rl, rr;
    };
    // The kernel for these settings (Natural or Linear Phase), with the bands'
    // gains as set (a dynamic band's change happens elsewhere). Slow: off the audio thread.
    std::unique_ptr<Kernel> buildKernel (const Params&, double fs);
    // What a kernel for these settings would take, and what the whole EQ would
    // (the spectral stage adds its own), in samples.
    int kernelLatency (Phase, PhaseRes, double fs);
    int expectedLatency (const Params&, double fs);
    // Do the kernels for these two settings differ?
    bool kernelChanged (const Params&, const Params&);

    //==============================================================================
    class Engine
    {
    public:
        Engine();
        ~Engine();
        // Allocates what the engine needs at this rate (not on the audio thread).
        void prepare (double sampleRate);
        // scOn: a sidechain is connected (bands with SC listen to it).
        void setParams (const Params&, int soloBand, bool scOn = false);
        // Stereo in, stereo out (they may alias); scL/scR: the sidechain, or nullptr.
        void process (const float* inL, const float* inR, float* outL, float* outR, int n,
                      const float* scL = nullptr, const float* scR = nullptr);
        // The host's bypass: the dry sound, delayed as much as the EQ delays it.
        void processBypassed (float* L, float* R, int n);
        double getAutoGainDb() const { return autoDb; }
        // Peaks since the last call: in L, in R, out L, out R.
        std::array<float, 4> takeMeters();
        // Each band's dynamic gain change now (dB), for the display.
        float getDynamicChange (int band) const { return dynShown[(size_t) band].load (std::memory_order_relaxed); }
        // The delay the EQ adds now (samples): Natural/Linear Phase, spectral dynamics.
        int getLatency() const { return latencyNow.load (std::memory_order_relaxed); }

        // A kernel from buildKernel. It's used when it's for the mode playing
        // (or kept for the mode coming); the engine gives the ones it's done
        // with back through takeRetired, so they're freed off the audio thread.
        void setKernel (std::unique_ptr<Kernel>);
        std::unique_ptr<Kernel> takeRetired();
        // Waiting for a kernel for the mode wanted (and holding none that fits): the builder should make one.
        bool needsKernel() const { return starving.load (std::memory_order_relaxed); }

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
            // dynamics: the detector (the band's part of the input) and its level
            bool dyn = false, sc = false, spectral = false, viaStft = false;
            double range = -6, thresh = -24, att = 10, rel = 120, env = 0, dynDb = 0, staticG = 0;
            Section det;
            std::array<double, 4> detZ {};
            Shape detShape = Shape::bell;
            double detF = -1, detQ = -1;
            // the spectral stage: the band's shape over the bins, and each bin's level
            std::vector<double> specW, specEnv;
            Shape wShape = Shape::bell;
            double wF = -1, wQ = -1, wRange = 0;
            int wSlope = -1;
            bool wGainQ = false;
        };
        void updateBand (BandState&);
        void runBand (BandState&, double* L, double* R, int len);
        void detect (BandState&, const double* dl, const double* dr, const double* sl, const double* sr, int len, int index);
        void runCharacter (double* L, double* R, int len);
        // Settled stereo bands in a row, all their sections sample by sample (see process).
        void runSettled (int count, double* L, double* R, int len);
        double updateMode (int len);
        bool kernelFits (const Kernel*, Phase, PhaseRes) const;
        void retire (std::unique_ptr<Kernel>);

        // NATURAL and LINEAR PHASE: uniformly partitioned convolution (overlap-save).
        struct Fir
        {
            std::vector<double> inL, inR, outL, outR, out2L, out2R, re, im, yl, yr;
            std::vector<std::vector<double>> fdlL, fdlR;   // the input's spectra, newest at head
            int pos = 0, head = 0;
            std::unique_ptr<Kernel> kern, next;
        };
        void firReset();
        void firSetKernel (std::unique_ptr<Kernel>);
        void firRender (const Kernel&, double* oL, double* oR);
        void firBlock();
        void firProcess (double* L, double* R, int len);

        // SPECTRAL dynamics (and every dynamic band in Natural/Linear Phase): a short-time FFT.
        struct Spec
        {
            int N = 2048, H = 512, c = 0;
            double cal = 0;
            std::vector<double> win, inL, inR, dI, dS, accL, accR, outL, outR, re, im, dre, dim, gdb, freqs, wRe, wIm;
        };
        void specReset();
        const std::vector<double>& specWeights (BandState&);
        void specProcess (double* L, double* R, const double* dI, const double* dS, int len);
        void specFrame();

        // A delay for two signals (the dry sound for BYPASS, the detectors).
        struct Delay
        {
            std::vector<double> a, b;
            int mask = 0, w = 0;
            void reset();
            void process (double* x, double* y, int len, int d);
        };

        double fs = 48000, fadeStep = 0, glide = 0;
        std::array<BandState, maxBands> bands {};
        int solo = -1, tSolo = -1;
        double soloMix = 0;
        Section soloSec;
        std::array<double, 4> soloZ {};
        double gain = 1, tGain = 1, pan = 0, tPan = 0, power = 1, tPower = 1, autoDb = 0;
        bool started = false, gainQ = false;
        Character character = Character::clean, tCharacter = Character::clean;
        double charMix = 0;
        std::array<double, 2> dcX {}, dcY {}, lpZ {};
        std::array<std::atomic<float>, maxBands> dynShown {};
        std::array<std::atomic<float>, 4> meters {};
        std::array<double, 32> bufL {}, bufR {}, dryL {}, dryR {}, detI {}, detS {};
        // the sections runSettled works through, each with its state, and where each band ends
        std::array<const Section*, maxBands * maxSections> runSecs {};
        std::array<double*, maxBands * maxSections> runZ {};
        std::array<bool, maxBands * maxSections> runEnd {};

        // PHASE: the mode playing and the one wanted; a change (or the spectral
        // stage coming or going) fades out, switches, and fades back in.
        Phase mode = Phase::zero, tMode = Phase::zero;
        PhaseRes res = PhaseRes::medium, tRes = PhaseRes::medium;
        bool specOn = false, tSpec = false, scOn = false;
        double gate = 1;
        int latency = 0, firLatency = 0;
        std::atomic<int> latencyNow { 0 };
        std::atomic<bool> starving { false };
        Fir fir;
        Spec spec;
        FftTable firTable, specTable;
        Delay dry, detDelay;
        std::unique_ptr<Kernel> waiting;   // a kernel for the mode coming
        std::array<std::unique_ptr<Kernel>, 32> retired;
        int maxParts = 0;
    };

    //==============================================================================
    struct FactoryPreset { const char* name; const char* spec; };
    const std::vector<FactoryPreset>& factoryPresets();
    // "key=value; ..." with bands as "bN=shape freq gain q [slope] [placement] [off]
    // [dyn:range,threshold,attack,release] [sc] [spec]" (slope 120: brickwall),
    // char=clean|subtle|warm, gq=1, phase=zero|natural|linear, res=low|medium|high|max.
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
