#pragma once

#include <atomic>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "EqDsp.h"
#include "UpdateChecker.h"

// Egret EQ as a plugin: every band setting a parameter (48 bands), the web
// version's engine, factory presets and user presets as files, undo, A/B.
// In the standalone app it can also play the demo loop, a file or pink noise.
class EgretEqProcessor : public juce::AudioProcessor
{
public:
    EgretEqProcessor();
    ~EgretEqProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Egret EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;

    static juce::String bandId (int band, const char* name) { return "b" + juce::String (band + 1) + name; }
    eq::Params readParams() const;                 // the knobs as the engine's Params
    void writeParams (const eq::Params&);           // every parameter, as one undo step
    double getSampleRateNow() const { return sampleRate; }

    // SOLO: not a parameter, it's for listening.
    std::atomic<int> soloBand { -1 };
    std::array<float, 4> takeMeters() { return engine.takeMeters(); }
    float getDynamicChange (int band) const { return engine.getDynamicChange (band); }
    double getAutoGainDb() const { return eq::autoGainDb (readParams(), sampleRate); }

    // The analyzer: what goes in (pre) and what comes out (post), mono, for the editor.
    struct Tap
    {
        juce::AbstractFifo fifo { 1 << 15 };
        std::vector<float> data = std::vector<float> (1 << 15);
        void push (const float* x, int n);
        int pull (float* dest, int max);
    };
    Tap preTap, postTap;

    //==============================================================================
    // The standalone app's sources (in a DAW the EQ works on its track).
    enum class Source { demo, file, input, noise };
    static bool isStandalone();
    void setSource (Source);
    Source getSource() const { return (Source) source.load(); }
    void setPlaying (bool on) { playing = on; }
    bool isPlaying() const { return playing; }
    bool loadFile (const juce::File&);
    juce::String getFileName() const { return fileName; }

    //==============================================================================
    // Presets and A/B
    void loadFactoryPreset (int index);
    static juce::File getUserPresetFolder();
    juce::StringArray getUserPresetNames() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    juce::String getPresetName() const { return presetName; }
    bool isPresetEdited() const { return readParams() != presetSnapshot; }
    char getAbSlot() const { return abSlot; }
    void switchAb (char slot);
    void copyAb();

    // The editor's own settings (analyzer, display range), kept with the project.
    juce::ValueTree uiState { "UI" };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void rememberPreset (const juce::String& name);
    static juce::String paramsToSpec (const eq::Params&);

    eq::Engine engine;
    juce::SharedResourcePointer<UpdateChecker> updates;   // looks for a newer version when the plugin starts
    eq::Params lastParams;
    int lastSolo = -2;
    double sampleRate = 48000;
    juce::AudioBuffer<float> scratch;

    struct BandRefs { std::atomic<float> *used, *on, *shape, *freq, *gain, *q, *slope, *place, *dyn, *range, *thresh, *attack, *release; };
    std::array<BandRefs, eq::maxBands> refs {};
    std::atomic<float> *outGain = nullptr, *outPan = nullptr, *gainScale = nullptr, *autoGain = nullptr, *power = nullptr, *character = nullptr;

    struct Clip { std::vector<float> left, right; };
    std::atomic<int> source { (int) Source::demo };
    std::atomic<bool> playing { false };
    std::shared_ptr<const Clip> demoClip, noiseClip, fileClip;
    std::shared_ptr<const Clip> playingClip;
    size_t clipPos = 0;
    juce::String fileName;
    juce::AudioFormatManager formats;

    juce::String presetName { "Flat" };
    eq::Params presetSnapshot;
    char abSlot = 'A';
    juce::String abOther;   // the other A/B slot, as a preset spec

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EgretEqProcessor)
};
