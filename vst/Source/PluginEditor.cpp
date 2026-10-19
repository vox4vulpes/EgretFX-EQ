#include "PluginEditor.h"

namespace
{
    // The panel at the web page's plugin size (950 px wide); the window scales it.
    constexpr int panelWidth = 950, topH = 38, screenH = 540, bottomH = 32, panelHeight = topH + screenH + bottomH;
    constexpr int meterW = 30;
    constexpr float defaultScale = 0.9f;
    constexpr double pi = juce::MathConstants<double>::pi;

    juce::Font sansFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
    juce::Font monoFont (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, bold ? juce::Font::bold : juce::Font::plain));
    }
    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }
    juce::String minus() { return utf8 ("\xe2\x88\x92"); }
    juce::String quoted (const juce::String& s) { return utf8 ("\xe2\x80\x9c") + s + utf8 ("\xe2\x80\x9d"); }
    juce::Colour rgba (int r, int g, int b, float a) { return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, a); }

    juce::String birdSvg (bool dark)
    {
        return juce::String (R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"> <path d="M6 9.5c0-2.6 2.1-4.5 4.7-4.5 2.3 0 4 1.5 4.4 3.5l.4 2.3c.4 2.2 1.9 3.6 3.9 4.6l-1.2.9c-1.1-.2-2.2-.6-3.1-1.2-.9 2.3-3.3 3.6-6 3.1-2.4-.5-3.9-2.6-3.6-5z" fill="#2a8fc4" stroke="#0d0a04" stroke-width=".7" stroke-linejoin="round"/> <path d="M7.4 11.4c.6 2.6 2.6 4.3 5.4 3.9 1-.2 1.8-.6 2.4-1.3-1.9.2-4.4-.6-5.9-2.7z" fill="#e07b2a"/> <path d="M6.6 9.6c.6.6 1.6.8 2.4.5-.2.7-.9 1.2-1.7 1.2-.5-.4-.7-1-.7-1.7z" fill="#fbf8ef"/> <path d="M6.2 8.4L1 9.3l5.4.5z" fill="BILL"/> <circle cx="8.2" cy="7.6" r=".55" fill="#0d0a04"/> <path d="M11 17.6l-.4 2.6M12.6 17.7l.2 2.5" stroke="#d9452f" stroke-width=".9" stroke-linecap="round"/> </svg>)svg")
            .replace ("BILL", dark ? "#f2ecd9" : "#2a241d");
    }
    juce::String subtitleText() { return utf8 ("\xe7\xbf\xa1\xe7\xbf\xa0 \xc2\xb7 PARAMETRIC EQ"); }
    juce::String birdNameText() { return utf8 ("\xe3\x82\xab\xe3\x83\xaf\xe3\x82\xbb\xe3\x83\x9f \xc2\xb7 KAWASEMI"); }

    juce::String fmtHz (double v)
    {
        return v >= 10000 ? juce::String (v / 1000, 1) + " kHz" : v >= 1000 ? juce::String (v / 1000, 2) + " kHz"
             : v >= 100 ? juce::String (juce::roundToInt (v)) + " Hz" : juce::String (v, 1) + " Hz";
    }
    juce::String noteName (double f)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const auto m = 69 + 12 * std::log2 (f / 440);
        const auto r = (int) std::floor (m + 0.5);
        const auto cents = (int) std::floor ((m - r) * 100 + 0.5);
        return juce::String (names[((r % 12) + 12) % 12]) + juce::String ((int) std::floor (r / 12.0) - 1)
             + (cents != 0 ? (cents > 0 ? "+" : "") + juce::String (cents) : juce::String());
    }

    // Cut, notch, band and all-pass dots have no gain: their height is Q, half the screen each way.
    double qNorm (double q)
    {
        constexpr auto r = 0.70710678118654752;
        return q >= r ? std::log (q / r) / std::log (eq::qMax / r) : -std::log (r / q) / std::log (r / eq::qMin);
    }
    double qFromNorm (double t)
    {
        constexpr auto r = 0.70710678118654752;
        t = juce::jlimit (-1.0, 1.0, t);
        return t >= 0 ? r * std::pow (eq::qMax / r, t) : r * std::pow (eq::qMin / r, -t);
    }

    const int anaSizes[] = { 2048, 4096, 8192, 16384 };
    const double anaSmooth[] = { 0.3, 0.55, 0.75, 0.88, 0.95 };
    const double anaCal = 20 * std::log10 (1 / 0.21);   // a full-scale sine peaks at 0 dB
    const char* const placeNames[] = { "Stereo", "Left", "Right", "Mid", "Side" };

    // Icons, in a 16 x 16 box like the web page's SVGs.
    juce::Path stroked (const juce::Path& p, float width)
    {
        juce::Path out;
        juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (out, p);
        return out;
    }
    juce::Path undoIcon()
    {
        juce::Path p;
        p.startNewSubPath (6, 3); p.lineTo (2, 7); p.lineTo (6, 11);
        p.startNewSubPath (2.5f, 7); p.lineTo (10, 7);
        p.addCentredArc (10, 11, 4, 4, 0, 0, juce::MathConstants<float>::pi, false);
        p.lineTo (8, 15);
        return stroked (p, 1.6f);
    }
    juce::Path powerIcon()
    {
        juce::Path p;
        p.startNewSubPath (8, 1.5f); p.lineTo (8, 7.5f);
        p.addCentredArc (8, 8.8f, 5.5f, 5.5f, 0, 0.7f, 2.0f * juce::MathConstants<float>::pi - 0.7f, true);
        return stroked (p, 1.7f);
    }
    juce::Path moonIcon()   // the web page's SVG
    {
        return juce::Drawable::parseSVGPath ("M13 9.5A5.5 5.5 0 016.5 3a5.5 5.5 0 106.5 6.5z");
    }
    juce::Path sunIcon()
    {
        juce::Path rays = juce::Drawable::parseSVGPath ("M8 1v2M8 13v2M1 8h2M13 8h2M3 3l1.4 1.4M11.6 11.6L13 13M13 3l-1.4 1.4M4.4 11.6L3 13");
        auto p = stroked (rays, 1.4f);
        p.addEllipse (5, 5, 6, 6);
        return p;
    }
}

//==============================================================================
juce::Colour EqTheme::band (int i) const
{
    static const juce::uint32 colours[] = { 0xfff0a830, 0xff7ec4be, 0xffe8846a, 0xffb6a5d0, 0xffa9c27e, 0xffe6cf5a,
                                            0xff6fb7d6, 0xffe793b6, 0xffd2a77c, 0xff93d493, 0xffc9c39a, 0xfff08a7a };
    const juce::Colour c (colours[i % 12]);
    if (bandDarken >= 1.0f) return c;
    return juce::Colour ((juce::uint8) juce::roundToInt (c.getRed() * bandDarken), (juce::uint8) juce::roundToInt (c.getGreen() * bandDarken),
                         (juce::uint8) juce::roundToInt (c.getBlue() * bandDarken));
}

const EqTheme& EqTheme::get (bool dark)
{
    static const EqTheme light = []
    {
        EqTheme t;
        t.dark = false;
        t.scr = juce::Colour (0xfff6f1e3); t.bar = juce::Colour (0xffe4dbc4); t.barHi = juce::Colour (0xffefe8d6); t.barLo = juce::Colour (0xffd9cfb5);
        t.edge = rgba (42, 36, 29, 0.18f); t.fg = juce::Colour (0xff2a241d); t.fgSoft = rgba (42, 36, 29, 0.6f);
        t.acc = juce::Colour (0xffa8700a); t.accSoft = rgba (168, 112, 10, 0.6f); t.accHover = rgba (168, 112, 10, 0.14f); t.onAcc = juce::Colour (0xfffbf8ef);
        t.floatBg = rgba (251, 248, 239, 0.96f); t.floatSolid = juce::Colour (0xfffbf8ef); t.floatBorder = rgba (42, 36, 29, 0.16f); t.floatShadow = rgba (60, 45, 20, 0.25f);
        t.hover = rgba (42, 36, 29, 0.06f); t.track = rgba (42, 36, 29, 0.14f); t.knobHi = juce::Colour (0xfffffdf6); t.knobLo = juce::Colour (0xffd9cfb5);
        t.inputBg = juce::Colour (0xfffffdf6); t.ledOff = juce::Colour (0xffc7bb9c); t.solo = juce::Colour (0xffd9b92a);
        t.bgTop = juce::Colour (0xfff8f4e8); t.bgBottom = juce::Colour (0xffefe8d6); t.scaleBg = rgba (42, 36, 29, 0.05f); t.nyquist = rgba (42, 36, 29, 0.08f);
        t.gridMajor = rgba (42, 36, 29, 0.16f); t.gridMinor = rgba (42, 36, 29, 0.06f); t.zero = rgba (168, 112, 10, 0.45f); t.hline = rgba (42, 36, 29, 0.06f);
        t.eqLabel = rgba (168, 112, 10, 0.85f); t.eqLabel0 = juce::Colour (0xffa8700a); t.anaLabel = rgba (42, 36, 29, 0.4f); t.freqLabel = rgba (42, 36, 29, 0.55f);
        t.preFill = rgba (51, 111, 107, 0.14f); t.postFill = rgba (42, 36, 29, 0.07f); t.postLine = rgba (42, 36, 29, 0.35f);
        t.curve = juce::Colour (0xff2a241d); t.curveGlow = rgba (42, 36, 29, 0.25f); t.dotEdge = rgba (42, 36, 29, 0.55f);
        t.ring = juce::Colour (0xff2a241d); t.ringHover = rgba (42, 36, 29, 0.4f); t.offFill = juce::Colour (0xfff8f4e8); t.badgeText = juce::Colour (0xfffbf8ef);
        t.tipBg = rgba (251, 248, 239, 0.96f); t.tipText = juce::Colour (0xff2a241d);
        t.meterBg = juce::Colour (0xffece4cf); t.meterTrack = rgba (42, 36, 29, 0.08f); t.meterLow = juce::Colour (0xffc9a35a); t.meterMid = juce::Colour (0xffa8700a);
        t.meterTop = juce::Colour (0xff6b4a10); t.meterHold = juce::Colour (0xff2a241d); t.meterLine = rgba (168, 112, 10, 0.45f); t.meterText = rgba (42, 36, 29, 0.75f);
        t.bandDarken = 0.72f;
        return t;
    }();
    static const EqTheme darkTheme = []
    {
        EqTheme t;
        t.dark = true;
        t.scr = juce::Colour (0xff171308); t.bar = juce::Colour (0xff211b10); t.barHi = juce::Colour (0xff2c2416); t.barLo = juce::Colour (0xff1a150c);
        t.edge = rgba (0, 0, 0, 0.5f); t.fg = juce::Colour (0xfff2ecd9); t.fgSoft = rgba (242, 236, 217, 0.6f);
        t.acc = juce::Colour (0xfff0a830); t.accSoft = rgba (240, 168, 48, 0.62f); t.accHover = rgba (240, 168, 48, 0.18f); t.onAcc = juce::Colour (0xff171308);
        t.floatBg = rgba (33, 27, 16, 0.94f); t.floatSolid = juce::Colour (0xff241d12); t.floatBorder = rgba (242, 236, 217, 0.14f); t.floatShadow = rgba (0, 0, 0, 0.55f);
        t.hover = rgba (242, 236, 217, 0.07f); t.track = rgba (242, 236, 217, 0.16f); t.knobHi = juce::Colour (0xff4a3f2c); t.knobLo = juce::Colour (0xff231c11);
        t.inputBg = juce::Colour (0xff0d0a04); t.ledOff = juce::Colour (0xff5a4a30); t.solo = juce::Colour (0xffe6cf5a);
        t.bgTop = juce::Colour (0xff1d170c); t.bgBottom = juce::Colour (0xff120e06); t.scaleBg = rgba (0, 0, 0, 0.25f); t.nyquist = rgba (0, 0, 0, 0.4f);
        t.gridMajor = rgba (240, 168, 48, 0.15f); t.gridMinor = rgba (240, 168, 48, 0.06f); t.zero = rgba (240, 168, 48, 0.3f); t.hline = rgba (240, 168, 48, 0.06f);
        t.eqLabel = rgba (240, 168, 48, 0.75f); t.eqLabel0 = juce::Colour (0xfff0a830); t.anaLabel = rgba (242, 236, 217, 0.38f); t.freqLabel = rgba (242, 236, 217, 0.5f);
        t.preFill = rgba (126, 196, 190, 0.13f); t.postFill = rgba (242, 236, 217, 0.08f); t.postLine = rgba (242, 236, 217, 0.4f);
        t.curve = juce::Colour (0xfff2ecd9); t.curveGlow = rgba (242, 236, 217, 0.55f); t.dotEdge = rgba (0, 0, 0, 0.6f);
        t.ring = juce::Colour (0xfff2ecd9); t.ringHover = rgba (242, 236, 217, 0.45f); t.offFill = juce::Colour (0xff171308); t.badgeText = juce::Colour (0xff171308);
        t.tipBg = rgba (23, 19, 8, 0.92f); t.tipText = juce::Colour (0xfff2ecd9);
        t.meterBg = juce::Colour (0xff120e06); t.meterTrack = rgba (240, 168, 48, 0.08f); t.meterLow = juce::Colour (0xff7a5a18); t.meterMid = juce::Colour (0xfff0a830);
        t.meterTop = juce::Colour (0xfff2ecd9); t.meterHold = juce::Colour (0xfff2ecd9); t.meterLine = rgba (240, 168, 48, 0.35f); t.meterText = rgba (242, 236, 217, 0.75f);
        t.bandDarken = 1.0f;
        return t;
    }();
    return dark ? darkTheme : light;
}

//==============================================================================
EgretLookAndFeel::EgretLookAndFeel() { setTheme (EqTheme::get (false)); }

void EgretLookAndFeel::setTheme (const EqTheme& t)
{
    theme = &t;
    setColour (juce::PopupMenu::backgroundColourId, t.floatSolid);
    setColour (juce::PopupMenu::textColourId, t.fg);
    setColour (juce::PopupMenu::headerTextColourId, t.fgSoft);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.acc.withAlpha (0.18f).withAlpha (1.0f).interpolatedWith (t.floatSolid, 0.75f));
    setColour (juce::PopupMenu::highlightedTextColourId, t.fg);
    setColour (juce::TextEditor::backgroundColourId, t.inputBg);
    setColour (juce::TextEditor::outlineColourId, t.acc);
    setColour (juce::TextEditor::focusedOutlineColourId, t.acc);
    setColour (juce::TextEditor::textColourId, t.fg);
    setColour (juce::TextEditor::highlightColourId, t.accHover);
    setColour (juce::CaretComponent::caretColourId, t.fg);
    setColour (juce::Label::textColourId, t.fg);
    setColour (juce::TooltipWindow::backgroundColourId, t.floatSolid);
    setColour (juce::TooltipWindow::textColourId, t.fg);
    setColour (juce::TooltipWindow::outlineColourId, t.floatBorder);
    setColour (juce::AlertWindow::backgroundColourId, t.floatSolid);
    setColour (juce::AlertWindow::textColourId, t.fg);
    setColour (juce::AlertWindow::outlineColourId, t.floatBorder);
    setColour (juce::TextButton::buttonColourId, t.hover);
    setColour (juce::TextButton::textColourOffId, t.fg);
    setColour (juce::TextButton::textColourOnId, t.onAcc);
    setColour (juce::TextButton::buttonOnColourId, t.acc);
}

void EgretLookAndFeel::drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) {}   // EqKnob paints itself

void EgretLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (b.getToggleState() ? theme->acc : (over || down) ? theme->hover.withMultipliedAlpha (2.0f) : theme->hover);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (theme->floatBorder);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void EgretLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? theme->onAcc : theme->fg);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

juce::Font EgretLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return sansFont (12.0f); }
juce::Font EgretLookAndFeel::getPopupMenuFont() { return sansFont (13.0f); }

void EgretLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (theme->floatSolid);
    g.setColour (theme->floatBorder);
    g.drawRect (0, 0, width, height);
}

//==============================================================================
void BarButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto& t = view->theme();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();
    const auto enabled = isEnabled();
    const auto boxed = getProperties().contains ("boxed"), panelSel = getProperties().contains ("panelSel");
    if (filledWhenOn && on)
    {
        g.setColour (getProperties().contains ("solo") ? t.solo : t.acc);
        g.fillRoundedRectangle (r, 5.0f);
    }
    else if (boxed)   // the range box and the band panel's menus
    {
        g.setColour (panelSel ? t.hover : t.floatBg);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (over && enabled ? (panelSel ? findColour (juce::TextButton::buttonOnColourId) : t.acc) : t.floatBorder);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    else if (enabled && (over || down))
    {
        g.setColour (t.hover);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (t.floatBorder);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }
    const auto ink = (filledWhenOn && on) ? t.onAcc : (on && ! filledWhenOn && ! led) ? t.acc : (getProperties().contains ("accent") ? t.acc : t.fg);
    const auto alpha = enabled ? 1.0f : 0.35f;
    auto area = getLocalBounds().toFloat().reduced (6.0f, 0.0f);
    if (led)
    {
        const auto l = juce::Rectangle<float> (9, 9).withCentre ({ area.getX() + 4.5f, area.getCentreY() });
        if (on) { g.setColour (t.acc.withAlpha (0.35f)); g.fillEllipse (l.expanded (2.5f)); }
        g.setColour (on ? t.acc : t.ledOff);
        g.fillEllipse (l);
        area.removeFromLeft (14.0f);
    }
    g.setColour (ink.withMultipliedAlpha (alpha));
    if (! icon.isEmpty())
    {
        const auto box = juce::Rectangle<float> (15, 15).withCentre (area.getCentre());
        g.fillPath (icon, icon.getTransformToScaleToFit (box.reduced (0.5f), true));
        return;
    }
    const auto font = getProperties().contains ("mono") ? monoFont (11.0f) : sansFont (12.0f);
    g.setFont (font);
    const auto label = getButtonText();
    if (getProperties().contains ("dimPrefix"))
    {
        const auto prefix = getProperties()["dimPrefix"].toString();
        const auto w = juce::GlyphArrangement::getStringWidth (font, prefix + " ");
        const auto full = w + juce::GlyphArrangement::getStringWidth (font, label);
        auto a = area.withSizeKeepingCentre (juce::jmin (full + 1, area.getWidth()), area.getHeight());
        g.setColour (t.fgSoft.withMultipliedAlpha (alpha));
        g.drawText (prefix, a.removeFromLeft (w), juce::Justification::centredLeft, false);
        g.setColour (ink.withMultipliedAlpha (alpha));
        g.drawText (label, a, juce::Justification::centredLeft, true);
        return;
    }
    if (panelSel)
    {
        g.drawText (label, area.withTrimmedRight (12), juce::Justification::centredLeft, true);
        g.setColour (t.fgSoft.withMultipliedAlpha (alpha));
        g.drawText (utf8 ("\xe2\x96\xbe"), area, juce::Justification::centredRight, false);
        return;
    }
    g.drawText (label, area, juce::Justification::centred, true);
}

//==============================================================================
EqKnob::EqKnob (const juce::String& text, Unit u, const EqView& v) : unit (u), view (v), labelText (text.toUpperCase())
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (150);   // the web knob: 150 px for the whole range
    slider.setWantsKeyboardFocus (false);
    slider.setAlpha (0.0f);                  // the knob is painted underneath; the slider only takes the mouse
    slider.onValueChange = [this] { updateReadout(); repaint(); };
    addAndMakeVisible (slider);
    readout.setFont (monoFont (10.5f));
    readout.setJustificationType (juce::Justification::centred);
    readout.setBorderSize ({});
    readout.setMouseCursor (juce::MouseCursor::IBeamCursor);
    readout.setTooltip ("Click to type a value");
    readout.addMouseListener (this, false);
    addAndMakeVisible (readout);
}

void EqKnob::setAccent (juce::Colour c)
{
    if (slider.findColour (juce::Slider::rotarySliderFillColourId) == c) return;
    slider.setColour (juce::Slider::rotarySliderFillColourId, c);
    repaint();
}

void EqKnob::setParameter (juce::RangedAudioParameter* p)
{
    if (p == param) return;
    attachment.reset();
    param = p;
    if (param != nullptr)
    {
        attachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }
    updateReadout();
    repaint();
}

juce::String EqKnob::format (Unit u, double v)
{
    switch (u)
    {
        case Unit::hz: return fmtHz (v);
        case Unit::dB:
        {
            const auto r = std::round (v * 10) / 10;
            return (r > 0 ? "+" : r < 0 ? minus() : juce::String()) + juce::String (std::abs (r), 1) + " dB";
        }
        case Unit::q:   return juce::String (v, v < 10 ? 2 : 1);
        case Unit::pan: return std::abs (v) < 0.5 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v)));
        case Unit::pct: break;
    }
    return juce::String (juce::roundToInt (v)) + "%";
}

// A typed value: a number with an optional unit -- 2.5k, 250, -3, 0.7, L20.
bool EqKnob::parse (Unit u, juce::String text, double& v)
{
    text = text.trim().toLowerCase().replaceCharacter (',', '.').replace (minus(), "-");
    if (u == Unit::pan && (text == "c" || text.startsWith ("l") || text.startsWith ("r")))
    {
        if (text == "c") { v = 0; return true; }
        const auto num = text.substring (1).trim();
        if (num.isEmpty() || ! num.containsOnly (".0123456789")) return false;
        v = (text[0] == 'l' ? -1 : 1) * num.getDoubleValue();
        return true;
    }
    const auto number = text.initialSectionContainingOnly ("+-.0123456789");
    const auto unitText = text.substring (number.length()).trim();
    if (! number.containsAnyOf ("0123456789")) return false;
    if (! juce::StringArray { "", "k", "khz", "hz", "db", "%" }.contains (unitText)) return false;
    v = number.getDoubleValue();
    if (unitText == "k" || unitText == "khz") v *= 1000;
    return true;
}

void EqKnob::updateReadout() { readout.setText (format (unit, slider.getValue()), juce::dontSendNotification); }

void EqKnob::resized()
{
    slider.setBounds ((getWidth() - 42) / 2, 0, 42, 42);
    readout.setBounds (0, 42, getWidth(), 13);
    if (typing != nullptr) typing->setBounds (readout.getBounds().expanded (0, 1));
}

void EqKnob::paint (juce::Graphics& g)
{
    const auto& t = view.theme();
    readout.setColour (juce::Label::textColourId, t.fg);
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto centre = juce::Point<float> (getWidth() * 0.5f, 21.0f);
    const auto start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
    const auto pos = param != nullptr ? param->convertTo0to1 ((float) slider.getValue()) : 0.5f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, 18, 18, 0, start, end, true);
    g.setColour (t.track);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    const auto cap = juce::Rectangle<float> (30, 30).withCentre (centre);
    g.setColour (t.floatShadow);
    g.fillEllipse (cap.translated (0, 1.5f));
    g.setGradientFill (juce::ColourGradient (t.knobHi, centre.x, centre.y - 11, t.knobLo, centre.x, centre.y + 19, true));
    g.fillEllipse (cap);
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.25f, -13.5f, 2.5f, 5.5f, 1.2f);
    g.setColour (accent);
    g.fillPath (pointer, juce::AffineTransform::rotation (start + pos * (end - start)).translated (centre));
    g.setColour (t.fgSoft);
    g.setFont (sansFont (8.5f, true).withExtraKerningFactor (0.06f));
    g.drawText (labelText, juce::Rectangle<int> (0, 55, getWidth(), 11), juce::Justification::centred, false);
}

void EqKnob::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &readout && readout.contains (e.getEventRelativeTo (&readout).getPosition()))
        startTyping();
}

// Enter or clicking away sets it, Escape keeps the old one.
void EqKnob::startTyping()
{
    if (typing != nullptr || param == nullptr)
        return;
    typing = std::make_unique<juce::TextEditor>();
    typing->setFont (monoFont (10.5f));
    typing->setJustification (juce::Justification::centred);
    typing->setIndents (1, 0);
    typing->setText (unit == Unit::pan ? format (unit, slider.getValue()) : juce::String (std::round (slider.getValue() * 100) / 100), false);
    addAndMakeVisible (*typing);
    resized();
    typing->grabKeyboardFocus();
    typing->selectAll();
    // The safe pointer is made out here: MSVC can't take `this` inside a nested lambda's capture.
    auto finish = [this, safe = juce::Component::SafePointer<EqKnob> (this)] (bool apply)
    {
        if (typing == nullptr || typing->getProperties().contains ("done"))
            return;
        typing->getProperties().set ("done", true);   // Escape, then the focus going: only the first counts
        const auto text = typing->getText();
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->typing.reset(); });
        double v;
        if (! apply || param == nullptr || ! parse (unit, text, v))
            return;
        const auto range = param->getNormalisableRange();
        v = juce::jlimit ((double) range.start, (double) range.end, v);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) v));
        param->endChangeGesture();
    };
    typing->onReturnKey = [finish] { finish (true); };
    typing->onEscapeKey = [finish] { finish (false); };
    typing->onFocusLost = [finish] { finish (true); };
}

//==============================================================================
DragValue::DragValue (const juce::String& p, EqKnob::Unit u, juce::RangedAudioParameter& prm, const EqView& v)
    : prefix (p), unit (u), param (prm), view (v)
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

int DragValue::preferredWidth() const
{
    return juce::roundToInt (juce::GlyphArrangement::getStringWidth (sansFont (10.5f), prefix + " ")
                             + juce::GlyphArrangement::getStringWidth (monoFont (11.5f), EqKnob::format (unit, param.convertFrom0to1 (param.getValue())))) + 16;
}

void DragValue::paint (juce::Graphics& g)
{
    const auto& t = view.theme();
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    if (isMouseOverOrDragging())
    {
        g.setColour (t.hover); g.fillRoundedRectangle (r, 4.0f);
        g.setColour (t.floatBorder); g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
    if (typing != nullptr) return;
    auto a = getLocalBounds().reduced (7, 0).toFloat();
    const auto pw = juce::GlyphArrangement::getStringWidth (sansFont (10.5f), prefix + " ");
    g.setColour (t.fgSoft); g.setFont (sansFont (10.5f));
    g.drawText (prefix, a.removeFromLeft (pw), juce::Justification::centredLeft, false);
    g.setColour (t.fg); g.setFont (monoFont (11.5f));
    g.drawText (EqKnob::format (unit, param.convertFrom0to1 (param.getValue())), a, juce::Justification::centredLeft, false);
}

void DragValue::setNormalised (float v)
{
    param.setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
    repaint();
}

void DragValue::mouseDown (const juce::MouseEvent&)
{
    dragStart = dragValue = param.getValue();
    moved = false;
    param.beginChangeGesture();
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    if (std::abs (e.getDistanceFromDragStartY()) > 2) moved = true;
    dragValue = dragStart - e.getDistanceFromDragStartY() / 200.0f * (e.mods.isShiftDown() ? 0.2f : 1.0f);
    setNormalised (dragValue);
}

void DragValue::mouseUp (const juce::MouseEvent&)
{
    param.endChangeGesture();
    if (moved || typing != nullptr) return;
    // a click: type a value
    typing = std::make_unique<juce::TextEditor>();
    typing->setFont (monoFont (11.0f));
    typing->setJustification (juce::Justification::centred);
    typing->setText (unit == EqKnob::Unit::pan ? EqKnob::format (unit, param.convertFrom0to1 (param.getValue()))
                                               : juce::String (std::round (param.convertFrom0to1 (param.getValue()) * 100) / 100), false);
    addAndMakeVisible (*typing);
    typing->setBounds (getLocalBounds().reduced (2, 2));
    typing->grabKeyboardFocus();
    typing->selectAll();
    auto finish = [this, safe = juce::Component::SafePointer<DragValue> (this)] (bool apply)
    {
        if (typing == nullptr || typing->getProperties().contains ("done")) return;
        typing->getProperties().set ("done", true);
        const auto text = typing->getText();
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->typing.reset(); safe->repaint(); } });
        double v;
        if (! apply || ! EqKnob::parse (unit, text, v)) return;
        const auto range = param.getNormalisableRange();
        param.beginChangeGesture();
        param.setValueNotifyingHost (param.convertTo0to1 ((float) juce::jlimit ((double) range.start, (double) range.end, v)));
        param.endChangeGesture();
    };
    typing->onReturnKey = [finish] { finish (true); };
    typing->onEscapeKey = [finish] { finish (false); };
    typing->onFocusLost = [finish] { finish (true); };
}

void DragValue::mouseDoubleClick (const juce::MouseEvent&)
{
    typing.reset();
    param.beginChangeGesture();
    setNormalised (param.getDefaultValue());
    param.endChangeGesture();
}

void DragValue::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    param.beginChangeGesture();
    setNormalised (param.getValue() + (w.deltaY * (w.isReversed ? -1.0f : 1.0f)) * (e.mods.isShiftDown() ? 0.02f : 0.1f));
    param.endChangeGesture();
}

//==============================================================================
BandPanel::BandPanel (EgretEqProcessor& p, EqView& v, EqDisplay& d)
    : processor (p), view (v), display (d), freq ("Freq", EqKnob::Unit::hz, v), gain ("Gain", EqKnob::Unit::dB, v), q ("Q", EqKnob::Unit::q, v)
{
    for (auto* b : { &shape, &slope, &place, &bypass, &solo, &prev, &next, &remove })
    {
        b->view = &view;
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    for (auto* b : { &shape, &slope, &place }) { b->getProperties().set ("boxed", true); b->getProperties().set ("panelSel", true); }
    shape.setTooltip ("Shape"); slope.setTooltip ("Slope"); place.setTooltip ("Stereo placement");
    bypass.icon = powerIcon(); bypass.filledWhenOn = true; bypass.setTooltip ("Band on/off (or double-click its dot)");
    solo.setButtonText ("S"); solo.filledWhenOn = true; solo.getProperties().set ("solo", true); solo.setTooltip ("Solo: hear only what this band works on");
    prev.setButtonText (utf8 ("\xe2\x80\xb9")); prev.setTooltip ("Previous band");
    next.setButtonText (utf8 ("\xe2\x80\xba")); next.setTooltip ("Next band");
    remove.setButtonText (utf8 ("\xe2\x9c\x95")); remove.setTooltip ("Remove the band (Delete, or Alt-click its dot)");
    for (auto* k : { &freq, &gain, &q }) addAndMakeVisible (*k);

    auto menu = [this] (BarButton& b, const char* id, juce::StringArray labels)
    {
        b.onClick = [this, &b, id, labels]
        {
            if (view.selected < 0) return;
            juce::PopupMenu m;
            const auto cur = juce::roundToInt (processor.apvts.getRawParameterValue (EgretEqProcessor::bandId (view.selected, id))->load());
            for (int i = 0; i < labels.size(); ++i) m.addItem (i + 1, labels[i], true, i == cur);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&b),
                             [this, id] (int r)
                             {
                                 if (r <= 0 || view.selected < 0) return;
                                 processor.undo.beginNewTransaction();
                                 display.setBandParam (view.selected, id, (float) (r - 1));
                             });
        };
    };
    juce::StringArray shapes, slopeNames, places;
    for (auto* l : eq::shapeLabels) shapes.add (l);
    for (auto s : eq::slopes) slopeNames.add (juce::String (s) + " dB/oct");
    for (auto* l : placeNames) places.add (l);
    menu (shape, "Shape", shapes);
    menu (slope, "Slope", slopeNames);
    menu (place, "Place", places);
    bypass.onClick = [this]
    {
        if (view.selected < 0) return;
        processor.undo.beginNewTransaction();
        display.setBandParam (view.selected, "On", processor.readParams().bands[(size_t) view.selected].on ? 0.0f : 1.0f);
    };
    solo.onClick = [this] { if (view.selected >= 0) processor.soloBand = processor.soloBand == view.selected ? -1 : view.selected; };
    prev.onClick = [this] { if (onStep) onStep (-1); };
    next.onClick = [this] { if (onStep) onStep (1); };
    remove.onClick = [this] { display.deleteBand (view.selected); };
    setSize (406, 108);
}

void BandPanel::refresh (const eq::Params& p)
{
    if (view.selected < 0 || ! p.bands[(size_t) view.selected].used) return;
    const auto& b = p.bands[(size_t) view.selected];
    const auto colour = view.theme().band (view.selected);
    if (bound != view.selected)
    {
        bound = view.selected;
        freq.setParameter (processor.apvts.getParameter (EgretEqProcessor::bandId (bound, "Freq")));
        gain.setParameter (processor.apvts.getParameter (EgretEqProcessor::bandId (bound, "Gain")));
        q.setParameter (processor.apvts.getParameter (EgretEqProcessor::bandId (bound, "Q")));
    }
    for (auto* k : { &freq, &gain, &q }) k->setAccent (colour);
    for (auto* s : { &shape, &slope, &place }) s->setColour (juce::TextButton::buttonOnColourId, colour);
    shape.setButtonText (eq::shapeLabels[(int) b.shape]);
    slope.setButtonText (eq::hasSlope (b.shape) ? juce::String (b.slope) + " dB/oct" : utf8 ("\xe2\x80\x94"));
    slope.setEnabled (eq::hasSlope (b.shape));
    place.setButtonText (placeNames[(int) b.place]);
    gain.setEnabled (eq::hasGain (b.shape)); gain.setAlpha (eq::hasGain (b.shape) ? 1.0f : 0.35f);
    q.setEnabled (eq::hasQ (b.shape)); q.setAlpha (eq::hasQ (b.shape) ? 1.0f : 0.35f);
    bypass.setToggleState (b.on, juce::dontSendNotification);
    solo.setToggleState (processor.soloBand == view.selected, juce::dontSendNotification);
    solo.setEnabled (eq::canSolo (b.shape));
    repaint();
}

void BandPanel::paint (juce::Graphics& g)
{
    const auto& t = view.theme();
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto colour = view.selected >= 0 ? t.band (view.selected) : t.acc;
    g.setColour (t.floatShadow.withMultipliedAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0, 2), 8.0f);
    g.setColour (t.floatBg);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (t.floatBorder);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (r.withHeight (2.0f).reduced (4.0f, 0.0f), 1.0f);
    g.setFont (monoFont (10.0f).withExtraKerningFactor (0.08f));
    g.drawText ("BAND " + juce::String (view.selected + 1), juce::Rectangle<int> (10, 7, 104, 14), juce::Justification::centred, false);
}

void BandPanel::resized()
{
    auto col = juce::Rectangle<int> (10, 23, 104, 22);
    for (auto* b : { &shape, &slope, &place }) { b->setBounds (col); col.translate (0, 26); }
    auto kx = 122;
    for (auto* k : { &freq, &gain, &q }) { k->setBounds (kx, 18, 66, 68); kx += 70; }
    const auto bx = kx + 4;
    bypass.setBounds (bx, 22, 26, 22); solo.setBounds (bx + 30, 22, 26, 22);
    prev.setBounds (bx, 48, 26, 22); next.setBounds (bx + 30, 48, 26, 22);
    remove.setBounds (bx, 74, 56, 22);
}

//==============================================================================
EqDisplay::EqDisplay (EgretEqProcessor& p, EqView& v) : processor (p), view (v)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setTitle ("The EQ curve");
    setDescription ("Double-click to add a band, drag a dot to move it, scroll on it to change Q.");
    pull.resize (1 << 15);
    params = processor.readParams();
    rangeBox.view = &view;
    rangeBox.getProperties().set ("boxed", true);
    rangeBox.getProperties().set ("mono", true);
    rangeBox.getProperties().set ("accent", true);
    rangeBox.setTooltip ("Display range");
    rangeBox.setWantsKeyboardFocus (false);
    rangeBox.onClick = [this]
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Display range");
        static const int ranges[] = { 3, 6, 12, 30 };
        for (int i = 0; i < 4; ++i) m.addItem (i + 1, utf8 ("\xc2\xb1") + juce::String (ranges[i]) + " dB", true, view.dispRange == ranges[i]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rangeBox),
                         [this] (int r) { if (r > 0) { view.dispRange = ranges[r - 1]; if (onSelectionChanged) onSelectionChanged(); } });
    };
    addAndMakeVisible (rangeBox);
    panel = std::make_unique<BandPanel> (p, v, *this);
    addChildComponent (*panel);
}

namespace
{
    constexpr float viewTop = 10, viewBottom = 20;
    const double logMin = std::log (eq::freqMin), logSpan = std::log (eq::freqMax / eq::freqMin);
}

float EqDisplay::fx (double f) const { return (float) ((std::log (f) - logMin) / logSpan * plotWidth()); }
double EqDisplay::xf (float x) const { return std::exp (logMin + x / plotWidth() * logSpan); }
float EqDisplay::gy (double db) const { const auto R = view.dispRange; return (float) (viewTop + (R - db) / (2.0 * R) * (getHeight() - viewTop - viewBottom)); }
double EqDisplay::yg (float y) const { const auto R = view.dispRange; return R - (y - viewTop) / (getHeight() - viewTop - viewBottom) * 2.0 * R; }
float EqDisplay::ay (double db) const { return (float) (viewTop + (-db / view.anaRange) * (getHeight() - viewTop - viewBottom)); }

void EqDisplay::resized()
{
    rangeBox.setBounds (getWidth() - 12 - 54, 8, 54, 22);
    placePanel();
}

juce::Point<float> EqDisplay::nodePos (int i) const
{
    const auto& b = params.bands[(size_t) i];
    const auto x = fx (juce::jlimit (eq::freqMin, eq::freqMax, b.freq));
    const double R = view.dispRange;
    const auto y = eq::hasGain (b.shape) ? gy (juce::jlimit (-R, R, params.effectiveGain (i))) : gy (qNorm (b.q) * R * 0.5);
    return { x, y };
}

int EqDisplay::hitBand (juce::Point<float> p) const
{
    int best = -1;
    float bd = 12 * 12;
    for (int i = eq::maxBands - 1; i >= 0; --i)
    {
        if (! params.bands[(size_t) i].used) continue;
        const auto d = nodePos (i).getDistanceSquaredFrom (p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

juce::RangedAudioParameter* EqDisplay::bandParam (int band, const char* name) const
{
    return processor.apvts.getParameter (EgretEqProcessor::bandId (band, name));
}

void EqDisplay::setBandParam (int band, const char* name, float plainValue)
{
    auto* p = bandParam (band, name);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    p->endChangeGesture();
}

void EqDisplay::addBand (double freq, double gain, eq::Shape shape)
{
    int i = 0;
    while (i < eq::maxBands && params.bands[(size_t) i].used) ++i;
    if (i == eq::maxBands) { info = "All " + juce::String (eq::maxBands) + " bands are in use"; return; }
    processor.undo.beginNewTransaction();
    const auto cut = shape == eq::Shape::lowcut || shape == eq::Shape::highcut;
    setBandParam (i, "Used", 1); setBandParam (i, "On", 1); setBandParam (i, "Shape", (float) shape);
    setBandParam (i, "Freq", (float) freq); setBandParam (i, "Gain", eq::hasGain (shape) ? (float) gain : 0.0f);
    setBandParam (i, "Q", shape == eq::Shape::bell ? 1.0f : 0.71f); setBandParam (i, "Slope", cut ? 3.0f : 1.0f);
    setBandParam (i, "Place", 0);
    params = processor.readParams();
    view.selected = i;
    if (onSelectionChanged) onSelectionChanged();
}

void EqDisplay::deleteBand (int i)
{
    if (i < 0 || ! params.bands[(size_t) i].used) return;
    processor.undo.beginNewTransaction();
    for (auto* n : { "Used", "On", "Shape", "Freq", "Gain", "Q", "Slope", "Place" })
    {
        auto* p = bandParam (i, n);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
    if (processor.soloBand == i) processor.soloBand = -1;
    params = processor.readParams();
    if (view.selected == i) view.selected = -1;
    if (onSelectionChanged) onSelectionChanged();
}

void EqDisplay::computeCurves()
{
    const auto fs = processor.getSampleRateNow();
    const auto cols = juce::jmax (2, plotWidth());
    const auto fresh = cols != curveCols || fs != curveFs || params.gainScale != curveScale;
    curveCols = cols; curveFs = fs; curveScale = params.gainScale;
    total.assign ((size_t) cols, 0.0f);
    eq::Section secs[eq::maxSections];
    for (int i = 0; i < eq::maxBands; ++i)
    {
        const auto& b = params.bands[(size_t) i];
        if (! b.used) { curves[(size_t) i].clear(); continue; }
        auto key = b; key.on = true; key.place = eq::Place::stereo;
        const auto g = params.effectiveGain (i);
        if (fresh || curves[(size_t) i].empty() || ! (curveKeys[(size_t) i] == key) || curveGains[(size_t) i] != g)
        {
            curveKeys[(size_t) i] = key; curveGains[(size_t) i] = g;
            const auto n = eq::designBand (b.shape, b.freq, g, b.q, b.slope, fs, secs);
            auto& db = curves[(size_t) i];
            db.resize ((size_t) cols);
            for (int k = 0; k < cols; ++k)
            {
                const auto w = 2 * pi * juce::jmin (xf ((float) k), fs / 2) / fs;
                double m2 = 1;
                for (int s = 0; s < n; ++s) { const auto m = eq::sectionMag (secs[s], w); m2 *= m * m; }
                db[(size_t) k] = (float) (10 * std::log10 (juce::jmax (m2, 1e-30)));
            }
        }
        if (b.on)
            for (int k = 0; k < cols; ++k) total[(size_t) k] += curves[(size_t) i][(size_t) k];
    }
}

// Under the band, like Pro-Q's: centred on its dot, or at the top when the dot is low.
void EqDisplay::placePanel()
{
    const auto has = view.selected >= 0 && params.bands[(size_t) view.selected].used;
    panel->setVisible (has);
    if (! has) return;
    const auto p = nodePos (view.selected);
    const auto pw = panel->getWidth(), ph = panel->getHeight();
    const auto x = juce::jlimit (8, juce::jmax (8, plotWidth() - pw - 8), juce::roundToInt (p.x - pw / 2.0f));
    const auto y = p.y > getHeight() - viewBottom - ph - 30 ? 34 : juce::roundToInt (getHeight() - viewBottom - 8 - ph);
    panel->setTopLeftPosition (x, y);
}

void EqDisplay::tick (const eq::Params& p)
{
    params = p;
    for (auto* tap : { &processor.preTap, &processor.postTap })
    {
        auto& a = tap == &processor.preTap ? pre : post;
        const auto got = tap->pull (pull.data(), (int) pull.size());
        if (got > 0) a.push (pull.data(), got);
    }
    const auto size = anaSizes[juce::jlimit (0, 3, view.anaRes)];
    if (pre.getSize() != size) { pre.setSize (size); post.setSize (size); }
    if (! view.freeze && view.anaMode != 0)
    {
        pre.compute (anaSmooth[juce::jlimit (0, 4, view.anaSpeed)]);
        post.compute (anaSmooth[juce::jlimit (0, 4, view.anaSpeed)]);
    }
    rangeBox.setButtonText (juce::String (view.dispRange) + " dB");
    panel->refresh (params);
    placePanel();
    repaint();
}

juce::Path EqDisplay::spectrumPath (const std::vector<float>& db, double binHz, bool close) const
{
    const auto fs = processor.getSampleRateNow();
    const auto bottom = getHeight() - viewBottom;
    juce::Path path;
    path.startNewSubPath (0, bottom);
    float lastX = 0;
    const auto bins = (int) db.size();
    for (float x = 0; x <= plotWidth(); x += 2)
    {
        const auto f0 = xf (x - 1), f1 = xf (x + 1), k0 = f0 / binHz, k1 = f1 / binHz;
        if (f0 >= fs / 2) break;
        double v;
        if (k1 - k0 < 1)
        {
            const auto kc = juce::jmin ((double) bins - 2, (k0 + k1) / 2);
            const auto i0 = (int) std::floor (kc);
            const auto fr = kc - i0;
            v = db[(size_t) i0] * (1 - fr) + db[(size_t) i0 + 1] * fr;
        }
        else
        {
            v = -200;
            for (auto k = (int) std::ceil (k0); k <= juce::jmin (bins - 1, (int) std::floor (k1)); ++k) v = juce::jmax (v, (double) db[(size_t) k]);
        }
        v += anaCal + (view.anaTilt ? 4.5 * std::log2 (xf (x) / 1000) : 0);
        path.lineTo (x, juce::jlimit (viewTop, bottom, ay (v)));
        lastX = x;
    }
    path.lineTo (lastX, bottom);
    if (close) path.closeSubPath();
    return path;
}

void EqDisplay::paint (juce::Graphics& g)
{
    computeCurves();
    const auto& t = view.theme();
    const auto W = (float) getWidth(), H = (float) getHeight(), PW = (float) plotWidth();
    const auto fs = processor.getSampleRateNow();
    const double R = view.dispRange;
    const auto bottom = H - viewBottom;
    g.setGradientFill (juce::ColourGradient (t.bgTop, 0, 0, t.bgBottom, 0, H, false));
    g.fillAll();
    g.setColour (t.scaleBg);
    g.fillRect (PW, 0.0f, W - PW, H);
    const auto xn = fx (fs / 2);
    if (xn < PW) { g.setColour (t.nyquist); g.fillRect (xn, 0.0f, PW - xn, H); }

    // grid
    for (auto f : { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 })
    {
        g.setColour (f == 100 || f == 1000 || f == 10000 ? t.gridMajor : t.gridMinor);
        g.fillRect (std::round (fx (f)), 0.0f, 1.0f, bottom);
    }
    const auto step = R == 30 ? 5.0 : 1.0, labelStep = R == 12 ? 2.0 : R == 30 ? 5.0 : 1.0;
    for (auto d = -R; d <= R + 1e-9; d += step)
    {
        g.setColour (d == 0 ? t.zero : t.hline);
        g.fillRect (0.0f, std::round (gy (d)), PW, 1.0f);
    }
    // the scales: EQ dB, analyzer dB
    g.setFont (monoFont (10.0f));
    for (auto d = -R; d <= R + 1e-9; d += labelStep)
    {
        if (std::abs (d) == R) continue;
        g.setColour (d == 0 ? t.eqLabel0 : t.eqLabel);
        g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), juce::Rectangle<float> (PW, gy (d) - 6, 24, 12), juce::Justification::centredRight, false);
    }
    if (view.anaMode != 0)
    {
        g.setColour (t.anaLabel);
        for (int a = -10; a > -view.anaRange; a -= 10)
            if (a % (view.anaRange > 60 ? 20 : 10) == 0)
                g.drawText (juce::String (a), juce::Rectangle<float> (W - 34, ay (a) - 6, 30, 12), juce::Justification::centredRight, false);
    }
    g.setColour (t.freqLabel);
    const std::pair<int, const char*> labels[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" },
                                                   { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
    for (const auto& [f, text] : labels)
        g.drawText (text, juce::Rectangle<float> (fx (f) - 20, H - 16, 40, 12), juce::Justification::centred, false);

    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (juce::Rectangle<int> (0, 0, (int) PW, (int) H));
        // spectrum
        if (view.anaMode != 0)
        {
            const auto binHz = fs / pre.getSize();
            if (view.anaMode == 1 || view.anaMode == 3) { g.setColour (t.preFill); g.fillPath (spectrumPath (pre.getDb(), binHz, true)); }
            if (view.anaMode == 2 || view.anaMode == 3)
            {
                g.setColour (t.postFill); g.fillPath (spectrumPath (post.getDb(), binHz, true));
                g.setColour (t.postLine); g.strokePath (spectrumPath (post.getDb(), binHz, false), juce::PathStrokeType (1.0f));
            }
        }
        // each band's own curve, filled to 0 dB
        const auto y0 = gy (0);
        auto clipY = [&] (double v) { return juce::jlimit (-2.0f, bottom + 2, gy (v)); };
        for (int i = 0; i < eq::maxBands; ++i)
        {
            const auto& db = curves[(size_t) i];
            if (db.empty() || ! params.bands[(size_t) i].used) continue;
            const auto on = params.bands[(size_t) i].on, sel = i == view.selected || i == hover;
            juce::Path fill, line;
            fill.startNewSubPath (0, y0);
            for (int k = 0; k < curveCols; ++k)
            {
                const auto y = clipY (db[(size_t) k]);
                fill.lineTo ((float) k, y);
                if (k == 0) line.startNewSubPath ((float) k, y); else line.lineTo ((float) k, y);
            }
            fill.lineTo ((float) curveCols - 1, y0);
            fill.closeSubPath();
            const auto c = t.band (i);
            g.setColour (c.withAlpha (on ? (sel ? 0.28f : 0.12f) : 0.05f));
            g.fillPath (fill);
            g.setColour (c.withAlpha (on ? (sel ? 0.95f : 0.45f) : 0.25f));
            g.strokePath (line, juce::PathStrokeType (sel ? 1.4f : 1.0f));
        }
        // the sum, with a soft glow
        juce::Path sum;
        for (int k = 0; k < curveCols; ++k)
        {
            const auto y = clipY (total[(size_t) k]);
            if (k == 0) sum.startNewSubPath (0, y); else sum.lineTo ((float) k, y);
        }
        g.setColour (t.curveGlow.withMultipliedAlpha (0.35f));
        g.strokePath (sum, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (t.curve);
        g.strokePath (sum, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // dots, with badges for the stereo placement and solo
        static const char* letters[] = { "", "L", "R", "M", "S" };
        const auto badgeFont = monoFont (8.5f, true);
        for (int i = 0; i < eq::maxBands; ++i)
        {
            const auto& b = params.bands[(size_t) i];
            if (! b.used) continue;
            const auto p = nodePos (i);
            const auto c = t.band (i);
            if (i == view.selected) { g.setColour (t.ring); g.drawEllipse (juce::Rectangle<float> (21, 21).withCentre (p), 1.5f); }
            else if (i == hover) { g.setColour (t.ringHover); g.drawEllipse (juce::Rectangle<float> (20, 20).withCentre (p), 1.0f); }
            const auto dot = juce::Rectangle<float> (13, 13).withCentre (p);
            if (b.on) { g.setColour (c); g.fillEllipse (dot); g.setColour (t.dotEdge); g.drawEllipse (dot, 1.0f); }
            else { g.setColour (t.offFill); g.fillEllipse (dot); g.setColour (c); g.drawEllipse (dot, 1.5f); }
            juce::StringArray tags;
            if (b.place != eq::Place::stereo) tags.add (letters[(int) b.place]);
            if (processor.soloBand == i) tags.add ("SOLO");
            auto bx = p.x + 10;
            g.setFont (badgeFont);
            for (const auto& tag : tags)
            {
                const auto tw = juce::GlyphArrangement::getStringWidth (badgeFont, tag) + 7;
                const juce::Rectangle<float> box (bx, p.y - 6, tw, 12);
                g.setColour ((tag == "SOLO" ? t.solo : c).withAlpha (b.on ? 0.85f : 0.4f));
                g.fillRoundedRectangle (box, 3.0f);
                g.setColour (t.badgeText);
                g.drawText (tag, box, juce::Justification::centred, false);
                bx += tw + 3;
            }
        }
    }
    // the hovered or dragged band's numbers
    const auto show = drag.band >= 0 ? drag.band : hover;
    if (show >= 0 && params.bands[(size_t) show].used)
    {
        const auto& b = params.bands[(size_t) show];
        const auto p = nodePos (show);
        auto text = juce::String (show + 1) + "  " + eq::shapeLabels[(int) b.shape] + "  " + fmtHz (b.freq) + "  " + noteName (b.freq);
        if (eq::hasGain (b.shape)) text << "  " << EqKnob::format (EqKnob::Unit::dB, params.effectiveGain (show));
        if (eq::hasQ (b.shape)) text << "  Q " << EqKnob::format (EqKnob::Unit::q, b.q);
        const auto font = monoFont (10.5f);
        const auto tw = juce::GlyphArrangement::getStringWidth (font, text) + 14;
        const auto tx = juce::jlimit (2.0f, juce::jmax (2.0f, PW - tw - 2), p.x - tw / 2), ty = p.y > 44 ? p.y - 38 : p.y + 20;
        const juce::Rectangle<float> box (tx, ty, tw, 19);
        g.setColour (t.tipBg); g.fillRoundedRectangle (box, 4.0f);
        g.setColour (t.band (show)); g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (t.tipText); g.setFont (font);
        g.drawText (text, box.withTrimmedLeft (7), juce::Justification::centredLeft, false);
    }
    if (dragOver) { g.setColour (t.acc); g.drawRect (getLocalBounds().reduced (3), 2); }
    // past 24 bands, for a few seconds: the EQ may use more CPU
    if (juce::Time::getMillisecondCounterHiRes() < view.loadWarnUntil)
    {
        const juce::String text ("More than 24 bands: the EQ may use noticeably more CPU");
        const auto font = sansFont (11.5f);
        const auto w = juce::GlyphArrangement::getStringWidth (font, text) + 26;
        const juce::Rectangle<float> box ((PW - w) / 2, 8, w, 26);
        g.setColour (t.floatShadow); g.fillRoundedRectangle (box.translated (0, 2), 5.0f);
        g.setColour (t.floatSolid); g.fillRoundedRectangle (box, 5.0f);
        g.setColour (t.solo); g.drawRoundedRectangle (box, 5.0f, 1.0f); g.fillRect (box.withWidth (4.0f));
        g.setColour (t.fg); g.setFont (font);
        g.drawText (text, box.withTrimmedLeft (14), juce::Justification::centredLeft, false);
    }
}

void EqDisplay::setInfo (juce::Point<float>* p)
{
    auto used = false;
    for (const auto& b : params.bands) used = used || b.used;
    if (p == nullptr || p->x > plotWidth()) { info = used ? juce::String() : juce::String ("Double-click the display to add a band"); return; }
    const auto f = xf (p->x);
    info = fmtHz (f) + " " + utf8 ("\xc2\xb7") + " " + noteName (f) + " " + utf8 ("\xc2\xb7") + " " + EqKnob::format (EqKnob::Unit::dB, yg (p->y));
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitBand (e.position);
    if (h != hover) { hover = h; repaint(); }
    auto p = e.position;
    setInfo (&p);
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    if (drag.band < 0) { hover = -1; setInfo (nullptr); repaint(); }
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! e.mods.isLeftButtonDown()) return;
    const auto i = hitBand (e.position);
    if (i < 0)
    {
        if (view.selected >= 0 && e.getNumberOfClicks() == 1) { view.selected = -1; if (onSelectionChanged) onSelectionChanged(); }
        return;
    }
    if (e.mods.isAltDown()) { deleteBand (i); return; }
    if (i != view.selected)
    {
        view.selected = i;
        if (processor.soloBand >= 0 && processor.soloBand != i) processor.soloBand = -1;
        if (onSelectionChanged) onSelectionChanged();
    }
    processor.undo.beginNewTransaction();
    drag.band = i;
    drag.fx = fx (params.bands[(size_t) i].freq);
    drag.fy = nodePos (i).y;
    drag.last = e.position;
    for (auto* n : { "Freq", "Gain", "Q" }) bandParam (i, n)->beginChangeGesture();
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.band < 0) return;
    const auto k = e.mods.isShiftDown() ? 0.15f : 1.0f;
    drag.fx += (e.position.x - drag.last.x) * k;
    drag.fy += (e.position.y - drag.last.y) * k;
    drag.last = e.position;
    const auto i = drag.band;
    const auto shape = params.bands[(size_t) i].shape;
    const double R = view.dispRange;
    auto set = [this, i] (const char* n, double v) { auto* p = bandParam (i, n); p->setValueNotifyingHost (p->convertTo0to1 ((float) v)); };
    set ("Freq", juce::jlimit (eq::freqMin, eq::freqMax, xf (drag.fx)));
    if (eq::hasGain (shape))
    {
        const auto gn = juce::jlimit (-R, R, yg (drag.fy));
        if (params.gainScale > 0) set ("Gain", juce::jlimit (-eq::gainMax, eq::gainMax, gn * 100 / params.gainScale));
    }
    else if (eq::hasQ (shape))
        set ("Q", qFromNorm (yg (drag.fy) / (R * 0.5)));
    params = processor.readParams();
    placePanel();
    repaint();
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drag.band >= 0)
        for (auto* n : { "Freq", "Gain", "Q" }) bandParam (drag.band, n)->endChangeGesture();
    drag.band = -1;
    repaint();
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.x > plotWidth()) return;
    const auto i = hitBand (e.position);
    if (i >= 0)
    {
        processor.undo.beginNewTransaction();
        setBandParam (i, "On", params.bands[(size_t) i].on ? 0.0f : 1.0f);
        return;
    }
    const auto f = juce::jlimit (eq::freqMin, eq::freqMax, xf (e.position.x));
    const auto shape = f <= 25 ? eq::Shape::lowcut : f >= 18000 ? eq::Shape::highcut : eq::Shape::bell;
    const auto gn = std::round (yg (e.position.y) * 10) / 10;
    addBand (f, params.gainScale > 0 ? gn * 100 / params.gainScale : 0, shape);
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto i = hitBand (e.position);
    if (i < 0) i = view.selected;
    if (i < 0 || ! eq::hasQ (params.bands[(size_t) i].shape)) return;
    // The web page gets about 100 per notch; JUCE gives 0.25 or so.
    const auto delta = -(w.deltaY != 0 ? w.deltaY : w.deltaX) * 400.0f * (w.isReversed ? -1.0f : 1.0f);
    const auto k = std::exp (-delta * (e.mods.isShiftDown() ? 0.0003 : 0.0015));
    const auto q = juce::jlimit (eq::qMin, eq::qMax, params.bands[(size_t) i].q * k);
    if (i != view.selected) { view.selected = i; if (onSelectionChanged) onSelectionChanged(); }
    setBandParam (i, "Q", (float) q);
    params = processor.readParams();
    repaint();
}

bool EqDisplay::keyPressed (const juce::KeyPress& key)
{
    const auto i = view.selected;
    if (i < 0 || ! params.bands[(size_t) i].used) return false;
    const auto fine = key.getModifiers().isShiftDown() ? 0.2 : 1.0;
    const auto code = key.getKeyCode();
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteBand (i); return true; }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        processor.undo.beginNewTransaction();
        setBandParam (i, "Freq", (float) juce::jlimit (eq::freqMin, eq::freqMax, params.bands[(size_t) i].freq * std::pow (2.0, (code == juce::KeyPress::rightKey ? 1 : -1) / 24.0 * fine)));
        return true;
    }
    if ((code == juce::KeyPress::upKey || code == juce::KeyPress::downKey) && eq::hasGain (params.bands[(size_t) i].shape))
    {
        processor.undo.beginNewTransaction();
        setBandParam (i, "Gain", (float) juce::jlimit (-eq::gainMax, eq::gainMax, params.bands[(size_t) i].gain + (code == juce::KeyPress::upKey ? 0.5 : -0.5) * fine));
        return true;
    }
    return false;
}

void EqDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    if (! files.isEmpty() && onFile) onFile (juce::File (files[0]));
}

//==============================================================================
void MeterView::push (const std::array<float, 4>& m)
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    for (size_t i = 0; i < 4; ++i)
    {
        shown[i] = juce::jmax (m[i], shown[i] * 0.88f);
        if (m[i] >= hold[i] || now - holdTime[i] > 1500) { hold[i] = m[i]; holdTime[i] = now; }
    }
    const auto pk = juce::jmax (m[2], m[3]);
    if (pk >= peak || now - peakTime > 1500) { peak = pk; peakTime = now; }
    repaint();
}

void MeterView::paint (juce::Graphics& g)
{
    const auto& t = view.theme();
    const auto W = (float) getWidth(), H = (float) getHeight();
    g.fillAll (t.meterBg);
    g.setColour (t.edge);
    g.fillRect (0.0f, 0.0f, 1.0f, H);
    const auto top = 22.0f, bottom = H - viewBottom, span = bottom - top;
    auto my = [&] (float lin)
    {
        const auto db = lin > 1e-6f ? 20 * std::log10 (lin) : -120.0f;
        return top + juce::jlimit (0.0f, 1.0f, (6 - db) / 66) * span;
    };
    const float xs[] = { 8, 16 };
    for (int c = 0; c < 2; ++c)
    {
        const auto lvl = shown[(size_t) (2 + c)];
        const auto y = my (lvl), yZero = my (1);
        g.setColour (t.meterTrack);
        g.fillRect (xs[c], top, 6.0f, span);
        juce::ColourGradient gr (t.meterLow, 0, bottom, t.meterTop, 0, top, false);
        gr.addColour (0.75, t.meterMid);
        g.setGradientFill (gr);
        g.fillRect (xs[c], y, 6.0f, bottom - y);
        if (lvl > 1) { g.setColour (juce::Colour (0xffe0452a)); g.fillRect (xs[c], y, 6.0f, juce::jmax (0.0f, yZero - y)); }
        g.setColour (t.meterHold);
        g.fillRect (xs[c], my (hold[(size_t) (2 + c)]), 6.0f, 1.5f);
    }
    g.setColour (t.meterLine);
    g.fillRect (4.0f, my (1), W - 8, 1.0f);
    g.setColour (peak >= 1 ? juce::Colour (0xffe0452a) : t.meterText);
    g.setFont (monoFont (9.0f, true));
    g.drawText (peak > 1e-5f ? juce::String (20 * std::log10 (peak), 1) : utf8 ("\xe2\x80\x94"), juce::Rectangle<float> (0, 4, W, 14), juce::Justification::centred, false);
}

//==============================================================================
BirdView::BirdView()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Kawasemi, the kingfisher");
    setTooltip (utf8 ("\xe7\xbf\xa1\xe7\xbf\xa0"));
    setDark (false);
}

void BirdView::setDark (bool d)
{
    if (built && d == dark) return;
    built = true; dark = d;
    const auto svg = birdSvg (dark);
    drawable = juce::Drawable::createFromImageData (svg.toRawUTF8(), svg.getNumBytesAsUTF8());
    repaint();
}

void BirdView::paint (juce::Graphics& g)
{
    if (drawable != nullptr)
        drawable->drawWithin (g, getLocalBounds().toFloat(), juce::RectanglePlacement::centred, 1.0f);
}

// The dive: down and nose-first, back up, settle (the web page's keyframes).
void BirdView::tick()
{
    if (diveStart < 0) return;
    const auto t = (juce::Time::getMillisecondCounterHiRes() - diveStart) / 600.0;
    if (t >= 1) { diveStart = -1; setTransform ({}); return; }
    const double keys[][3] = { { 0, 0, 0 }, { 0.35, 6, 25 }, { 0.7, -3, -8 }, { 1, 0, 0 } };
    int k = 0;
    while (k < 2 && t > keys[k + 1][0]) ++k;
    const auto u = (t - keys[k][0]) / (keys[k + 1][0] - keys[k][0]);
    const auto dy = keys[k][1] + (keys[k + 1][1] - keys[k][1]) * u, deg = keys[k][2] + (keys[k + 1][2] - keys[k][2]) * u;
    const auto cx = getX() + getWidth() * 0.5f, cy = getY() + getHeight() * 0.9f;
    setTransform (juce::AffineTransform::rotation ((float) juce::degreesToRadians (deg), cx, cy).translated (0.0f, (float) dy));
}

//==============================================================================
EgretEqEditor::EgretEqEditor (EgretEqProcessor& p)
    : AudioProcessorEditor (p), audioProcessor (p), display (p, view), meter (view)
{
    setLookAndFeel (&lnf);
    panel.painter = [this] (juce::Graphics& g) { paintPanel (g); };
    addAndMakeVisible (panel);

    // the display settings kept with the project
    auto& ui = audioProcessor.uiState;
    view.anaMode = ui.getProperty ("anaMode", 3); view.anaRes = ui.getProperty ("anaRes", 2);
    view.anaSpeed = ui.getProperty ("anaSpeed", 2); view.anaRange = ui.getProperty ("anaRange", 90);
    view.dispRange = ui.getProperty ("dispRange", 12); view.anaTilt = ui.getProperty ("anaTilt", true);
    view.dark = ui.getProperty ("theme", "light").toString() == "dark";
    const auto params = audioProcessor.readParams();
    for (int i = 0; i < eq::maxBands && view.selected < 0; ++i) if (params.bands[(size_t) i].used) view.selected = i;

    // ---- top bar
    brandSub = subtitleText();
    bird.onClick = [this, safe = juce::Component::SafePointer<EgretEqEditor> (this)]
    {
        brandSub = birdNameText();
        bird.dive();
        panel.repaint (topBar);
        juce::Timer::callAfterDelay (2600, [safe] { if (safe != nullptr) { safe->brandSub = subtitleText(); safe->panel.repaint (safe->topBar); } });
    };
    panel.addAndMakeVisible (bird);
    for (auto* b : { &undoButton, &redoButton, &abButton, &copyButton, &presetPrev, &presetButton, &presetNext, &themeButton, &bypassButton,
                     &phaseButton, &anaButton, &sourceButton, &playButton, &autoButton })
    {
        b->view = &view;
        b->setWantsKeyboardFocus (false);
        panel.addAndMakeVisible (*b);
    }
    undoButton.icon = undoIcon(); undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.icon = undoIcon(); redoButton.icon.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, 8.0f, 8.0f)); redoButton.setTooltip ("Redo (Ctrl+Shift+Z)");
    undoButton.onClick = [this] { audioProcessor.undo.undo(); };
    redoButton.onClick = [this] { audioProcessor.undo.redo(); };
    abButton.getProperties().set ("dimPrefix", "A/B");
    abButton.setTooltip ("Switch between settings A and B");
    abButton.onClick = [this] { audioProcessor.switchAb (audioProcessor.getAbSlot() == 'A' ? 'B' : 'A'); selectionChanged(); };
    copyButton.setTooltip ("Copy this setting to the other one");
    copyButton.onClick = [this]
    {
        audioProcessor.copyAb();
        display.info = juce::String ("Copied ") + juce::String::charToString (audioProcessor.getAbSlot()) + " to "
                     + (audioProcessor.getAbSlot() == 'A' ? "B" : "A");
    };
    auto stepPreset = [this] (int d)
    {
        const auto nf = (int) eq::factoryPresets().size();
        const auto users = audioProcessor.getUserPresetNames();
        const auto total = nf + users.size();
        int index = d > 0 ? -1 : 0;
        for (int i = 0; i < nf; ++i) if (audioProcessor.getPresetName() == eq::factoryPresets()[(size_t) i].name) index = i;
        if (const auto u = users.indexOf (audioProcessor.getPresetName()); u >= 0) index = nf + u;
        index = ((index + d) % total + total) % total;
        if (index < nf) audioProcessor.loadFactoryPreset (index); else audioProcessor.loadUserPreset (users[index - nf]);
        selectionChanged();
    };
    presetPrev.setButtonText (utf8 ("\xe2\x80\xb9")); presetPrev.setTooltip ("Previous preset");
    presetNext.setButtonText (utf8 ("\xe2\x80\xba")); presetNext.setTooltip ("Next preset");
    presetPrev.onClick = [stepPreset] { stepPreset (-1); };
    presetNext.onClick = [stepPreset] { stepPreset (1); };
    presetButton.setTooltip ("Presets");
    presetButton.onClick = [this] { presetMenu(); };
    themeButton.onClick = [this] { view.dark = ! view.dark; saveView(); applyTheme(); };
    bypassButton.led = true;
    bypassButton.setTooltip ("Bypass: hear the sound without the EQ");
    {
        auto* power = audioProcessor.apvts.getParameter ("power");
        bypassAttachment = std::make_unique<juce::ParameterAttachment> (*power, [this] (float v)
        {
            bypassButton.setToggleState (v < 0.5f, juce::dontSendNotification);
        });
        bypassAttachment->sendInitialUpdate();
        bypassButton.onClick = [this] { bypassAttachment->setValueAsCompleteGesture (bypassButton.getToggleState() ? 1.0f : 0.0f); };
    }

    // ---- screen
    display.onSelectionChanged = [this] { selectionChanged(); };
    display.onFile = [this] (const juce::File& f) { loadFile (f); };
    display.panel->onStep = [this] (int d) { stepBand (d); };
    panel.addAndMakeVisible (display);
    panel.addAndMakeVisible (meter);

    // ---- bottom bar
    phaseButton.setTooltip ("Processing mode");
    phaseButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addSectionHeader ("Processing mode");
        m.addItem (1, "Zero Latency", true, true);
        m.addItem (2, "Natural Phase (coming)", false, false);
        m.addItem (3, "Linear Phase (coming)", false, false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&phaseButton));
    };
    anaButton.getProperties().set ("dimPrefix", "Analyzer:");
    anaButton.onClick = [this] { analyzerMenu(); };
    autoButton.setTooltip ("Keep the loudness the same while you EQ");
    autoButton.onClick = [this]
    {
        audioProcessor.undo.beginNewTransaction();
        auto* ag = audioProcessor.apvts.getParameter ("autoGain");
        ag->beginChangeGesture(); ag->setValueNotifyingHost (ag->getValue() > 0.5f ? 0.0f : 1.0f); ag->endChangeGesture();
    };
    scaleValue = std::make_unique<DragValue> ("Scale", EqKnob::Unit::pct, *audioProcessor.apvts.getParameter ("gainScale"), view);
    gainValue = std::make_unique<DragValue> ("Out", EqKnob::Unit::dB, *audioProcessor.apvts.getParameter ("outGain"), view);
    panValue = std::make_unique<DragValue> ("Pan", EqKnob::Unit::pan, *audioProcessor.apvts.getParameter ("outPan"), view);
    scaleValue->setTooltip ("Gain scale: drag, scroll, double-click to reset, click to type");
    gainValue->setTooltip ("Output gain: drag, scroll, double-click to reset, click to type");
    panValue->setTooltip ("Output pan: drag, scroll, double-click to reset, click to type");
    for (auto* d : { scaleValue.get(), gainValue.get(), panValue.get() }) panel.addAndMakeVisible (*d);
    // The standalone app's sources (the web page has them beside the panel).
    const auto standalone = EgretEqProcessor::isStandalone();
    sourceButton.setVisible (standalone);
    playButton.setVisible (standalone);
    sourceButton.getProperties().set ("dimPrefix", "Source:");
    sourceButton.onClick = [this] { sourceMenu(); };
    playButton.setTooltip ("Play or stop (Space)");
    playButton.onClick = [this] { audioProcessor.setPlaying (! audioProcessor.isPlaying()); };

    applyTheme();
    layoutPanel();
    selectionChanged();

    addMouseListener (this, true);   // every click starts a new undo step
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (panelWidth / 2, panelHeight / 2, panelWidth * 2, panelHeight * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) panelWidth / panelHeight);
    setSize (juce::roundToInt (panelWidth * defaultScale), juce::roundToInt (panelHeight * defaultScale));
    startTimerHz (60);
}

EgretEqEditor::~EgretEqEditor()
{
    setLookAndFeel (nullptr);
}

void EgretEqEditor::resized()
{
    const auto scale = (float) getWidth() / panelWidth;
    panel.setBounds (0, 0, panelWidth, panelHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));
}

void EgretEqEditor::mouseDown (const juce::MouseEvent&)
{
    audioProcessor.undo.beginNewTransaction();
}

// Space plays and stops (standalone); Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) undo and redo.
bool EgretEqEditor::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = key.getModifiers().isCommandDown();
    if (key.getKeyCode() == juce::KeyPress::spaceKey && EgretEqProcessor::isStandalone())
    {
        audioProcessor.setPlaying (! audioProcessor.isPlaying());
        return true;
    }
    if (cmd && (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z'))
    {
        if (key.getModifiers().isShiftDown()) audioProcessor.undo.redo(); else audioProcessor.undo.undo();
        return true;
    }
    if (cmd && (key.getKeyCode() == 'Y' || key.getKeyCode() == 'y')) { audioProcessor.undo.redo(); return true; }
    return false;
}

void EgretEqEditor::saveView()
{
    auto& ui = audioProcessor.uiState;
    ui.setProperty ("anaMode", view.anaMode, nullptr); ui.setProperty ("anaRes", view.anaRes, nullptr);
    ui.setProperty ("anaSpeed", view.anaSpeed, nullptr); ui.setProperty ("anaRange", view.anaRange, nullptr);
    ui.setProperty ("dispRange", view.dispRange, nullptr); ui.setProperty ("anaTilt", view.anaTilt, nullptr);
    ui.setProperty ("theme", view.dark ? "dark" : "light", nullptr);
}

void EgretEqEditor::applyTheme()
{
    lnf.setTheme (view.theme());
    themeButton.icon = view.dark ? sunIcon() : moonIcon();
    themeButton.setTooltip (view.dark ? "Light theme" : "Dark theme");
    bird.setDark (view.dark);
    sendLookAndFeelChange();
    panel.repaint();
}

void EgretEqEditor::loadFile (const juce::File& f)
{
    if (! EgretEqProcessor::isStandalone()) return;
    if (! audioProcessor.loadFile (f))
        display.info = "Couldn" + utf8 ("\xe2\x80\x99") + "t read " + f.getFileName();
}

void EgretEqEditor::selectionChanged()
{
    const auto p = audioProcessor.readParams();
    if (view.selected >= 0 && ! p.bands[(size_t) view.selected].used) view.selected = -1;
    saveView();
    display.tick (p);
    refreshAll();
}

void EgretEqEditor::stepBand (int d)
{
    const auto p = audioProcessor.readParams();
    std::vector<int> used;
    for (int i = 0; i < eq::maxBands; ++i) if (p.bands[(size_t) i].used) used.push_back (i);
    if (used.empty()) return;
    const auto n = (int) used.size();
    auto k = (int) (std::find (used.begin(), used.end(), view.selected) - used.begin());
    k = k >= n ? 0 : ((k + d) % n + n) % n;
    view.selected = used[(size_t) k];
    if (audioProcessor.soloBand >= 0 && audioProcessor.soloBand != view.selected) audioProcessor.soloBand = -1;
    selectionChanged();
}

void EgretEqEditor::presetMenu()
{
    const auto& factory = eq::factoryPresets();
    const auto users = audioProcessor.getUserPresetNames();
    const auto name = audioProcessor.getPresetName();
    const auto edited = audioProcessor.isPresetEdited();
    juce::PopupMenu m;
    m.addSectionHeader ("Factory");
    for (int i = 0; i < (int) factory.size(); ++i) m.addItem (i + 1, factory[(size_t) i].name, true, ! edited && name == factory[(size_t) i].name);
    if (! users.isEmpty())
    {
        m.addSectionHeader ("Your presets");
        for (int i = 0; i < users.size(); ++i) m.addItem (100 + i, users[i], true, ! edited && name == users[i]);
    }
    m.addSeparator();
    m.addItem (900, "Save as...");
    const auto isUser = users.contains (name, true);
    if (isUser) m.addItem (901, "Delete " + quoted (name));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [this, users, name, isUser] (int r)
    {
        if (r <= 0) return;
        if (r < 100) audioProcessor.loadFactoryPreset (r - 1);
        else if (r < 900) audioProcessor.loadUserPreset (users[r - 100]);
        else if (r == 900)
        {
            auto* w = new juce::AlertWindow ("Save preset", "Name for your preset:", juce::MessageBoxIconType::NoIcon, this);
            w->addTextEditor ("name", isUser ? name : juce::String(), {});
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
            {
                auto n = w->getTextEditorContents ("name").trim();
                if (result != 1) return;
                if (n.isEmpty()) n = "My preset " + juce::String (audioProcessor.getUserPresetNames().size() + 1);
                for (const auto& f : eq::factoryPresets()) if (n == f.name) n << " (mine)";
                display.info = audioProcessor.saveUserPreset (n) ? "Saved " + quoted (n) : juce::String ("Couldn't save the preset");
                refreshAll();
            }), true);
        }
        else if (r == 901)
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                "Delete your preset " + quoted (name) + "?", "Delete", "Cancel", this,
                juce::ModalCallbackFunction::create ([this, name] (int ok)
                {
                    if (ok == 0) return;
                    if (audioProcessor.deleteUserPreset (name)) audioProcessor.loadFactoryPreset (0);
                    selectionChanged();
                }));
        }
        selectionChanged();
    });
}

void EgretEqEditor::analyzerMenu()
{
    const auto pre = view.anaMode == 1 || view.anaMode == 3, post = view.anaMode == 2 || view.anaMode == 3;
    juce::PopupMenu m;
    m.addSectionHeader ("Show");
    m.addItem (1, "Pre (input)", true, pre);
    m.addItem (2, "Post (output)", true, post);
    m.addSeparator();
    m.addSectionHeader ("Resolution");
    const char* res[] = { "Low", "Medium", "High", "Maximum" };
    for (int i = 0; i < 4; ++i) m.addItem (10 + i, res[i], true, view.anaRes == i);
    m.addSectionHeader ("Speed");
    const char* speeds[] = { "Very fast", "Fast", "Medium", "Slow", "Very slow" };
    for (int i = 0; i < 5; ++i) m.addItem (20 + i, speeds[i], true, view.anaSpeed == i);
    m.addSectionHeader ("Range");
    static const int ranges[] = { 60, 90, 120 };
    for (int i = 0; i < 3; ++i) m.addItem (30 + i, juce::String (ranges[i]) + " dB", true, view.anaRange == ranges[i]);
    m.addSeparator();
    m.addItem (40, "Tilt 4.5 dB/oct", true, view.anaTilt);
    m.addItem (41, "Freeze", true, view.freeze);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anaButton), [this, pre, post] (int r)
    {
        if (r <= 0) return;
        auto p = pre, q = post;
        if (r == 1) p = ! p;
        if (r == 2) q = ! q;
        if (r <= 2) view.anaMode = p && q ? 3 : p ? 1 : q ? 2 : 0;
        if (r >= 10 && r < 14) view.anaRes = r - 10;
        if (r >= 20 && r < 25) view.anaSpeed = r - 20;
        if (r >= 30 && r < 33) view.anaRange = ranges[r - 30];
        if (r == 40) view.anaTilt = ! view.anaTilt;
        if (r == 41) view.freeze = ! view.freeze;
        saveView();
        refreshAll();
    });
}

void EgretEqEditor::sourceMenu()
{
    juce::PopupMenu m;
    const char* names[] = { "Demo loop", "File", "Audio input", "Pink noise" };
    for (int i = 0; i < 4; ++i) m.addItem (i + 1, names[i], true, (int) audioProcessor.getSource() == i);
    m.addSeparator();
    m.addItem (10, "Load a file...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sourceButton), [this] (int r)
    {
        if (r >= 1 && r <= 4)
        {
            audioProcessor.setSource ((EgretEqProcessor::Source) (r - 1));
            if (r != 2 || audioProcessor.getFileName().isNotEmpty()) { audioProcessor.setPlaying (true); return; }
        }
        if (r == 10 || r == 2)
        {
            chooser = std::make_unique<juce::FileChooser> ("Play a sound through the EQ", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
        }
    });
}

void EgretEqEditor::refreshAll()
{
    const auto p = audioProcessor.readParams();
    undoButton.setEnabled (audioProcessor.undo.canUndo());
    redoButton.setEnabled (audioProcessor.undo.canRedo());
    const auto slot = juce::String::charToString (audioProcessor.getAbSlot());
    abButton.setButtonText (slot);
    copyButton.setButtonText ("Copy " + slot + utf8 ("\xe2\x86\x92") + (audioProcessor.getAbSlot() == 'A' ? "B" : "A"));
    presetButton.setButtonText (audioProcessor.getPresetName() + (audioProcessor.isPresetEdited() ? " (edited)" : ""));
    static const char* modes[] = { "Off", "Pre", "Post", "Pre+Post" };
    anaButton.setButtonText (juce::String (modes[juce::jlimit (0, 3, view.anaMode)]) + (view.freeze ? " (frozen)" : ""));
    autoButton.setToggleState (p.autoGain, juce::dontSendNotification);
    autoButton.setButtonText (p.autoGain ? "Auto Gain " + EqKnob::format (EqKnob::Unit::dB, eq::autoGainDb (p, audioProcessor.getSampleRateNow())) : juce::String ("Auto Gain"));
    if (EgretEqProcessor::isStandalone())
    {
        static const char* srcNames[] = { "Demo", "File", "Input", "Noise" };
        const auto src = (int) audioProcessor.getSource();
        sourceButton.setButtonText (src == 1 && audioProcessor.getFileName().isNotEmpty() ? audioProcessor.getFileName() : juce::String (srcNames[src]));
        playButton.setButtonText (audioProcessor.isPlaying() ? "Stop" : "Play");
        playButton.setToggleState (audioProcessor.isPlaying(), juce::dontSendNotification);
    }
    layoutPanel();
}

void EgretEqEditor::timerCallback()
{
    const auto p = audioProcessor.readParams();
    int count = 0;
    for (const auto& b : p.bands) count += b.used ? 1 : 0;
    if (lastBandCount >= 0 && count > eq::bandsLight && lastBandCount <= eq::bandsLight)
        view.loadWarnUntil = juce::Time::getMillisecondCounterHiRes() + 5000;
    if (count <= eq::bandsLight) view.loadWarnUntil = 0;
    lastBandCount = count;
    display.tick (p);
    meter.push (audioProcessor.takeMeters());
    bird.tick();
    refreshAll();
    for (auto* d : { scaleValue.get(), gainValue.get(), panValue.get() }) d->repaint();
    if (display.info != shownInfo)
    {
        shownInfo = display.info;
        panel.repaint (bottomBar);
    }
}

//==============================================================================
void EgretEqEditor::paintPanel (juce::Graphics& g)
{
    const auto& t = view.theme();
    g.fillAll (t.scr);
    // the bars
    g.setGradientFill (juce::ColourGradient (t.barHi, 0, 0, t.bar, 0, (float) topH, false));
    g.fillRect (topBar);
    g.setColour (t.edge);
    g.fillRect (0, topH - 1, panelWidth, 1);
    g.setGradientFill (juce::ColourGradient (t.bar, 0, (float) bottomBar.getY(), t.barLo, 0, (float) panelHeight, false));
    g.fillRect (bottomBar);
    g.setColour (t.edge);
    g.fillRect (0, bottomBar.getY(), panelWidth, 1);

    // the logo: EGRET EQ and the subtitle
    auto x = 10.0f;
    const auto logoFont = sansFont (21.0f, true);
    g.setFont (logoFont);
    g.setColour (t.fg);
    g.drawText ("EGRET", juce::Rectangle<float> (x, 0, 90, (float) topH), juce::Justification::centredLeft, false);
    x += juce::GlyphArrangement::getStringWidth (logoFont, "EGRET ");
    g.setColour (t.acc);
    g.drawText ("EQ", juce::Rectangle<float> (x, 0, 40, (float) topH), juce::Justification::centredLeft, false);
    x += juce::GlyphArrangement::getStringWidth (logoFont, "EQ") + 8;
    g.setColour (t.fgSoft);
    g.setFont (monoFont (9.0f).withExtraKerningFactor (0.14f));
    g.drawText (brandSub, juce::Rectangle<float> (x, 2, 170, (float) topH), juce::Justification::centredLeft, false);

    // the hint in the bottom bar
    g.setColour (t.fgSoft);
    g.setFont (monoFont (10.5f));
    const auto infoX = (EgretEqProcessor::isStandalone() ? playButton.getRight() : anaButton.getRight()) + 10;
    g.drawText (display.info, juce::Rectangle<int> (infoX, bottomBar.getY(), juce::jmax (0, autoButton.getX() - infoX - 10), bottomH),
                juce::Justification::centredLeft, true);
}

void EgretEqEditor::layoutPanel()
{
    topBar = { 0, 0, panelWidth, topH };
    bottomBar = { 0, topH + screenH, panelWidth, bottomH };
    // top: logo, undo, redo, A/B, copy | preset | theme, power
    const auto logoFont = sansFont (21.0f, true);
    auto x = 10 + juce::roundToInt (juce::GlyphArrangement::getStringWidth (logoFont, "EGRET EQ")) + 8;
    x += juce::roundToInt (juce::GlyphArrangement::getStringWidth (monoFont (9.0f).withExtraKerningFactor (0.14f), subtitleText())) + 4;
    x += 10;   // the subtitle's font may draw a little wider than measured
    bird.setBounds (x, 8, 22, 22);
    x += 32;
    auto place = [&x] (juce::Component& c, int w) { c.setBounds (x, 7, w, 24); x += w + 4; };
    place (undoButton, 30); place (redoButton, 30); x += 4;
    place (abButton, 48); place (copyButton, 84);
    const auto presetW = 220, cx = 610;
    presetPrev.setBounds (cx - presetW / 2 - 26, 7, 24, 24);
    presetButton.setBounds (cx - presetW / 2, 7, presetW, 24);
    presetNext.setBounds (cx + presetW / 2 + 2, 7, 24, 24);
    bypassButton.setBounds (panelWidth - 10 - 90, 7, 90, 24);
    themeButton.setBounds (bypassButton.getX() - 34, 7, 30, 24);

    // the screen
    display.setBounds (0, topH, panelWidth - meterW, screenH);
    meter.setBounds (panelWidth - meterW, topH, meterW, screenH);

    // bottom: mode, analyzer, (source, play) | auto gain, scale, out, pan
    const auto by = bottomBar.getY() + 4, bh = bottomH - 8;
    auto bx = 10;
    phaseButton.setBounds (bx, by, 100, bh); bx += 104;
    const auto anaW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (sansFont (12.0f), "Analyzer: " + anaButton.getButtonText())) + 18;
    anaButton.setBounds (bx, by, anaW, bh); bx += anaW + 4;
    if (EgretEqProcessor::isStandalone())
    {
        const auto sw = juce::jmin (180, juce::roundToInt (juce::GlyphArrangement::getStringWidth (sansFont (12.0f), "Source: " + sourceButton.getButtonText())) + 18);
        sourceButton.setBounds (bx, by, sw, bh); bx += sw + 4;
        playButton.setBounds (bx, by, 48, bh);
    }
    auto rx = panelWidth - 10;
    for (auto* d : { panValue.get(), gainValue.get(), scaleValue.get() })
    {
        const auto w = d->preferredWidth();
        d->setBounds (rx - w, by, w, bh);
        rx -= w + 4;
    }
    const auto aw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (sansFont (12.0f), autoButton.getButtonText())) + 18;
    autoButton.setBounds (rx - aw, by, aw, bh);
}
