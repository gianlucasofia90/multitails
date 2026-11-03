#pragma once
// MULTITAILS UI parts. Same design language as SONIC GRAIN and DR. SEGG-UENCER (glass panels, slanted black plates, dark
// knobs with a yellow value arc, pixel-font readouts). The ground is fox orange with a sky-blue slab and two white-tipped
// tails behind the panels: the third colour of the family.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <juce_audio_processors/juce_audio_processors.h>
#include "TailsProcessor.h"
#include "TailsFonts.h"

namespace tails
{
namespace c
{
    // only the ground layer, its shadows and the preset tag differ from the two sibling plugins
    const juce::Colour ground    { 0xffef7d0f };   // fox orange
    const juce::Colour groundAlt { 0xffb24a00 };   // header strip, white on it 5.3:1
    const juce::Colour slab      { 0xff3aa3ea };   // sky-blue diagonal band behind the right side
    const juce::Colour fur       { 0xfff7a21e };   // tail body, a shade lighter than the ground
    const juce::Colour furTip    { 0xfffdfcf8 };   // white tail tip
    const juce::Colour furLine   { 0xff7a3300 };   // tail outline
    const juce::Colour furDeep   { 0xffd2560a };   // fur band, left end
    const juce::Colour furGold   { 0xffffc861 };   // fur band, towards the tip
    // the routing canvas of the chain panel
    const juce::Colour node      { 0xff2a64a8 };   // effect block, on: eye blue, white on it 6.0:1
    const juce::Colour nodeHi    { 0xff3b7cc4 };
    const juce::Colour wire      { 0xff5f95cf };
    const juce::Colour modGround { 0xff17191d };   // track deck
    const juce::Colour glass     { 0xf5f6f7fa };   // rgba(246, 247, 250, 0.96): almost opaque, no cast from the ground
    const juce::Colour panelShadow { 0x524a1c00 };
    const juce::Colour glassEdge { 0xe6ffffff };
    const juce::Colour ink       { 0xff0b1430 };
    const juce::Colour inkSoft   { 0xff33415f };
    const juce::Colour plate     { 0xff111111 };
    const juce::Colour onPlate   { 0xffffffff };
    const juce::Colour accent    { 0xffffd21f };
    const juce::Colour onAccent  { 0xff0b1430 };
    const juce::Colour go        { 0xff1f9e1a };
    const juce::Colour signal    { 0xffe90101 };
    const juce::Colour tag       { 0xff0b63c9 };   // eye blue, white on it 5.6:1
        const juce::Colour knob      { 0xff16212f };
    const juce::Colour knobHi    { 0xff3a4b63 };
    const juce::Colour track     { 0x240b1430 };   // ink at 14 %
    const juce::Colour trackDark { 0x24ffffff };   // white at 14 %, for controls on plates and the deck
    const juce::Colour display   { 0xeb082660 };   // rgba(8, 38, 96, 0.92)
    const juce::Colour wave      { 0xffffffff };
    const juce::Colour checker   { 0x1fffffff };   // white 12 % on the ground and the slab
    const juce::Colour grid      { 0x2effffff };
}

/** Embedded faces (Rubik, Silkscreen; SIL Open Font License, see Resources/fonts). */
struct Fonts
{
    static Fonts& get() { static Fonts f; return f; }
    juce::Typeface::Ptr medium, bold, black, pixel;
private:
    Fonts()
    {
        medium = juce::Typeface::createSystemTypefaceFor (TailsFonts::RubikMedium_ttf, TailsFonts::RubikMedium_ttfSize);
        bold   = juce::Typeface::createSystemTypefaceFor (TailsFonts::RubikBold_ttf, TailsFonts::RubikBold_ttfSize);
        black  = juce::Typeface::createSystemTypefaceFor (TailsFonts::RubikBlackItalic_ttf, TailsFonts::RubikBlackItalic_ttfSize);
        pixel  = juce::Typeface::createSystemTypefaceFor (TailsFonts::SilkscreenRegular_ttf, TailsFonts::SilkscreenRegular_ttfSize);
    }
};

inline juce::Font face (const juce::Typeface::Ptr& t, float size, float tracking = 0.0f)
{
    return juce::Font (juce::FontOptions (t).withHeight (size)).withExtraKerningFactor (tracking);
}
inline juce::Font medium (float size, float tracking = 0.0f) { return face (Fonts::get().medium, size, tracking); }
inline juce::Font bold (float size, float tracking = 0.0f)   { return face (Fonts::get().bold, size, tracking); }
inline juce::Font black (float size, float tracking = 0.0f)  { return face (Fonts::get().black, size, tracking); }
// Silkscreen draws on a grid of 1/8 em and JUCE sizes a font by ascent + descent (1.28 em): x 1.024 puts every
// font pixel on exactly one (10) or two (20) screen pixels, so the readouts stay sharp instead of smeared.
inline juce::Font pixel (float size)                         { return face (Fonts::get().pixel, size * 1.024f); }

/** A rectangle skewed by -30 degrees: the plate every button, tab and tag is drawn on. */
inline juce::Path platePath (juce::Rectangle<float> r)
{
    const float lean = juce::jmin (r.getHeight() * 0.577f, r.getWidth() * 0.3f);
    juce::Path p;
    p.addQuadrilateral (r.getX() + lean, r.getY(), r.getRight(), r.getY(),
                        r.getRight() - lean, r.getBottom(), r.getX(), r.getBottom());
    return p;
}

/** Plate with the stacked hard shadow (signal, then go) used by section tags and the preset tag. */
inline void taggedPlate (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill)
{
    g.setColour (c::go);     g.fillPath (platePath (r.translated (7, 7)));
    g.setColour (c::signal); g.fillPath (platePath (r.translated (4, 4)));
    g.setColour (fill);      g.fillPath (platePath (r));
}

/** The fur band: deep orange through the coat colour to the white tail tip, left to right. It edges the header
    and the track deck where the sibling plugins have hazard stripes. */
inline void furBand (juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::ColourGradient grad (c::furDeep, r.getX(), 0.0f, c::furTip, r.getRight(), 0.0f, false);
    grad.addColour (0.45, c::fur);
    grad.addColour (0.78, c::furGold);
    g.setGradientFill (grad);
    g.fillRect (r);
    g.setColour (c::furLine.withAlpha (0.35f));
    g.fillRect (r.withTop (r.getBottom() - 1.0f));
}

/** One fox tail along an arc around (cx, cy): thin at the root, full in the middle, a white tip with a jagged edge.
    a0 is the root angle and a1 the tip angle (radians, JUCE convention: 0 at 12 o'clock, clockwise). */
inline void foxTail (juce::Graphics& g, float cx, float cy, float rad, float a0, float a1, float maxW)
{
    const float k = juce::jlimit (0.3f, 1.0f, maxW / 130.0f);   // shadow and outline follow the size
    auto at = [&] (float t, float off)
    {
        const float a = a0 + (a1 - a0) * t, r = rad + off;
        return juce::Point<float> (cx + r * std::sin (a), cy - r * std::cos (a));
    };
    auto width = [&] (float t)
    {
        const float b = std::sin (juce::MathConstants<float>::pi * std::pow (t, 0.75f));
        return maxW * (0.22f + 0.78f * b) * (t > 0.82f ? juce::jmax (0.0f, (1.0f - t) / 0.18f) : 1.0f);
    };
    auto side = [&] (float from, float to, int n, juce::Path& p, bool outer, bool start)
    {
        for (int i = 0; i <= n; ++i)
        {
            const float t = outer ? from + (to - from) * (float) i / (float) n : to - (to - from) * (float) i / (float) n;
            const auto pt = at (t, (outer ? 0.5f : -0.5f) * width (t));
            if (start && i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
    };
    juce::Path body;
    side (0.0f, 1.0f, 60, body, true, true);
    side (0.0f, 1.0f, 60, body, false, false);
    body.closeSubPath();

    // tip: from a zigzag across the tail at t ~ 0.68 to the point
    const float tz = 0.68f;
    juce::Path tip;
    side (tz, 1.0f, 30, tip, true, true);
    side (tz, 1.0f, 30, tip, false, false);
    for (int k = 0; k <= 6; ++k)
    {
        const float u = (float) k / 6.0f;
        const float t = tz + ((k % 2) ? -0.045f : 0.0f);
        tip.lineTo (at (t, (u - 0.5f) * width (t)));
    }
    tip.closeSubPath();

    g.setColour (juce::Colour (0x33000000));
    g.fillPath (body, juce::AffineTransform::translation (8.0f * k, 8.0f * k));
    g.setColour (c::fur);
    g.fillPath (body);
    g.setColour (c::furTip);
    g.fillPath (tip);
    g.setColour (c::furLine);
    g.strokePath (body, juce::PathStrokeType (3.0f * k, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** Glass panel with its two-line title: kicker word, then the section word on a tagged plate. */
inline void glassPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& kicker, const juce::String& title)
{
    juce::Path body;
    body.addRoundedRectangle (r, 10.0f);
    juce::DropShadow (c::panelShadow, 28, { 0, 12 }).drawForPath (g, body);
    g.setColour (c::glass);
    g.fillPath (body);
    g.setColour (c::glassEdge);
    g.strokePath (body, juce::PathStrokeType (1.0f));

    g.setColour (c::inkSoft);
    g.setFont (medium (10.0f, 0.3f));
    g.drawText (kicker.toUpperCase(), (int) r.getX() + 16, (int) r.getY() + 8, 200, 12, juce::Justification::left);
    const auto f = bold (13.0f, 0.22f);
    const float tw = juce::GlyphArrangement::getStringWidth (f, title.toUpperCase()) + 36.0f;
    auto tr = juce::Rectangle<float> (r.getX() + 18, r.getY() + 22, tw, 22);
    taggedPlate (g, tr, c::plate);
    g.setColour (c::onPlate);
    g.setFont (f);
    g.drawText (title.toUpperCase(), tr.reduced (14, 0), juce::Justification::centred);
}

/** Controls on a dark ground (header plates, modulation deck) carry this property. */
inline bool isOnDark (const juce::Component& comp)
{
    for (auto* p = &comp; p != nullptr; p = p->getParentComponent())
        if (p->getProperties()["sgDark"]) return true;
    return false;
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    static constexpr int kValueH = 13, kCaptionH = 15;   // knob readout and caption rows

    LookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, c::ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, c::accent);
        setColour (juce::Label::textColourId, c::ink);
        setColour (juce::ComboBox::textColourId, c::ink);
        setColour (juce::ComboBox::arrowColourId, c::ink);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xfff6f7fa));
        setColour (juce::PopupMenu::textColourId, c::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, c::plate);
        setColour (juce::PopupMenu::highlightedTextColourId, c::onPlate);
        setColour (juce::TextButton::buttonColourId, c::plate);
        setColour (juce::TextButton::buttonOnColourId, c::accent);
        setColour (juce::TextButton::textColourOffId, c::onPlate);
        setColour (juce::TextButton::textColourOnId, c::onAccent);
        setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xfff6f7fa));
        setColour (juce::AlertWindow::textColourId, c::ink);
        setColour (juce::AlertWindow::outlineColourId, c::plate);
        setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
        setColour (juce::TextEditor::textColourId, c::ink);
        setColour (juce::TextEditor::highlightColourId, c::accent);
        setColour (juce::TextEditor::outlineColourId, c::inkSoft);
        setColour (juce::TextEditor::focusedOutlineColourId, c::ink);
        setColour (juce::CaretComponent::caretColourId, c::ink);
    }

    juce::Font getComboBoxFont (juce::ComboBox& b) override { return medium (b.getWidth() < 62 ? 10.5f : 12.0f); }
    juce::Font getPopupMenuFont() override { return medium (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return black (juce::jmin (12.5f, h * 0.55f), 0.04f); }
    juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
    juce::Font getAlertWindowTitleFont() override { return bold (15.0f); }
    juce::Font getAlertWindowMessageFont() override { return medium (13.0f); }
    juce::Font getAlertWindowFont() override { return medium (12.0f); }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (pixel (10.0f));
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::textColourId, isOnDark (s) ? c::onPlate : c::ink);
        l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
        l->setColour (juce::Label::backgroundWhenEditingColourId, juce::Colours::white);
        l->setColour (juce::Label::textWhenEditingColourId, c::ink);
        l->setColour (juce::Label::outlineWhenEditingColourId, c::ink);
        return l;
    }

    // rotary knobs: knob on top, then a gap for the caption (a sibling Label), then the value
    juce::Slider::SliderLayout getSliderLayout (juce::Slider& s) override
    {
        if (! s.isRotary()) return juce::LookAndFeel_V4::getSliderLayout (s);
        juce::Slider::SliderLayout l;
        auto r = s.getLocalBounds();
        l.textBoxBounds = r.removeFromBottom (kValueH);
        r.removeFromBottom (kCaptionH);
        l.sliderBounds = r;
        return l;
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1,
                           juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float size = juce::jmin (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (size, size);
        const auto ctr = r.getCentre();
        const float ar = size * 0.5f - 3.0f, br = size * 0.5f - 9.0f;
        const bool dark = isOnDark (s);
        auto arc = [&] (float rad, float from, float to, juce::Colour col, float width)
        {
            juce::Path p;
            p.addCentredArc (ctr.x, ctr.y, rad, rad, 0, from, to, true);
            g.setColour (col);
            g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        const float mod = (float) s.getProperties().getWithDefault ("sgMod", -1.0);
        const float ang = a0 + (mod >= 0.0f ? mod : pos) * (a1 - a0), setAng = a0 + pos * (a1 - a0);

        arc (ar, a0, a1, dark ? c::trackDark : c::track, 4.0f);
        if (pos > 0.002f) arc (ar, a0, setAng, c::accent, 4.0f);
        if (mod >= 0.0f)
        {   // the sequencer has the knob: the arc shows where it took it, the tick where it was set
            arc (ar + 4.5f, juce::jmin (setAng, ang), juce::jmax (setAng, ang), dark ? c::accent : c::go, 2.5f);
            g.setColour (dark ? c::onPlate : c::ink);
            g.fillEllipse (ctr.x + ar * std::sin (setAng) - 2.5f, ctr.y - ar * std::cos (setAng) - 2.5f, 5.0f, 5.0f);
        }

        // body
        juce::Path body;
        body.addEllipse (ctr.x - br, ctr.y - br, br * 2, br * 2);
        juce::DropShadow (juce::Colour (0x73000000), 6, { 0, 3 }).drawForPath (g, body);
        g.setGradientFill (juce::ColourGradient (c::knobHi, ctr.x, ctr.y - br, c::knob, ctr.x, ctr.y + br, false));
        g.fillPath (body);
        // pointer
        g.setColour (c::onPlate);
        g.drawLine ({ { ctr.x + br * 0.3f * std::sin (ang), ctr.y - br * 0.3f * std::cos (ang) },
                      { ctr.x + (br - 3) * std::sin (ang), ctr.y - (br - 3) * std::cos (ang) } }, 2.0f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const bool dark = isOnDark (s);
        g.setColour (dark ? c::trackDark : c::track);
        g.fillRoundedRectangle (r, 4.0f);
        const bool bip = s.getMinimum() < 0 && s.getMaximum() > 0;
        if (style == juce::Slider::LinearBarVertical || style == juce::Slider::LinearVertical)
        {
            const float zero = bip ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
            const float yz = r.getBottom() - zero * r.getHeight();
            const float yv = pos;   // already a pixel position
            auto bar = juce::Rectangle<float>::leftTopRightBottom (r.getX(), juce::jmin (yz, yv), r.getRight(), juce::jmax (yz, yv));
            g.setColour (bip ? (yv < yz ? (dark ? c::accent : c::go) : c::signal) : c::accent);   // go is lost on the green deck
            g.fillRoundedRectangle (bar, 3.0f);
            if (bip) { g.setColour (dark ? c::onPlate.withAlpha (0.6f) : c::inkSoft); g.fillRect (r.getX() + 3, yz - 0.5f, r.getWidth() - 6, 1.0f); }
        }
        else
        {
            const float zero = bip ? (float) s.valueToProportionOfLength (0.0) : 0.0f;
            const float xz = r.getX() + zero * r.getWidth();
            const float xv = pos;
            auto bar = juce::Rectangle<float>::leftTopRightBottom (juce::jmin (xz, xv), r.getY(), juce::jmax (xz, xv), r.getBottom());
            g.setColour (bip ? (xv >= xz ? (dark ? c::accent : c::go) : c::signal) : c::accent);
            g.fillRoundedRectangle (bar, 3.0f);
            if (bip) { g.setColour (dark ? c::onPlate.withAlpha (0.6f) : c::inkSoft); g.fillRect (xz - 0.5f, r.getY() + 3, 1.0f, r.getHeight() - 6); }
        }
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f, 2.0f).withTrimmedRight (3.0f).withTrimmedBottom (1.0f);
        const bool on = b.getToggleState();
        const bool rec = b.getProperties()["sgRec"];
        auto p = platePath (r);
        g.setColour (juce::Colour (0x59000000));
        g.fillPath (p, juce::AffineTransform::translation (3, 3));
        // on the black deck an off plate would vanish: lift it a step
        juce::Colour fill = b.getProperties()["sgDeck"] ? juce::Colour (0xff2c3038) : c::plate;
        if (on) fill = rec ? c::signal : c::accent;
        else if (down) fill = c::tag.darker (0.3f);
        else if (over) fill = c::tag;
        g.setColour (fill);
        g.fillPath (p);
        if (b.getClickingTogglesState())
        {   // LED: state is never colour alone
            const float d = juce::jmin (8.0f, r.getHeight() * 0.34f);
            auto led = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + r.getHeight() * 0.58f + d * 0.5f + 2, r.getCentreY() });
            g.setColour (on ? (rec ? c::onPlate : c::go) : c::onPlate.withAlpha (0.25f));
            g.fillEllipse (led);
            if (on && ! rec) { g.setColour (c::onAccent); g.drawEllipse (led, 1.2f); }
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f, 2.0f).withTrimmedRight (3.0f).withTrimmedBottom (1.0f);
        const bool on = b.getToggleState(), rec = b.getProperties()["sgRec"];
        if (b.getClickingTogglesState()) r.removeFromLeft (r.getHeight() * 0.58f + 12.0f);
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (on && ! rec ? c::onAccent : c::onPlate);
        g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred, false);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        if (box.getWidth() < 62) { label.setBounds (1, 1, box.getWidth() - 2, box.getHeight() - 2); label.setJustificationType (juce::Justification::centred); }
        else { label.setBounds (4, 1, box.getWidth() - 22, box.getHeight() - 2); label.setJustificationType (juce::Justification::centredLeft); }
        label.setBorderSize ({ 0, 3, 0, 2 });
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, isOnDark (box) ? c::onPlate : c::ink);
        label.setMinimumHorizontalScale (0.7f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        const bool dark = isOnDark (box);
        g.setColour (dark ? juce::Colour (0x14ffffff) : juce::Colours::white);
        g.fillRoundedRectangle (r, 4.0f);
        const bool over = box.isMouseOver (true);
        g.setColour (dark ? c::onPlate.withAlpha (over ? 0.6f : 0.2f) : (over ? c::inkSoft : c::ink.withAlpha (0.18f)));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        if (w < 62) return;
        juce::Path chev;
        const float ax = (float) w - 11.0f, ay = h * 0.5f;
        chev.startNewSubPath (ax - 3.5f, ay - 2.0f); chev.lineTo (ax, ay + 1.5f); chev.lineTo (ax + 3.5f, ay - 2.0f);
        g.setColour (dark ? c::onPlate : c::ink);
        g.strokePath (chev, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
        g.setColour (c::ink.withAlpha (0.25f));
        g.drawRect (0, 0, w, h, 1);
    }
};

/** The right button never moves a control: it opens the parameter's menu (MIDI learn, sequencer). */
template <class Base>
struct RightClick : public Base
{
    std::function<void()> onRight;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { right = true; if (onRight) onRight(); return; } right = false; Base::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override { if (! right) Base::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (right) { right = false; return; } Base::mouseUp (e); }
private:
    bool right = false;
};

/** Parameter-bound control. Kinds: Knob, Box (number bar), VSlider, HBar, Toggle, Menu. Empty label = no caption.
    Knobs carry their caption between the knob and the value; the other kinds above the control. */
class Control : public juce::Component
{
public:
    enum class Kind { Knob, Box, VSlider, HBar, Toggle, Menu };

    Control (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& label, Kind k)
        : kind (k), paramId (id)
    {
        caption.setText (label.toUpperCase(), juce::dontSendNotification);
        caption.setJustificationType (k == Kind::Knob ? juce::Justification::centred : juce::Justification::centredLeft);
        caption.setFont (bold (11.0f, 0.06f));
        caption.setColour (juce::Label::textColourId, c::ink);
        caption.setBorderSize ({});
        caption.setMinimumHorizontalScale (0.75f);
        caption.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (caption);
        caption.setVisible (label.isNotEmpty() && k != Kind::Toggle);

        if (k == Kind::Toggle)
        {
            button.setClickingTogglesState (true);
            button.setButtonText (label);
            addAndMakeVisible (button);
            ba = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, id, button);
        }
        else if (k == Kind::Menu)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id))) fillMenu (*p);
            addAndMakeVisible (combo);
            ca = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, id, combo);
        }
        else
        {
            using S = juce::Slider;
            switch (k)
            {
                case Kind::Knob: slider.setSliderStyle (S::RotaryHorizontalVerticalDrag);
                                 slider.setTextBoxStyle (S::TextBoxBelow, false, 80, LookAndFeel::kValueH); break;
                case Kind::Box:
                case Kind::VSlider: slider.setSliderStyle (S::LinearBarVertical);
                                 slider.setTextBoxStyle (S::NoTextBox, false, 0, 0); break;
                default:         slider.setSliderStyle (S::LinearBar);
                                 slider.setTextBoxStyle (S::NoTextBox, false, 0, 0); break;
            }
            slider.setDoubleClickReturnValue (true, 0.0);
            addAndMakeVisible (slider);
            sa = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
            if (auto* p = s.getParameter (id)) slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            if (k != Kind::Knob) slider.setTooltip (label);
            slider.onValueChange = [this] { repaint(); };
        }
        auto right = [this] { if (onRight) onRight (paramId); };
        slider.onRight = right; button.onRight = right; combo.onRight = right;
    }

    /** Position the sequencer gives this knob, or < 0. */
    void setModulation (float norm)
    {
        if (std::abs (norm - shownMod) < 0.002f) return;
        shownMod = norm;
        slider.getProperties().set ("sgMod", norm);
        slider.repaint();
    }

    const juce::String& getParamId() const noexcept { return paramId; }
    std::function<void (const juce::String&)> onRight;

    void resized() override
    {
        auto r = getLocalBounds();
        if (kind == Kind::Toggle) { button.setBounds (r); return; }
        if (kind == Kind::Knob)
        {
            slider.setBounds (r);
            caption.setBounds (0, r.getBottom() - LookAndFeel::kValueH - LookAndFeel::kCaptionH, r.getWidth(), LookAndFeel::kCaptionH);
            caption.toFront (false);
            return;
        }
        if (caption.isVisible()) caption.setBounds (r.removeFromTop (16).withTrimmedBottom (2));
        if (kind == Kind::Menu) { combo.setBounds (r.withHeight (juce::jmin (r.getHeight(), 22))); return; }
        slider.setBounds (r);
    }

    void paintOverChildren (juce::Graphics& g) override
    {
        if (kind == Kind::Box || kind == Kind::HBar)
        {
            const auto t = slider.getTextFromValue (slider.getValue());
            const auto f = pixel (10.0f);
            auto sb = slider.getBounds().toFloat();
            const float tw = juce::GlyphArrangement::getStringWidth (f, t) + 8.0f;
            auto patch = juce::Rectangle<float> (tw, 12.0f).withCentre (sb.getCentre());
            if (patch.getHeight() < sb.getHeight() - 2)
            {
                g.setColour (isOnDark (*this) ? c::plate.withAlpha (0.75f) : c::glass.withAlpha (1.0f));
                g.fillRoundedRectangle (patch, 2.0f);
            }
            g.setColour (isOnDark (*this) ? c::onPlate : c::ink);
            g.setFont (f);
            g.drawText (t, sb, juce::Justification::centred);
        }
    }

    void setCaptionColour (juce::Colour col) { caption.setColour (juce::Label::textColourId, col); }

    /** Long lists open as submenus. The item ids stay 1..n, so the attachment needs no change. */
    void fillMenu (juce::AudioParameterChoice& p)
    {
        if (paramId.endsWith ("_c_ir"))
        {
            juce::PopupMenu plates, digital, rooms;
            for (int i = 0; i < p.choices.size(); ++i)
                (p.choices[i].startsWith ("Plate") ? plates : p.choices[i].startsWith ("Digital") ? digital : rooms).addItem (i + 1, p.choices[i]);
            auto* root = combo.getRootMenu();
            for (int i = 0; i < p.choices.size(); ++i) combo.addItem (p.choices[i], i + 1);    // keeps the names for the display
            root->clear();
            root->addSubMenu ("Plates", plates);
            root->addSubMenu ("Digital", digital);
            root->addSubMenu ("Rooms and User", rooms);
            return;
        }
        if (paramId.endsWith ("_dest"))
        {
            std::map<int, juce::PopupMenu> groups;
            juce::PopupMenu top;
            const auto& table = DestTable::get();
            for (int i = 0; i < p.choices.size(); ++i)
            {
                if (i < kNumGlobalDest) { top.addItem (i + 1, p.choices[i]); continue; }
                const int param = table.param[(size_t) (i - kNumGlobalDest)];
                if (streamParams()[param].fx == fxStream) top.addItem (i + 1, "Chain On");
                else groups[streamParams()[param].fx].addItem (i + 1, p.choices[i]);
            }
            for (int i = 0; i < p.choices.size(); ++i) combo.addItem (p.choices[i], i + 1);
            auto* root = combo.getRootMenu();
            root->clear();
            root->addSectionHeader ("Global");
            for (juce::PopupMenu::MenuItemIterator it (top); it.next();) root->addItem (it.getItem());
            root->addSeparator();
            for (auto& g : groups) root->addSubMenu (kFxNames[g.first], g.second);
            return;
        }
        combo.addItemList (p.choices, 1);
    }

    RightClick<juce::Slider> slider;
    RightClick<juce::TextButton> button;
    RightClick<juce::ComboBox> combo;

private:
    Kind kind;
    juce::String paramId;
    float shownMod = -1.0f;
    juce::Label caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sa;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ca;
};

/** Display well: the recessed dark-blue rectangle data is drawn in. */
inline void displayWell (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (c::display);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (c::plate);                                   // black cockpit bezel
    g.drawRoundedRectangle (r.reduced (3.0f), 4.0f, 6.0f);
    g.setColour (c::onPlate.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (6.5f), 3.0f, 1.0f);
    g.setColour (c::onPlate.withAlpha (0.18f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}

/** Value of a stream parameter as the engine has it right now: sequenced if a lane holds it. */
inline float liveValue (TailsProcessor& p, int s, int param)
{
    const float n = p.engine->ui.norm[(size_t) s][(size_t) param].load (std::memory_order_relaxed);
    const auto& d = streamParams()[param];
    if (n < 0.0f) return p.sf (s, param);
    if (d.type == T::Float) return fromNorm (d, skewOf (d), n);
    return n * d.hi;
}

// ---------------------------------------------------------------- the two chains

/** Routing view: a dark grid with one wire per stream, the six blocks threaded on it in signal order. Left of the
    grid each stream has its name and a compact strip of the order (one letter per effect, dim when off). Click a block
    to edit it, its power dot to switch it, drag it along the wire to reorder. A switched-off block is hatched. */
class ChainView : public juce::Component
{
public:
    static constexpr int kRowH = 64, kRowGap = 0, kLabelW = 100, kBlockW = 96, kBlockH = 40, kPitch = 112, kPad = 30;
    std::function<void (int, int)> onSelect;

    explicit ChainView (TailsProcessor& p) : proc (p) {}

    void setSelected (int stream, int fx) { selStream = stream; selFx = fx; repaint(); }
    void tick() { repaint(); }

    static constexpr const char* kLetter[kNumFx] = { "C", "T", "D", "H", "R", "F" };

    void paint (juce::Graphics& g) override
    {
        const auto canvas = juce::Rectangle<float> ((float) kLabelW - 4.0f, 0.0f, (float) getWidth() - kLabelW + 4.0f, (float) getHeight());
        {
            juce::Path body;
            body.addRoundedRectangle (canvas, 6.0f);
            g.setColour (c::display);
            g.fillPath (body);
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (body);
            g.setColour (c::onPlate.withAlpha (0.045f));
            for (float x = canvas.getX() + 8.0f; x < canvas.getRight(); x += 16.0f) g.fillRect (x, canvas.getY(), 1.0f, canvas.getHeight());
            for (float y = 8.0f; y < canvas.getBottom(); y += 16.0f) g.fillRect (canvas.getX(), y, canvas.getWidth(), 1.0f);
            g.setColour (c::plate);
            g.drawRoundedRectangle (canvas.reduced (0.5f), 6.0f, 1.0f);
        }

        for (int s = 0; s < kStreams; ++s)
        {
            const bool streamOn = proc.sf (s, sOn) > 0.5f;
            const float cy = (float) rowY (s) + kRowH * 0.5f;
            int order[kNumFx];
            proc.getOrder (s, order);

            // name and order strip
            g.setColour (streamOn ? c::ink : c::inkSoft);
            g.setFont (black (13.5f, 0.02f));
            g.drawText (s == 0 ? "TRANSIENT" : "TONAL", 0, (int) cy - 22, kLabelW - 10, 16, juce::Justification::left);
            drawStrip (g, s, order, { 0.0f, cy + 2.0f, (float) kLabelW - 12.0f, 18.0f }, streamOn);

            // wire, end to end, with its terminals
            g.setColour (c::wire.withAlpha (streamOn ? 1.0f : 0.4f));
            g.fillRect (canvas.getX(), cy - 1.0f, canvas.getWidth(), 2.0f);
            g.setFont (pixel (10.0f));
            g.setColour (c::onPlate.withAlpha (0.55f));
            g.drawText ("IN", (int) canvas.getX() + 6, (int) cy - 15, 30, 12, juce::Justification::left);
            g.drawText ("OUT", (int) canvas.getRight() - 36, (int) cy - 15, 30, 12, juce::Justification::right);

            if (dragging && dragStream == s) applyDrag (order);
            for (int i = 0; i < kNumFx; ++i)
            {
                if (dragging && dragStream == s && order[i] == dragFx)
                {
                    g.setColour (c::onPlate.withAlpha (0.25f));   // the free slot
                    g.drawRoundedRectangle (blockRect (s, i).reduced (1.0f), 5.0f, 1.0f);
                    continue;
                }
                drawBlock (g, s, order[i], blockRect (s, i));
            }
            if (dragging && dragStream == s)
                drawBlock (g, s, dragFx, blockRect (s, 0).withX ((float) dragX - kBlockW * 0.5f).translated (0.0f, -3.0f));
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto hit = locate (e.getPosition());
        pressed = hit.fx;
        pressStream = hit.stream;
        moved = false;
        if (hit.fx < 0) return;
        if (e.mods.isPopupMenu()) { pressed = -1; if (onRightBlock) onRightBlock (hit.stream, hit.fx); return; }
        if (hit.led)
        {
            const auto id = TailsProcessor::sid (hit.stream, onParam (hit.fx));
            proc.setParam (id, proc.sf (hit.stream, onParam (hit.fx)) > 0.5f ? 0.0f : 1.0f);
            pressed = -1;
            repaint();
            return;
        }
        if (onSelect) onSelect (hit.stream, hit.fx);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (pressed < 0 || e.getDistanceFromDragStart() < 6) return;
        dragging = true; moved = true; dragStream = pressStream; dragFx = pressed;
        dragX = juce::jlimit (slotX (0) + kBlockW / 2, slotX (kNumFx - 1) + kBlockW / 2, e.x);
        dragSlot = juce::jlimit (0, kNumFx - 1, (dragX - slotX (0) + (kPitch - kBlockW) / 2) / kPitch);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
        {
            int order[kNumFx];
            proc.getOrder (dragStream, order);
            applyDrag (order);
            proc.setOrder (dragStream, order);
        }
        dragging = false; pressed = -1;
        repaint();
    }

    std::function<void (int, int)> onRightBlock;

    static int onParam (int fx) { static constexpr int t[kNumFx] = { cOn, tOn, dOn, hOn, rOn, fOn }; return t[fx]; }
    static int mixParam (int fx) { static constexpr int t[kNumFx] = { cMix, tMix, dMix, hMix, rMix, fMix }; return t[fx]; }

private:
    struct Hit { int stream = 0, fx = -1; bool led = false; };

    static int rowY (int s) { return s * (kRowH + kRowGap); }
    static int slotX (int i) { return kLabelW + kPad + i * kPitch; }
    static juce::Rectangle<float> blockRect (int s, int i)
    {
        return { (float) slotX (i), (float) rowY (s) + (kRowH - kBlockH) * 0.5f, (float) kBlockW, (float) kBlockH };
    }

    Hit locate (juce::Point<int> p) const
    {
        Hit h;
        for (int s = 0; s < kStreams; ++s)
        {
            if (p.y < rowY (s) || p.y >= rowY (s) + kRowH) continue;
            int order[kNumFx];
            proc.getOrder (s, order);
            for (int i = 0; i < kNumFx; ++i)
                if (blockRect (s, i).expanded (4.0f, 8.0f).contains (p.toFloat()))
                {
                    h.stream = s; h.fx = order[i];
                    h.led = p.x < slotX (i) + 26;
                }
        }
        return h;
    }

    void applyDrag (int* order) const
    {
        int rest[kNumFx], n = 0;
        for (int i = 0; i < kNumFx; ++i) if (order[i] != dragFx) rest[n++] = order[i];
        for (int i = 0, k = 0; i < kNumFx; ++i) order[i] = i == dragSlot ? dragFx : rest[k++];
    }

    /** One letter per effect in signal order, threaded on a line: the compact view of the chain. */
    void drawStrip (juce::Graphics& g, int s, const int* order, juce::Rectangle<float> r, bool streamOn) const
    {
        const float cell = 12.0f, gap = (r.getWidth() - kNumFx * cell) / (float) (kNumFx - 1);
        const float cy = r.getCentreY();
        g.setColour (c::ink.withAlpha (streamOn ? 0.45f : 0.2f));
        g.fillRect (r.getX(), cy - 0.5f, r.getWidth(), 1.0f);
        g.setFont (pixel (10.0f));
        for (int i = 0; i < kNumFx; ++i)
        {
            const int fx = order[i];
            const bool on = proc.sf (s, onParam (fx)) > 0.5f && streamOn;
            const bool sel = s == selStream && fx == selFx;
            auto b = juce::Rectangle<float> (r.getX() + (float) i * (cell + gap), cy - cell * 0.5f - 1.0f, cell, cell + 2.0f);
            g.setColour (sel ? c::fur : (on ? c::node : juce::Colour (0xffc9ced8)));
            g.fillRoundedRectangle (b, 2.0f);
            g.setColour (sel ? c::ink : (on ? c::onPlate : c::ink.withAlpha (0.4f)));
            g.drawText (kLetter[fx], b.translated (0.5f, 0.0f), juce::Justification::centred);
        }
    }

    void drawBlock (juce::Graphics& g, int s, int fx, juce::Rectangle<float> r) const
    {
        const bool on = proc.sf (s, onParam (fx)) > 0.5f;
        const bool live = liveValue (proc, s, onParam (fx)) > 0.5f;
        const bool sel = s == selStream && fx == selFx;
        juce::Path body;
        body.addRoundedRectangle (r, 5.0f);

        g.setColour (juce::Colour (0x59000000));
        g.fillPath (body, juce::AffineTransform::translation (0.0f, 3.0f));
        if (sel)         g.setGradientFill (juce::ColourGradient (c::furGold, 0, r.getY(), c::fur, 0, r.getBottom(), false));
        else if (on)     g.setGradientFill (juce::ColourGradient (c::nodeHi, 0, r.getY(), c::node, 0, r.getBottom(), false));
        else             g.setColour (juce::Colour (0xff15315c));
        g.fillPath (body);
        if (! on)
        {
            // bypassed: hatched, as on a routing diagram
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (body);
            g.setColour (c::onPlate.withAlpha (0.09f));
            for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 7.0f)
                g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 2.0f);
        }
        g.setColour (sel ? c::furTip : c::onPlate.withAlpha (on ? 0.22f : 0.14f));
        g.strokePath (body, juce::PathStrokeType (sel ? 2.0f : 1.0f));

        const auto ink = sel ? c::onAccent : (on ? c::onPlate : c::onPlate.withAlpha (0.45f));
        // power dot: green while the effect is live
        auto led = juce::Rectangle<float> (9, 9).withCentre ({ r.getX() + 14.0f, r.getCentreY() - 4.0f });
        g.setColour (live ? c::go : ink.withAlpha (0.35f));
        g.fillEllipse (led);
        g.setColour (sel ? c::onAccent : juce::Colours::black.withAlpha (0.4f));
        g.drawEllipse (led, 1.0f);

        g.setColour (ink);
        g.setFont (bold (12.0f, 0.06f));
        g.drawText (kFxShort[fx], (int) r.getX() + 24, (int) r.getY() + 5, (int) r.getWidth() - 28, 16, juce::Justification::left);
        g.setFont (pixel (10.0f));
        g.setColour (ink.withAlpha (0.8f));
        g.drawText (juce::String (juce::roundToInt (proc.sf (s, mixParam (fx)))) + " %", (int) r.getX() + 24, (int) r.getY() + 21, (int) r.getWidth() - 30, 12, juce::Justification::left);

        // level of this effect's wet output, along the bottom edge
        const float lvl = proc.engine->ui.fxLevel[(size_t) s][(size_t) fx].load (std::memory_order_relaxed);
        const float db = juce::jlimit (0.0f, 1.0f, 1.0f + 20.0f * std::log10 (lvl + 1.0e-5f) / 48.0f);
        auto bar = juce::Rectangle<float> (r.getX() + 6.0f, r.getBottom() - 6.0f, r.getWidth() - 12.0f, 3.0f);
        g.setColour (sel ? c::onAccent.withAlpha (0.18f) : c::onPlate.withAlpha (0.12f));
        g.fillRect (bar);
        g.setColour (sel ? c::onAccent : c::accent);
        g.fillRect (bar.withWidth (bar.getWidth() * db));
    }

    TailsProcessor& proc;
    int selStream = 0, selFx = 0, pressed = -1, pressStream = 0, dragStream = 0, dragFx = 0, dragX = 0, dragSlot = 0;
    bool dragging = false, moved = false;
};

// ---------------------------------------------------------------- input, transient and tonal

/** The input in grey, the transient stream above the line, the tonal stream below it. */
class SplitWave : public juce::Component
{
public:
    explicit SplitWave (TailsProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        displayWell (g, r);
        auto in = r.reduced (9.0f, 9.0f);
        const float mid = in.getCentreY(), half = in.getHeight() * 0.5f;
        auto& u = proc.engine->ui;
        const int n = UiState::kWave, head = u.waveHead.load (std::memory_order_relaxed);
        const float dx = in.getWidth() / (float) n;
        auto level = [] (float v) { return std::pow (juce::jlimit (0.0f, 1.0f, v), 0.6f); };
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) ((head + 1 + i) % n);
            const float x = in.getX() + (float) i * dx;
            const float a = level (u.in[k].load (std::memory_order_relaxed)) * half;
            const float t = level (u.tr[k].load (std::memory_order_relaxed)) * half;
            const float s = level (u.to[k].load (std::memory_order_relaxed)) * half;
            g.setColour (c::onPlate.withAlpha (0.14f));
            g.fillRect (x, mid - a, juce::jmax (1.0f, dx), a * 2.0f);
            g.setColour (c::accent);
            g.fillRect (x, mid - t, juce::jmax (1.0f, dx), t);
            g.setColour (c::onPlate);
            g.fillRect (x, mid, juce::jmax (1.0f, dx), s);
        }
        g.setColour (c::onPlate.withAlpha (0.3f));
        g.fillRect (in.getX(), mid - 0.5f, in.getWidth(), 1.0f);
        g.setFont (pixel (10.0f));
        g.setColour (c::accent);
        g.drawText ("TRANSIENT", in.withHeight (14.0f).reduced (4.0f, 0.0f), juce::Justification::left);
        g.setColour (c::onPlate);
        g.drawText ("TONAL", in.withTrimmedTop (in.getHeight() - 14.0f).reduced (4.0f, 0.0f), juce::Justification::left);
        if (proc.state.getRawParameterValue ("g_split")->load() < 0.5f)
        {
            g.setColour (c::onPlate.withAlpha (0.8f));
            g.drawText ("SPLIT OFF: CROSSFADE", in.withHeight (14.0f).reduced (4.0f, 0.0f), juce::Justification::right);
        }
    }
private:
    TailsProcessor& proc;
};

/** Stereo level bars, for the input or the output. */
class Meter : public juce::Component
{
public:
    explicit Meter (std::atomic<float>& source) : value (source) {}
    void tick()
    {
        const float v = value.load (std::memory_order_relaxed);
        shown = juce::jmax (v, shown * 0.86f);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (c::track);
        g.fillRoundedRectangle (r, 2.0f);
        const float db = 20.0f * std::log10 (shown + 1.0e-6f);
        const float x = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        g.setColour (db > 0.0f ? c::signal : c::go);
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * x), 2.0f);
    }
private:
    std::atomic<float>& value;
    float shown = 0.0f;
};

// ---------------------------------------------------------------- the picture of each effect

/** What each effect is doing, drawn from its live values. */
class FxWell : public juce::Component
{
public:
    explicit FxWell (TailsProcessor& p) : proc (p) {}
    void setFx (int s, int f) { stream = s; fx = f; repaint(); }
    void tick() { ++frame; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        displayWell (g, r);
        auto in = r.reduced (12.0f);
        switch (fx)
        {
            case fxConv:   drawConv (g, in); break;
            case fxTape:   drawTape (g, in); break;
            case fxDelay:  drawDelay (g, in); break;
            case fxChorus: drawChorus (g, in); break;
            case fxRotary: drawRotary (g, in); break;
            default:       drawFilter (g, in); break;
        }
    }

private:
    float v (int p) const { return liveValue (proc, stream, p); }

    static void corner (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& a, const juce::String& b)
    {
        g.setFont (pixel (10.0f));
        g.setColour (c::onPlate);
        g.drawText (a, r.withHeight (14.0f), juce::Justification::left);
        g.setColour (c::accent);
        g.drawText (b, r.withHeight (14.0f), juce::Justification::right);
    }

    void drawConv (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const float t60 = proc.irT60 (stream) * v (cDecay) * 0.01f;
        corner (g, r, proc.irDisplayName (stream).toUpperCase(), "RT " + juce::String (t60, 2) + " S");
        auto plot = r.withTrimmedTop (22.0f).withTrimmedBottom (14.0f);
        if (proc.irFailed (stream))
        {
            g.setColour (c::accent);
            g.setFont (pixel (10.0f));
            g.drawText (proc.irDisplayName (stream) == "No file loaded" ? "DROP A WAV, AIFF OR FLAC HERE" : "CANNOT READ THIS FILE", plot, juce::Justification::centred);
            return;
        }
        // the file's envelope, and on top the envelope Decay leaves of it
        const float decay = juce::jmax (0.05f, v (cDecay) * 0.01f), rt = juce::jmax (0.05f, proc.irT60 (stream));
        const float seconds = rt * 1.6f;
        juce::Path file, shaped;
        const float w = plot.getWidth(), base = plot.getBottom(), h = plot.getHeight();
        file.startNewSubPath (plot.getX(), base); shaped.startNewSubPath (plot.getX(), base);
        for (int i = 0; i < kPreviewPoints; ++i)
        {
            const float x = (float) i / (float) (kPreviewPoints - 1), val = proc.irPreview (stream, i);
            const float t = x * seconds;
            const float cut = 60.0f * t / rt * (1.0f / decay - 1.0f) / 60.0f;      // dB-scale fraction removed
            file.lineTo (plot.getX() + x * w, base - val * h);
            shaped.lineTo (plot.getX() + x * w, base - juce::jmax (0.0f, val - cut) * h);
        }
        file.lineTo (plot.getRight(), base); shaped.lineTo (plot.getRight(), base);
        file.closeSubPath(); shaped.closeSubPath();
        g.setColour (c::onPlate.withAlpha (0.2f)); g.fillPath (file);
        g.setColour (c::accent); g.fillPath (shaped);
        g.setFont (pixel (10.0f));
        g.setColour (c::onPlate.withAlpha (0.7f));
        g.drawText ("0", plot.withY (base + 2).withHeight (12.0f), juce::Justification::left);
        g.drawText (juce::String (seconds, 1) + " S", plot.withY (base + 2).withHeight (12.0f), juce::Justification::right);
    }

    void drawTape (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const float ms = v (tSync) > 0.5f ? (float) (kDivisions[(int) v (tNote)].beats * 60000.0 / juce::jmax (1.0f, proc.uiBpm.load())) : v (tTime);
        static const char* modes[3] = { "ECHO", "REPEAT", "SWELL" };
        corner (g, r, modes[(int) v (tMode)], juce::String (juce::roundToInt (juce::jlimit (40.0f, 1500.0f, ms))) + " MS");
        auto area = r.withTrimmedTop (18.0f);
        const auto ctr = area.getCentre();
        const float rad = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 22.0f;
        g.setColour (c::onPlate.withAlpha (0.18f)); g.fillEllipse (ctr.x - rad, ctr.y - rad, rad * 2, rad * 2);
        g.setColour (c::onPlate.withAlpha (0.8f)); g.drawEllipse (ctr.x - rad, ctr.y - rad, rad * 2, rad * 2, 2.0f);
        // the memory turns at the speed the time asks for
        const float turn = (float) frame * 0.033f * 1000.0f / juce::jmax (40.0f, ms * 1.333f) * kTwoPi;
        for (int i = 0; i < 16; ++i)
        {
            const float a = turn + (float) i * kTwoPi / 16.0f;
            g.setColour (c::onPlate.withAlpha (0.35f));
            g.drawLine (ctr.x + rad * 0.72f * std::sin (a), ctr.y - rad * 0.72f * std::cos (a), ctr.x + rad * 0.92f * std::sin (a), ctr.y - rad * 0.92f * std::cos (a), 1.5f);
        }
        auto head = [&] (float angle, juce::String label, bool on, juce::Colour col) {
            const float hx = ctr.x + (rad + 11.0f) * std::sin (angle), hy = ctr.y - (rad + 11.0f) * std::cos (angle);
            g.setColour (on ? col : c::onPlate.withAlpha (0.2f));
            g.fillRoundedRectangle (hx - 10, hy - 9, 20, 18, 3);
            g.setColour (on ? c::onAccent : c::onPlate);
            g.setFont (pixel (10.0f));
            g.drawText (label, (int) hx - 10, (int) hy - 7, 20, 14, juce::Justification::centred);
        };
        head (-kPi * 0.75f, "R", true, c::signal.brighter (0.4f));
        for (int h = 0; h < 4; ++h)
            head (-kPi * 0.75f + (float) (h + 1) * kPi * 0.3f, juce::String (h + 1), v (tH1 + h) > 0.5f, c::accent);
    }

    void drawDelay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const float ms = v (dSync) > 0.5f ? (float) (kDivisions[(int) v (dNote)].beats * 60000.0 / juce::jmax (1.0f, proc.uiBpm.load())) : v (dTime);
        const bool ping = v (dPing) > 0.5f;
        corner (g, r, ping ? "PING PONG" : "STEREO", juce::String (juce::roundToInt (ms)) + " MS");
        auto plot = r.withTrimmedTop (22.0f);
        const float mid = plot.getCentreY(), fb = juce::jmin (0.98f, v (dFb) * 0.01f), ratio = v (dDual) * 0.01f;
        g.setColour (c::onPlate.withAlpha (0.25f));
        g.fillRect (plot.getX(), mid - 0.5f, plot.getWidth(), 1.0f);
        const float span = juce::jmax (ms * 8.0f * juce::jmax (1.0f, ratio), 200.0f), px = plot.getWidth() / span;
        for (int side = 0; side < 2; ++side)
        {
            float level = 1.0f;
            const float step = side == 0 ? ms : ms * ratio;
            for (int i = 1; i < 32; ++i)
            {
                const bool show = ! ping || (i % 2 == 1) == (side == 0);
                if (show)
                {
                    const float x = plot.getX() + (ping ? ms : step) * (float) i * px;
                    if (x >= plot.getRight()) break;
                    const float h = level * plot.getHeight() * 0.48f;
                    g.setColour (side == 0 ? c::accent : c::onPlate);
                    g.fillRect (x, side == 0 ? mid - h : mid, 3.0f, h);
                }
                level *= fb;
                if (level < 0.01f) break;
            }
        }
        g.setColour (c::onPlate.withAlpha (0.8f));
        g.setFont (pixel (10.0f));
        g.drawText ("L", plot.withHeight (14.0f), juce::Justification::left);
        g.drawText ("R", plot.withTrimmedTop (plot.getHeight() - 14.0f), juce::Justification::left);
        static const char* analog[5] = { "CLEAN", "ANALOG 1", "ANALOG 2", "ANALOG 3", "ANALOG 4" };
        g.drawText (juce::String (analog[(int) v (dAnalog)]) + (v (dLofi) > 0.5f ? " + LOFI" : ""), plot.withTrimmedTop (plot.getHeight() - 14.0f), juce::Justification::right);
    }

    void drawChorus (juce::Graphics& g, juce::Rectangle<float> r)
    {
        static const char* modes[3] = { "MONO", "STEREO", "WIDE" };
        corner (g, r, modes[(int) v (hMode)], juce::String (v (hRate), 2) + " HZ");
        auto plot = r.withTrimmedTop (22.0f);
        const float depth = v (hDepth) * 0.01f, mid = plot.getCentreY();
        const float amp = plot.getHeight() * 0.45f * juce::jmax (0.05f, depth);
        const int voices = (int) v (hMode) == 0 ? 1 : ((int) v (hMode) == 1 ? 2 : 4);
        const float offset = (float) frame * 0.033f * v (hRate);
        for (int k = 0; k < voices; ++k)
        {
            juce::Path p;
            const float ph = (float) k / (float) voices;
            for (int i = 0; i <= 80; ++i)
            {
                float t = (float) i / 80.0f * 3.0f - offset + ph;
                t -= std::floor (t);
                const float tri = 4.0f * std::abs (t - 0.5f) - 1.0f;
                const float x = plot.getX() + (float) i / 80.0f * plot.getWidth(), y = mid + tri * amp;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (k % 2 == 0 ? c::accent : c::onPlate);
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }
    }

    void drawRotary (juce::Graphics& g, juce::Rectangle<float> r)
    {
        auto& u = proc.engine->ui;
        const float hz = u.hornHz[(size_t) stream].load (std::memory_order_relaxed);
        static const char* names[3] = { "STOP", "CHORALE", "TREMOLO" };
        corner (g, r, names[(int) v (rSpeed)], juce::String (juce::roundToInt (juce::jmax (0.0f, hz) * 60.0f)) + " RPM");
        auto area = r.withTrimmedTop (18.0f);
        const float rad = juce::jmin (area.getWidth() * 0.25f, area.getHeight() * 0.5f) - 8.0f;
        auto rotor = [&] (juce::Point<float> ctr, float angle, bool horn, const char* name) {
            g.setColour (c::onPlate.withAlpha (0.16f)); g.fillEllipse (ctr.x - rad, ctr.y - rad, rad * 2, rad * 2);
            g.setColour (c::onPlate.withAlpha (0.8f)); g.drawEllipse (ctr.x - rad, ctr.y - rad, rad * 2, rad * 2, 1.5f);
            juce::Path p;
            if (horn) { p.addTriangle (-rad * 0.14f, 0, rad * 0.14f, 0, 0, 0); p.addQuadrilateral (-rad * 0.1f, 0, rad * 0.1f, 0, rad * 0.32f, -rad * 0.88f, -rad * 0.32f, -rad * 0.88f); p.addRectangle (-rad * 0.1f, 0, rad * 0.2f, rad * 0.8f); }
            else p.addPieSegment (-rad * 0.86f, -rad * 0.86f, rad * 1.72f, rad * 1.72f, -0.55f, 0.55f, 0.15f);
            g.setColour (c::accent);
            g.fillPath (p, juce::AffineTransform::rotation (angle).translated (ctr));
            g.setColour (c::onPlate);
            g.setFont (pixel (10.0f));
            g.drawText (name, (int) ctr.x - 40, (int) (ctr.y + rad + 2), 80, 12, juce::Justification::centred);
        };
        rotor ({ area.getX() + area.getWidth() * 0.27f, area.getCentreY() - 6 }, u.hornAngle[(size_t) stream].load (std::memory_order_relaxed), true, "HORN");
        rotor ({ area.getX() + area.getWidth() * 0.73f, area.getCentreY() - 6 }, u.drumAngle[(size_t) stream].load (std::memory_order_relaxed), false, "DRUM");
    }

    void drawFilter (juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::String tag;
        if (v (fHpOn) > 0.5f) tag << "HPF ";
        if (v (fBpOn) > 0.5f) tag << "BPF ";
        if (v (fLpOn) > 0.5f) tag << "LPF";
        corner (g, r, tag, {});
        auto plot = r.withTrimmedTop (18.0f);
        g.setColour (c::onPlate.withAlpha (0.14f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = plot.getX() + std::log (f / 20.0f) / std::log (1000.0f) * plot.getWidth();
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
        }
        auto svf = [] (float f, float fc, float q, int kind) {
            const float w = f / fc, a = 1.0f - w * w, b = w / q, d = 1.0f / std::sqrt (a * a + b * b);
            return kind == 0 ? d : (kind == 1 ? d * w * w : d * b);
        };
        auto reso = [] (float pc) { return 0.5f * std::pow (32.0f, pc * 0.01f); };
        juce::Path p;
        for (int i = 0; i <= 160; ++i)
        {
            const float x = (float) i / 160.0f, f = 20.0f * std::pow (1000.0f, x);
            float m = 1.0f;
            if (v (fHpOn) > 0.5f) { m *= svf (f, v (fHpF), reso (v (fHpQ)), 1); if (v (fHpS) > 0.5f) m *= svf (f, v (fHpF), 0.7071f, 1); }
            if (v (fLpOn) > 0.5f) { m *= svf (f, v (fLpF), reso (v (fLpQ)), 0); if (v (fLpS) > 0.5f) m *= svf (f, v (fLpF), 0.7071f, 0); }
            if (v (fBpOn) > 0.5f) { const float a = v (fBpM) * 0.01f; m *= (1.0f - a) + a * svf (f, v (fBpF), v (fBpQ), 2); }
            const float db = 20.0f * std::log10 (m + 1.0e-5f);
            const float y = plot.getY() + plot.getHeight() * juce::jlimit (0.0f, 1.0f, (24.0f - db) / 72.0f);
            const float px = plot.getX() + x * plot.getWidth();
            if (i == 0) p.startNewSubPath (px, y); else p.lineTo (px, y);
        }
        auto fill = p;
        fill.lineTo (plot.getRight(), plot.getBottom()); fill.lineTo (plot.getX(), plot.getBottom()); fill.closeSubPath();
        g.setColour (c::accent.withAlpha (0.28f)); g.fillPath (fill);
        g.setColour (c::accent); g.strokePath (p, juce::PathStrokeType (2.0f));
    }

    TailsProcessor& proc;
    int stream = 0, fx = 0, frame = 0;
};

// ---------------------------------------------------------------- sequencer

/** The eight lanes, each with its destination. */
class LaneList : public juce::Component
{
public:
    static constexpr int kRowH = 21;
    std::function<void (int)> onSelect;
    explicit LaneList (TailsProcessor& p) : proc (p) {}
    void setSelected (int l) { selected = l; repaint(); }
    void tick() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& table = DestTable::get();
        for (int l = 0; l < kLanes; ++l)
        {
            auto r = juce::Rectangle<float> (0.0f, (float) (l * kRowH), (float) getWidth(), (float) kRowH - 2.0f);
            const bool sel = l == selected, on = proc.lf (l, lOn) > 0.5f;
            g.setColour (sel ? c::accent : c::onPlate.withAlpha (on ? 0.1f : 0.04f));
            g.fillRoundedRectangle (r, 3.0f);
            const auto ink = sel ? c::onAccent : c::onPlate;
            g.setFont (black (11.0f));
            g.setColour (ink);
            g.drawText (juce::String (l + 1), 7, l * kRowH, 14, kRowH - 2, juce::Justification::left);
            g.setColour (on ? c::go : ink.withAlpha (0.25f));
            g.fillEllipse (23.0f, (float) (l * kRowH) + 5.5f, 9.0f, 9.0f);
            if (sel && on) { g.setColour (c::onAccent); g.drawEllipse (23.0f, (float) (l * kRowH) + 5.5f, 9.0f, 9.0f, 1.2f); }
            const int d = (int) proc.lf (l, lDest);
            juce::String name = "Off";
            if (d > 0) name = d < kNumGlobalDest ? kGlobalDestNames[d] : streamParams()[table.param[(size_t) (d - kNumGlobalDest)]].name;
            if (name == "On") name = "Chain On";
            g.setFont (medium (11.0f));
            g.setColour (ink.withAlpha (d > 0 ? 1.0f : 0.5f));
            g.drawFittedText (name, 37, l * kRowH, getWidth() - 62, kRowH - 2, juce::Justification::centredLeft, 1, 0.8f);
            if (d >= kNumGlobalDest)
            {
                static const char* who[3] = { "TR", "TO", "T+T" };
                g.setFont (pixel (10.0f));
                g.drawText (who[(int) proc.lf (l, lTarget)], getWidth() - 28, l * kRowH, 24, kRowH - 2, juce::Justification::right);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int l = juce::jlimit (0, kLanes - 1, e.y / kRowH);
        if (e.x >= 18 && e.x < 36) proc.setParam (TailsProcessor::lid (l, lOn), proc.lf (l, lOn) > 0.5f ? 0.0f : 1.0f);
        if (onSelect) onSelect (l);
    }

private:
    TailsProcessor& proc;
    int selected = 0;
};

/** The steps of one lane. Drag to draw, right-click to clear a step. */
class StepView : public juce::Component
{
public:
    explicit StepView (TailsProcessor& p) : proc (p) {}
    void setLane (int l) { lane = l; repaint(); }
    void tick() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0d0f13));
        g.fillRoundedRectangle (r, 4.0f);
        const float w = r.getWidth() / (float) kSteps, mid = r.getCentreY(), half = r.getHeight() * 0.5f - 3.0f;
        const int len = juce::jlimit (1, kSteps, (int) proc.lf (lane, lLen));
        const int pos = proc.engine->ui.pos[(size_t) lane].load (std::memory_order_relaxed);
        const bool live = proc.uiPlaying.load (std::memory_order_relaxed) && proc.lf (lane, lOn) > 0.5f;
        const auto kind = destKind();
        for (int i = 0; i < kSteps; ++i)
        {
            const float x = (float) i * w;
            if (i % 4 == 0) { g.setColour (c::onPlate.withAlpha (0.08f)); g.fillRect (x, 2.0f, w, r.getHeight() - 4.0f); }
            if (i >= len) continue;
            const float val = proc.engine->patterns[(size_t) lane].step[(size_t) i].load (std::memory_order_relaxed);
            g.setColour (c::accent);
            if (kind == T::Bool)
            {
                if (val > 0.0f) g.fillRect (x + 1.5f, mid - half, w - 3.0f, half * 2.0f);
                else { g.setColour (c::onPlate.withAlpha (0.2f)); g.fillRect (x + 1.5f, mid + half - 3.0f, w - 3.0f, 3.0f); }
            }
            else
            {
                const float h = val * half;
                g.fillRect (x + 1.5f, juce::jmin (mid, mid - h), w - 3.0f, juce::jmax (1.5f, std::abs (h)));
            }
            if (live && i == pos) { g.setColour (c::signal); g.drawRect (x + 0.5f, 1.5f, w - 1.0f, r.getHeight() - 3.0f, 1.5f); }
        }
        g.setColour (c::onPlate.withAlpha (0.28f));
        g.fillRect (0.0f, mid - 0.5f, r.getWidth(), 1.0f);
        g.setColour (c::onPlate.withAlpha (0.6f));
        g.fillRect ((float) len * w - 1.0f, 0.0f, 2.0f, r.getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override { draw (e); }
    void mouseDrag (const juce::MouseEvent& e) override { draw (e); }

private:
    T destKind() const
    {
        const int d = (int) proc.lf (lane, lDest);
        if (d < kNumGlobalDest) return T::Float;
        return streamParams()[DestTable::get().param[(size_t) (d - kNumGlobalDest)]].type;
    }

    void draw (const juce::MouseEvent& e)
    {
        const int i = juce::jlimit (0, kSteps - 1, e.x * kSteps / juce::jmax (1, getWidth()));
        float val = juce::jlimit (-1.0f, 1.0f, 1.0f - 2.0f * (float) e.y / (float) getHeight());
        const auto kind = destKind();
        if (e.mods.isPopupMenu()) val = kind == T::Bool ? -1.0f : 0.0f;
        else if (kind == T::Bool) val = val > 0.0f ? 1.0f : -1.0f;
        else if (std::abs (val) < 0.04f) val = 0.0f;
        proc.engine->patterns[(size_t) lane].step[(size_t) i].store (val, std::memory_order_relaxed);
        repaint();
    }

    TailsProcessor& proc;
    int lane = 0;
};

// ---------------------------------------------------------------- MIDI map page

/** Small number field in the family style: drag to change, double-click to reset. */
class NumBox : public juce::Component
{
public:
    NumBox (double lo, double hi, double step, double def)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (lo, hi, step);
        s.setDoubleClickReturnValue (true, def);
        s.onValueChange = [this] { repaint(); if (onChange) onChange (s.getValue()); };
        addAndMakeVisible (s);
    }
    void resized() override { s.setBounds (getLocalBounds()); }
    void paintOverChildren (juce::Graphics& g) override
    {
        g.setColour (c::ink);
        g.setFont (pixel (10.0f));
        g.drawText (format ? format (s.getValue()) : juce::String ((int) s.getValue()), getLocalBounds(), juce::Justification::centred);
    }
    void set (double v) { s.setValue (v, juce::dontSendNotification); repaint(); }
    juce::Slider s;
    std::function<void (double)> onChange;
    std::function<juce::String (double)> format;
};

/** Every parameter a slot can reach, as a menu with submenus. */
inline juce::PopupMenu parameterMenu (TailsProcessor& p)
{
    juce::PopupMenu root, global, lanes;
    for (auto id : { "g_focus", "g_smooth", "g_decay", "g_source", "g_split", "g_mix", "g_in", "g_out", "g_reset", "g_run" })
    {
        const int i = p.indexOfParam (id);
        global.addItem (i + 1, p.paramName (i));
    }
    root.addSubMenu ("Global", global);
    for (int s = 0; s < kStreams; ++s)
    {
        juce::PopupMenu streamMenu;
        std::map<int, juce::PopupMenu> groups;
        for (int q = 0; q < kNumP; ++q)
        {
            const int i = p.indexOfParam (TailsProcessor::sid (s, q));
            groups[streamParams()[q].fx].addItem (i + 1, streamParams()[q].name);
        }
        for (auto& g : groups) streamMenu.addSubMenu (g.first < kNumFx ? kFxNames[g.first] : "Stream", g.second);
        root.addSubMenu (kStreamNames[s], streamMenu);
    }
    for (int l = 0; l < kLanes; ++l)
    {
        juce::PopupMenu lane;
        for (int q = 0; q < kNumLaneP; ++q)
        {
            const int i = p.indexOfParam (TailsProcessor::lid (l, q));
            lane.addItem (i + 1, p.paramName (i));
        }
        lanes.addSubMenu ("Lane " + juce::String (l + 1), lane);
    }
    root.addSubMenu ("Sequencer", lanes);
    return root;
}

class MidiRow : public juce::Component
{
public:
    static constexpr int kH = 30;
    MidiRow (TailsProcessor& p, int index) : proc (p), slot (p.midiMap.slots[(size_t) index]), row (index)
    {
        for (auto* n : { "Off", "CC", "Note", "Pitch Bend", "Pressure" }) source.addItem (n, source.getNumItems() + 1);
        for (auto* n : { "Absolute", "Toggle", "Momentary", "Relative" }) mode.addItem (n, mode.getNumItems() + 1);
        channel.addItem ("Any", 1);
        for (int c = 1; c <= 16; ++c) channel.addItem (juce::String (c), c + 1);
        for (auto* b : { &source, &mode, &channel }) addAndMakeVisible (b);
        for (auto* b : { &number, &lo, &hi }) addAndMakeVisible (b);
        addAndMakeVisible (target); addAndMakeVisible (learn); addAndMakeVisible (clear);
        learn.setButtonText ("LEARN"); clear.setButtonText ("X");
        learn.setClickingTogglesState (true);
        learn.getProperties().set ("sgRec", true);

        lo.format = hi.format = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
        source.onChange = [this] { slot.source = source.getSelectedItemIndex(); slot.mode = slot.source.load() == msNote ? (int) mmMomentary : (int) mmAbsolute; refresh(); };
        mode.onChange = [this] { slot.mode = mode.getSelectedItemIndex(); };
        channel.onChange = [this] { slot.channel = channel.getSelectedItemIndex(); };
        number.onChange = [this] (double v) { slot.number = (int) v; };
        lo.onChange = [this] (double v) { slot.lo = (float) v; };
        hi.onChange = [this] (double v) { slot.hi = (float) v; };
        clear.onClick = [this] { slot.clear(); refresh(); };
        target.onClick = [this] {
            parameterMenu (proc).showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target), [this] (int r) {
                if (r <= 0) return;
                slot.param = r - 1;
                if (slot.source.load() == msOff) slot.source = (int) msCC;
                refresh();
            });
        };
        learn.onClick = [this] {
            if (learn.getToggleState()) proc.midiMap.learnParam = slot.param.load();
            else proc.midiMap.learnParam = -1;
        };
        refresh();
    }

    void refresh()
    {
        const int src = slot.source.load(), param = slot.param.load();
        source.setSelectedItemIndex (src, juce::dontSendNotification);
        mode.setSelectedItemIndex (slot.mode.load(), juce::dontSendNotification);
        channel.setSelectedItemIndex (slot.channel.load(), juce::dontSendNotification);
        number.set (slot.number.load());
        lo.set (slot.lo.load()); hi.set (slot.hi.load());
        target.setButtonText (param >= 0 ? proc.paramName (param) : juce::String ("Choose target"));
        number.setVisible (src == msCC || src == msNote);
        learn.setEnabled (param >= 0);
        const bool armed = param >= 0 && proc.midiMap.learnParam.load() == param;
        learn.setToggleState (armed, juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (row % 2 == 0 ? c::ink.withAlpha (0.05f) : juce::Colours::transparentBlack);
        g.fillRect (getLocalBounds());
        g.setColour (c::ink);
        g.setFont (black (12.0f));
        g.drawText (juce::String (row + 1), 6, 0, 22, kH, juce::Justification::left);
    }

    void resized() override
    {
        const int y = 4, h = 22;
        source.setBounds (30, y, 96, h);
        number.setBounds (132, y, 44, h);
        channel.setBounds (182, y, 58, h);
        target.setBounds (246, y - 3, 216, h + 6);
        mode.setBounds (468, y, 100, h);
        lo.setBounds (574, y, 56, h);
        hi.setBounds (636, y, 56, h);
        learn.setBounds (698, y - 3, 82, h + 6);
        clear.setBounds (784, y - 3, 30, h + 6);
    }

private:
    TailsProcessor& proc;
    MidiSlot& slot;
    int row;
    juce::ComboBox source, mode, channel;
    NumBox number { 0, 127, 1, 1 }, lo { 0, 1, 0.01, 0 }, hi { 0, 1, 0.01, 1 };
    juce::TextButton target, learn, clear;
};

class MidiPage : public juce::Component
{
public:
    explicit MidiPage (TailsProcessor& p) : proc (p)
    {
        for (int i = 0; i < kMidiSlots; ++i)
        {
            auto* r = rows.add (new MidiRow (p, i));
            r->setBounds (0, i * MidiRow::kH, kW, MidiRow::kH);
            holder.addAndMakeVisible (r);
        }
        holder.setSize (kW, kMidiSlots * MidiRow::kH);
        view.setViewedComponent (&holder, false);
        view.setScrollBarsShown (true, false);
        view.setScrollBarThickness (8);
        addAndMakeVisible (view);
        addAndMakeVisible (clearAll);
        clearAll.setButtonText ("CLEAR ALL");
        clearAll.onClick = [this] { proc.midiMap.clearAll(); refresh(); };
    }

    void refresh() { for (auto* r : rows) r->refresh(); repaint(); }

    void tick()
    {
        const int serial = proc.midiMap.learnedSerial.load();
        if (serial != seenSerial) { seenSerial = serial; refresh(); }
        const int last = proc.midiMap.lastSerial.load();
        if (last != seenLast) { seenLast = last; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (c::inkSoft);
        g.setFont (medium (12.0f));
        g.drawText ("Right-click any control and choose MIDI Learn, or fill in a row here.", 210, 10, 390, 18, juce::Justification::left);
        static const char* src[5] = { "-", "CC", "NOTE", "BEND", "PRESSURE" };
        const int s = proc.midiMap.lastSource.load();
        const int armed = proc.midiMap.learnParam.load();
        g.setFont (pixel (10.0f));
        g.setColour (armed >= 0 ? c::signal : c::ink);
        g.drawText (armed >= 0 ? "LEARN: MOVE A CONTROL ON YOUR DEVICE"
                               : (s == 0 ? juce::String ("NO MIDI YET") : "LAST: " + juce::String (src[s]) + (s <= 2 ? " " + juce::String (proc.midiMap.lastNumber.load()) : juce::String()) + "  CH " + juce::String (proc.midiMap.lastChannel.load())),
                    210, 28, 390, 14, juce::Justification::left);
        g.setColour (c::ink);
        g.setFont (bold (11.0f, 0.06f));
        auto head = [&] (const char* t, int x, int w) { g.drawText (t, x, kHeadY, w, 14, juce::Justification::left); };
        head ("SOURCE", 30, 96); head ("NO", 132, 44); head ("CH", 182, 58); head ("TARGET", 254, 200); head ("MODE", 468, 100); head ("MIN", 574, 56); head ("MAX", 636, 56);
    }

    void resized() override
    {
        clearAll.setBounds (getWidth() - 116, 6, 112, 30);
        view.setBounds (0, kHeadY + 18, getWidth(), getHeight() - kHeadY - 18);
    }

private:
    static constexpr int kW = 820, kHeadY = 44;
    TailsProcessor& proc;
    juce::Viewport view;
    juce::Component holder;
    juce::OwnedArray<MidiRow> rows;
    juce::TextButton clearAll;
    int seenSerial = -1, seenLast = -1;
};
} // namespace tails
