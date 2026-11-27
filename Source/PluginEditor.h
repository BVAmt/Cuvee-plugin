#pragma once
#include "PluginProcessor.h"

//==============================================================================
// Palette: bordeaux, cream, brass, bakelite
namespace cuvee
{
    inline const juce::Colour wineDark  { 0xff260610 };
    inline const juce::Colour wine      { 0xff5a1426 };
    inline const juce::Colour cream     { 0xffeadfc4 };
    inline const juce::Colour gold      { 0xffc8a04a };
    inline const juce::Colour bakelite  { 0xff1f1411 };

    inline juce::Font serif (float h, bool italic = false)
    {
        return juce::Font (juce::FontOptions ("Georgia", h, italic ? juce::Font::italic : juce::Font::plain));
    }
}

//==============================================================================
struct VintageLookAndFeel : public juce::LookAndFeel_V4
{
    VintageLookAndFeel()
    {
        using namespace cuvee;
        setColour (juce::Slider::textBoxTextColourId,       cream);
        setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, wineDark);
        setColour (juce::ComboBox::textColourId,       cream);
        setColour (juce::ComboBox::outlineColourId,    gold);
        setColour (juce::ComboBox::arrowColourId,      gold);
        setColour (juce::PopupMenu::backgroundColourId,            wineDark);
        setColour (juce::PopupMenu::textColourId,                  cream);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, wine);
        setColour (juce::PopupMenu::highlightedTextColourId,       cream);
    }

    juce::Font getLabelFont (juce::Label&) override           { return cuvee::serif (13.f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override     { return cuvee::serif (14.f, true); }
    juce::Font getPopupMenuFont() override                    { return cuvee::serif (14.f, true); }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float a0, float a1, juce::Slider&) override
    {
        using namespace cuvee;
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
        const float r  = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const auto  c  = area.getCentre();
        const float kr = r * 0.70f;
        const float ang = a0 + pos * (a1 - a0);

        auto dir = [&] (float a, float rad)
        {
            return c + juce::Point<float> (std::sin (a), -std::cos (a)) * rad;
        };

        // scale ticks
        for (int i = 0; i <= 10; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / 10.f;
            const bool major = (i % 5 == 0);
            g.setColour (major ? gold : cream.withAlpha (0.6f));
            g.drawLine (juce::Line<float> (dir (a, r * 0.84f), dir (a, r * (major ? 1.0f : 0.94f))),
                        major ? 2.f : 1.2f);
        }

        // shadow
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (juce::Rectangle<float> (kr * 2.f, kr * 2.f).withCentre (c + juce::Point<float> (2.f, 4.f)));

        // bakelite body
        auto body = juce::Rectangle<float> (kr * 2.f, kr * 2.f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (bakelite.brighter (0.5f), c.x - kr * 0.4f, c.y - kr * 0.5f,
                                                 bakelite.darker (0.6f),   c.x + kr * 0.6f, c.y + kr * 0.8f, true));
        g.fillEllipse (body);
        g.setColour (gold);
        g.drawEllipse (body, 2.f);
        g.setColour (gold.withAlpha (0.3f));
        g.drawEllipse (body.reduced (kr * 0.12f), 1.f);

        // pointer
        juce::Path p;
        p.startNewSubPath (dir (ang, kr * 0.30f));
        p.lineTo (dir (ang, kr * 0.88f));
        g.setColour (cream);
        g.strokePath (p, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // brass hub
        auto hub = juce::Rectangle<float> (kr * 0.44f, kr * 0.44f).withCentre (c);
        g.setGradientFill (juce::ColourGradient (gold.brighter (0.4f), hub.getX(), hub.getY(),
                                                 gold.darker (0.5f), hub.getRight(), hub.getBottom(), false));
        g.fillEllipse (hub);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                           float, float, juce::Slider::SliderStyle, juce::Slider&) override
    {
        using namespace cuvee;
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle ((float) x, cy - 3.f, (float) w, 6.f, 3.f);
        g.setColour (gold.withAlpha (0.75f));
        g.fillRoundedRectangle ((float) x, cy - 1.5f, sliderPos - (float) x, 3.f, 1.5f);

        auto t = juce::Rectangle<float> (20.f, 20.f).withCentre ({ sliderPos, cy });
        g.setGradientFill (juce::ColourGradient (gold.brighter (0.4f), t.getX(), t.getY(),
                                                 gold.darker (0.5f), t.getRight(), t.getBottom(), false));
        g.fillEllipse (t);
        g.setColour (bakelite);
        g.drawEllipse (t, 1.5f);
    }
};

//==============================================================================
// Needle-style gain reduction meter
class VuMeter : public juce::Component, private juce::Timer
{
public:
    explicit VuMeter (std::atomic<float>& src) : source (src) { startTimerHz (30); }
    ~VuMeter() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        using namespace cuvee;
        auto b = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff1e4c0), b.getCentreX(), b.getBottom(),
                                                 juce::Colour (0xffd2bb84), b.getCentreX(), b.getY(), false));
        g.fillRoundedRectangle (b, 8.f);
        g.setColour (gold.darker (0.3f));
        g.drawRoundedRectangle (b.reduced (1.f), 8.f, 2.f);

        const juce::Point<float> pivot (b.getCentreX(), b.getBottom() - b.getHeight() * 0.10f);
        const float L = b.getHeight() * 0.74f;

        auto ang = [] (float v) { return juce::degreesToRadians (-45.f + 90.f * v / 20.f); };
        auto pt  = [&] (float a, float rad) { return pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * rad; };

        // arc
        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, L * 0.80f, L * 0.80f, 0.f, ang (0.f), ang (20.f), true);
        g.setColour (bakelite.withAlpha (0.8f));
        g.strokePath (arc, juce::PathStrokeType (1.f));

        // ticks and labels
        g.setFont (serif (11.f, true));
        for (int v = 0; v <= 20; ++v)
        {
            const bool major = (v % 5 == 0);
            const float a = ang ((float) v);
            g.setColour (v >= 15 ? juce::Colour (0xff8b1a1a) : bakelite);
            g.drawLine (juce::Line<float> (pt (a, L * 0.80f), pt (a, L * (major ? 0.94f : 0.88f))), major ? 2.f : 1.f);
            if (major)
                g.drawText (juce::String (v),
                            juce::Rectangle<float> (24.f, 14.f).withCentre (pt (a, L * 1.07f)).toNearestInt(),
                            juce::Justification::centred);
        }

        g.setColour (bakelite.withAlpha (0.75f));
        g.setFont (serif (11.f, true));
        g.drawText ("GAIN REDUCTION  \xc2\xb7  dB",
                    juce::Rectangle<float> (b.getX(), pivot.y - L * 0.45f, b.getWidth(), 14.f).toNearestInt(),
                    juce::Justification::centred);

        // needle
        const float a = ang (juce::jlimit (0.f, 20.f, shown));
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawLine (juce::Line<float> (pivot + juce::Point<float> (2.f, 2.f), pt (a, L * 0.9f) + juce::Point<float> (2.f, 2.f)), 2.f);
        g.setColour (juce::Colour (0xff7a1020));
        g.drawLine (juce::Line<float> (pivot, pt (a, L * 0.9f)), 2.f);
        g.setColour (gold);
        g.fillEllipse (juce::Rectangle<float> (12.f, 12.f).withCentre (pivot));
        g.setColour (bakelite);
        g.drawEllipse (juce::Rectangle<float> (12.f, 12.f).withCentre (pivot), 1.2f);
    }

private:
    void timerCallback() override
    {
        const float t = source.load();
        shown += (t > shown ? 0.5f : 0.12f) * (t - shown);
        repaint();
    }

    std::atomic<float>& source;
    float shown = 0.f;
};

//==============================================================================
class CuveeEditor : public juce::AudioProcessorEditor
{
public:
    explicit CuveeEditor (CuveeProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p), vu (p.grMeter)
    {
        setLookAndFeel (&laf);
        setSize (780, 440);

        static const char* ids[]   = { "input", "comp", "wow", "lofi", "noise", "output" };
        static const char* names[] = { "INPUT", "COMPRESS", "WOW", "LO-FI", "NOISE", "OUTPUT" };

        for (int i = 0; i < 6; ++i)
        {
            auto& s = knobs[(size_t) i];
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                   juce::MathConstants<float>::pi * 2.8f, true);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
            addAndMakeVisible (s);
            knobNames[(size_t) i] = names[i];
            attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                proc.apvts, ids[i], s));
        }

        mix.setSliderStyle (juce::Slider::LinearHorizontal);
        mix.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
        addAndMakeVisible (mix);
        attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            proc.apvts, "mix", mix));

        noiseType.addItemList ({ "Tape Hiss", "Vinyl Crackle", "Mains Hum" }, 1);
        addAndMakeVisible (noiseType);
        comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            proc.apvts, "noisetype", noiseType);

        addAndMakeVisible (vu);
    }

    ~CuveeEditor() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        using namespace cuvee;
        auto b = getLocalBounds().toFloat();

        g.setGradientFill (juce::ColourGradient (wine.brighter (0.15f), b.getCentreX(), b.getCentreY() * 0.7f,
                                                 wineDark, 0.f, 0.f, true));
        g.fillAll();

        // paper / leather grain
        juce::Random r (7);
        for (int i = 0; i < 900; ++i)
        {
            g.setColour (r.nextBool() ? juce::Colours::white.withAlpha (0.025f)
                                      : juce::Colours::black.withAlpha (0.06f));
            g.fillRect (r.nextFloat() * b.getWidth(), r.nextFloat() * b.getHeight(), 1.5f, 1.5f);
        }

        // double gold frame
        g.setColour (gold.withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (10.f), 10.f, 1.5f);
        g.setColour (gold.withAlpha (0.4f));
        g.drawRoundedRectangle (b.reduced (16.f), 7.f, 0.8f);

        // title
        g.setColour (cream);
        g.setFont (serif (42.f, true));
        g.drawText (juce::String::fromUTF8 ("Cuv\xc3\xa9" "e"), 34, 20, 260, 50, juce::Justification::centredLeft);

        g.setColour (gold);
        g.setFont (serif (12.f));
        g.drawText ("T A P E   C O M P R E S S O R",
                    getWidth() - 34 - 320, 30, 320, 20, juce::Justification::centredRight);
        g.setColour (cream.withAlpha (0.6f));
        g.setFont (serif (11.f, true));
        g.drawText ("warmth, wobble & dust", getWidth() - 34 - 320, 48, 320, 18, juce::Justification::centredRight);

        // ornamental divider
        const float dy = 74.f;
        g.setColour (gold.withAlpha (0.6f));
        g.drawLine (34.f, dy, b.getCentreX() - 14.f, dy, 1.f);
        g.drawLine (b.getCentreX() + 14.f, dy, b.getRight() - 34.f, dy, 1.f);
        juce::Path d;
        d.addRectangle (-4.f, -4.f, 8.f, 8.f);
        g.fillPath (d, juce::AffineTransform::rotation (juce::MathConstants<float>::pi * 0.25f)
                           .translated (b.getCentreX(), dy));

        // knob labels
        g.setColour (cream);
        g.setFont (serif (13.f));
        for (int i = 0; i < 6; ++i)
            g.drawText (knobNames[(size_t) i], 30 + i * 120, 84, 120, 18, juce::Justification::centred);

        // lower panel
        auto panel = juce::Rectangle<float> (24.f, 276.f, b.getWidth() - 48.f, 140.f);
        g.setColour (wineDark.withAlpha (0.55f));
        g.fillRoundedRectangle (panel, 10.f);
        g.setColour (gold.withAlpha (0.5f));
        g.drawRoundedRectangle (panel, 10.f, 1.f);

        g.setColour (cream);
        g.setFont (serif (13.f));
        g.drawText ("DRY  \xc2\xb7  MIX  \xc2\xb7  WET", 340, 296, 300, 20, juce::Justification::centred);
        g.drawText ("NOISE TYPE", 640, 296, 110, 20, juce::Justification::centred);
    }

    void resized() override
    {
        for (int i = 0; i < 6; ++i)
            knobs[(size_t) i].setBounds (30 + i * 120, 104, 120, 160);

        vu.setBounds (36, 286, 250, 120);
        mix.setBounds (340, 330, 300, 30);
        noiseType.setBounds (640, 326, 110, 28);
    }

private:
    CuveeProcessor& proc;
    VintageLookAndFeel laf;

    std::array<juce::Slider, 6> knobs;
    std::array<juce::String, 6> knobNames;
    juce::Slider mix;
    juce::ComboBox noiseType;
    VuMeter vu;

    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CuveeEditor)
};
