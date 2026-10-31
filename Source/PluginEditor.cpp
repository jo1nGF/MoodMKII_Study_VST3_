#include "PluginEditor.h"

using namespace ui;

namespace
{
constexpr int kW = 800, kH = 900;
const char* kClockLabels[11] = { "2k", "3k", "4k", "6k", "8k", "12k", "16k", "24k", "32k", "48k", "64k" };
}

MoodAudioProcessorEditor::MoodAudioProcessorEditor (MoodAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p),
      kTime (p.apvts, ids::wetTime, "TIME", laf), kWetMod (p.apvts, ids::wetModify, "MODIFY", laf),
      kMix (p.apvts, ids::mix, "MIX", laf), kClock (p.apvts, ids::clock, "CLOCK", laf),
      kLength (p.apvts, ids::loopLength, "LENGTH", laf), kLoopMod (p.apvts, ids::loopModify, "MODIFY", laf),
      hWidth (p.apvts, ids::hStereoWidth, "STEREO WIDTH", laf), hTone (p.apvts, ids::hTone, "TONE", laf),
      hRampWave (p.apvts, ids::hRampWave, "RAMP WAVEFORM", laf), hBalance (p.apvts, ids::hBalance, "LEVEL BALANCE", laf),
      hFade (p.apvts, ids::hFade, "FADE", laf), hDirect (p.apvts, ids::hDirectLoop, "DIRECT MICRO-LOOP", laf),
      kRampSpeed (p.apvts, ids::rmpSpeed, "RAMP SPEED", laf),
      kDegrade (p.apvts, ids::clockDegrade, "DEGRADE", laf),
      swWet (p.apvts, ids::wetMode, { "REVERB", "DELAY", "SLIP" }),
      swRoute (p.apvts, ids::routing, { "IN", "IN + LOOP", "LOOP" }),
      swLoop (p.apvts, ids::loopMode, { "ENV", "TAPE", "STRETCH" }),
      swPreset (p.apvts, ids::presetSel, { "LEFT", "LIVE", "RIGHT" }),
      hSync (p.apvts, ids::hSync, { "UNSYNCED", "LOOP -> WET", "WET -> LOOP" }),
      hSpreadSolo (p.apvts, ids::hSpreadSolo, { "BOTH", "WET ONLY", "LOOP ONLY" }),
      hLoopLen (p.apvts, ids::hLoopLen, { "HALF", "FULL" }),
      swCtrl (p.apvts, ids::ctrlSource, { "RAMP", "EXP / CV" })
{
    setSize (kW, kH);

    for (auto* k : { &kTime, &kWetMod, &kMix, &kClock, &kLength, &kLoopMod, &hWidth, &hTone, &hRampWave, &hBalance, &hFade, &hDirect, &kRampSpeed, &kDegrade })
        addAndMakeVisible (k);
    for (auto* s : { &swWet, &swRoute, &swLoop, &swPreset, &hSync, &hSpreadSolo, &hLoopLen, &swCtrl })
        addAndMakeVisible (s);

    // ---- dip switches (config) ----
    const char* cfgIds[8] = { ids::dipClassic, ids::dipMiso, ids::dipSpread, ids::dipDryKill, ids::dipTrails, ids::dipLatch, ids::dipNoDub, ids::dipSmooth };
    const char* cfgNames[8] = { "CLASSIC", "MISO", "SPREAD", "DRY KILL", "TRAILS", "LATCH", "NO DUB", "SMOOTH" };
    const char* rmpIds[8] = { ids::rmpTime, ids::rmpWetModify, ids::rmpClock, ids::rmpLoopModify, ids::rmpLength, ids::rmpBounce, ids::rmpSweepTop, ids::rmpRise };
    const char* rmpNames[8] = { "TIME", "MOD (W)", "CLOCK", "MOD (L)", "LENGTH", "BOUNCE", "SWEEP", "POLARITY" };
    for (int i = 0; i < 8; ++i)
    {
        dipCfg[i].setup (cfgNames[i]);
        dipRamp[i].setup (rmpNames[i], i == 6 ? "T" : (i == 7 ? "R" : ""), i == 6 ? "B" : (i == 7 ? "F" : ""));
        addAndMakeVisible (dipCfg[i]);
        addAndMakeVisible (dipRamp[i]);
        dipAtt[i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, cfgIds[i], dipCfg[i]);
        dipAtt[8 + i] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, rmpIds[i], dipRamp[i]);
    }

    // ---- extras ----
    addAndMakeVisible (expSlider);
    addAndMakeVisible (outSlider);
    addAndMakeVisible (trueBypassBtn);
    addAndMakeVisible (hiddenBtn);
    addAndMakeVisible (saveL);
    addAndMakeVisible (saveR);
    addAndMakeVisible (resetHiddenBtn);
    hiddenBtn.setClickingTogglesState (true);
    hiddenBtn.onClick = [this] { setHiddenVisible (hiddenBtn.getToggleState()); };
    saveL.onClick = [this] { proc.savePresetSlot (0); };
    saveR.onClick = [this] { proc.savePresetSlot (1); };
    resetHiddenBtn.onClick = [this] { proc.resetHiddenOptions(); };
    saveL.setTooltip ("Guarda os knobs/toggles/dips atuais no preset ESQUERDO");
    saveR.setTooltip ("Guarda os knobs/toggles/dips atuais no preset DIREITO");
    trueBypassBtn.setColour (juce::ToggleButton::textColourId, kInk);
    trueBypassBtn.setColour (juce::ToggleButton::tickColourId, kRed);

    // ---- footswitches ----
    // ESQUERDO (Wet): tap = liga/desliga; segurar = FREEZE (LATCH: segurar alterna)
    fsWet.onTap = [this]
    {
        const bool on = ! getBool (ids::wetOn);
        setBool (ids::wetOn, on);
        if (! on) setBool (ids::wetFreeze, false);
    };
    fsWet.onHoldStart = [this]
    {
        if (! getBool (ids::wetOn)) setBool (ids::wetOn, true);
        setBool (ids::wetFreeze, getBool (ids::dipLatch) ? ! getBool (ids::wetFreeze) : true);
    };
    fsWet.onHoldEnd = [this] { if (! getBool (ids::dipLatch)) setBool (ids::wetFreeze, false); };
    // DIREITO (Micro-Looper): tap = gravar <-> tocar; segurar (tocando) = OVERDUB
    fsLoop.onTap = [this]
    {
        const bool on = ! getBool (ids::loopOn);
        setBool (ids::loopOn, on);
        if (! on) setBool (ids::loopOverdub, false);
    };
    fsLoop.onHoldStart = [this]
    {
        if (! getBool (ids::loopOn)) return;
        setBool (ids::loopOverdub, getBool (ids::dipLatch) ? ! getBool (ids::loopOverdub) : true);
    };
    fsLoop.onHoldEnd = [this] { if (! getBool (ids::dipLatch)) setBool (ids::loopOverdub, false); };
    addAndMakeVisible (fsWet);
    addAndMakeVisible (fsLoop);

    setHiddenVisible (false);
    startTimerHz (30);
}

MoodAudioProcessorEditor::~MoodAudioProcessorEditor() { stopTimer(); }

bool MoodAudioProcessorEditor::getBool (const char* id) const { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }

void MoodAudioProcessorEditor::setBool (const char* id, bool v)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (v ? 1.f : 0.f);
        prm->endChangeGesture();
    }
}

void MoodAudioProcessorEditor::setHiddenVisible (bool hidden)
{
    hiddenOn = hidden;
    for (auto* k : { &kTime, &kWetMod, &kMix, &kClock, &kLength, &kLoopMod }) k->setVisible (! hidden);
    for (auto* s : { &swWet, &swRoute, &swLoop }) s->setVisible (! hidden);
    for (auto* k : { &hWidth, &hTone, &hRampWave, &hBalance, &hFade, &hDirect }) k->setVisible (hidden);
    for (auto* s : { &hSync, &hSpreadSolo, &hLoopLen }) s->setVisible (hidden);
    resetHiddenBtn.setVisible (hidden);
    repaint();
}

void MoodAudioProcessorEditor::timerCallback() { repaint (0, 690, kW, 70); }

void MoodAudioProcessorEditor::resized()
{
    // dip switches
    for (int i = 0; i < 8; ++i)
    {
        dipCfg[i].setBounds (30 + i * 40, 58, 40, 62);
        dipRamp[i].setBounds (400 + i * 40, 58, 40, 62);
    }
    // linha de controles extras
    swCtrl.setBounds (24, 124, 110, 50);
    expSlider.setBounds (210, 140, 150, 22);
    kRampSpeed.setBounds (380, 118, 80, 66);
    kDegrade.setBounds (480, 118, 80, 66);
    outSlider.setBounds (580, 140, 150, 22);
    trueBypassBtn.setBounds (470, 8, 130, 26);
    hiddenBtn.setBounds (620, 8, 160, 28);
    resetHiddenBtn.setBounds (640, 196, 130, 26);

    // painel do pedal
    const int cxL = 170, cxC = 400, cxR = 630;
    const juce::Rectangle<int> kb (0, 0, 118, 132);
    kTime.setBounds (kb.withCentre ({ cxL, 310 })); hWidth.setBounds (kb.withCentre ({ cxL, 310 }));
    kMix.setBounds (kb.withCentre ({ cxC, 310 }));  hRampWave.setBounds (kb.withCentre ({ cxC, 310 }));
    kLength.setBounds (kb.withCentre ({ cxR, 310 })); hFade.setBounds (kb.withCentre ({ cxR, 310 }));
    kWetMod.setBounds (kb.withCentre ({ cxL, 480 })); hTone.setBounds (kb.withCentre ({ cxL, 480 }));
    kLoopMod.setBounds (kb.withCentre ({ cxR, 480 })); hDirect.setBounds (kb.withCentre ({ cxR, 480 }));
    const juce::Rectangle<int> big (0, 0, 156, 170);
    kClock.setBounds (big.withCentre ({ cxC, 490 })); hBalance.setBounds (big.withCentre ({ cxC, 490 }));

    const juce::Rectangle<int> sw (0, 0, 130, 78);
    swWet.setBounds (sw.withCentre ({ cxL, 618 })); hSync.setBounds (sw.withCentre ({ cxL, 618 }));
    swRoute.setBounds (sw.withCentre ({ cxC, 618 })); hSpreadSolo.setBounds (sw.withCentre ({ cxC, 618 }));
    swLoop.setBounds (sw.withCentre ({ cxR, 618 })); hLoopLen.setBounds (sw.withCentre ({ cxR, 618 }));

    fsWet.setBounds (juce::Rectangle<int> (0, 0, 96, 96).withCentre ({ 250, 800 }));
    fsLoop.setBounds (juce::Rectangle<int> (0, 0, 96, 96).withCentre ({ 550, 800 }));
    swPreset.setBounds (juce::Rectangle<int> (0, 0, 110, 70).withCentre ({ 410, 770 }));
    saveL.setBounds (335, 828, 62, 24);
    saveR.setBounds (405, 828, 62, 24);
}

void MoodAudioProcessorEditor::drawLed (juce::Graphics& g, float cx, float cy, juce::Colour c, bool on) const
{
    if (on)
    {
        g.setColour (c.withAlpha (0.28f));
        g.fillEllipse (cx - 13.f, cy - 13.f, 26.f, 26.f);
    }
    g.setColour (on ? c : juce::Colour (0xff3a1f3d));
    g.fillEllipse (cx - 7.f, cy - 7.f, 14.f, 14.f);
    g.setColour (kInk);
    g.drawEllipse (cx - 7.f, cy - 7.f, 14.f, 14.f, 1.5f);
}

void MoodAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kCream);

    // ---- cabeçalho ----
    g.setColour (kInk);
    g.setFont (26.f);
    g.drawText ("MOOD  MKII", 24, 6, 260, 32, juce::Justification::centredLeft);
    g.setFont (11.f);
    g.setColour (kInk.withAlpha (0.7f));
    g.drawText ("estudo de DSP - recriacao nao oficial (Chase Bliss / Drolo FX / Old Blood Noise)", 24, 36, 440, 14, juce::Justification::centredLeft);

    // ---- bancos de dip switches ----
    auto bank = [&] (int x, const char* title)
    {
        g.setColour (kRed);
        g.fillRoundedRectangle ((float) x - 8.f, 54.f, 8.f * 40.f + 16.f, 70.f, 6.f);
        g.setColour (kInk);
        g.setFont (10.f);
        g.drawText (title, x - 8, 38 + 0, 8 * 40 + 16, 14, juce::Justification::centredLeft);
    };
    bank (30, "DIP SWITCHES - CONFIG");
    bank (400, "DIP SWITCHES - RAMPING / EXTERNAL CONTROL");

    g.setColour (kInk);
    g.setFont (11.f);
    g.drawText ("CONTROL", 24, 112, 90, 12, juce::Justification::centredLeft);
    g.drawText ("EXP / CV", 210, 122, 100, 14, juce::Justification::centredLeft);
    g.drawText ("OUT (dB)", 580, 122, 100, 14, juce::Justification::centredLeft);

    // ---- corpo do pedal ----
    const juce::Rectangle<float> body (20.f, 184.f, 760.f, 704.f);
    juce::ColourGradient bg (hiddenOn ? kPlum.brighter (0.2f) : kBodyHi, 400.f, 184.f, hiddenOn ? kPlum : kBody, 400.f, 888.f, false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (body, 22.f);
    g.setColour (kInk);
    g.drawRoundedRectangle (body, 22.f, 2.f);

    // motivo dos dois círculos (canal wet = escuro, looper = rosa)
    g.setColour (juce::Colour (0x40361340));
    g.fillEllipse (255.f, 690.f, 190.f, 190.f);
    g.setColour (juce::Colour (0x40ffd6e2));
    g.fillEllipse (355.f, 690.f, 190.f, 190.f);

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (30.f);
    g.drawText (hiddenOn ? "HIDDEN OPTIONS" : "M  O  O  D", 200, 196, 400, 40, juce::Justification::centred);
    g.setFont (11.f);
    g.drawText (hiddenOn ? "no pedal: segure os dois footswitches" : "MKII", 200, 232, 400, 16, juce::Justification::centred);

    // rótulos dos toggles
    g.setColour (kInk);
    g.setFont (11.f);
    const char* capN[3] = { "WET MODE", "ROUTING", "LOOP MODE" };
    const char* capH[3] = { "SYNC", "SPREAD SOLO", "LOOP LENGTH" };
    const int cx[3] = { 170, 400, 630 };
    for (int i = 0; i < 3; ++i)
        g.drawText (hiddenOn ? capH[i] : capN[i], cx[i] - 65, 568, 130, 14, juce::Justification::centred);
    g.drawText ("PRESET", 355, 722, 110, 14, juce::Justification::centred);

    // marcas do knob CLOCK
    if (! hiddenOn)
    {
        const auto c = kClock.getBounds().getCentre().toFloat().translated (0.f, -17.f);
        const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
        g.setFont (10.f);
        for (int i = 0; i < 11; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / 10.f;
            const float rr = 70.f;
            g.drawText (kClockLabels[i], juce::Rectangle<float> (c.x + std::sin (a) * rr - 14.f, c.y - std::cos (a) * rr - 7.f, 28.f, 14.f),
                        juce::Justification::centred);
        }
    }

    // ---- LEDs ----
    const auto& t = proc.getTelemetry();
    const bool wetOn = getBool (ids::wetOn), frozen = getBool (ids::wetFreeze);
    const int ls = t.loopState.load();
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double per = juce::jmax (0.05, (double) t.loopPeriod.load());
    const bool blinkOn = std::fmod (now, per) < 0.12;
    drawLed (g, 250.f, 712.f, frozen ? juce::Colour (0xffffffff) : kRed, wetOn);
    const int sel = (int) std::lround (proc.apvts.getRawParameterValue (ids::presetSel)->load());
    drawLed (g, 410.f, 706.f, juce::Colour (0xffffb347), sel != 1);
    if (ls == 0) drawLed (g, 550.f, 712.f, juce::Colours::white, blinkOn);
    else drawLed (g, 550.f, 712.f, ls == 1 ? juce::Colour (0xff39d353) : kRed, true);
}
