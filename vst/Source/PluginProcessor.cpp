#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const juce::Identifier presetProp ("preset"), abProp ("abSlot"), abOtherProp ("abOther");
    constexpr const char* presetTag = "EgretEqPreset";

    juce::NormalisableRange<float> logRange (float mn, float mx)
    {
        return { mn, mx,
                 [] (float start, float end, float t) { return start * std::pow (end / start, t); },
                 [] (float start, float end, float v) { return std::log (v / start) / std::log (end / start); },
                 nullptr };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout EgretEqProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "outGain", 1 }, "Output Gain", juce::NormalisableRange<float> (-36.0f, 36.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "outPan", 1 }, "Output Pan", juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gainScale", 1 }, "Gain Scale", juce::NormalisableRange<float> (0.0f, 200.0f), 100.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "autoGain", 1 }, "Auto Gain", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "power", 1 }, "Power", true));
    // Added in 1.1 (new IDs only, so 1.0 projects still load).
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "character", 2 }, "Character", juce::StringArray { "Clean", "Subtle", "Warm" }, 0));
    // Added in 1.2.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "gainQ", 3 }, "Gain-Q", false));
    juce::StringArray phases, resolutions;
    for (auto* l : eq::phaseLabels) phases.add (l);
    for (auto* l : eq::phaseResLabels) resolutions.add (l);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "phase", 3 }, "Processing Mode", phases, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "phaseRes", 3 }, "Linear Phase Resolution", resolutions, 1));

    juce::StringArray shapes, slopes, places;
    for (auto* s : eq::shapeLabels) shapes.add (s);
    for (auto s : eq::slopes) slopes.add (juce::String (s) + " dB/oct");
    for (auto* s : eq::placeLabels) places.add (s);
    for (int i = 0; i < eq::maxBands; ++i)
    {
        const auto name = "Band " + juce::String (i + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (i + 1), "Band " + juce::String (i + 1), "|");
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (i, "Used"), 1 }, name + "Used", false));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (i, "On"), 1 }, name + "On", true));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bandId (i, "Shape"), 1 }, name + "Shape", shapes, 0));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandId (i, "Freq"), 1 }, name + "Freq", logRange ((float) eq::freqMin, (float) eq::freqMax), 1000.0f));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandId (i, "Gain"), 1 }, name + "Gain", juce::NormalisableRange<float> (-30.0f, 30.0f), 0.0f));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandId (i, "Q"), 1 }, name + "Q", logRange ((float) eq::qMin, (float) eq::qMax), 1.0f));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bandId (i, "Slope"), 1 }, name + "Slope", slopes, 1));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { bandId (i, "Place"), 1 }, name + "Stereo", places, 0));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (i, "Dyn"), 2 }, name + "Dynamic", false));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandId (i, "Range"), 2 }, name + "Range", juce::NormalisableRange<float> (-30.0f, 30.0f), -6.0f));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandId (i, "Thresh"), 2 }, name + "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f), -24.0f));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandId (i, "Attack"), 2 }, name + "Attack", logRange (0.5f, 500.0f), 10.0f));
        group->addChild (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandId (i, "Release"), 2 }, name + "Release", logRange (10.0f, 5000.0f), 120.0f));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (i, "SC"), 3 }, name + "Sidechain", false));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (i, "Spectral"), 3 }, name + "Spectral", false));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { bandId (i, "Brick"), 3 }, name + "Brickwall", false));
        layout.add (std::move (group));
    }
    return layout;
}

EgretEqProcessor::EgretEqProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undo, "EgretEq", createLayout())
{
    for (int i = 0; i < eq::maxBands; ++i)
    {
        auto get = [this, i] (const char* n) { return apvts.getRawParameterValue (bandId (i, n)); };
        refs[(size_t) i] = { get ("Used"), get ("On"), get ("Shape"), get ("Freq"), get ("Gain"), get ("Q"), get ("Slope"), get ("Place"),
                             get ("Dyn"), get ("Range"), get ("Thresh"), get ("Attack"), get ("Release"), get ("SC"), get ("Spectral"), get ("Brick") };
    }
    outGain = apvts.getRawParameterValue ("outGain");
    outPan = apvts.getRawParameterValue ("outPan");
    gainScale = apvts.getRawParameterValue ("gainScale");
    autoGain = apvts.getRawParameterValue ("autoGain");
    power = apvts.getRawParameterValue ("power");
    character = apvts.getRawParameterValue ("character");
    gainQ = apvts.getRawParameterValue ("gainQ");
    phase = apvts.getRawParameterValue ("phase");
    phaseRes = apvts.getRawParameterValue ("phaseRes");
    formats.registerBasicFormats();
    rememberPreset ("Flat");
    updates->checkOnLaunch();
    builder.startThread (juce::Thread::Priority::low);
    startTimerHz (10);
}

EgretEqProcessor::~EgretEqProcessor()
{
    stopTimer();
    builder.stopThread (4000);
    delete freshKernel.exchange (nullptr);
    for (auto& slot : spentKernels) delete slot.exchange (nullptr);
}

// The latency the host is told: what the mode wanted takes, ready before the
// engine has switched (the host lines the tracks up by it).
void EgretEqProcessor::timerCallback()
{
    const auto want = eq::expectedLatency (readParams(), sampleRate);
    if (want != getLatencySamples()) setLatencySamples (want);
}

void EgretEqProcessor::Builder::sweep()
{
    for (auto& slot : owner.spentKernels) delete slot.exchange (nullptr);
}

void EgretEqProcessor::Builder::run()
{
    int starved = 0;
    while (! threadShouldExit())
    {
        sweep();
        const auto p = owner.readParams();
        const auto rate = fs.load();
        if (p.phase == eq::Phase::zero) { hasBuilt = false; starved = 0; }
        else
        {
            // the engine still waiting a while after a kernel was handed over (it went
            // back to Zero Latency and forward again, say): make it another
            starved = owner.engine.needsKernel() && owner.freshKernel.load() == nullptr ? starved + 1 : 0;
            if (! hasBuilt || rate != builtFs || eq::kernelChanged (p, built) || starved > 6)
            {
                auto k = eq::buildKernel (p, rate);
                built = p; builtFs = rate; hasBuilt = true; starved = 0;
                delete owner.freshKernel.exchange (k.release());
            }
        }
        wait (15);
    }
    sweep();
}

// Audio thread: take the newest kernel; pass the spent ones to the builder to free.
void EgretEqProcessor::handOverKernels()
{
    if (auto* k = freshKernel.exchange (nullptr)) engine.setKernel (std::unique_ptr<eq::Kernel> (k));
    while (auto spent = engine.takeRetired())
    {
        auto* raw = spent.release();
        for (auto& slot : spentKernels)
        {
            eq::Kernel* empty = nullptr;
            if (slot.compare_exchange_strong (empty, raw)) { raw = nullptr; break; }
        }
        delete raw;   // every slot full (the builder has stopped): a last resort
    }
}

bool EgretEqProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    // the sidechain: off, mono or stereo
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono())
            return false;
    }
    return true;
}

bool EgretEqProcessor::isStandalone()
{
    return juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
}

eq::Params EgretEqProcessor::readParams() const
{
    eq::Params p;
    for (int i = 0; i < eq::maxBands; ++i)
    {
        const auto& r = refs[(size_t) i];
        auto& b = p.bands[(size_t) i];
        b.used = r.used->load() > 0.5f;
        b.on = r.on->load() > 0.5f;
        b.shape = (eq::Shape) juce::jlimit (0, eq::numShapes - 1, juce::roundToInt (r.shape->load()));
        b.freq = r.freq->load();
        b.gain = r.gain->load();
        b.q = r.q->load();
        b.slope = r.brick->load() > 0.5f && eq::isCut (b.shape) ? eq::brickSlope
                                                                 : eq::slopes[juce::jlimit (0, eq::numSlopes - 1, juce::roundToInt (r.slope->load()))];
        b.place = (eq::Place) juce::jlimit (0, eq::numPlaces - 1, juce::roundToInt (r.place->load()));
        b.dyn = r.dyn->load() > 0.5f;
        b.range = r.range->load(); b.thresh = r.thresh->load(); b.attack = r.attack->load(); b.release = r.release->load();
        b.sc = r.sc->load() > 0.5f; b.spectral = r.spectral->load() > 0.5f;
    }
    p.outGain = outGain->load();
    p.outPan = outPan->load();
    p.gainScale = gainScale->load();
    p.autoGain = autoGain->load() > 0.5f;
    p.power = power->load() > 0.5f;
    p.character = (eq::Character) juce::jlimit (0, eq::numCharacters - 1, juce::roundToInt (character->load()));
    p.gainQ = gainQ->load() > 0.5f;
    p.phase = (eq::Phase) juce::jlimit (0, eq::numPhases - 1, juce::roundToInt (phase->load()));
    p.phaseRes = (eq::PhaseRes) juce::jlimit (0, eq::numPhaseRes - 1, juce::roundToInt (phaseRes->load()));
    return p;
}

void EgretEqProcessor::writeParams (const eq::Params& p)
{
    auto set = [this] (const juce::String& id, float plainValue)
    {
        auto* param = apvts.getParameter (id);
        const auto v = param->convertTo0to1 (plainValue);
        if (std::abs (param->getValue() - v) > 1e-7f)
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (v);
            param->endChangeGesture();
        }
    };
    for (int i = 0; i < eq::maxBands; ++i)
    {
        const auto& b = p.bands[(size_t) i];
        set (bandId (i, "Used"), b.used ? 1.0f : 0.0f);
        set (bandId (i, "On"), b.on ? 1.0f : 0.0f);
        set (bandId (i, "Shape"), (float) b.shape);
        set (bandId (i, "Freq"), (float) b.freq);
        set (bandId (i, "Gain"), (float) b.gain);
        set (bandId (i, "Q"), (float) b.q);
        int slopeIndex = 1;
        for (int k = 0; k < eq::numSlopes; ++k) if (eq::slopes[k] == b.slope) slopeIndex = k;
        if (b.slope >= eq::brickSlope) slopeIndex = eq::numSlopes - 1;
        set (bandId (i, "Slope"), (float) slopeIndex);
        set (bandId (i, "Brick"), b.slope >= eq::brickSlope ? 1.0f : 0.0f);
        set (bandId (i, "SC"), b.sc ? 1.0f : 0.0f); set (bandId (i, "Spectral"), b.spectral ? 1.0f : 0.0f);
        set (bandId (i, "Place"), (float) b.place);
        set (bandId (i, "Dyn"), b.dyn ? 1.0f : 0.0f);
        set (bandId (i, "Range"), (float) b.range); set (bandId (i, "Thresh"), (float) b.thresh);
        set (bandId (i, "Attack"), (float) b.attack); set (bandId (i, "Release"), (float) b.release);
    }
    set ("outGain", (float) p.outGain);
    set ("outPan", (float) p.outPan);
    set ("gainScale", (float) p.gainScale);
    set ("autoGain", p.autoGain ? 1.0f : 0.0f);
    set ("character", (float) p.character);
    set ("gainQ", p.gainQ ? 1.0f : 0.0f);
    set ("phase", (float) p.phase);
    set ("phaseRes", (float) p.phaseRes);
}

//==============================================================================
void EgretEqProcessor::prepareToPlay (double newSampleRate, int samplesPerBlock)
{
    sampleRate = newSampleRate;
    engine.prepare (sampleRate);
    builder.fs = sampleRate;
    lastSolo = -2;
    scratch.setSize (2, juce::jmax (1, samplesPerBlock));
    scScratch.setSize (2, juce::jmax (1, samplesPerBlock));
    setLatencySamples (eq::expectedLatency (readParams(), sampleRate));
    if (isStandalone())
    {
        auto demo = std::make_shared<Clip>(), noise = std::make_shared<Clip>();
        eq::makeDemo (sampleRate, demo->left, demo->right);
        eq::makeNoise (sampleRate, noise->left, noise->right);
        std::atomic_store (&demoClip, std::shared_ptr<const Clip> (demo));
        std::atomic_store (&noiseClip, std::shared_ptr<const Clip> (noise));
    }
}

void EgretEqProcessor::Tap::push (const float* x, int n)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 > 0) std::copy (x, x + n1, data.begin() + s1);
    if (n2 > 0) std::copy (x + n1, x + n1 + n2, data.begin() + s2);
    fifo.finishedWrite (n1 + n2);
}

int EgretEqProcessor::Tap::pull (float* dest, int max)
{
    int s1, n1, s2, n2;
    fifo.prepareToRead (juce::jmin (max, fifo.getNumReady()), s1, n1, s2, n2);
    if (n1 > 0) std::copy (data.begin() + s1, data.begin() + s1 + n1, dest);
    if (n2 > 0) std::copy (data.begin() + s2, data.begin() + s2 + n2, dest + n1);
    fifo.finishedRead (n1 + n2);
    return n1 + n2;
}

void EgretEqProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, false, 0);
    const auto n = main.getNumSamples();
    const auto chans = main.getNumChannels();
    if (n == 0 || chans == 0) return;
    if (scratch.getNumSamples() < n || scScratch.getNumSamples() < n) return;   // the host broke its promise on block size

    auto* L = main.getWritePointer (0);
    auto* R = chans > 1 ? main.getWritePointer (1) : scratch.getWritePointer (1);
    if (chans == 1) std::copy (L, L + n, R);

    // The standalone app's own sources replace the input.
    if (isStandalone() && getSource() != Source::input)
    {
        auto clip = getSource() == Source::demo ? std::atomic_load (&demoClip)
                  : getSource() == Source::noise ? std::atomic_load (&noiseClip) : std::atomic_load (&fileClip);
        if (clip != playingClip) { playingClip = clip; clipPos = 0; }
        if (! playing || clip == nullptr || clip->left.empty())
        {
            std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
        }
        else
        {
            const auto len = clip->left.size();
            for (int i = 0; i < n; ++i)
            {
                if (clipPos >= len) clipPos = 0;
                L[i] = clip->left[clipPos]; R[i] = clip->right[clipPos];
                ++clipPos;
            }
        }
    }
    else if (isStandalone() && ! playing)
    {
        std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
    }

    // The sidechain: the standalone app's own, or the plugin's second input.
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (isStandalone() && getScSource() != ScSource::off)
    {
        const auto src = getScSource();
        auto clip = src == ScSource::demo ? std::atomic_load (&demoClip) : src == ScSource::noise ? std::atomic_load (&noiseClip) : std::atomic_load (&scFileClip);
        if (clip != scPlayingClip) { scPlayingClip = clip; scPos = 0; }
        auto* a = scScratch.getWritePointer (0);
        auto* b = scScratch.getWritePointer (1);
        if (! playing || clip == nullptr || clip->left.empty()) { std::fill (a, a + n, 0.0f); std::fill (b, b + n, 0.0f); }
        else
            for (int i = 0; i < n; ++i)
            {
                if (scPos >= clip->left.size()) scPos = 0;
                a[i] = clip->left[scPos]; b[i] = clip->right[scPos];
                ++scPos;
            }
        scL = a; scR = b;
    }
    else if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0) { scL = sc.getReadPointer (0); scR = sc.getReadPointer (sc.getNumChannels() > 1 ? 1 : 0); }
    }
    const auto scOn = scL != nullptr;
    sidechainOn = scOn;

    const auto p = readParams();
    const auto s = soloBand.load();
    if (p != lastParams || s != lastSolo || scOn != lastScOn)
    {
        engine.setParams (p, s, scOn);
        lastParams = p;
        lastSolo = s;
        lastScOn = scOn;
    }
    handOverKernels();

    auto* mono = scratch.getWritePointer (0);
    for (int i = 0; i < n; ++i) mono[i] = (L[i] + R[i]) * 0.5f;
    preTap.push (mono, n);
    engine.process (L, R, L, R, n, scL, scR);
    for (int i = 0; i < n; ++i) mono[i] = (L[i] + R[i]) * 0.5f;
    postTap.push (mono, n);
    if (scOn)
    {
        for (int i = 0; i < n; ++i) mono[i] = (scL[i] + scR[i]) * 0.5f;
        scTap.push (mono, n);
    }
}

// The host's bypass: the sound as it comes in, delayed by the latency the host was told.
void EgretEqProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto main = getBusBuffer (buffer, false, 0);
    const auto n = main.getNumSamples();
    const auto chans = main.getNumChannels();
    if (n == 0 || chans == 0 || scratch.getNumSamples() < n) return;
    auto* L = main.getWritePointer (0);
    auto* R = chans > 1 ? main.getWritePointer (1) : scratch.getWritePointer (1);
    if (chans == 1) std::copy (L, L + n, R);
    engine.processBypassed (L, R, n);
}

//==============================================================================
void EgretEqProcessor::setSource (Source s)
{
    source = (int) s;
}

bool EgretEqProcessor::loadScFile (const juce::File& f)
{
    if (! loadFile (f, true)) return false;
    scFileName = f.getFileName();
    setScSource (ScSource::file);
    return true;
}

bool EgretEqProcessor::loadFile (const juce::File& f, bool asSidechain)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > (juce::int64) reader->sampleRate * 60 * 20)
        return false;
    juce::AudioBuffer<float> raw (2, (int) reader->lengthInSamples);
    reader->read (&raw, 0, (int) reader->lengthInSamples, 0, true, true);
    if (reader->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, raw.getNumSamples());

    // To the engine's rate.
    auto clip = std::make_shared<Clip>();
    const auto ratio = reader->sampleRate / sampleRate;
    const auto outLen = (int) std::floor (raw.getNumSamples() / ratio);
    for (int c = 0; c < 2; ++c)
    {
        auto& dest = c == 0 ? clip->left : clip->right;
        dest.resize ((size_t) outLen);
        if (std::abs (ratio - 1.0) < 1e-9)
            std::copy (raw.getReadPointer (c), raw.getReadPointer (c) + outLen, dest.begin());
        else
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, raw.getReadPointer (c), dest.data(), outLen);
        }
    }
    if (asSidechain)
    {
        std::atomic_store (&scFileClip, std::shared_ptr<const Clip> (clip));
        return true;
    }
    std::atomic_store (&fileClip, std::shared_ptr<const Clip> (clip));
    fileName = f.getFileName();
    setSource (Source::file);
    playing = true;
    return true;
}

//==============================================================================
juce::String EgretEqProcessor::paramsToSpec (const eq::Params& p)
{
    juce::StringArray items;
    for (int i = 0; i < eq::maxBands; ++i)
    {
        const auto& b = p.bands[(size_t) i];
        if (! b.used) continue;
        items.add ("b" + juce::String (i + 1) + "=" + eq::shapeKeys[(int) b.shape] + " " + juce::String (b.freq, 6) + " " + juce::String (b.gain, 6)
                   + " " + juce::String (b.q, 6) + " " + juce::String (b.slope) + " " + eq::placeKeys[(int) b.place] + (b.on ? "" : " off")
                   + (b.dyn ? " dyn:" + juce::String (b.range, 4) + "," + juce::String (b.thresh, 4) + "," + juce::String (b.attack, 4) + "," + juce::String (b.release, 4) : juce::String())
                   + (b.sc ? " sc" : "") + (b.spectral ? " spec" : ""));
    }
    items.add ("scale=" + juce::String (p.gainScale, 6));
    items.add ("gain=" + juce::String (p.outGain, 6));
    items.add ("pan=" + juce::String (p.outPan, 6));
    items.add (juce::String ("auto=") + (p.autoGain ? "1" : "0"));
    items.add (juce::String ("char=") + eq::characterKeys[(int) p.character]);
    if (p.gainQ) items.add ("gq=1");
    items.add (juce::String ("phase=") + eq::phaseKeys[(int) p.phase]);
    items.add (juce::String ("res=") + eq::phaseResKeys[(int) p.phaseRes]);
    return items.joinIntoString ("; ");
}

void EgretEqProcessor::rememberPreset (const juce::String& name)
{
    presetName = name;
    presetSnapshot = readParams();
}

void EgretEqProcessor::loadFactoryPreset (int index)
{
    const auto& list = eq::factoryPresets();
    const auto& preset = list[(size_t) juce::jlimit (0, (int) list.size() - 1, index)];
    undo.beginNewTransaction();
    auto p = eq::presetParams (preset.spec);
    p.power = readParams().power;
    writeParams (p);
    soloBand = -1;
    rememberPreset (preset.name);
}

juce::File EgretEqProcessor::getUserPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Egret").getChildFile ("Egret EQ").getChildFile ("Presets");
}

namespace
{
    juce::File findPreset (const juce::String& name)
    {
        for (const auto& file : EgretEqProcessor::getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.xml"))
            if (auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName (presetTag)
                    && xml->getStringAttribute ("name").equalsIgnoreCase (name))
                return file;
        return {};
    }
}

juce::StringArray EgretEqProcessor::getUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& file : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.xml"))
        if (auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName (presetTag))
            names.addIfNotAlreadyThere (xml->getStringAttribute ("name"), true);
    names.sortNatural();
    return names;
}

bool EgretEqProcessor::saveUserPreset (const juce::String& name)
{
    juce::XmlElement xml (presetTag);
    xml.setAttribute ("name", name);
    xml.setAttribute ("spec", paramsToSpec (readParams()));
    const auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;
    auto file = findPreset (name);
    if (file == juce::File())
        file = folder.getNonexistentChildFile (juce::File::createLegalFileName (name), ".xml");
    if (! xml.writeTo (file))
        return false;
    rememberPreset (name);
    return true;
}

bool EgretEqProcessor::loadUserPreset (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (findPreset (name));
    if (xml == nullptr)
        return false;
    undo.beginNewTransaction();
    auto p = eq::presetParams (xml->getStringAttribute ("spec").toStdString());
    p.power = readParams().power;
    writeParams (p);
    soloBand = -1;
    rememberPreset (xml->getStringAttribute ("name"));
    return true;
}

bool EgretEqProcessor::deleteUserPreset (const juce::String& name)
{
    const auto file = findPreset (name);
    return file != juce::File() && file.deleteFile();
}

void EgretEqProcessor::switchAb (char slot)
{
    if (slot == abSlot) return;
    const auto current = paramsToSpec (readParams());
    undo.beginNewTransaction();
    if (abOther.isNotEmpty())
    {
        auto p = eq::presetParams (abOther.toStdString());
        p.power = readParams().power;
        writeParams (p);
    }
    abOther = current;
    abSlot = slot;
    soloBand = -1;
}

void EgretEqProcessor::copyAb()
{
    abOther = paramsToSpec (readParams());
}

//==============================================================================
void EgretEqProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty (presetProp, presetName, nullptr);
    state.setProperty (abProp, juce::String::charToString (abSlot), nullptr);
    state.setProperty (abOtherProp, abOther, nullptr);
    state.removeChild (state.getChildWithName (uiState.getType()), nullptr);
    state.appendChild (uiState.createCopy(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EgretEqProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (auto ui = tree.getChildWithName (uiState.getType()); ui.isValid())
    {
        uiState.copyPropertiesFrom (ui, nullptr);
        tree.removeChild (ui, nullptr);
    }
    const auto slot = tree.getProperty (abProp, "A").toString();
    abSlot = slot == "B" ? 'B' : 'A';
    abOther = tree.getProperty (abOtherProp, {}).toString();
    apvts.replaceState (tree);
    undo.clearUndoHistory();
    rememberPreset (tree.getProperty (presetProp, "Flat").toString());
}

juce::AudioProcessorEditor* EgretEqProcessor::createEditor()
{
    return new EgretEqEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EgretEqProcessor();
}
