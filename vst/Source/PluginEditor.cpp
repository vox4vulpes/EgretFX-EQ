#include "PluginEditor.h"

using namespace EgretColours;

namespace
{
    // The panel at the web page's size; the window scales it.
    constexpr int panelWidth = 900, panelHeight = 1060;
    constexpr float defaultScale = 0.82f;
    constexpr int pad = 20, knobW = 52, knobH = 66;
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
    juce::String quoted (const juce::String& s) { return utf8 ("\xe2\x80\x9c") + s + utf8 ("\xe2\x80\x9d"); }
    juce::String minus() { return utf8 ("\xe2\x88\x92"); }

    constexpr const char* birdSvg = R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" width="24" height="24"> <path d="M6 9.5c0-2.6 2.1-4.5 4.7-4.5 2.3 0 4 1.5 4.4 3.5l.4 2.3c.4 2.2 1.9 3.6 3.9 4.6l-1.2.9c-1.1-.2-2.2-.6-3.1-1.2-.9 2.3-3.3 3.6-6 3.1-2.4-.5-3.9-2.6-3.6-5z" fill="#2a8fc4" stroke="#2a241d" stroke-width=".7" stroke-linejoin="round"/> <path d="M7.4 11.4c.6 2.6 2.6 4.3 5.4 3.9 1-.2 1.8-.6 2.4-1.3-1.9.2-4.4-.6-5.9-2.7z" fill="#e07b2a"/> <path d="M6.6 9.6c.6.6 1.6.8 2.4.5-.2.7-.9 1.2-1.7 1.2-.5-.4-.7-1-.7-1.7z" fill="#fbf8ef"/> <path d="M6.2 8.4L1 9.3l5.4.5z" fill="#2a241d"/> <circle cx="8.2" cy="7.6" r=".55" fill="#2a241d"/> <path d="M11 17.6l-.4 2.6M12.6 17.7l.2 2.5" stroke="#d9452f" stroke-width=".9" stroke-linecap="round"/> </svg>)svg";
    juce::String subtitleText() { return utf8 ("\xe7\xbf\xa1\xe7\xbf\xa0 \xc2\xb7 PARAMETRIC EQUALIZER"); }
    juce::String birdNameText() { return utf8 ("\xe3\x82\xab\xe3\x83\xaf\xe3\x82\xbb\xe3\x83\x9f \xc2\xb7 KAWASEMI \xc2\xb7 KINGFISHER"); }

    juce::String fmtHz (double v)
    {
        return v >= 10000 ? juce::String (v / 1000, 1) + "k" : v >= 1000 ? juce::String (v / 1000, 2) + "k"
             : v >= 100 ? juce::String (juce::roundToInt (v)) + "Hz" : juce::String (v, 1) + "Hz";
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

    void layoutKnobs (juce::Rectangle<int> area, std::initializer_list<juce::Component*> list)
    {
        const auto n = (int) list.size();
        auto row = area.withWidth (n * knobW + (n - 1) * 8).withHeight (knobH);
        for (auto* c : list) { c->setBounds (row.removeFromLeft (knobW)); row.removeFromLeft (8); }
    }
}

juce::Colour EgretColours::band (int i)
{
    static const juce::uint32 colours[] = { 0xfff0a830, 0xff7ec4be, 0xffe8846a, 0xffb6a5d0, 0xffa9c27e, 0xffe6cf5a,
                                            0xff6fb7d6, 0xffe793b6, 0xffd2a77c, 0xff93d493, 0xffc9c39a, 0xfff08a7a };
    return juce::Colour (colours[i % 12]);
}
juce::Colour EgretColours::bandInk (int i)
{
    const auto c = band (i);
    return juce::Colour ((juce::uint8) juce::roundToInt (c.getRed() * 0.62f), (juce::uint8) juce::roundToInt (c.getGreen() * 0.62f),
                         (juce::uint8) juce::roundToInt (c.getBlue() * 0.62f));
}

//==============================================================================
EgretLookAndFeel::EgretLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::outlineColourId, grooveDark);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::arrowColourId, ink);
    setColour (juce::ComboBox::focusedOutlineColourId, teal);
    setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::headerTextColourId, inkSoft);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, mustard);
    setColour (juce::PopupMenu::highlightedTextColourId, panelRaised);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, panelRaised);
    setColour (juce::TextButton::buttonOnColourId, mustard);
    setColour (juce::TextEditor::backgroundColourId, panel);
    setColour (juce::TextEditor::outlineColourId, grooveDark);
    setColour (juce::TextEditor::focusedOutlineColourId, teal);
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::highlightColourId, mustard.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, ink);
    setColour (juce::Label::textColourId, inkSoft);
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId, ink);
    setColour (juce::TooltipWindow::outlineColourId, grooveDark);
}

void EgretLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto area = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 2.0f;
    const auto centre = area.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto arcRadius = radius - 3.0f;
    const juce::PathStrokeType arcStroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    g.setColour (grooveDark.withAlpha (0.25f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (grooveDark);
    g.strokePath (track, arcStroke);

    const auto capRadius = radius * 0.72f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setColour (shadow);
    g.fillEllipse (cap.translated (0.0f, 1.5f));
    g.setGradientFill (juce::ColourGradient (panelRaised, centre.x, centre.y - capRadius * 0.5f,
                                             groove, centre.x, centre.y + capRadius * 1.3f, true));
    g.fillEllipse (cap);
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -capRadius * 0.85f, 3.0f, capRadius * 0.55f, 1.5f);
    g.setColour (accent);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void EgretLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool isMouseOver, bool isDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();
    const auto accent = button.findColour (juce::TextButton::buttonOnColourId);
    const auto alpha = button.isEnabled() ? 1.0f : 0.45f;
    auto fill = on ? accent : panel;
    if (! on && button.isEnabled() && (isMouseOver || isDown))
        fill = fill.brighter (0.15f);
    g.setColour (fill.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour ((on ? accent : grooveDark).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

juce::Font EgretLookAndFeel::getTextButtonFont (juce::TextButton& b, int) { return sansFont (b.getHeight() < 24 ? 10.5f : 11.0f, true); }
juce::Font EgretLookAndFeel::getComboBoxFont (juce::ComboBox&) { return sansFont (11.5f, true); }
void EgretLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    LookAndFeel_V4::positionComboBoxText (box, label);
    label.setMinimumHorizontalScale (0.6f);
}

//==============================================================================
EqKnob::EqKnob (const juce::String& text, Unit u, juce::Colour accent) : unit (u)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setMouseDragSensitivity (150);   // the web knob: 150 px for the whole range
    slider.setWantsKeyboardFocus (false);
    slider.onValueChange = [this] { updateReadout(); };
    addAndMakeVisible (slider);
    label.setText (text.toUpperCase(), juce::dontSendNotification);
    label.setFont (sansFont (9.0f, true));
    readout.setFont (monoFont (10.0f));
    for (auto* l : { &label, &readout })
    {
        l->setJustificationType (juce::Justification::centred);
        l->setBorderSize ({});
        addAndMakeVisible (*l);
    }
    label.setInterceptsMouseClicks (false, false);
    readout.setMouseCursor (juce::MouseCursor::IBeamCursor);
    readout.setTooltip ("Click to type a value");
    readout.addMouseListener (this, false);
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
}

juce::String EqKnob::format (Unit u, double v)
{
    switch (u)
    {
        case Unit::hz: return fmtHz (v);
        case Unit::dB:
        {
            const auto r = std::round (v * 10) / 10;
            return (r > 0 ? "+" : r < 0 ? minus() : juce::String()) + juce::String (std::abs (r), 1) + "dB";
        }
        case Unit::q:   return juce::String (v, v < 10 ? 2 : 1);
        case Unit::pan: return std::abs (v) < 0.5 ? juce::String ("C") : (v < 0 ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v)));
        case Unit::pct: break;
    }
    return juce::String (juce::roundToInt (v)) + "%";
}

void EqKnob::updateReadout() { readout.setText (format (unit, slider.getValue()), juce::dontSendNotification); }

void EqKnob::resized()
{
    auto area = getLocalBounds();
    readout.setBounds (area.removeFromBottom (13));
    label.setBounds (area.removeFromBottom (13));
    slider.setBounds (area.reduced (1));
    if (typing != nullptr)
        typing->setBounds (readout.getBounds().expanded (3, 1));
}

void EqKnob::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &readout && readout.contains (e.getEventRelativeTo (&readout).getPosition()))
        startTyping();
}

// A typed value: a number with an optional unit -- 2.5k, 250, -3, 0.7, L20 --
// clamped to the range. Enter or clicking away sets it, Escape keeps the old one.
void EqKnob::startTyping()
{
    if (typing != nullptr || param == nullptr)
        return;
    typing = std::make_unique<juce::TextEditor>();
    typing->setFont (monoFont (10.0f));
    typing->setJustification (juce::Justification::centred);
    typing->setIndents (1, 0);
    typing->setColour (juce::TextEditor::outlineColourId, teal);
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
        auto text = typing->getText().trim().toLowerCase().replaceCharacter (',', '.').replace (minus(), "-");
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->typing.reset(); });
        if (! apply || param == nullptr)
            return;
        double v;
        const auto range = param->getNormalisableRange();
        if (unit == Unit::pan && (text == "c" || text.startsWith ("l") || text.startsWith ("r")))
        {
            if (text == "c") v = 0;
            else
            {
                const auto num = text.substring (1).trim();
                if (! num.containsOnly (".0123456789") || num.isEmpty()) return;
                v = (text[0] == 'l' ? -1 : 1) * num.getDoubleValue();
            }
        }
        else
        {
            const auto number = text.initialSectionContainingOnly ("+-.0123456789");
            const auto unitText = text.substring (number.length()).trim();
            if (! number.containsAnyOf ("0123456789")) return;
            if (! juce::StringArray { "", "k", "khz", "hz", "db", "%" }.contains (unitText)) return;
            v = number.getDoubleValue();
            if (unitText == "k" || unitText == "khz") v *= 1000;
        }
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
ChipRow::ChipRow (const juce::StringArray& labels, juce::Colour accent)
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setColour (juce::TextButton::buttonOnColourId, accent);
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, i] { if (onPick) onPick (i); refresh(); };
        addAndMakeVisible (b);
    }
}

void ChipRow::refresh()
{
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (isOn != nullptr && isOn (i), juce::dontSendNotification);
}

void ChipRow::resized()
{
    const auto n = buttons.size(), perRow = columns > 0 ? columns : n, rows = (n + perRow - 1) / perRow;
    const auto w = (getWidth() - (perRow - 1) * 4) / (float) perRow;
    const auto h = (getHeight() - (rows - 1) * 4) / (float) rows;
    for (int i = 0; i < n; ++i)
    {
        const auto c = i % perRow, r = i / perRow;
        buttons[i]->setBounds (juce::roundToInt (c * (w + 4)), juce::roundToInt (r * (h + 4)), juce::roundToInt (w), juce::roundToInt (h));
    }
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
}

namespace
{
    constexpr float viewTop = 8, viewBottom = 18;
    const double logMin = std::log (eq::freqMin), logSpan = std::log (eq::freqMax / eq::freqMin);
}

float EqDisplay::fx (double f) const { return (float) ((std::log (f) - logMin) / logSpan * getWidth()); }
double EqDisplay::xf (float x) const { return std::exp (logMin + x / getWidth() * logSpan); }
float EqDisplay::gy (double db) const { const auto R = view.dispRange; return (float) (viewTop + (R - db) / (2.0 * R) * (getHeight() - viewTop - viewBottom)); }
double EqDisplay::yg (float y) const { const auto R = view.dispRange; return R - (y - viewTop) / (getHeight() - viewTop - viewBottom) * 2.0 * R; }
float EqDisplay::ay (double db) const { return (float) (viewTop + (-db / view.anaRange) * (getHeight() - viewTop - viewBottom)); }

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
    if (i == eq::maxBands) return;
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
    if (view.selected == i)
    {
        view.selected = -1;
        for (int k = 0; k < eq::maxBands; ++k) if (params.bands[(size_t) k].used) { view.selected = k; break; }
    }
    if (onSelectionChanged) onSelectionChanged();
}

void EqDisplay::computeCurves()
{
    const auto fs = processor.getSampleRateNow();
    const auto cols = juce::jmax (2, getWidth());
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

void EqDisplay::tick (const eq::Params& p)
{
    params = p;
    // Analyzer: take what the audio thread sent, one frame per tick.
    for (auto* pair : { &processor.preTap, &processor.postTap })
    {
        auto& a = pair == &processor.preTap ? pre : post;
        const auto got = pair->pull (pull.data(), (int) pull.size());
        if (got > 0) a.push (pull.data(), got);
    }
    const auto size = anaSizes[juce::jlimit (0, 3, view.anaRes)];
    if (pre.getSize() != size) { pre.setSize (size); post.setSize (size); }
    if (! view.freeze && view.anaMode != 0)
    {
        pre.compute (anaSmooth[juce::jlimit (0, 4, view.anaSpeed)]);
        post.compute (anaSmooth[juce::jlimit (0, 4, view.anaSpeed)]);
    }
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
    for (float x = 0; x <= getWidth(); x += 2)
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
    const auto W = (float) getWidth(), H = (float) getHeight();
    const auto fs = processor.getSampleRateNow();
    const double R = view.dispRange;
    const auto bottom = H - viewBottom;
    g.setColour (readoutBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // above Nyquist: nothing to hear
    const auto xn = fx (fs / 2);
    if (xn < W) { g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRect (xn, 0.0f, W - xn, H); }

    // grid
    for (auto f : { 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000 })
    {
        const auto major = f == 100 || f == 1000 || f == 10000;
        g.setColour (readoutInk.withAlpha (major ? 0.16f : 0.07f));
        g.fillRect (std::round (fx (f)), viewTop, 1.0f, bottom - viewTop);
    }
    const auto step = R == 3 ? 1.0 : R == 6 ? 2.0 : R == 12 ? 3.0 : 6.0;
    for (auto d = -R; d <= R + 1e-9; d += step)
    {
        g.setColour (readoutInk.withAlpha (d == 0 ? 0.32f : 0.08f));
        g.fillRect (0.0f, std::round (gy (d)), W, 1.0f);
    }
    g.setFont (monoFont (9.0f));
    g.setColour (readoutInk.withAlpha (0.5f));
    for (auto d = -R; d <= R + 1e-9; d += step)
        if (d != -R && d != R)
            g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), juce::Rectangle<float> (W - 44, gy (d) - 6, 40, 12), juce::Justification::centredRight, false);
    if (view.anaMode != 0)
    {
        g.setColour (screenTeal.withAlpha (0.4f));
        for (int a = -30; a > -view.anaRange; a -= 30)
            g.drawText (juce::String (a), juce::Rectangle<float> (4, ay (a) - 6, 40, 12), juce::Justification::centredLeft, false);
    }
    g.setColour (readoutInk.withAlpha (0.55f));
    const std::pair<int, const char*> labels[] = { { 20, "20" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" },
                                                   { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" }, { 20000, "20k" } };
    for (const auto& [f, text] : labels)
        g.drawText (text, juce::Rectangle<float> (fx (f) - 20, H - 15, 40, 12), juce::Justification::centred, false);

    // spectrum
    if (view.anaMode != 0)
    {
        const auto binHz = fs / pre.getSize();
        if (view.anaMode == 1 || view.anaMode == 3)
        {
            g.setColour (screenTeal.withAlpha (0.16f));
            g.fillPath (spectrumPath (pre.getDb(), binHz, true));
        }
        if (view.anaMode == 2 || view.anaMode == 3)
        {
            g.setColour (readoutInk.withAlpha (0.14f));
            g.fillPath (spectrumPath (post.getDb(), binHz, true));
            g.setColour (readoutInk.withAlpha (0.45f));
            g.strokePath (spectrumPath (post.getDb(), binHz, false), juce::PathStrokeType (1.0f));
        }
    }

    // each band's own curve, filled to 0 dB
    const auto y0 = gy (0);
    auto clip = [&] (double v) { return juce::jlimit (viewTop - 2, bottom + 2, gy (v)); };
    for (int i = 0; i < eq::maxBands; ++i)
    {
        const auto& db = curves[(size_t) i];
        if (db.empty() || ! params.bands[(size_t) i].used) continue;
        const auto on = params.bands[(size_t) i].on, sel = i == view.selected;
        juce::Path fill, line;
        fill.startNewSubPath (0, y0);
        for (int k = 0; k < curveCols; ++k)
        {
            const auto y = clip (db[(size_t) k]);
            fill.lineTo ((float) k, y);
            if (k == 0) line.startNewSubPath ((float) k, y); else line.lineTo ((float) k, y);
        }
        fill.lineTo ((float) curveCols - 1, y0);
        fill.closeSubPath();
        g.setColour (band (i).withAlpha (on ? (sel ? 0.24f : 0.11f) : 0.05f));
        g.fillPath (fill);
        if (sel)
        {
            g.setColour (band (i).withAlpha (on ? 0.9f : 0.4f));
            g.strokePath (line, juce::PathStrokeType (1.2f));
        }
    }
    // the sum, with a soft glow
    {
        juce::Path sum;
        for (int k = 0; k < curveCols; ++k)
        {
            const auto y = clip (total[(size_t) k]);
            if (k == 0) sum.startNewSubPath (0, y); else sum.lineTo ((float) k, y);
        }
        g.setColour (panelRaised.withAlpha (0.12f));
        g.strokePath (sum, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (panelRaised);
        g.strokePath (sum, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // dots
    for (int i = 0; i < eq::maxBands; ++i)
    {
        const auto& b = params.bands[(size_t) i];
        if (! b.used) continue;
        const auto p = nodePos (i);
        const auto c = band (i);
        if (i == view.selected) { g.setColour (panelRaised); g.drawEllipse (juce::Rectangle<float> (22, 22).withCentre (p), 1.5f); }
        else if (i == hover) { g.setColour (panelRaised.withAlpha (0.45f)); g.drawEllipse (juce::Rectangle<float> (21, 21).withCentre (p), 1.0f); }
        const auto dot = juce::Rectangle<float> (15, 15).withCentre (p);
        if (b.on) { g.setColour (c); g.fillEllipse (dot); }
        else { g.setColour (readoutBg); g.fillEllipse (dot); g.setColour (c); g.drawEllipse (dot, 1.5f); }
        g.setColour (b.on ? readoutBg : c);
        g.setFont (monoFont (9.0f, true));
        g.drawText (juce::String (i + 1), dot.expanded (4).translated (0, 0.5f), juce::Justification::centred, false);
        if (b.place != eq::Place::stereo)
        {
            static const char* letters[] = { "", "L", "R", "M", "S" };
            g.setColour (c);
            g.setFont (monoFont (8.0f, true));
            g.drawText (letters[(int) b.place], juce::Rectangle<float> (20, 10).withCentre (p.translated (0, 16)), juce::Justification::centred, false);
        }
        if (processor.soloBand == i)
        {
            g.setColour (mustard);
            g.setFont (monoFont (8.0f, true));
            g.drawText ("SOLO", juce::Rectangle<float> (40, 10).withCentre (p.translated (0, -17)), juce::Justification::centred, false);
        }
    }
    // the hovered or dragged band's numbers
    const auto show = drag.band >= 0 ? drag.band : hover;
    if (show >= 0 && params.bands[(size_t) show].used)
    {
        const auto& b = params.bands[(size_t) show];
        const auto p = nodePos (show);
        auto text = juce::String (show + 1) + " " + eq::shapeLabels[(int) b.shape] + "  " + fmtHz (b.freq) + "  " + noteName (b.freq);
        if (eq::hasGain (b.shape)) text << "  " << EqKnob::format (EqKnob::Unit::dB, params.effectiveGain (show));
        if (eq::hasQ (b.shape)) text << "  Q " << EqKnob::format (EqKnob::Unit::q, b.q);
        const auto font = monoFont (10.0f);
        const auto tw = juce::GlyphArrangement::getStringWidth (font, text) + 12;
        const auto tx = juce::jlimit (2.0f, W - tw - 2, p.x - tw / 2), ty = p.y > 40 ? p.y - 36 : p.y + 20;
        const juce::Rectangle<float> box (tx, ty, tw, 17);
        g.setColour (readoutBg.withAlpha (0.88f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (band (show));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (panelRaised);
        g.setFont (font);
        g.drawText (text, box.withTrimmedLeft (6), juce::Justification::centredLeft, false);
    }
    if (dragOver)
    {
        g.setColour (teal);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1), 6.0f, 2.0f);
    }
}

void EqDisplay::setInfo (juce::Point<float>* p)
{
    auto used = false;
    for (const auto& b : params.bands) used = used || b.used;
    if (p == nullptr) { info = used ? "DRAG A DOT " + utf8 ("\xc2\xb7") + " SCROLL FOR Q" : "DOUBLE-CLICK TO ADD A BAND"; return; }
    const auto f = xf (p->x);
    info = fmtHz (f).toUpperCase() + " " + utf8 ("\xc2\xb7") + " " + noteName (f) + " " + utf8 ("\xc2\xb7") + " " + EqKnob::format (EqKnob::Unit::dB, yg (p->y)).toUpperCase();
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
    const auto W = (float) getWidth(), H = (float) getHeight();
    g.setColour (readoutBg);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const auto top = viewTop, bottom = H - viewBottom, span = bottom - top;
    auto my = [&] (float lin)
    {
        const auto db = lin > 1e-6f ? 20 * std::log10 (lin) : -120.0f;
        return top + juce::jlimit (0.0f, 1.0f, (6 - db) / 66) * span;
    };
    const float xs[] = { 4, 11, 24, 31 };
    g.setColour (readoutInk.withAlpha (0.1f));
    for (auto x : xs) g.fillRect (x, top, 6.0f, span);
    for (size_t i = 0; i < 4; ++i)
    {
        const auto y = my (shown[i]), yZero = my (1);
        g.setColour (i < 2 ? screenTeal.withAlpha (0.75f) : readoutInk.withAlpha (0.85f));
        g.fillRect (xs[i], y, 6.0f, bottom - y);
        if (shown[i] > 1) { g.setColour (hot); g.fillRect (xs[i], y, 6.0f, juce::jmax (0.0f, yZero - y)); }
        g.setColour (panelRaised);
        g.fillRect (xs[i], my (hold[i]), 6.0f, 1.5f);
    }
    g.setColour (readoutInk.withAlpha (0.3f));
    g.fillRect (2.0f, my (1), W - 4, 1.0f);
    g.setFont (monoFont (8.0f));
    g.setColour (readoutInk.withAlpha (0.55f));
    g.drawText ("IN", juce::Rectangle<float> (0, H - 14, 20, 10), juce::Justification::centred, false);
    g.drawText ("OUT", juce::Rectangle<float> (19, H - 14, 24, 10), juce::Justification::centred, false);
}

//==============================================================================
BirdView::BirdView()
{
    drawable = juce::Drawable::createFromImageData (birdSvg, std::strlen (birdSvg));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Kawasemi, the kingfisher");
    setTooltip (utf8 ("\xe7\xbf\xa1\xe7\xbf\xa0"));
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

void PowerButton::paintButton (juce::Graphics& g, bool isMouseOver, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (isMouseOver ? panelRaised.brighter (0.1f) : panelRaised);
    g.fillRoundedRectangle (r, r.getHeight() / 2);
    g.setColour (grooveDark);
    g.drawRoundedRectangle (r, r.getHeight() / 2, 1.0f);
    const auto led = juce::Rectangle<float> (10, 10).withCentre ({ r.getX() + 20, r.getCentreY() });
    if (getToggleState())
    {
        g.setColour (coral.withAlpha (0.35f));
        g.fillEllipse (led.expanded (3));
    }
    g.setColour (getToggleState() ? coral : grooveDark);
    g.fillEllipse (led);
    g.setColour (ink);
    g.setFont (sansFont (11.0f, true).withExtraKerningFactor (0.08f));
    g.drawText ("POWER", r.withTrimmedLeft (32).withTrimmedRight (12), juce::Justification::centredLeft, false);
}

//==============================================================================
EgretEqEditor::EgretEqEditor (EgretEqProcessor& p)
    : AudioProcessorEditor (p), audioProcessor (p), display (p, view),
      shapes ([] { juce::StringArray s; for (auto* l : eq::shapeLabels) s.add (l); return s; }(), teal),
      slopes ([] { juce::StringArray s; for (auto v : eq::slopes) s.add (juce::String (v)); return s; }(), teal),
      places ([] { juce::StringArray s; for (auto* l : eq::placeLabels) s.add (l); return s; }(), teal),
      freqKnob ("Freq", EqKnob::Unit::hz, teal), gainKnob ("Gain", EqKnob::Unit::dB, teal), qKnob ("Q", EqKnob::Unit::q, teal),
      sources ({ "DEMO", "FILE", "INPUT", "NOISE" }, coral),
      anaMode ({ "OFF", "PRE", "POST", "BOTH" }, lavender), anaRes ({ "LOW", "MED", "HIGH", "MAX" }, lavender),
      anaSpeed ({ "V.FAST", "FAST", "MED", "SLOW", "V.SLOW" }, lavender), anaRange ({ "60", "90", "120" }, lavender),
      dispRange ({ "3", "6", "12", "30" }, lavender),
      scaleKnob ("Scale", EqKnob::Unit::pct, mustard), outGainKnob ("Gain", EqKnob::Unit::dB, mustard), panKnob ("Pan", EqKnob::Unit::pan, mustard)
{
    setLookAndFeel (&lnf);
    panel.painter = [this] (juce::Graphics& g) { paintPanel (g); };
    addAndMakeVisible (panel);

    // the display settings kept with the project
    auto& ui = audioProcessor.uiState;
    view.anaMode = ui.getProperty ("anaMode", 3); view.anaRes = ui.getProperty ("anaRes", 2);
    view.anaSpeed = ui.getProperty ("anaSpeed", 2); view.anaRange = ui.getProperty ("anaRange", 90);
    view.dispRange = ui.getProperty ("dispRange", 12); view.anaTilt = ui.getProperty ("anaTilt", true);
    const auto params = audioProcessor.readParams();
    for (int i = 0; i < eq::maxBands && view.selected < 0; ++i) if (params.bands[(size_t) i].used) view.selected = i;

    // ---- header
    brandSub = subtitleText();
    bird.onClick = [this, safe = juce::Component::SafePointer<EgretEqEditor> (this)]
    {
        brandSub = birdNameText();
        bird.dive();
        panel.repaint (headerArea);
        juce::Timer::callAfterDelay (2600, [safe] { if (safe != nullptr) { safe->brandSub = subtitleText(); safe->panel.repaint (safe->headerArea); } });
    };
    panel.addAndMakeVisible (bird);
    powerAttachment = std::make_unique<juce::ButtonParameterAttachment> (*audioProcessor.apvts.getParameter ("power"), power);
    power.setTooltip ("Off passes the sound through untouched");
    panel.addAndMakeVisible (power);

    // ---- presets, undo, A/B
    presetBox.onChange = [this]
    {
        const auto id = presetBox.getSelectedId();
        if (id <= 0 || id == 999) return;
        if (id < 100) audioProcessor.loadFactoryPreset (id - 1);
        else if (! audioProcessor.loadUserPreset (userPresets[id - 100])) presetStatus.setText ("Couldn't read that preset", juce::dontSendNotification);
        selectionChanged();
        refreshPresetBox();
    };
    auto step = [this] (int d)
    {
        const auto nf = (int) eq::factoryPresets().size();
        const auto total = nf + userPresets.size();
        const auto id = presetBox.getSelectedId();
        auto index = id >= 100 && id != 999 ? nf + id - 100 : id >= 1 && id < 100 ? id - 1 : 0;
        index = ((index + d) % total + total) % total;
        if (index < nf) audioProcessor.loadFactoryPreset (index);
        else audioProcessor.loadUserPreset (userPresets[index - nf]);
        selectionChanged();
        refreshPresetBox();
    };
    presetPrev.onClick = [step] { step (-1); };
    presetNext.onClick = [step] { step (1); };
    presetName.setTextToShowWhenEmpty ("Preset name", inkSoft);
    presetName.setInputRestrictions (32);
    presetName.setFont (sansFont (12.0f));
    presetName.setIndents (8, 6);
    presetName.onReturnKey = [this] { savePreset(); };
    presetSave.onClick = [this] { savePreset(); };
    presetDelete.onClick = [this] { deletePreset(); };
    presetStatus.setFont (monoFont (10.5f));
    presetStatus.setJustificationType (juce::Justification::centredRight);
    undoButton.setTooltip ("Undo (Ctrl+Z)"); redoButton.setTooltip ("Redo (Ctrl+Shift+Z)");
    undoButton.onClick = [this] { audioProcessor.undo.undo(); };
    redoButton.onClick = [this] { audioProcessor.undo.redo(); };
    abA.onClick = [this] { audioProcessor.switchAb ('A'); selectionChanged(); };
    abB.onClick = [this] { audioProcessor.switchAb ('B'); selectionChanged(); };
    abCopy.setTooltip ("Copy this setting to the other one");
    abCopy.onClick = [this]
    {
        audioProcessor.copyAb();
        const auto cur = juce::String::charToString (audioProcessor.getAbSlot());
        const juce::String other = audioProcessor.getAbSlot() == 'A' ? "B" : "A";
        presetStatus.setText ("Copied " + cur + " to " + other, juce::dontSendNotification);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &presetPrev, &presetNext, &presetName, &presetSave, &presetDelete, &presetStatus,
                                                             &undoButton, &redoButton, &abA, &abB, &abCopy })
        panel.addAndMakeVisible (*c);
    for (auto* b : { &presetPrev, &presetNext, &presetSave, &presetDelete, &undoButton, &redoButton, &abA, &abB, &abCopy })
    {
        b->setColour (juce::TextButton::buttonOnColourId, sage);
        b->setWantsKeyboardFocus (false);
    }

    // ---- display and meters
    display.onSelectionChanged = [this] { selectionChanged(); };
    display.onFile = [this] (const juce::File& f) { loadFile (f); };
    panel.addAndMakeVisible (display);
    panel.addAndMakeVisible (meter);

    // ---- band
    for (int i = 0; i < eq::maxBands; ++i)
    {
        auto* b = bandChips.add (new juce::TextButton (juce::String (i + 1)));
        b->setWantsKeyboardFocus (false);
        b->onClick = [this, i]
        {
            view.selected = i;
            if (audioProcessor.soloBand >= 0 && audioProcessor.soloBand != i) audioProcessor.soloBand = -1;
            selectionChanged();
        };
        panel.addChildComponent (b);
    }
    addChip.setTooltip ("Add a band");
    addChip.setWantsKeyboardFocus (false);
    addChip.onClick = [this] { display.addBand (1000, 0, eq::Shape::bell); };
    panel.addAndMakeVisible (addChip);
    auto bandValue = [this] (const char* n) { return audioProcessor.apvts.getRawParameterValue (EgretEqProcessor::bandId (view.selected, n))->load(); };
    shapes.setColumns (5);
    shapes.isOn = [this, bandValue] (int i) { return view.selected >= 0 && juce::roundToInt (bandValue ("Shape")) == i; };
    shapes.onPick = [this] (int i) { if (view.selected >= 0) { audioProcessor.undo.beginNewTransaction(); display.setBandParam (view.selected, "Shape", (float) i); } };
    slopes.isOn = [this, bandValue] (int i) { return view.selected >= 0 && juce::roundToInt (bandValue ("Slope")) == i; };
    slopes.onPick = [this] (int i) { if (view.selected >= 0) { audioProcessor.undo.beginNewTransaction(); display.setBandParam (view.selected, "Slope", (float) i); } };
    places.isOn = [this, bandValue] (int i) { return view.selected >= 0 && juce::roundToInt (bandValue ("Place")) == i; };
    places.onPick = [this] (int i) { if (view.selected >= 0) { audioProcessor.undo.beginNewTransaction(); display.setBandParam (view.selected, "Place", (float) i); } };
    for (auto* b : shapes.buttons) b->setColour (juce::TextButton::buttonOnColourId, teal);
    for (auto [l, t] : { std::pair { &shapeLabel, "SHAPE" }, std::pair { &slopeLabel, "SLOPE" }, std::pair { &placeLabel, "STEREO" } })
    {
        l->setText (t, juce::dontSendNotification);
        l->setFont (monoFont (9.5f).withExtraKerningFactor (0.12f));
        l->setBorderSize ({});
        panel.addAndMakeVisible (*l);
    }
    bandEmpty.setText ("No band selected. Double-click the display to add one, or click a dot.", juce::dontSendNotification);
    bandEmpty.setFont (sansFont (12.0f));
    bandEmpty.setBorderSize ({});
    panel.addChildComponent (bandEmpty);
    for (auto* c : std::initializer_list<juce::Component*> { &shapes, &slopes, &places, &freqKnob, &gainKnob, &qKnob, &bypassButton, &soloButton, &deleteButton })
        panel.addAndMakeVisible (*c);
    bypassButton.setTooltip ("Switch the band off (or double-click its dot)");
    soloButton.setTooltip ("Hear only what this band works on");
    deleteButton.setTooltip ("Remove the band (Delete, or Alt-click its dot)");
    bypassButton.setColour (juce::TextButton::buttonOnColourId, teal);
    soloButton.setColour (juce::TextButton::buttonOnColourId, mustard);
    for (auto* b : { &bypassButton, &soloButton, &deleteButton }) b->setWantsKeyboardFocus (false);
    bypassButton.onClick = [this]
    {
        if (view.selected < 0) return;
        audioProcessor.undo.beginNewTransaction();
        display.setBandParam (view.selected, "On", audioProcessor.readParams().bands[(size_t) view.selected].on ? 0.0f : 1.0f);
    };
    soloButton.onClick = [this] { if (view.selected >= 0) audioProcessor.soloBand = audioProcessor.soloBand == view.selected ? -1 : view.selected; };
    deleteButton.onClick = [this] { display.deleteBand (view.selected); };

    // ---- source (the standalone app's), analyzer, output
    const auto standalone = EgretEqProcessor::isStandalone();
    sources.isOn = [this, standalone] (int i) { return standalone && (int) audioProcessor.getSource() == i; };
    sources.onPick = [this] (int i)
    {
        audioProcessor.setSource ((EgretEqProcessor::Source) i);
        if (i == (int) EgretEqProcessor::Source::file && audioProcessor.getFileName().isEmpty()) loadButton.triggerClick();
    };
    playButton.setTooltip ("Play or stop (Space)");
    playButton.setColour (juce::TextButton::buttonOnColourId, coral);
    playButton.onClick = [this] { audioProcessor.setPlaying (! audioProcessor.isPlaying()); };
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Play a sound through the EQ", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
    };
    sourceStatus.setFont (monoFont (10.5f));
    sourceStatus.setColour (juce::Label::textColourId, ink);
    sourceStatus.setBorderSize ({});
    for (auto* c : std::initializer_list<juce::Component*> { &sources, &playButton, &loadButton, &sourceStatus }) panel.addAndMakeVisible (*c);
    if (! standalone)
    {
        sources.setEnabled (false); playButton.setVisible (false); loadButton.setVisible (false);
        sourceStatus.setText ("In a DAW the EQ works on its track", juce::dontSendNotification);
    }

    auto uiRow = [this] (ChipRow& row, int& field, std::vector<int> values)
    {
        row.isOn = [&field, values] (int i) { return field == values[(size_t) i]; };
        row.onPick = [this, &field, values] (int i) { field = values[(size_t) i]; saveView(); };
        panel.addAndMakeVisible (row);
    };
    uiRow (anaMode, view.anaMode, { 0, 1, 2, 3 });
    uiRow (anaRes, view.anaRes, { 0, 1, 2, 3 });
    uiRow (anaSpeed, view.anaSpeed, { 0, 1, 2, 3, 4 });
    uiRow (anaRange, view.anaRange, { 60, 90, 120 });
    uiRow (dispRange, view.dispRange, { 3, 6, 12, 30 });
    const char* rowNames[] = { "SHOW", "RES", "SPEED", "RANGE", "EQ" };
    for (int i = 0; i < 5; ++i)
    {
        anaLabels[i].setText (i == 4 ? utf8 ("EQ \xc2\xb1") : juce::String (rowNames[i]), juce::dontSendNotification);
        anaLabels[i].setFont (monoFont (9.5f).withExtraKerningFactor (0.12f));
        anaLabels[i].setBorderSize ({});
        panel.addAndMakeVisible (anaLabels[i]);
    }
    tiltButton.setTooltip ("Tilt the spectrum 4.5 dB per octave, so music looks flat");
    freezeButton.setTooltip ("Hold the spectrum still");
    for (auto* b : { &tiltButton, &freezeButton }) { b->setColour (juce::TextButton::buttonOnColourId, lavender); b->setWantsKeyboardFocus (false); panel.addAndMakeVisible (*b); }
    tiltButton.onClick = [this] { view.anaTilt = ! view.anaTilt; saveView(); };
    freezeButton.onClick = [this] { view.freeze = ! view.freeze; };

    scaleKnob.setParameter (audioProcessor.apvts.getParameter ("gainScale"));
    outGainKnob.setParameter (audioProcessor.apvts.getParameter ("outGain"));
    panKnob.setParameter (audioProcessor.apvts.getParameter ("outPan"));
    autoButton.setTooltip ("Keep the loudness the same while you EQ");
    autoButton.setColour (juce::TextButton::buttonOnColourId, mustard);
    autoButton.setWantsKeyboardFocus (false);
    autoButton.onClick = [this]
    {
        audioProcessor.undo.beginNewTransaction();
        auto* ag = audioProcessor.apvts.getParameter ("autoGain");
        ag->beginChangeGesture(); ag->setValueNotifyingHost (ag->getValue() > 0.5f ? 0.0f : 1.0f); ag->endChangeGesture();
    };
    autoReadout.setFont (monoFont (10.5f));
    autoReadout.setColour (juce::Label::textColourId, ink);
    for (auto* c : std::initializer_list<juce::Component*> { &scaleKnob, &outGainKnob, &panKnob, &autoButton, &autoReadout }) panel.addAndMakeVisible (*c);

    userPresets = audioProcessor.getUserPresetNames();
    layoutPanel();
    selectionChanged();
    refreshPresetBox();

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
    refreshAll();
}

void EgretEqEditor::loadFile (const juce::File& f)
{
    if (! EgretEqProcessor::isStandalone()) return;
    if (! audioProcessor.loadFile (f))
        sourceStatus.setText ("Couldn" + utf8 ("\xe2\x80\x99") + "t read " + f.getFileName(), juce::dontSendNotification);
}

//==============================================================================
void EgretEqEditor::selectionChanged()
{
    const auto p = audioProcessor.readParams();
    if (view.selected >= 0 && ! p.bands[(size_t) view.selected].used) view.selected = -1;
    const auto has = view.selected >= 0;
    if (has)
    {
        freqKnob.setParameter (audioProcessor.apvts.getParameter (EgretEqProcessor::bandId (view.selected, "Freq")));
        gainKnob.setParameter (audioProcessor.apvts.getParameter (EgretEqProcessor::bandId (view.selected, "Gain")));
        qKnob.setParameter (audioProcessor.apvts.getParameter (EgretEqProcessor::bandId (view.selected, "Q")));
    }
    shownParams = {};   // force refreshAll to redo everything
    shownSelected = -2;
    refreshAll();
}

void EgretEqEditor::layoutStrip()
{
    const auto p = audioProcessor.readParams();
    const auto titleW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (sansFont (14.0f, true).withExtraKerningFactor (0.12f), bandTitle)) + 14;
    auto x = bandArea.getX() + 12 + titleW;
    const auto y = bandArea.getY() + 8;
    for (int i = 0; i < eq::maxBands; ++i)
    {
        auto* b = bandChips[i];
        const auto used = p.bands[(size_t) i].used;
        b->setVisible (used);
        if (! used) continue;
        b->setBounds (x, y, i < 9 ? 34 : 40, 22);
        x += b->getWidth() + 4;
        b->setToggleState (i == view.selected, juce::dontSendNotification);
        b->setColour (juce::TextButton::buttonOnColourId, readoutBg);
        b->setColour (juce::TextButton::textColourOnId, band (i));
        b->setColour (juce::TextButton::textColourOffId, p.bands[(size_t) i].on ? bandInk (i) : bandInk (i).withAlpha (0.5f));
    }
    addChip.setBounds (x, y, 28, 22);
}

void EgretEqEditor::refreshAll()
{
    const auto p = audioProcessor.readParams();
    const auto solo = audioProcessor.soloBand.load();
    if (view.selected >= 0 && ! p.bands[(size_t) view.selected].used) { view.selected = -1; }
    const auto has = view.selected >= 0;
    if (p != shownParams || view.selected != shownSelected || solo != shownSolo)
    {
        const auto selChanged = view.selected != shownSelected;
        shownParams = p; shownSelected = view.selected; shownSolo = solo;
        bandTitle = has ? "BAND " + juce::String (view.selected + 1) : "BAND";
        bandAccent = has ? bandInk (view.selected) : teal;
        bandEmpty.setVisible (! has);
        for (auto* c : std::initializer_list<juce::Component*> { &shapes, &slopes, &places, &freqKnob, &gainKnob, &qKnob, &bypassButton, &soloButton, &deleteButton, &shapeLabel, &slopeLabel, &placeLabel })
            c->setVisible (has);
        if (has)
        {
            const auto& b = p.bands[(size_t) view.selected];
            slopes.setAlpha (eq::hasSlope (b.shape) ? 1.0f : 0.42f); slopes.setEnabled (eq::hasSlope (b.shape));
            slopeLabel.setAlpha (eq::hasSlope (b.shape) ? 1.0f : 0.42f);
            gainKnob.setAlpha (eq::hasGain (b.shape) ? 1.0f : 0.42f); gainKnob.setEnabled (eq::hasGain (b.shape));
            qKnob.setAlpha (eq::hasQ (b.shape) ? 1.0f : 0.42f); qKnob.setEnabled (eq::hasQ (b.shape));
            bypassButton.setToggleState (! b.on, juce::dontSendNotification);
            soloButton.setToggleState (solo == view.selected, juce::dontSendNotification);
            soloButton.setEnabled (eq::canSolo (b.shape));
            if (selChanged)
            {
                freqKnob.setParameter (audioProcessor.apvts.getParameter (EgretEqProcessor::bandId (view.selected, "Freq")));
                gainKnob.setParameter (audioProcessor.apvts.getParameter (EgretEqProcessor::bandId (view.selected, "Gain")));
                qKnob.setParameter (audioProcessor.apvts.getParameter (EgretEqProcessor::bandId (view.selected, "Q")));
            }
        }
        layoutStrip();
        panel.repaint (bandArea);
    }
    for (auto* row : { &shapes, &slopes, &places, &sources, &anaMode, &anaRes, &anaSpeed, &anaRange, &dispRange }) row->refresh();
    tiltButton.setToggleState (view.anaTilt, juce::dontSendNotification);
    freezeButton.setToggleState (view.freeze, juce::dontSendNotification);
    autoButton.setToggleState (p.autoGain, juce::dontSendNotification);
    autoReadout.setText (p.autoGain ? EqKnob::format (EqKnob::Unit::dB, eq::autoGainDb (p, audioProcessor.getSampleRateNow())) : juce::String(), juce::dontSendNotification);
    undoButton.setEnabled (audioProcessor.undo.canUndo());
    redoButton.setEnabled (audioProcessor.undo.canRedo());
    abA.setToggleState (audioProcessor.getAbSlot() == 'A', juce::dontSendNotification);
    abB.setToggleState (audioProcessor.getAbSlot() == 'B', juce::dontSendNotification);
    abCopy.setButtonText (audioProcessor.getAbSlot() == 'A' ? utf8 ("A\xe2\x86\x92" "B") : utf8 ("B\xe2\x86\x92" "A"));
    if (EgretEqProcessor::isStandalone())
    {
        const auto playing = audioProcessor.isPlaying();
        playButton.setToggleState (playing, juce::dontSendNotification);
        playButton.setButtonText (playing ? "STOP" : "PLAY");
        const auto src = audioProcessor.getSource();
        const auto status = src == EgretEqProcessor::Source::demo ? juce::String ("Demo loop, 112 BPM")
                          : src == EgretEqProcessor::Source::noise ? juce::String ("Pink noise")
                          : src == EgretEqProcessor::Source::file ? (audioProcessor.getFileName().isEmpty() ? juce::String ("No file yet") : audioProcessor.getFileName())
                          : juce::String ("Microphone or line in");
        if (! sourceStatus.getText().startsWith ("Couldn") || src != EgretEqProcessor::Source::file)
            sourceStatus.setText (status, juce::dontSendNotification);
    }
}

void EgretEqEditor::timerCallback()
{
    const auto p = audioProcessor.readParams();
    display.tick (p);
    meter.push (audioProcessor.takeMeters());
    bird.tick();
    refreshAll();

    int bands = 0;
    for (const auto& b : p.bands) bands += b.used ? 1 : 0;
    const auto pk = meter.getPeakHold();
    const auto peakText = pk > 1e-5f ? juce::String (20 * std::log10 (pk), 1) : utf8 ("\xe2\x80\x94");
    if (bands != shownBands || peakText != shownPeak)
    {
        shownBands = bands; shownPeak = peakText;
        panel.repaint (headerArea);
    }
    if (display.info != shownInfo)
    {
        shownInfo = display.info;
        panel.repaint (screenBay.withTrimmedTop (screenBay.getHeight() - 22));
    }
    const auto sig = audioProcessor.getPresetName() + (audioProcessor.isPresetEdited() ? "*" : "") + juce::String (userPresets.size());
    if (sig != shownPresetSig)
        refreshPresetBox();
}

//==============================================================================
void EgretEqEditor::refreshPresetBox()
{
    const auto name = audioProcessor.getPresetName();
    const auto edited = audioProcessor.isPresetEdited();
    const auto& factory = eq::factoryPresets();
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Factory");
    for (int i = 0; i < (int) factory.size(); ++i)
        presetBox.addItem (factory[(size_t) i].name, i + 1);
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("Your presets");
        for (int i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i], 100 + i);
    }
    int selected = 999;
    if (! edited)
    {
        for (int i = 0; i < (int) factory.size(); ++i)
            if (name == factory[(size_t) i].name) selected = i + 1;
        if (const auto i = userPresets.indexOf (name, true); i >= 0) selected = 100 + i;
    }
    if (selected == 999)
        presetBox.addItem (name + " (edited)", 999);
    presetBox.setSelectedId (selected, juce::dontSendNotification);
    const auto isUser = userPresets.contains (name, true);
    presetDelete.setEnabled (isUser);
    if (! isUser) { deleteArmed = false; presetDelete.setButtonText ("DELETE"); }
    shownPresetSig = name + (edited ? "*" : "") + juce::String (userPresets.size());
}

void EgretEqEditor::savePreset()
{
    const auto current = audioProcessor.getPresetName();
    auto name = presetName.getText().trim();
    if (name.isEmpty())
        name = userPresets.contains (current, true) ? current : "My preset " + juce::String (userPresets.size() + 1);
    for (const auto& f : eq::factoryPresets())
        if (name == f.name) name << " (mine)";
    const auto existed = userPresets.contains (name, true);
    if (! audioProcessor.saveUserPreset (name))
    {
        presetStatus.setText ("Couldn't save " + quoted (name), juce::dontSendNotification);
        return;
    }
    presetName.clear();
    userPresets = audioProcessor.getUserPresetNames();
    refreshPresetBox();
    presetStatus.setText ((existed ? "Updated " : "Saved ") + quoted (name), juce::dontSendNotification);
}

void EgretEqEditor::deletePreset()
{
    const auto name = audioProcessor.getPresetName();
    if (! userPresets.contains (name, true))
        return;
    if (! deleteArmed)   // two-step delete: the first click arms the button
    {
        deleteArmed = true;
        presetDelete.setButtonText ("SURE?");
        presetStatus.setText ("Click again to delete " + quoted (name), juce::dontSendNotification);
        return;
    }
    const auto deleted = audioProcessor.deleteUserPreset (name);
    userPresets = audioProcessor.getUserPresetNames();
    if (deleted)
        audioProcessor.loadFactoryPreset (0);
    selectionChanged();
    refreshPresetBox();
    presetStatus.setText ((deleted ? "Deleted " : "Couldn't delete ") + quoted (name), juce::dontSendNotification);
}

//==============================================================================
void EgretEqEditor::paintPanel (juce::Graphics& g)
{
    g.fillAll (EgretColours::panel);

    // brand: EGRET EQ, the subtitle and the kingfisher
    auto brand = headerArea.toFloat();
    auto titleRow = brand.removeFromTop (32.0f);
    const auto brandFont = sansFont (30.0f, true);
    const std::pair<juce::String, juce::Colour> words[] = { { "EGRET", ink }, { "EQ", teal } };
    for (const auto& [text, colour] : words)
    {
        g.setFont (brandFont);
        g.setColour (colour);
        const auto w = juce::GlyphArrangement::getStringWidth (brandFont, text);
        g.drawText (text, titleRow.removeFromLeft (w + 8.0f), juce::Justification::bottomLeft, false);
    }
    g.setColour (inkSoft);
    g.setFont (monoFont (10.0f).withExtraKerningFactor (0.14f));
    g.drawText (brandSub, brand.removeFromTop (18.0f), juce::Justification::centredLeft, false);

    auto readout = [&g] (juce::Rectangle<float> w, const juce::String& label, const juce::String& value, bool isHot)
    {
        g.setColour (readoutBg);
        g.fillRoundedRectangle (w, 6.0f);
        w.reduce (10.0f, 0.0f);
        g.setColour (readoutInk.withAlpha (0.55f));
        g.setFont (monoFont (9.0f).withExtraKerningFactor (0.1f));
        g.drawText (label, w.removeFromLeft (40.0f), juce::Justification::centredLeft, false);
        g.setColour (isHot ? hot : readoutInk);
        g.setFont (monoFont (15.0f));
        g.drawText (value, w, juce::Justification::centredLeft, false);
    };
    auto readouts = headerArea.toFloat().withTrimmedRight (124.0f);
    readout (readouts.removeFromRight (100.0f).withSizeKeepingCentre (100.0f, 28.0f), "PEAK", shownPeak, meter.getPeakHold() >= 1);
    readouts.removeFromRight (10.0f);
    readout (readouts.removeFromRight (84.0f).withSizeKeepingCentre (84.0f, 28.0f), "BANDS", juce::String (juce::jmax (0, shownBands)), false);

    // modules
    auto drawModule = [&g] (const juce::String& title, juce::Colour accent, juce::Rectangle<int> r)
    {
        const auto b = r.toFloat();
        g.setColour (accent);
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (panelRaised);
        g.fillRoundedRectangle (b.withTrimmedTop (3.0f), 12.0f);
        g.setColour (grooveDark.withAlpha (0.6f));
        g.drawRoundedRectangle (b.withTrimmedTop (3.0f), 12.0f, 1.0f);
        g.setColour (accent);
        g.setFont (sansFont (14.0f, true).withExtraKerningFactor (0.12f));
        g.drawText (title.toUpperCase(), r.reduced (12, 0).withTrimmedTop (8).withHeight (22), juce::Justification::centredLeft);
    };
    for (const auto& m : modules)
        drawModule (m.title, m.accent, m.bounds);
    drawModule (bandTitle, bandAccent, bandArea);

    // the screen bay and its caption
    g.setColour (grooveDark);
    g.fillRoundedRectangle (screenBay.toFloat(), 10.0f);
    g.setColour (ink.withAlpha (0.75f));
    g.setFont (monoFont (9.0f).withExtraKerningFactor (0.15f));
    const auto caption = screenBay.reduced (10, 0).removeFromBottom (20).toFloat();
    g.drawText (display.info, caption, juce::Justification::centredLeft, false);
    g.drawText ("ZERO LATENCY", caption, juce::Justification::centredRight, false);

    // help
    g.setColour (inkSoft);
    g.setFont (sansFont (12.0f));
    const auto help = juce::String (EgretEqProcessor::isStandalone() ? "Press PLAY to hear the demo loop, or load a file (or drop one on the display), use your input or pink noise. " : "")
                    + "Double-click the display to add a band, drag a dot to move it (Shift for fine moves), scroll on it to change Q, "
                      "double-click it to switch it off, Alt-click or Delete to remove it. Click a value to type it.";
    g.drawFittedText (help, helpArea, juce::Justification::topLeft, 3, 1.0f);
}

void EgretEqEditor::layoutPanel()
{
    modules.clear();
    headerArea = { pad, pad, panelWidth - 2 * pad, 48 };
    const auto subWidth = juce::GlyphArrangement::getStringWidth (monoFont (10.0f).withExtraKerningFactor (0.14f), subtitleText());
    bird.setBounds (headerArea.getX() + juce::roundToInt (subWidth) + 8, headerArea.getY() + 30, 22, 22);
    power.setBounds (headerArea.getRight() - 112, headerArea.getY() + 10, 112, 30);

    // presets
    const juce::Rectangle<int> presetsArea { pad, 80, panelWidth - 2 * pad, 76 };
    modules.push_back ({ "Presets", sage, presetsArea });
    auto pr = presetsArea.reduced (12).withTrimmedTop (24).withHeight (28);
    presetPrev.setBounds (pr.removeFromLeft (28)); pr.removeFromLeft (6);
    presetBox.setBounds (pr.removeFromLeft (180)); pr.removeFromLeft (6);
    presetNext.setBounds (pr.removeFromLeft (28)); pr.removeFromLeft (8);
    presetName.setBounds (pr.removeFromLeft (130)); pr.removeFromLeft (8);
    presetSave.setBounds (pr.removeFromLeft (52)); pr.removeFromLeft (8);
    presetDelete.setBounds (pr.removeFromLeft (62)); pr.removeFromLeft (14);
    undoButton.setBounds (pr.removeFromLeft (54)); pr.removeFromLeft (8);
    redoButton.setBounds (pr.removeFromLeft (54)); pr.removeFromLeft (14);
    abA.setBounds (pr.removeFromLeft (30)); pr.removeFromLeft (4);
    abB.setBounds (pr.removeFromLeft (30)); pr.removeFromLeft (4);
    abCopy.setBounds (pr.removeFromLeft (44)); pr.removeFromLeft (8);
    presetStatus.setBounds (pr);

    // display and meters
    screenBay = { pad, 168, panelWidth - 2 * pad, 420 };
    auto sb = screenBay.reduced (10).withTrimmedBottom (20);
    meter.setBounds (sb.removeFromRight (44));
    sb.removeFromRight (8);
    display.setBounds (sb);

    // band
    bandArea = { pad, 600, panelWidth - 2 * pad, 172 };
    auto bb = bandArea.reduced (12).withTrimmedTop (30);
    bandEmpty.setBounds (bb.removeFromTop (20));
    bb = bandArea.reduced (12).withTrimmedTop (34);
    auto right = bb.removeFromRight (172 + 12 + 76);
    bb.removeFromRight (16);
    auto shapeRow = bb.removeFromTop (52); bb.removeFromTop (7);
    shapeLabel.setBounds (shapeRow.removeFromLeft (52).withHeight (24));
    shapes.setBounds (shapeRow);
    auto slopeRow = bb.removeFromTop (26); bb.removeFromTop (7);
    slopeLabel.setBounds (slopeRow.removeFromLeft (52));
    slopes.setBounds (slopeRow);
    auto placeRow = bb.removeFromTop (26);
    placeLabel.setBounds (placeRow.removeFromLeft (52));
    places.setBounds (placeRow);
    auto actions = right.removeFromRight (76);
    bypassButton.setBounds (actions.removeFromTop (24)); actions.removeFromTop (5);
    soloButton.setBounds (actions.removeFromTop (24)); actions.removeFromTop (5);
    deleteButton.setBounds (actions.removeFromTop (24));
    right.removeFromRight (12);
    layoutKnobs (right.withTrimmedTop (4), { &freqKnob, &gainKnob, &qKnob });

    // source | analyzer | output
    const auto lowerY = 784, lowerH = 212;
    const auto unit = (panelWidth - 2 * pad - 24) / 3.45;
    const juce::Rectangle<int> srcArea { pad, lowerY, juce::roundToInt (unit), lowerH };
    const juce::Rectangle<int> anaArea { srcArea.getRight() + 12, lowerY, juce::roundToInt (unit * 1.45), lowerH };
    const juce::Rectangle<int> outArea { anaArea.getRight() + 12, lowerY, panelWidth - pad - anaArea.getRight() - 12, lowerH };
    modules.push_back ({ "Source", coral, srcArea });
    modules.push_back ({ "Analyzer", lavender, anaArea });
    modules.push_back ({ "Output", mustard, outArea });

    auto s = srcArea.reduced (12).withTrimmedTop (30);
    sources.setBounds (s.removeFromTop (26)); s.removeFromTop (8);
    auto sr = s.removeFromTop (26);
    playButton.setBounds (sr.removeFromLeft (56)); sr.removeFromLeft (6);
    loadButton.setBounds (sr.removeFromLeft (84));
    s.removeFromTop (8);
    sourceStatus.setBounds (s.removeFromTop (18));

    auto a = anaArea.reduced (12).withTrimmedTop (30);
    ChipRow* rows[] = { &anaMode, &anaRes, &anaSpeed, &anaRange, &dispRange };
    for (int i = 0; i < 5; ++i)
    {
        auto row = a.removeFromTop (24); a.removeFromTop (8);
        anaLabels[i].setBounds (row.removeFromLeft (48));
        if (i == 4)
        {
            freezeButton.setBounds (row.removeFromRight (62)); row.removeFromRight (4);
            tiltButton.setBounds (row.removeFromRight (46)); row.removeFromRight (4);
        }
        rows[i]->setBounds (row);
    }

    auto o = outArea.reduced (12).withTrimmedTop (32);
    layoutKnobs (o.removeFromTop (knobH), { &scaleKnob, &outGainKnob, &panKnob });
    o.removeFromTop (12);
    auto ar = o.removeFromTop (26);
    autoButton.setBounds (ar.removeFromLeft (84)); ar.removeFromLeft (8);
    autoReadout.setBounds (ar);

    helpArea = { pad, 1008, panelWidth - 2 * pad, 44 };
}
