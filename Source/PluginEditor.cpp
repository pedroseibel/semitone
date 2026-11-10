#include "PluginEditor.h"

namespace
{
    const juce::String minusSign = juce::String::fromUTF8 ("\xe2\x88\x92");
    const juce::String arrowSign = juce::String::fromUTF8 ("\xe2\x86\x92");

    const char* const intervalNames[] = { "Unison", "Minor second", "Major second", "Minor third", "Major third",
                                          "Perfect fourth", "Tritone", "Perfect fifth", "Minor sixth", "Major sixth",
                                          "Minor seventh", "Major seventh", "Octave" };

    const char* const modeDescriptions[] = { "Vocals \xc2\xb7 keys \xc2\xb7 full mixes", "Mixed material", "Drums \xc2\xb7 percussion" };

    float textWidth (const juce::Font& f, const juce::String& s)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (f, s, 0.0f, 0.0f);
        return glyphs.getBoundingBox (0, glyphs.getNumGlyphs(), true).getWidth();
    }

    juce::String signedText (double v, int decimals)
    {
        const auto magnitude = decimals > 0 ? juce::String (std::abs (v), decimals)
                                            : juce::String (juce::roundToInt (std::abs (v)));
        const bool isZero = magnitude.containsOnly ("0.");
        return (isZero ? juce::String() : (v > 0 ? juce::String ("+") : minusSign)) + magnitude;
    }

    /** A groove cut into the panel: a dark line with a light edge beneath it. */
    void drawGroove (juce::Graphics& g, float x1, float x2, float y)
    {
        if (x2 - x1 < 1.0f) return;
        g.setColour (ui::black (0.17f));
        g.fillRect (x1, y, x2 - x1, 1.0f);
        g.setColour (ui::white (0.65f));
        g.fillRect (x1, y + 1.0f, x2 - x1, 1.0f);
    }

    /** A section heading printed on the panel, with a groove running out from it. */
    void drawHeading (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, bool centred)
    {
        const auto f = ui::label (10.5f, 0.16f);
        const float w = textWidth (f, text);
        const auto r = area.toFloat();
        const float y = r.getCentreY() - 1.0f;

        g.setColour (ui::inkDim);
        g.setFont (f);

        if (centred)
        {
            g.drawText (text, r, juce::Justification::centred, false);
            drawGroove (g, r.getX(), r.getCentreX() - w * 0.5f - 7.0f, y);
            drawGroove (g, r.getCentreX() + w * 0.5f + 7.0f, r.getRight(), y);
        }
        else
        {
            g.drawText (text, r, juce::Justification::centredLeft, false);
            drawGroove (g, r.getX() + w + 8.0f, r.getRight(), y);
        }
    }

    /** A black fader cap: ridged, slightly dished, with a white indicator line across the middle.
        `vertical` means it travels up and down. */
    void drawCapBody (juce::Graphics& g, juce::Rectangle<float> r, bool vertical, float cornerRadius, int ridgesPerSide)
    {
        juce::ColourGradient body (juce::Colour (0xff4a4a47), r.getCentreX(), r.getY(),
                                   juce::Colour (0xff0e0e0d), r.getCentreX(), r.getBottom(), false);
        body.addColour (0.07, juce::Colour (0xff232322));
        body.addColour (0.50, juce::Colour (0xff1a1a19));
        body.addColour (0.93, juce::Colour (0xff2c2c2a));
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, cornerRadius);

        const auto centre = r.getCentre();
        auto bar = [&] (float offset, float thickness, float inset)
        {
            return vertical ? juce::Rectangle<float> (r.getX() + inset, centre.y + offset - thickness * 0.5f, r.getWidth() - 2.0f * inset, thickness)
                            : juce::Rectangle<float> (centre.x + offset - thickness * 0.5f, r.getY() + inset, thickness, r.getHeight() - 2.0f * inset);
        };

        for (int i = 1; i <= ridgesPerSide; ++i)
            for (float side : { -1.0f, 1.0f })
            {
                const float offset = side * (3.0f + 5.0f * (float) i);
                g.setColour (ui::black (0.55f));
                g.fillRect (bar (offset, 1.0f, 4.0f));
                g.setColour (ui::white (0.13f));
                g.fillRect (bar (offset + 1.0f, 1.0f, 4.0f));
            }

        g.setColour (ui::capLine);
        g.fillRoundedRectangle (bar (0.0f, 2.0f, 3.0f), 1.0f);
    }

    constexpr float capShadowPad = 22.0f;   // room around the cap image for its shadow

    //==========================================================================
    // seven-segment digits
    int segmentsFor (juce::juce_wchar c)
    {
        switch (c)   // bit 0 = top, then clockwise, bit 6 = middle
        {
            case '0': return 0x3f;  case '1': return 0x06;  case '2': return 0x5b;  case '3': return 0x4f;
            case '4': return 0x66;  case '5': return 0x6d;  case '6': return 0x7d;  case '7': return 0x07;
            case '8': return 0x7f;  case '9': return 0x6f;  case '-': return 0x40;  case '+': return 0x40;
            default:  return 0;
        }
    }

    juce::Path segmentPath (int segment, float w, float h, float t)
    {
        const float gap = t * 0.16f, half = t * 0.5f, mid = h * 0.5f;
        juce::Path p;

        auto horizontal = [&] (float y)
        {
            const float x1 = half + gap, x2 = w - half - gap;
            p.startNewSubPath (x1, y);
            p.lineTo (x1 + half, y - half);  p.lineTo (x2 - half, y - half);  p.lineTo (x2, y);
            p.lineTo (x2 - half, y + half);  p.lineTo (x1 + half, y + half);  p.closeSubPath();
        };
        auto vertical = [&] (float x, float y1, float y2)
        {
            p.startNewSubPath (x, y1);
            p.lineTo (x + half, y1 + half);  p.lineTo (x + half, y2 - half);  p.lineTo (x, y2);
            p.lineTo (x - half, y2 - half);  p.lineTo (x - half, y1 + half);  p.closeSubPath();
        };

        switch (segment)
        {
            case 0: horizontal (half); break;
            case 1: vertical (w - half, half + gap, mid - gap); break;
            case 2: vertical (w - half, mid + gap, h - half - gap); break;
            case 3: horizontal (h - half); break;
            case 4: vertical (half, mid + gap, h - half - gap); break;
            case 5: vertical (half, half + gap, mid - gap); break;
            default: horizontal (mid); break;
        }
        return p;
    }
}

//==============================================================================
bool ui::Spring::reduceMotion = false;

bool ui::Spring::step (float dt)
{
    if (juce::exactlyEqual (value, target) && juce::exactlyEqual (velocity, 0.0f))
        return false;

    if (positional && reduceMotion)
    {
        snap (target);
        return true;
    }

    const float d = value - target;
    const float decay = std::exp (-omega * dt);
    const float c = velocity + omega * d;
    value = target + (d + c * dt) * decay;
    velocity = (velocity - omega * c * dt) * decay;

    if (std::abs (value - target) < 0.0005f && std::abs (velocity) < 0.01f)
        snap (target);

    return true;
}

namespace
{
    /** The condensed DIN-style face each platform ships with, chosen once. */
    struct PanelFace
    {
        juce::String family, style;
        float horizontalScale = 1.0f;   // used to squeeze a regular-width face when no condensed one exists
    };

    const PanelFace& panelFace()
    {
        static const PanelFace face = []
        {
           #if JUCE_MAC
            return PanelFace { "DIN Condensed", "Bold", 1.0f };
           #else
            // Windows 10 and later ship Bahnschrift, Microsoft's DIN. Its condensed widths are
            // instances of a variable font, and JUCE 8.0.4 draws those with the advance widths
            // of the regular width (letters come out narrow but spaced far apart). So take the
            // regular-width bold and squeeze it, which keeps outlines and spacing consistent.
            const juce::String family = juce::Font::findAllTypefaceNames().contains ("Bahnschrift") ? "Bahnschrift" : "Arial";
            const auto styles = juce::Font::findAllTypefaceStyles (family);
            return PanelFace { family, styles.contains ("Bold") ? "Bold" : "Regular", 0.72f };
           #endif
        }();
        return face;
    }

    juce::Font bodyFont (float points)
    {
       #if JUCE_MAC
        return juce::Font (juce::FontOptions (".AppleSystemUIFont", "Regular", 12.0f).withPointHeight (points));
       #elif JUCE_WINDOWS
        return juce::Font (juce::FontOptions ("Segoe UI", "Regular", 12.0f).withPointHeight (points));
       #else
        return juce::Font (juce::FontOptions().withPointHeight (points));
       #endif
    }
}

juce::Font ui::label (float points, float tracking)
{
    const auto& face = panelFace();
    return juce::Font (juce::FontOptions (face.family, face.style, 12.0f).withPointHeight (points))
        .withHorizontalScale (face.horizontalScale)
        .withExtraKerningFactor (tracking);
}

void ui::drawTabular (juce::Graphics& g, const juce::Font& f, const juce::String& text,
                      juce::Rectangle<float> area, juce::Justification justification)
{
    float digitWidth = 0.0f;
    for (juce::juce_wchar c = '0'; c <= '9'; ++c)
        digitWidth = juce::jmax (digitWidth, textWidth (f, juce::String::charToString (c)));

    juce::Array<float> widths;
    float total = 0.0f;
    for (auto c : text)
    {
        const float w = juce::CharacterFunctions::isDigit (c) ? digitWidth : textWidth (f, juce::String::charToString (c));
        widths.add (w);
        total += w;
    }

    float x = justification.testFlags (juce::Justification::right) ? area.getRight() - total
            : (justification.testFlags (juce::Justification::horizontallyCentred) ? area.getCentreX() - total * 0.5f : area.getX());

    g.setFont (f);
    int i = 0;
    for (auto c : text)
    {
        const float w = widths[i++];
        g.drawText (juce::String::charToString (c), juce::Rectangle<float> (x - 4.0f, area.getY(), w + 8.0f, area.getHeight()),
                    juce::Justification::centred, false);
        x += w;
    }
}

juce::Rectangle<float> ui::drawLcd (juce::Graphics& g, juce::Rectangle<float> bounds, float bezelWidth, float cornerRadius)
{
    // set into the panel: the lower lip catches the light
    g.setColour (white (0.75f));
    g.fillRoundedRectangle (bounds.translated (0.0f, 1.0f), cornerRadius);
    g.setColour (bezel);
    g.fillRoundedRectangle (bounds, cornerRadius);

    const auto glass = bounds.reduced (bezelWidth);
    const float glassRadius = juce::jmax (1.0f, cornerRadius - bezelWidth);   // concentric with the bezel
    g.setGradientFill (juce::ColourGradient (lcdTop, glass.getX(), glass.getY(), lcdBottom, glass.getX(), glass.getBottom(), false));
    g.fillRoundedRectangle (glass, glassRadius);

    // the bezel shades the top of the glass
    juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (glass, glassRadius);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (black (0.22f), glass.getX(), glass.getY(), black (0.0f), glass.getX(), glass.getY() + 7.0f, false));
    g.fillRect (glass.withHeight (7.0f));

    return glass;
}

void ui::drawSegments (juce::Graphics& g, const juce::String& text, juce::Point<float> topLeft,
                       float cellWidth, float cellHeight, float gap)
{
    const float t = cellHeight * 0.115f;
    const float slant = 0.09f;
    float x = topLeft.x;

    for (auto c : text)
    {
        if (c == '.')
        {
            g.setColour (lcdInk);
            g.fillRect (x - gap * 0.5f, topLeft.y + cellHeight - t, t, t);
            x += t + gap * 0.5f;
            continue;
        }

        const auto transform = juce::AffineTransform::shear (-slant, 0.0f).translated (x + slant * cellHeight, topLeft.y);
        const bool isSign = (c == '+' || c == '-');
        const int lit = segmentsFor (c);

        for (int s = 0; s < 7; ++s)
        {
            const bool on = (lit & (1 << s)) != 0;
            if (! on && isSign && s != 6) continue;          // a sign cell only has its own two strokes
            g.setColour (lcdInk.withAlpha (on ? 1.0f : 0.07f));
            g.fillPath (segmentPath (s, cellWidth, cellHeight, t), transform);
        }

        if (isSign)   // the upright stroke of the plus sign, ghosted for a minus
        {
            juce::Path upright;
            upright.addRectangle (cellWidth * 0.5f - t * 0.5f, cellHeight * 0.3f, t, cellHeight * 0.4f);
            g.setColour (lcdInk.withAlpha (c == '+' ? 1.0f : 0.07f));
            g.fillPath (upright, transform);
        }

        x += cellWidth + gap;
    }
}

//==============================================================================
ConsoleFader::ConsoleFader()
{
    setSliderStyle (juce::Slider::LinearVertical);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setTitle ("Semitones");
}

bool ConsoleFader::animate (float dt)
{
    // Follow the hand tightly while dragging; glide only on jumps (reset, automation).
    display.omega = isMouseButtonDown() ? 70.0f : 24.0f;
    const bool a = display.step (dt), b = hover.step (dt), c = press.step (dt);
    if (a || b || c) repaint();
    return a || b || c;
}

/** The cap never changes shape and its soft shadow is the expensive part, so draw it once. */
void ConsoleFader::renderCap (float scale)
{
    capScale = scale;
    const float w = (float) capWidth + 2.0f * capShadowPad, h = (float) capHeight + 2.0f * capShadowPad;
    cap = juce::Image (juce::Image::ARGB, juce::roundToInt (w * scale), juce::roundToInt (h * scale), true);

    juce::Graphics g (cap);
    g.addTransform (juce::AffineTransform::scale (scale));

    const auto body = juce::Rectangle<float> (capShadowPad, capShadowPad, (float) capWidth, (float) capHeight);
    juce::Path shape;
    shape.addRoundedRectangle (body, 4.0f);

    // elevation over the panel: a wide soft shadow plus a tight contact shadow
    juce::DropShadow (ui::black (0.34f), 14, { 0, 8 }).drawForPath (g, shape);
    juce::DropShadow (ui::black (0.36f), 4, { 0, 2 }).drawForPath (g, shape);

    drawCapBody (g, body, true, 4.0f, 3);
}

void ConsoleFader::paint (juce::Graphics& g)
{
    const float scale = (float) g.getInternalContext().getPhysicalPixelScaleFactor();
    if (cap.isNull() || std::abs (scale - capScale) > 0.01f)
        renderCap (scale);

    const float cx = 57.0f;                                // centre line of the slot
    const float top = getPositionOfValue (getMaximum());   // y of +12
    const float bottom = getPositionOfValue (getMinimum());
    const float value = display.value;

    auto yFor = [this, top, bottom] (float semitones)
    {
        return bottom + (semitones - (float) getMinimum()) / (float) (getMaximum() - getMinimum()) * (top - bottom);
    };

    // ---- printed scale on both sides of the slot; unity is marked in orange ----
    const float gap = 19.0f;   // from slot centre to where the ticks start: just clear of the cap

    for (int i = -12; i <= 12; ++i)
    {
        const bool unity = (i == 0);
        const float length = unity ? 11.0f : (i % 3 == 0 ? 8.0f : 4.5f);
        const float weight = unity ? 2.0f : (i % 3 == 0 ? 1.5f : 1.0f);
        const float y = yFor ((float) i);

        g.setColour (unity ? ui::accent : ui::ink.withAlpha (i % 3 == 0 ? 0.85f : 0.5f));
        g.fillRect (cx - gap - length, y - weight * 0.5f, length, weight);
        g.fillRect (cx + gap, y - weight * 0.5f, length, weight);
    }

    g.setFont (ui::label (11.0f, 0.04f));
    for (int i = -12; i <= 12; i += 3)
    {
        g.setColour (i == 0 ? ui::ink : ui::ink.withAlpha (0.72f));
        g.drawText (i == 0 ? juce::String ("0") : signedText ((double) i, 0),
                    juce::Rectangle<float> (0.0f, yFor ((float) i) - 6.0f, cx - gap - 16.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    // ---- the slot, cut into the panel ----
    const auto slot = juce::Rectangle<float> (cx - 2.5f, top - 13.0f, 5.0f, bottom - top + 26.0f);
    g.setColour (ui::white (0.80f));
    g.fillRoundedRectangle (slot.translated (0.0f, 1.0f).expanded (0.5f, 0.0f), 3.0f);
    g.setColour (ui::black (0.20f));
    g.fillRoundedRectangle (slot.expanded (0.75f), 3.25f);
    g.setColour (juce::Colour (0xff161615));
    g.fillRoundedRectangle (slot, 2.5f);

    // how far the cap sits from unity, lit inside the slot
    const float y = yFor (value), origin = yFor (0.0f);
    g.setColour (ui::accent);
    g.fillRoundedRectangle (cx - 1.0f, juce::jmin (y, origin), 2.0f, std::abs (y - origin), 1.0f);

    // ---- the cap: lifts a touch on hover, presses in when grabbed ----
    const float capZoom = 1.0f + 0.03f * hover.value - 0.05f * press.value;
    g.drawImage (cap, juce::Rectangle<float> (((float) capWidth + 2.0f * capShadowPad) * capZoom,
                                               ((float) capHeight + 2.0f * capShadowPad) * capZoom).withCentre ({ cx, y }));
}

//==============================================================================
MiniFader::MiniFader (const juce::String& name)
{
    setSliderStyle (juce::Slider::LinearHorizontal);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setTitle (name);
}

bool MiniFader::animate (float dt)
{
    const bool a = hover.step (dt), b = press.step (dt);
    if (a || b) repaint();
    return a || b;
}

void MiniFader::paint (juce::Graphics& g)
{
    const float cy = (float) getHeight() * 0.5f;
    const float left = getPositionOfValue (getMinimum()), right = getPositionOfValue (getMaximum());
    const float x = getPositionOfValue (getValue());
    const bool bipolar = getMinimum() < 0.0 && getMaximum() > 0.0;
    const float origin = bipolar ? getPositionOfValue (0.0) : left;

    // printed line with end stops, and a mark where "no change" lives
    g.setColour (ui::ink.withAlpha (0.85f));
    g.fillRect (left, cy - 1.0f, right - left, 2.0f);
    g.fillRect (left - 1.0f, cy - 4.0f, 1.5f, 8.0f);
    g.fillRect (right - 0.5f, cy - 4.0f, 1.5f, 8.0f);

    if (bipolar)
    {
        g.setColour (ui::accent);
        g.fillRect (origin - 0.75f, cy - 7.0f, 1.5f, 5.0f);
    }

    g.setColour (ui::accent);
    g.fillRect (juce::jmin (origin, x), cy - 1.0f, std::abs (x - origin), 2.0f);

    const auto handle = juce::Rectangle<float> ((float) capLength, 24.0f + 1.5f * hover.value - 1.5f * press.value).withCentre ({ x, cy });
    g.setColour (ui::black (0.08f));
    g.fillRoundedRectangle (handle.expanded (1.5f, 0.0f).translated (0.0f, 3.5f), 4.5f);
    g.setColour (ui::black (0.20f));
    g.fillRoundedRectangle (handle.expanded (0.5f, 0.0f).translated (0.0f, 1.5f), 3.5f);
    drawCapBody (g, handle, false, 3.0f, 0);
}

//==============================================================================
KeyButton::KeyButton (const juce::String& text) : juce::Button (text)
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (false);   // leave the keyboard to the host (space bar = transport)
}

bool KeyButton::animate (float dt)
{
    led.set (getToggleState() ? 1.0f : 0.0f);
    travel.set (isDown() ? 1.0f : 0.0f);
    hover.set (isOver() ? 1.0f : 0.0f);
    const bool a = led.step (dt), b = travel.step (dt), c = hover.step (dt);
    if (a || b || c) repaint();
    return a || b || c;
}

void KeyButton::paintButton (juce::Graphics& g, bool, bool)
{
    // The key sits 2 px proud of the panel and travels down by that much when pressed.
    const float down = 2.0f * juce::jlimit (0.0f, 1.0f, travel.value);
    const auto seat = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (3.0f);
    const auto key = seat.translated (0.0f, down);
    const float radius = 5.0f;

    // shadows for elevation: ambient, then the key's own edge; both shrink as it goes down
    g.setColour (ui::black (0.10f * (1.0f - 0.5f * travel.value)));
    g.fillRoundedRectangle (seat.translated (0.0f, 3.0f).expanded (1.0f, 0.0f), radius + 1.0f);
    g.setColour (ui::black (0.32f));
    g.fillRoundedRectangle (seat.translated (0.0f, 2.0f).expanded (0.5f), radius + 0.5f);
    g.setColour (ui::black (0.28f));
    g.fillRoundedRectangle (key.expanded (0.75f), radius + 0.75f);

    const float lift = 0.5f * hover.value - travel.value;   // lighter under the pointer, darker when held
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f5ee).interpolatedWith (juce::Colour (0xffe9e4d8), juce::jlimit (0.0f, 1.0f, -lift)),
                                             key.getX(), key.getY(),
                                             juce::Colour (0xffe4decf).interpolatedWith (juce::Colour (0xffeee9de), juce::jlimit (0.0f, 1.0f, lift)),
                                             key.getX(), key.getBottom(), false));
    g.fillRoundedRectangle (key, radius);
    g.setColour (ui::white (0.85f));
    g.fillRoundedRectangle (key.getX() + radius, key.getY() + 0.5f, key.getWidth() - 2.0f * radius, 1.0f, 0.5f);

    // LED
    const float on = juce::jlimit (0.0f, 1.0f, led.value);
    const juce::Point<float> ledCentre (key.getX() + 12.0f, key.getCentreY());
    if (on > 0.01f)
    {
        g.setColour (ui::accent.withAlpha (0.10f * on));
        g.fillEllipse (juce::Rectangle<float> (15.0f, 15.0f).withCentre (ledCentre));
        g.setColour (ui::accent.withAlpha (0.20f * on));
        g.fillEllipse (juce::Rectangle<float> (10.5f, 10.5f).withCentre (ledCentre));
    }
    g.setColour (ui::black (0.35f));
    g.fillEllipse (juce::Rectangle<float> (7.5f, 7.5f).withCentre (ledCentre));
    g.setColour (juce::Colour (0xffb9b2a1).interpolatedWith (ui::accent, on));
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (ledCentre));
    g.setColour (ui::white (0.25f + 0.3f * on));
    g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre (ledCentre.translated (-1.0f, -1.0f)));

    g.setColour (ui::ink);
    g.setFont (ui::label (13.0f, 0.06f));
    g.drawFittedText (getButtonText().toUpperCase(), key.withTrimmedLeft (21.0f).withTrimmedRight (3.0f).translated (0.0f, 1.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.75f);
}

//==============================================================================
void LcdDisplay::paint (juce::Graphics& g)
{
    const auto glass = ui::drawLcd (g, getLocalBounds().toFloat().withTrimmedBottom (1.0f), 6.0f, 9.0f);

    const int semis = juce::roundToInt (getSemitones ? getSemitones() : 0.0);
    const int cents = juce::roundToInt (getCents ? getCents() : 0.0);
    const double ratio = std::pow (2.0, ((double) semis + (double) cents * 0.01) / 12.0);

    const float x0 = glass.getX() + 18.0f, y0 = glass.getY();
    const auto caption = ui::label (10.0f, 0.14f);

    auto drawCaption = [&] (const juce::String& s, float x, float y)
    {
        g.setColour (ui::lcdInk.withAlpha (0.72f));
        g.setFont (caption);
        g.drawText (s, juce::Rectangle<float> (x, y, 160.0f, 12.0f), juce::Justification::centredLeft, false);
    };

    auto sign = [] (int v) { return v > 0 ? "+" : (v < 0 ? "-" : " "); };

    // ---- semitones, large ----
    drawCaption ("SEMITONES", x0, y0 + 11.0f);
    ui::drawSegments (g, juce::String (sign (semis)) + juce::String (std::abs (semis)).paddedLeft (' ', 2),
                      { x0, y0 + 30.0f }, 33.0f, 60.0f, 9.0f);

    g.setColour (ui::lcdInk.withAlpha (0.22f));
    g.fillRect (x0 + 140.0f, y0 + 13.0f, 1.0f, glass.getHeight() - 26.0f);

    // ---- cents, ratio, and what A 440 becomes ----
    const float c1 = x0 + 160.0f, c2 = c1 + 94.0f, c3 = c2 + 104.0f;
    const float cellW = 15.0f, cellH = 28.0f, cellGap = 4.5f;

    drawCaption ("CENTS", c1, y0 + 11.0f);
    ui::drawSegments (g, juce::String (sign (cents)) + juce::String (std::abs (cents)).paddedLeft (' ', 3), { c1, y0 + 30.0f }, cellW, cellH, cellGap);

    drawCaption ("RATIO", c2, y0 + 11.0f);
    ui::drawSegments (g, juce::String (ratio, 3), { c2, y0 + 30.0f }, cellW, cellH, cellGap);

    drawCaption ("A 440 " + arrowSign + " HZ", c3, y0 + 11.0f);
    ui::drawSegments (g, juce::String (440.0 * ratio, 1), { c3, y0 + 30.0f }, cellW, cellH, cellGap);

    // ---- interval, spelled out ----
    drawCaption ("INTERVAL", c1, y0 + 68.0f);
    g.setColour (ui::lcdInk);
    g.setFont (ui::label (21.0f, 0.06f));
    g.drawText ((semis == 0 ? juce::String ("Unison")
                            : juce::String (intervalNames[std::abs (semis)]) + (semis > 0 ? " up" : " down")).toUpperCase(),
                juce::Rectangle<float> (c1, y0 + 80.0f, glass.getRight() - c1 - 12.0f, 22.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
SemitoneLookAndFeel::SemitoneLookAndFeel()
{
    setColour (juce::TooltipWindow::textColourId, juce::Colours::white);
}

static juce::TextLayout layoutTooltip (const juce::String& text)
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, bodyFont (12.0f), juce::Colour (0xfff1eee6));
    juce::TextLayout layout;
    layout.createLayout (s, 230.0f);
    return layout;
}

juce::Rectangle<int> SemitoneLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltip (text);
    const int w = juce::roundToInt (layout.getWidth()) + 20;
    const int h = juce::roundToInt (layout.getHeight()) + 14;

    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 16,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h)
        .constrainedWithin (parentArea);
}

void SemitoneLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (ui::bezel);
    g.fillRoundedRectangle (bounds, 6.0f);
    layoutTooltip (text).draw (g, bounds.reduced (10.0f, 7.0f));
}

//==============================================================================
SemitoneEditor::SemitoneEditor (SemitoneProcessor& p)
    : AudioProcessorEditor (p), owner (p),
      fine ("Fine", [] (double v) { return signedText (v, 0) + " CT"; }),
      mix ("Mix", [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; }),
      output ("Output", [] (double v) { return signedText (v, 1) + " DB"; })
{
    ui::Spring::reduceMotion = semitoneShouldReduceMotion();
    setLookAndFeel (&lookAndFeel);

    lcd.getSemitones = [this] { return fader.getValue(); };
    lcd.getCents = [this] { return fine.fader.getValue(); };
    lcd.setInterceptsMouseClicks (false, false);

    fader.onValueChange = [this] { lcd.repaint(); };
    fader.setTooltip ("Pitch in semitones. Double-click to return to 0.");
    fine.fader.onValueChange = [this] { lcd.repaint(); repaint (fine.valueArea); };
    mix.fader.onValueChange = [this] { repaint (mix.valueArea); };
    output.fader.onValueChange = [this] { repaint (output.valueArea); };

    const char* modeTips[] = { "Longest window. Cleanest on vocals, bass, keys, pads and full mixes.",
                               "Medium window. A middle ground for mixed material.",
                               "Shortest window and lowest latency. For drums and percussion; keep bass out of it." };

    for (int i = 0; i < SemitoneProcessor::numModes; ++i)
    {
        auto& key = modeKeys[i];
        key.setRadioGroupId (1);
        key.setTooltip (modeTips[i]);
        key.onClick = [this, i]
        {
            if (modeKeys[i].getToggleState() && modeAttachment != nullptr)
                modeAttachment->setValueAsCompleteGesture ((float) i);
        };
        addAndMakeVisible (key);
    }

    attackKey.setTooltip ("Keeps drum hits and note attacks tight instead of smearing them.");

    for (auto* c : std::initializer_list<juce::Component*> { &lcd, &fader, &fine.fader, &mix.fader, &output.fader, &attackKey })
        addAndMakeVisible (c);

    auto& state = owner.apvts;
    faderAttachment  = std::make_unique<SliderAttachment> (state, "semitones", fader);
    fineAttachment   = std::make_unique<SliderAttachment> (state, "cents", fine.fader);
    mixAttachment    = std::make_unique<SliderAttachment> (state, "mix", mix.fader);
    outputAttachment = std::make_unique<SliderAttachment> (state, "output", output.fader);
    attackAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, "attack", attackKey);
    modeAttachment   = std::make_unique<juce::ParameterAttachment> (*state.getParameter ("mode"),
                                                                    [this] (float v) { modeChanged (juce::roundToInt (v)); });

    // Start settled: nothing should animate just because the window opened.
    modeChanged (juce::roundToInt (state.getRawParameterValue ("mode")->load()));
    fader.settle();
    attackKey.settle();
    for (auto& key : modeKeys) key.settle();

    setSize (640, 400);

    lastFrameMs = juce::Time::getMillisecondCounterHiRes();
    vblank = std::make_unique<juce::VBlankAttachment> (this, [this] { advanceAnimations(); });
    startTimerHz (4);
}

SemitoneEditor::~SemitoneEditor()
{
    stopTimer();
    vblank.reset();
    setLookAndFeel (nullptr);
}

void SemitoneEditor::advanceAnimations()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 1.0 / 30.0, (now - lastFrameMs) * 0.001);
    lastFrameMs = now;

    fader.animate (dt);
    fine.fader.animate (dt);
    mix.fader.animate (dt);
    output.fader.animate (dt);
    attackKey.animate (dt);
    for (auto& key : modeKeys) key.animate (dt);
}

void SemitoneEditor::modeChanged (int mode)
{
    currentMode = juce::jlimit (0, SemitoneProcessor::numModes - 1, mode);
    for (int i = 0; i < SemitoneProcessor::numModes; ++i)
        modeKeys[i].setToggleState (i == currentMode, juce::dontSendNotification);

    timerCallback();
    repaint();
}

void SemitoneEditor::timerCallback()
{
    ui::Spring::reduceMotion = semitoneShouldReduceMotion();

    const double sr = owner.getSampleRate();
    const auto text = sr > 0.0 ? juce::String (juce::roundToInt (1000.0 * owner.windowForMode (currentMode) / sr)) + " MS"
                               : juce::String ("-- MS");
    if (text != latencyText)
    {
        latencyText = text;
        repaint (latencyArea);
    }
}

void SemitoneEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (ui::panelTop, 0.0f, 0.0f, ui::panelBottom, 0.0f, bounds.getBottom(), false));
    g.fillAll();
    g.setColour (ui::white (0.7f));
    g.fillRect (0.0f, 0.0f, bounds.getWidth(), 1.0f);

    auto chip = [&g] (juce::Rectangle<int> area, const juce::String& text)
    {
        const auto glass = ui::drawLcd (g, area.toFloat(), 2.5f, 5.0f);
        g.setColour (ui::lcdInk);
        ui::drawTabular (g, ui::label (13.5f, 0.05f), text, glass.translated (0.0f, 1.0f), juce::Justification::centred);
    };

    // ---- header: name on the left, latency on the right ----
    g.setColour (ui::accent);
    g.fillRoundedRectangle (24.0f, 20.0f, 9.0f, 9.0f, 2.0f);

    const auto nameFont = ui::label (22.0f, 0.10f);
    g.setColour (ui::ink);
    g.setFont (nameFont);
    g.drawText ("SEMITONE", juce::Rectangle<float> (41.0f, 13.0f, 200.0f, 26.0f), juce::Justification::centredLeft, false);
    g.setColour (ui::inkDim);
    g.setFont (ui::label (10.5f, 0.16f));
    g.drawText ("REAL-TIME PITCH SHIFTER", juce::Rectangle<float> (41.0f + textWidth (nameFont, "SEMITONE") + 14.0f, 17.0f, 220.0f, 18.0f),
                juce::Justification::centredLeft, false);

    g.drawText ("LATENCY", juce::Rectangle<float> ((float) latencyArea.getX() - 80.0f, (float) latencyArea.getY(), 72.0f, (float) latencyArea.getHeight()),
                juce::Justification::centredRight, false);
    chip (latencyArea, latencyText);

    drawGroove (g, 24.0f, bounds.getWidth() - 24.0f, 50.0f);

    // ---- section headings ----
    drawHeading (g, "PITCH", { 24, 62, 88, 12 }, true);
    drawHeading (g, "MODE", { 132, 200, 254, 12 }, false);
    drawHeading (g, "ATTACK", { 132, 288, 254, 12 }, false);
    drawHeading (g, "ADJUST", { 408, 200, 208, 12 }, false);

    g.setColour (ui::inkDim);
    g.setFont (ui::label (10.5f, 0.12f));
    g.drawText (juce::String::fromUTF8 (modeDescriptions[currentMode]).toUpperCase(),
                juce::Rectangle<float> (132.0f, 262.0f, 254.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawFittedText ("KEEPS HITS AND NOTE\nATTACKS TIGHT", juce::Rectangle<int> (268, 308, 118, 32), juce::Justification::centredLeft, 2, 1.0f);

    // ---- adjust rows: printed name, fader (a component), value in a small display ----
    for (auto* row : { &fine, &mix, &output })
    {
        g.setColour (ui::ink);
        g.setFont (ui::label (13.0f, 0.08f));
        g.drawFittedText (row->fader.getTitle().toUpperCase(), row->labelArea.translated (0, 1), juce::Justification::centredLeft, 1, 0.75f);
        chip (row->valueArea, row->format (row->fader.getValue()));
    }
}

void SemitoneEditor::resized()
{
    fader.setBounds (22, 78, 92, 302);
    lcd.setBounds (132, 64, 484, 120);

    for (int i = 0; i < SemitoneProcessor::numModes; ++i)
        modeKeys[i].setBounds (132 + i * 86, 218, 82, 38);

    attackKey.setBounds (132, 306, 124, 38);

    int y = 214;
    for (auto* row : { &fine, &mix, &output })
    {
        row->labelArea = { 408, y, 46, 40 };
        row->fader.setBounds (456, y, 94, 40);
        row->valueArea = { 558, y + 8, 58, 24 };
        y += 48;
    }

    latencyArea = { getWidth() - 24 - 58, 13, 58, 24 };
}
