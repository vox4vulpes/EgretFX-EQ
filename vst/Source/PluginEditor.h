#pragma once

#include <array>
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// The web version's :root palette.
namespace EgretColours
{
    inline const juce::Colour panel       { 0xffe9e1cc };
    inline const juce::Colour panelRaised { 0xfff2ecd9 };
    inline const juce::Colour groove      { 0xffc7bb9c };
    inline const juce::Colour grooveDark  { 0xffa99c79 };
    inline const juce::Colour ink         { 0xff2a241d };
    inline const juce::Colour inkSoft     { 0x9e2a241d };
    inline const juce::Colour mustard     { 0xffc08f17 };
    inline const juce::Colour teal        { 0xff336f6b };
    inline const juce::Colour coral       { 0xffb85540 };
    inline const juce::Colour lavender    { 0xff8a78a3 };
    inline const juce::Colour sage        { 0xff5f7344 };
    inline const juce::Colour readoutBg   { 0xff171308 };
    inline const juce::Colour readoutInk  { 0xfff0a830 };
    inline const juce::Colour screenTeal  { 0xff7ec4be };
    inline const juce::Colour shadow      { 0x591e190f };
    inline const juce::Colour hot         { 0xffff6a4a };
    juce::Colour band (int index);      // the band colours on the dark screen
    juce::Colour bandInk (int index);   // the same, darker, for the light panel
}

class EgretLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EgretLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isMouseOver, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

// What the editor's parts share: the selected band and the display settings.
struct EqView
{
    int selected = -1;
    int anaMode = 3, anaRes = 2, anaSpeed = 2, anaRange = 90, dispRange = 12;   // as the web page's ui settings
    bool anaTilt = true, freeze = false;
};

// A knob as on the web panel: dial, label, value; click the value to type one.
// The band knobs follow the selected band, so the parameter can change.
class EqKnob : public juce::Component
{
public:
    enum class Unit { hz, dB, q, pct, pan };
    EqKnob (const juce::String& label, Unit, juce::Colour accent);
    void setParameter (juce::RangedAudioParameter*);
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    static juce::String format (Unit, double v);

private:
    void updateReadout();
    void startTyping();
    Unit unit;
    juce::RangedAudioParameter* param = nullptr;
    juce::Slider slider;
    juce::Label label, readout;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    std::unique_ptr<juce::TextEditor> typing;
};

// A row of chip buttons; isOn and onPick say what they show and do.
class ChipRow : public juce::Component
{
public:
    ChipRow (const juce::StringArray& labels, juce::Colour accent);
    std::function<bool (int)> isOn;
    std::function<void (int)> onPick;
    void refresh();
    void setColumns (int c) { columns = c; resized(); }
    void resized() override;
    void setChipEnabled (int index, bool enabled) { buttons[index]->setEnabled (enabled); }
    juce::OwnedArray<juce::TextButton> buttons;

private:
    int columns = 0;
};

// The EQ screen: grid, analyzer, each band's curve, the sum, the dots.
class EqDisplay : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    EqDisplay (EgretEqProcessor&, EqView&);
    std::function<void()> onSelectionChanged;
    std::function<void (const juce::File&)> onFile;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

    void tick (const eq::Params&);   // the editor's timer: analyzer frame and repaint
    juce::String info;               // the caption under the screen
    void addBand (double freq, double gain, eq::Shape);
    void deleteBand (int band);
    void setBandParam (int band, const char* name, float plainValue);   // one gesture

private:
    float fx (double f) const;
    double xf (float x) const;
    float gy (double db) const;
    double yg (float y) const;
    float ay (double db) const;
    juce::Point<float> nodePos (int band) const;
    int hitBand (juce::Point<float>) const;
    void computeCurves();
    juce::Path spectrumPath (const std::vector<float>& db, double binHz, bool close) const;
    void setInfo (juce::Point<float>* p);
    juce::RangedAudioParameter* bandParam (int band, const char* name) const;

    EgretEqProcessor& processor;
    EqView& view;
    eq::Params params;
    std::array<std::vector<float>, eq::maxBands> curves;
    std::array<eq::Band, eq::maxBands> curveKeys {};
    std::array<double, eq::maxBands> curveGains {};
    std::vector<float> total;
    double curveFs = 0, curveScale = -1;
    int curveCols = 0;
    eq::Analyzer pre, post;
    std::vector<float> pull;
    int hover = -1;
    struct Drag { int band = -1; float fx = 0, fy = 0; juce::Point<float> last; } drag;
    bool dragOver = false;
};

class MeterView : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void push (const std::array<float, 4>&);
    float getPeakHold() const { return peak; }

private:
    std::array<float, 4> shown {}, hold {};
    std::array<double, 4> holdTime {};
    float peak = 0;
    double peakTime = 0;
};

class BirdView : public juce::Component, public juce::SettableTooltipClient
{
public:
    BirdView();
    std::function<void()> onClick;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (contains (e.getPosition()) && onClick) onClick(); }
    void dive() { diveStart = juce::Time::getMillisecondCounterHiRes(); }
    void tick();

private:
    std::unique_ptr<juce::Drawable> drawable;
    double diveStart = -1;
};

class PowerButton : public juce::Button
{
public:
    PowerButton() : juce::Button ("POWER") { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
};

// The whole panel at its native 900 px width; the editor scales it to the window.
class PanelView : public juce::Component
{
public:
    std::function<void (juce::Graphics&)> painter;
    void paint (juce::Graphics& g) override { if (painter) painter (g); }
};

class EgretEqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EgretEqEditor (EgretEqProcessor&);
    ~EgretEqEditor() override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void paintPanel (juce::Graphics&);
    void layoutPanel();
    void refreshPresetBox();
    void savePreset();
    void deletePreset();
    void selectionChanged();
    void layoutStrip();
    void refreshAll();
    void saveView();
    void loadFile (const juce::File&);
    struct Module { juce::String title; juce::Colour accent; juce::Rectangle<int> bounds; };

    EgretEqProcessor& audioProcessor;
    EgretLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };
    PanelView panel;
    EqView view;
    std::vector<Module> modules;

    // header
    BirdView bird;
    juce::String brandSub;
    PowerButton power;
    std::unique_ptr<juce::ButtonParameterAttachment> powerAttachment;
    juce::Rectangle<int> headerArea, screenBay, bandArea, helpArea;
    int shownBands = -1;
    juce::String shownPeak;

    // presets, undo, A/B
    juce::ComboBox presetBox;
    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "SAVE" }, presetDelete { "DELETE" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" }, abA { "A" }, abB { "B" }, abCopy { "A>B" };
    juce::TextEditor presetName;
    juce::Label presetStatus;
    juce::StringArray userPresets;
    juce::String shownPresetSig;
    bool deleteArmed = false;

    // display
    EqDisplay display;
    MeterView meter;
    juce::String shownInfo;

    // band
    juce::OwnedArray<juce::TextButton> bandChips;
    juce::TextButton addChip { "+" };
    ChipRow shapes, slopes, places;
    juce::Label shapeLabel, slopeLabel, placeLabel, bandEmpty;
    EqKnob freqKnob, gainKnob, qKnob;
    juce::TextButton bypassButton { "BYPASS" }, soloButton { "SOLO" }, deleteButton { "DELETE" };
    juce::String bandTitle { "BAND" };
    juce::Colour bandAccent = EgretColours::teal;
    eq::Params shownParams;
    int shownSelected = -2, shownSolo = -2;

    // source, analyzer, output
    ChipRow sources;
    juce::TextButton playButton { "PLAY" }, loadButton { "LOAD FILE" };
    juce::Label sourceStatus;
    std::unique_ptr<juce::FileChooser> chooser;
    ChipRow anaMode, anaRes, anaSpeed, anaRange, dispRange;
    juce::Label anaLabels[5];
    juce::TextButton tiltButton { "TILT" }, freezeButton { "FREEZE" }, autoButton { "AUTO GAIN" };
    juce::Label autoReadout;
    EqKnob scaleKnob, outGainKnob, panKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EgretEqEditor)
};
