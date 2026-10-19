#pragma once

#include <array>
#include <functional>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

// The web version's two themes (its CSS variables and the screen's colours).
struct EqTheme
{
    bool dark = false;
    juce::Colour scr, bar, barHi, barLo, edge, fg, fgSoft, acc, accSoft, accHover, onAcc,
                 floatBg, floatSolid, floatBorder, floatShadow, hover, track, knobHi, knobLo, inputBg, ledOff, solo;
    // the screen
    juce::Colour bgTop, bgBottom, scaleBg, nyquist, gridMajor, gridMinor, zero, hline, eqLabel, eqLabel0, anaLabel, freqLabel,
                 preFill, postFill, postLine, curve, curveGlow, dotEdge, ring, ringHover, offFill, badgeText, tipBg, tipText,
                 meterBg, meterTrack, meterLow, meterMid, meterTop, meterHold, meterLine, meterText;
    float bandDarken = 1.0f;
    juce::Colour band (int index) const;
    static const EqTheme& get (bool dark);
};

class EgretLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EgretLookAndFeel();
    void setTheme (const EqTheme&);
    const EqTheme* theme = nullptr;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isMouseOver, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOver, bool isDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};

// What the editor's parts share: the selected band and the display settings.
struct EqView
{
    int selected = -1;
    int anaMode = 3, anaRes = 2, anaSpeed = 2, anaRange = 90, dispRange = 12;   // as the web page's ui settings
    bool anaTilt = true, freeze = false, dark = false;
    double loadWarnUntil = 0;   // the "more than 24 bands" note shows until then (ms)
    const EqTheme& theme() const { return EqTheme::get (dark); }
};

// A flat button as in the web's bars: text or an icon path, highlighted when on.
class BarButton : public juce::Button
{
public:
    explicit BarButton (const juce::String& name = {}) : juce::Button (name) {}
    juce::Path icon;
    bool led = false, filledWhenOn = false;
    const EqView* view = nullptr;
    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
};

// A knob in the band panel: dial, value, label; click the value to type one.
class EqKnob : public juce::Component
{
public:
    enum class Unit { hz, dB, q, pct, pan };
    EqKnob (const juce::String& label, Unit, const EqView&);
    void setParameter (juce::RangedAudioParameter*);
    void setAccent (juce::Colour);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    static juce::String format (Unit, double v);
    static bool parse (Unit, juce::String text, double& value);

private:
    void updateReadout();
    void startTyping();
    Unit unit;
    const EqView& view;
    juce::String labelText;
    juce::RangedAudioParameter* param = nullptr;
    juce::Slider slider;
    juce::Label readout;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    std::unique_ptr<juce::TextEditor> typing;
};

// A value in the bottom bar: drag it, scroll it, double-click to reset, click to type.
class DragValue : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragValue (const juce::String& prefix, EqKnob::Unit, juce::RangedAudioParameter&, const EqView&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    int preferredWidth() const;

private:
    void setNormalised (float);
    juce::String prefix;
    EqKnob::Unit unit;
    juce::RangedAudioParameter& param;
    const EqView& view;
    float dragStart = 0, dragValue = 0;
    bool moved = false;
    std::unique_ptr<juce::TextEditor> typing;
};

class EqDisplay;

// The band panel: under the selected band, like Pro-Q's.
class BandPanel : public juce::Component
{
public:
    BandPanel (EgretEqProcessor&, EqView&, EqDisplay&);
    void refresh (const eq::Params&);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (int)> onStep;   // previous / next band

private:
    EgretEqProcessor& processor;
    EqView& view;
    EqDisplay& display;
    BarButton shape, slope, place, bypass, solo, prev, next, remove;
    EqKnob freq, gain, q;
    int bound = -2;
};

// The EQ screen: grid, scales, analyzer, each band's curve, the sum, the dots.
class EqDisplay : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    EqDisplay (EgretEqProcessor&, EqView&);
    std::function<void()> onSelectionChanged;
    std::function<void (const juce::File&)> onFile;
    void paint (juce::Graphics&) override;
    void resized() override;
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

    void tick (const eq::Params&);   // the editor's timer: analyzer frame, panel, repaint
    juce::String info;               // the hint in the bottom bar
    void addBand (double freq, double gain, eq::Shape);
    void deleteBand (int band);
    void setBandParam (int band, const char* name, float plainValue);   // one gesture
    juce::Point<float> nodePos (int band) const;
    int plotWidth() const { return getWidth() - scaleWidth; }
    static constexpr int scaleWidth = 58;
    BarButton rangeBox;
    std::unique_ptr<BandPanel> panel;

private:
    float fx (double f) const;
    double xf (float x) const;
    float gy (double db) const;
    double yg (float y) const;
    float ay (double db) const;
    int hitBand (juce::Point<float>) const;
    void computeCurves();
    juce::Path spectrumPath (const std::vector<float>& db, double binHz, bool close) const;
    void setInfo (juce::Point<float>* p);
    void placePanel();
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
    explicit MeterView (const EqView& v) : view (v) {}
    void paint (juce::Graphics&) override;
    void push (const std::array<float, 4>&);

private:
    const EqView& view;
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
    void setDark (bool);

private:
    std::unique_ptr<juce::Drawable> drawable;
    double diveStart = -1;
    bool dark = false, built = false;
};

// The whole panel at its native size; the editor scales it to the window.
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
    void refreshAll();
    void saveView();
    void applyTheme();
    void selectionChanged();
    void stepBand (int);
    void presetMenu();
    void analyzerMenu();
    void sourceMenu();
    void loadFile (const juce::File&);

    EgretEqProcessor& audioProcessor;
    EgretLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 };
    PanelView panel;
    EqView view;

    // top bar
    BirdView bird;
    juce::String brandSub;
    BarButton undoButton, redoButton, abButton, copyButton, presetPrev { "<" }, presetButton, presetNext { ">" }, themeButton, bypassButton { "BYPASS" };
    std::unique_ptr<juce::ParameterAttachment> bypassAttachment;   // lit when the "power" parameter is off
    juce::Rectangle<int> topBar, bottomBar;
    juce::String shownPresetSig;

    // screen
    EqDisplay display;
    MeterView meter;

    // bottom bar
    BarButton phaseButton { "Zero Latency" }, anaButton, sourceButton, playButton, autoButton;
    std::unique_ptr<DragValue> scaleValue, gainValue, panValue;
    juce::String shownInfo, shownAuto;
    int lastBandCount = -1;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EgretEqEditor)
};
