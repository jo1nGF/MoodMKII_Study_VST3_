#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <map>
#include "PluginProcessor.h"

namespace ui
{
const juce::Colour kInk (0xff4f2650), kPlum (0xff6d3a73), kBody (0xffb784c4), kBodyHi (0xffcfa5da),
                   kPink (0xfff5a3b8), kCream (0xfffff8f0), kRed (0xffe0405a);

// Knob "metálico" lilás com ponteiro branco
struct MoodLookAndFeel : public juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (5.f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        g.setColour (juce::Colour (0x44000000));
        g.fillEllipse (c.x - radius + 1.f, c.y - radius + 4.f, radius * 2.f, radius * 2.f);
        juce::ColourGradient grad (juce::Colour (0xffe3b9ea), c.x - radius * 0.5f, c.y - radius * 0.6f,
                                   juce::Colour (0xff8e5599), c.x + radius * 0.6f, c.y + radius * 0.8f, true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - radius, c.y - radius, radius * 2.f, radius * 2.f);
        g.setColour (kInk);
        g.drawEllipse (c.x - radius, c.y - radius, radius * 2.f, radius * 2.f, 1.6f);
        g.setColour (juce::Colour (0x33ffffff));
        g.drawEllipse (c.x - radius * 0.72f, c.y - radius * 0.72f, radius * 1.44f, radius * 1.44f, 1.f);

        juce::Path p;
        p.addRoundedRectangle (-2.5f, -radius * 0.88f, 5.f, radius * 0.55f, 2.f);
        g.setColour (juce::Colours::white);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }
};

class KnobComp : public juce::Component
{
public:
    KnobComp (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& name, juce::LookAndFeel& laf)
        : label (name), attach (apvts, paramId, slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setLookAndFeel (&laf);
        slider.onValueChange = [this] { repaint(); };
        if (auto* prm = apvts.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        addAndMakeVisible (slider);
    }
    ~KnobComp() override { slider.setLookAndFeel (nullptr); }

    void setLabel (const juce::String& s) { label = s; repaint(); }
    void resized() override { auto r = getLocalBounds(); r.removeFromBottom (34); slider.setBounds (r); }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().removeFromBottom (34);
        g.setColour (kInk);
        g.setFont (12.f);
        g.drawText (label, r.removeFromTop (17), juce::Justification::centred);
        g.setColour (kInk.withAlpha (0.65f));
        g.setFont (11.f);
        g.drawText (slider.getTextFromValue (slider.getValue()), r, juce::Justification::centred);
    }

    juce::Slider slider;

private:
    juce::String label;
    juce::AudioProcessorValueTreeState::SliderAttachment attach;
};

// Slider horizontal compacto com rótulo à esquerda e valor à direita
class LabeledSlider : public juce::Component
{
public:
    LabeledSlider (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const juce::String& name, int labelWidth)
        : label (name), lw (labelWidth), attach (apvts, paramId, slider)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        slider.setColour (juce::Slider::textBoxTextColourId, kInk);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::trackColourId, kPlum);
        slider.setColour (juce::Slider::thumbColourId, kPink);
        slider.setColour (juce::Slider::backgroundColourId, kInk.withAlpha (0.18f));
        if (auto* prm = apvts.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
        addAndMakeVisible (slider);
    }
    void resized() override { slider.setBounds (getLocalBounds().withTrimmedLeft (lw)); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (kInk);
        g.setFont (11.f);
        g.drawText (label, getLocalBounds().withWidth (lw), juce::Justification::centredLeft);
    }

    juce::Slider slider;

private:
    juce::String label;
    int lw;
    juce::AudioProcessorValueTreeState::SliderAttachment attach;
};

// Toggle de 2 ou 3 posições ligado a um parâmetro "choice"
class ChoiceSwitch : public juce::Component
{
public:
    ChoiceSwitch (juce::AudioProcessorValueTreeState& apvts, const char* paramId, juce::StringArray labelsIn)
        : labels (std::move (labelsIn)),
          param (*apvts.getParameter (paramId)),
          attachment (param, [this] (float v) { index = juce::jlimit (0, labels.size() - 1, (int) std::lround (v)); repaint(); })
    {
        attachment.sendInitialUpdate();
    }

    void paint (juce::Graphics& g) override
    {
        const int n = labels.size();
        const auto b = getLocalBounds().toFloat();
        const float slotX = 24.f, slotW = 14.f;
        const juce::Rectangle<float> slotR (slotX - slotW * 0.5f, 6.f, slotW, b.getHeight() - 12.f);
        g.setColour (kInk.withAlpha (0.85f));
        g.fillRoundedRectangle (slotR, 7.f);
        const float step = (slotR.getHeight() - 16.f) / (float) juce::jmax (1, n - 1);
        const float cy = slotR.getY() + 8.f + (float) index * step;
        g.setColour (juce::Colour (0xffdcdce6));
        g.fillEllipse (slotX - 10.f, cy - 10.f, 20.f, 20.f);
        g.setColour (kInk);
        g.drawEllipse (slotX - 10.f, cy - 10.f, 20.f, 20.f, 1.6f);
        g.setFont (11.f);
        for (int i = 0; i < n; ++i)
        {
            const float ly = slotR.getY() + 8.f + (float) i * step;
            g.setColour (i == index ? kInk : kInk.withAlpha (0.45f));
            g.drawText (labels[i], juce::Rectangle<float> (slotX + 16.f, ly - 8.f, b.getWidth() - slotX - 16.f, 16.f),
                        juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;   // botão direito = menu MIDI (tratado pelo editor)
        const int n = labels.size();
        const float top = 14.f, bottom = (float) getHeight() - 14.f;
        const float step = (bottom - top) / (float) juce::jmax (1, n - 1);
        int best = 0;
        float bestD = 1e9f;
        for (int i = 0; i < n; ++i)
        {
            const float d = std::abs (e.position.y - (top + (float) i * step));
            if (d < bestD) { bestD = d; best = i; }
        }
        attachment.setValueAsCompleteGesture ((float) best);
    }

private:
    juce::StringArray labels;
    juce::RangedAudioParameter& param;
    int index = 0;
    juce::ParameterAttachment attachment;
};

// Dip switch (liga/desliga) com rótulo embaixo
class DipSwitch : public juce::Button
{
public:
    DipSwitch() : juce::Button (juce::String()) { setClickingTogglesState (true); }
    void setup (const juce::String& labelText, const juce::String& up = {}, const juce::String& down = {})
    {
        label = labelText; upChar = up; downChar = down; repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Button::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Button::mouseUp (e); }
    void paintButton (juce::Graphics& g, bool, bool) override
    {
        auto b = getLocalBounds().toFloat();
        const auto labelArea = b.removeFromBottom (24.f).toNearestInt();
        const auto sw = b.reduced (9.f, 3.f);
        g.setColour (juce::Colour (0xffeee6dc));
        g.fillRoundedRectangle (sw, 3.f);
        const bool on = getToggleState();
        const float kh = sw.getHeight() * 0.48f;
        const float ky = on ? sw.getY() + 2.f : sw.getBottom() - 2.f - kh;
        g.setColour (on ? kRed : juce::Colour (0xff6e6e78));
        g.fillRoundedRectangle (sw.getX() + 2.f, ky, sw.getWidth() - 4.f, kh, 2.f);
        g.setColour (juce::Colours::white);
        g.setFont (9.5f);
        if (upChar.isNotEmpty())
        {
            g.drawText (upChar, juce::Rectangle<float> (sw.getX(), b.getY() - 2.f, sw.getWidth(), 10.f), juce::Justification::centred);
            g.drawText (downChar, juce::Rectangle<float> (sw.getX(), b.getBottom() - 8.f, sw.getWidth(), 10.f), juce::Justification::centred);
        }
        g.drawFittedText (label, labelArea, juce::Justification::centredTop, 2);
    }
private:
    juce::String label, upChar, downChar;
};

// ToggleButton que ignora o botão direito (reservado ao menu de MIDI Learn)
class NoRightClickToggle : public juce::ToggleButton
{
public:
    using juce::ToggleButton::ToggleButton;
    void mouseDown (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ToggleButton::mouseUp (e); }
};

// Footswitch: só informa "pressionado / solto" (o gesto tap / hold é decodificado no processador)
class FootSwitchComp : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onPress, onRelease;
    ~FootSwitchComp() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (6.f);
        const auto c = b.getCentre();
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        g.setColour (juce::Colour (0x55000000));
        g.fillEllipse (c.x - r, c.y - r + 4.f, r * 2.f, r * 2.f);
        juce::ColourGradient grad (juce::Colour (down ? 0xffa4a4ae : 0xffeeeef4), c.x - r * 0.4f, c.y - r * 0.5f,
                                   juce::Colour (0xff7a7a86), c.x + r * 0.6f, c.y + r * 0.8f, true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f);
        g.setColour (kInk);
        g.drawEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f, 2.f);
        g.setColour (juce::Colour (0x44ffffff));
        g.drawEllipse (c.x - r * 0.6f, c.y - r * 0.6f, r * 1.2f, r * 1.2f, 1.5f);
        if (held)
        {
            g.setColour (kPink);
            g.drawEllipse (c.x - r - 4.f, c.y - r - 4.f, r * 2.f + 8.f, r * 2.f + 8.f, 3.f);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        down = true; held = false;
        downMs = juce::Time::getMillisecondCounter();
        startTimerHz (30);
        repaint();
        if (onPress) onPress();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! down) return;
        stopTimer();
        down = held = false;
        repaint();
        if (onRelease) onRelease();
    }
private:
    void timerCallback() override
    {
        if (down && ! held && juce::Time::getMillisecondCounter() - downMs > 450u) { held = true; repaint(); }
    }
    bool down = false, held = false;
    juce::uint32 downMs = 0;
};
} // namespace ui

class MoodAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MoodAudioProcessorEditor (MoodAudioProcessor&);
    ~MoodAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;   // botão direito em qualquer controle => menu de MIDI Learn

private:
    void timerCallback() override;
    void setHiddenVisible (bool hidden);
    bool getBool (const char* id) const;
    void setFs (const char* id, bool pressed);
    void drawLed (juce::Graphics& g, float cx, float cy, juce::Colour c, bool on) const;
    void showMidiMenu (const juce::String& paramId);
    void handleMidiMenuResult (int result, const juce::String& paramId);
    void refreshPresetList();
    void syncPresetBox();
    void reg (juce::Component* c, const char* paramId) { paramOf[c] = paramId; }

    MoodAudioProcessor& proc;
    ui::MoodLookAndFeel laf;
    bool hiddenOn = false;

    // knobs (modo normal) e (hidden options)
    ui::KnobComp kTime, kWetMod, kMix, kClock, kLength, kLoopMod;
    ui::KnobComp hWidth, hTone, hRampWave, hBalance, hFade, hDirect;
    // toggles
    ui::ChoiceSwitch swWet, swRoute, swLoop, swPreset;
    ui::ChoiceSwitch hSync, hSpreadSolo, hLoopLen;
    ui::ChoiceSwitch swCtrl;
    // sliders compactos
    ui::LabeledSlider sExp, sRamp, sIn, sOut, sLowCut, sDegrade, sNoise;
    // dip switches
    ui::DipSwitch dipCfg[8], dipRamp[8];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dipAtt[16];
    // footswitches
    ui::FootSwitchComp fsWet, fsLoop;
    // botões
    ui::NoRightClickToggle trueBypassBtn { "TRUE BYPASS" };
    juce::AudioProcessorValueTreeState::ButtonAttachment trueBypassAtt { proc.apvts, ids::hTrueBypass, trueBypassBtn };
    juce::TextButton hiddenBtn { "HIDDEN OPTIONS" }, saveL { "SAVE L" }, saveR { "SAVE R" }, resetHiddenBtn { "RESET HIDDEN" };
    // barra de presets
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAVE" }, delBtn { "DEL" }, dirBtn { "DIR" };
    std::vector<MoodAudioProcessor::PresetEntry> presetEntries;
    juce::String shownPreset;

    // MIDI learn / status / medidores
    std::map<juce::Component*, juce::String> paramOf;
    juce::String statusText, lastLearnName;
    double statusUntil = 0.0, limitHoldUntil = 0.0;
    bool wasLearning = false;
    float meterIn = 0.f, meterOut = 0.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoodAudioProcessorEditor)
};
