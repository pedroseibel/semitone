#pragma once

#include "PluginProcessor.h"

/** True when the user has asked macOS to reduce motion (implemented in ReducedMotion.mm). */
bool semitoneShouldReduceMotion();

namespace ui
{
    //==========================================================================
    // A hardware front panel: warm cream metal, black caps, one orange, a grey-green LCD.
    const juce::Colour panelTop    { 0xffece7db };
    const juce::Colour panelBottom { 0xffe2dccd };
    const juce::Colour ink         { 0xff262522 };
    const juce::Colour inkDim      { 0xff7d776a };
    const juce::Colour accent      { 0xffee5a1e };
    const juce::Colour bezel       { 0xff1a1a19 };
    const juce::Colour lcdTop      { 0xffbbc5aa };
    const juce::Colour lcdBottom   { 0xffaab59a };
    const juce::Colour lcdInk      { 0xff1b2117 };
    const juce::Colour capLine     { 0xfff4f1e8 };

    inline juce::Colour white (float alpha)  { return juce::Colours::white.withAlpha (alpha); }
    inline juce::Colour black (float alpha)  { return juce::Colours::black.withAlpha (alpha); }

    /** Condensed capitals, as printed on a panel. */
    juce::Font label (float points, float tracking = 0.06f);

    /** Draws text with every digit in a cell of equal width, so values don't jitter as they change. */
    void drawTabular (juce::Graphics&, const juce::Font&, const juce::String&, juce::Rectangle<float>, juce::Justification);

    /** A recessed display: dark bezel, grey-green glass, shaded along its top edge. Returns the glass area. */
    juce::Rectangle<float> drawLcd (juce::Graphics&, juce::Rectangle<float> bounds, float bezelWidth, float cornerRadius);

    /** Seven-segment text ('0'-'9', '+', '-', ' ', '.'), with the unlit segments faintly visible. */
    void drawSegments (juce::Graphics&, const juce::String& text, juce::Point<float> topLeft,
                       float cellWidth, float cellHeight, float gap);

    //==========================================================================
    /** A critically damped spring: no overshoot, and it keeps its velocity when the
        target changes mid-flight, so every transition can be interrupted. */
    struct Spring
    {
        explicit Spring (float speed, bool movesThings = true) : omega (speed), positional (movesThings) {}

        void snap (float v)        { value = target = v; velocity = 0.0f; }
        void set (float t)         { target = t; }
        bool step (float dt);      // returns true if the value changed

        float value = 0.0f, target = 0.0f, velocity = 0.0f;
        float omega;
        bool positional;           // positional springs jump straight to the target under Reduce Motion

        static bool reduceMotion;
    };
}

//==============================================================================
/** The pitch fader, drawn like a mixing-console channel fader: a slot in the panel, a
    printed scale with one tick per semitone, and a black ridged cap with a white line. */
class ConsoleFader final : public juce::Slider
{
public:
    ConsoleFader();

    void settle()                           { display.snap ((float) getValue()); }
    bool animate (float dt);

    void paint (juce::Graphics&) override;
    void valueChanged() override            { display.set ((float) getValue()); }
    void mouseEnter (const juce::MouseEvent& e) override  { hover.set (1.0f); Slider::mouseEnter (e); }
    void mouseExit (const juce::MouseEvent& e) override   { hover.set (0.0f); Slider::mouseExit (e); }
    void mouseDown (const juce::MouseEvent& e) override   { press.set (1.0f); Slider::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override     { press.set (0.0f); Slider::mouseUp (e); }

    static constexpr int capWidth = 30, capHeight = 46;

private:
    void renderCap (float scale);

    ui::Spring display { 24.0f }, hover { 30.0f, false }, press { 40.0f, false };
    juce::Image cap;
    float capScale = 0.0f;
};

//==============================================================================
/** A small horizontal fader: a printed line with a black cap riding on it. */
class MiniFader final : public juce::Slider
{
public:
    explicit MiniFader (const juce::String& name);

    bool animate (float dt);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent& e) override  { hover.set (1.0f); Slider::mouseEnter (e); }
    void mouseExit (const juce::MouseEvent& e) override   { hover.set (0.0f); Slider::mouseExit (e); }
    void mouseDown (const juce::MouseEvent& e) override   { press.set (1.0f); Slider::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override     { press.set (0.0f); Slider::mouseUp (e); }

    static constexpr int capLength = 12;

private:
    ui::Spring hover { 36.0f, false }, press { 40.0f, false };
};

//==============================================================================
/** A panel key with an indicator LED. It travels down when pressed; the LED shows its state. */
class KeyButton final : public juce::Button
{
public:
    explicit KeyButton (const juce::String& text);

    void settle()                           { led.snap (getToggleState() ? 1.0f : 0.0f); }
    bool animate (float dt);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    ui::Spring led { 30.0f, false }, travel { 60.0f }, hover { 36.0f, false };
};

//==============================================================================
/** The main display: semitones in large segments, then cents, ratio, reference pitch and interval. */
class LcdDisplay final : public juce::Component
{
public:
    std::function<double()> getSemitones, getCents;
    void paint (juce::Graphics&) override;
};

//==============================================================================
class SemitoneLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SemitoneLookAndFeel();

    // how far the centre of a handle stays from the ends of its travel: half the handle's length
    int getSliderThumbRadius (juce::Slider& s) override
    {
        return dynamic_cast<ConsoleFader*> (&s) != nullptr ? ConsoleFader::capHeight / 2 : MiniFader::capLength / 2;
    }

    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    void drawTooltip (juce::Graphics&, const juce::String&, int width, int height) override;
};

//==============================================================================
class SemitoneEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit SemitoneEditor (SemitoneProcessor&);
    ~SemitoneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void advanceAnimations();
    void modeChanged (int mode);

    struct Row
    {
        Row (const juce::String& name, std::function<juce::String (double)> formatter) : fader (name), format (std::move (formatter)) {}

        MiniFader fader;
        std::function<juce::String (double)> format;
        juce::Rectangle<int> labelArea, valueArea;
    };

    SemitoneProcessor& owner;
    SemitoneLookAndFeel lookAndFeel;

    ConsoleFader fader;
    LcdDisplay lcd;
    Row fine, mix, output;
    KeyButton modeKeys[SemitoneProcessor::numModes] { KeyButton ("Smooth"), KeyButton ("Balanced"), KeyButton ("Tight") };
    KeyButton attackKey { "Attack lock" };

    int currentMode = 0;
    juce::String latencyText;
    double lastFrameMs = 0.0;

    juce::Rectangle<int> latencyArea;
    juce::TooltipWindow tooltips { this, 700 };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> faderAttachment, fineAttachment, mixAttachment, outputAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attackAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    std::unique_ptr<juce::VBlankAttachment> vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SemitoneEditor)
};
