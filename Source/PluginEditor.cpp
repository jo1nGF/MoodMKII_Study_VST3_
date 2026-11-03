#include "PluginEditor.h"

using namespace ui;

namespace
{
constexpr int kW = 800, kH = 960, kDy = 56;   // kDy: deslocamento do corpo do pedal (cabecalho + dips + extras ficam acima)
const char* const kClockLabels[11] = { "2k", "3k", "4k", "6k", "8k", "12k", "16k", "24k", "32k", "48k", "64k" };
const char* const kTip = "Botao direito em qualquer controle = MIDI Learn   |   Presets: digite um nome na caixa e clique SAVE";
}

MoodAudioProcessorEditor::MoodAudioProcessorEditor (MoodAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p),
      kTime (p.apvts, ids::wetTime, "TIME", laf), kWetMod (p.apvts, ids::wetModify, "MODIFY", laf),
      kMix (p.apvts, ids::mix, "MIX", laf), kClock (p.apvts, ids::clock, "CLOCK", laf),
      kLength (p.apvts, ids::loopLength, "LENGTH", laf), kLoopMod (p.apvts, ids::loopModify, "MODIFY", laf),
      hWidth (p.apvts, ids::hStereoWidth, "STEREO WIDTH", laf), hTone (p.apvts, ids::hTone, "TONE", laf),
      hRampWave (p.apvts, ids::hRampWave, "RAMP WAVEFORM", laf), hBalance (p.apvts, ids::hBalance, "LEVEL BALANCE", laf),
      hFade (p.apvts, ids::hFade, "FADE", laf), hDirect (p.apvts, ids::hDirectLoop, "DIRECT MICRO-LOOP", laf),
      swWet (p.apvts, ids::wetMode, { "REVERB", "DELAY", "SLIP" }),
      swRoute (p.apvts, ids::routing, { "IN", "IN + LOOP", "LOOP" }),
      swLoop (p.apvts, ids::loopMode, { "ENV", "TAPE", "STRETCH" }),
      swPreset (p.apvts, ids::presetSel, { "LEFT", "LIVE", "RIGHT" }),
      hSync (p.apvts, ids::hSync, { "UNSYNCED", "LOOP -> WET", "WET -> LOOP" }),
      hSpreadSolo (p.apvts, ids::hSpreadSolo, { "BOTH", "WET ONLY", "LOOP ONLY" }),
      hLoopLen (p.apvts, ids::hLoopLen, { "HALF", "FULL" }),
      swCtrl (p.apvts, ids::ctrlSource, { "RAMP", "EXP / CV" }),
      sExp (p.apvts, ids::expCv, "EXP / CV", 62), sRamp (p.apvts, ids::rmpSpeed, "RAMP SPEED", 62),
      sRampAtt (p.apvts, ids::rmpAtten, "RAMP ATTEN", 62),
      sIn (p.apvts, ids::inLevel, "INPUT dB", 62), sOut (p.apvts, ids::outLevel, "OUTPUT dB", 62),
      sLowCut (p.apvts, ids::wetLowCut, "WET LOW CUT", 62),
      sDegrade (p.apvts, ids::clockDegrade, "DEGRADE", 62), sNoise (p.apvts, ids::clockNoise, "NOISE", 62)
{
    setSize (kW, kH);

    for (auto* k : { &kTime, &kWetMod, &kMix, &kClock, &kLength, &kLoopMod, &hWidth, &hTone, &hRampWave, &hBalance, &hFade, &hDirect })
        addAndMakeVisible (k);
    for (auto* s : { &swWet, &swRoute, &swLoop, &swPreset, &hSync, &hSpreadSolo, &hLoopLen, &swCtrl })
        addAndMakeVisible (s);
    for (auto* s : { &sExp, &sRamp, &sRampAtt, &sIn, &sOut, &sLowCut, &sDegrade, &sNoise })
        addAndMakeVisible (s);

    // ---- dip switches ----
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
        reg (&dipCfg[i], cfgIds[i]);
        reg (&dipRamp[i], rmpIds[i]);
    }

    // ---- botoes ----
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
    trueBypassBtn.setColour (juce::ToggleButton::textColourId, kInk);
    trueBypassBtn.setColour (juce::ToggleButton::tickColourId, kRed);

    // ---- barra de presets (fabrica + usuario) ----
    presetBox.setEditableText (true);
    presetBox.setTextWhenNothingSelected ("Preset...");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id <= (int) presetEntries.size())
        {
            proc.loadPreset (presetEntries[(size_t) id - 1]);
            shownPreset = proc.getCurrentPresetName();
        }
    };
    addAndMakeVisible (presetBox);
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &delBtn, &dirBtn })
        addAndMakeVisible (b);
    prevBtn.onClick = [this] { proc.stepPreset (-1); syncPresetBox(); };
    nextBtn.onClick = [this] { proc.stepPreset (1); syncPresetBox(); };
    saveBtn.onClick = [this]
    {
        const auto name = presetBox.getText().trim();
        if (name.isNotEmpty() && proc.saveUserPreset (name))
        {
            refreshPresetList();
            presetBox.setText (proc.getCurrentPresetName(), juce::dontSendNotification);
            shownPreset = proc.getCurrentPresetName();
            statusText = "Preset salvo: " + proc.getCurrentPresetName();
            statusUntil = juce::Time::getMillisecondCounterHiRes() + 4000.0;
        }
    };
    delBtn.onClick = [this]
    {
        if (proc.deleteUserPreset (presetBox.getText().trim()))
        {
            refreshPresetList();
            statusText = "Preset removido";
            statusUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0;
        }
    };
    dirBtn.onClick = [this] { proc.getPresetDirectory().revealToUser(); };
    refreshPresetList();

    // ---- footswitches: pressionar / soltar viram o parametro momentaneo (gesto decodificado no processador) ----
    fsWet.onPress = [this] { setFs (ids::fsWet, true); };
    fsWet.onRelease = [this] { setFs (ids::fsWet, false); };
    fsWet.onRightClick = [this] { showFootswitchMenu (0); };
    fsLoop.onPress = [this] { setFs (ids::fsLoop, true); };
    fsLoop.onRelease = [this] { setFs (ids::fsLoop, false); };
    fsLoop.onRightClick = [this] { showFootswitchMenu (1); };
    addAndMakeVisible (fsWet);
    addAndMakeVisible (fsLoop);

    // ---- mapa controle -> parametro (para o menu de MIDI Learn com botao direito) ----
    reg (&kTime, ids::wetTime); reg (&kWetMod, ids::wetModify); reg (&kMix, ids::mix); reg (&kClock, ids::clock);
    reg (&kLength, ids::loopLength); reg (&kLoopMod, ids::loopModify);
    reg (&hWidth, ids::hStereoWidth); reg (&hTone, ids::hTone); reg (&hRampWave, ids::hRampWave);
    reg (&hBalance, ids::hBalance); reg (&hFade, ids::hFade); reg (&hDirect, ids::hDirectLoop);
    reg (&swWet, ids::wetMode); reg (&swRoute, ids::routing); reg (&swLoop, ids::loopMode); reg (&swPreset, ids::presetSel);
    reg (&hSync, ids::hSync); reg (&hSpreadSolo, ids::hSpreadSolo); reg (&hLoopLen, ids::hLoopLen); reg (&swCtrl, ids::ctrlSource);
    reg (&sExp, ids::expCv); reg (&sRamp, ids::rmpSpeed); reg (&sRampAtt, ids::rmpAtten); reg (&sIn, ids::inLevel); reg (&sOut, ids::outLevel);
    reg (&sLowCut, ids::wetLowCut); reg (&sDegrade, ids::clockDegrade); reg (&sNoise, ids::clockNoise);
    reg (&fsWet, ids::fsWet); reg (&fsLoop, ids::fsLoop); reg (&trueBypassBtn, ids::hTrueBypass);
    addMouseListener (this, true);

    statusText = kTip;
    setHiddenVisible (false);
    startTimerHz (30);
}

MoodAudioProcessorEditor::~MoodAudioProcessorEditor() { stopTimer(); }

bool MoodAudioProcessorEditor::getBool (const char* id) const { return proc.apvts.getRawParameterValue (id)->load() > 0.5f; }

void MoodAudioProcessorEditor::setFs (const char* id, bool pressed)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        if (pressed) prm->beginChangeGesture();
        prm->setValueNotifyingHost (pressed ? 1.f : 0.f);
        if (! pressed) prm->endChangeGesture();
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

// ---------------------------------------------------------------------------------------------
// MIDI learn: botao direito em qualquer controle
void MoodAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu()) return;
    for (juce::Component* c = e.originalComponent; c != nullptr && c != this; c = c->getParentComponent())
    {
        const auto it = paramOf.find (c);
        if (it != paramOf.end()) { showMidiMenu (it->second); return; }
    }
}

void MoodAudioProcessorEditor::showMidiMenu (const juce::String& paramId)
{
    const int cc = proc.getMappedCC (paramId);
    juce::PopupMenu m;
    m.addSectionHeader (proc.getParamDisplayName (paramId));
    m.addItem (1, "MIDI Learn (mova um controle MIDI)");
    m.addItem (2, cc >= 0 ? "Remover mapeamento (CC " + juce::String (cc) + ")" : juce::String ("Remover mapeamento"), cc >= 0);
    m.addItem (3, "Remover TODOS os mapeamentos MIDI");
    m.addSeparator();
    m.addItem (4, "Restaurar valor padrao");
    if (proc.isMidiLearning()) m.addItem (5, "Cancelar aprendizado");
    juce::Component::SafePointer<MoodAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, paramId] (int result)
    {
        if (safe != nullptr && result != 0) safe->handleMidiMenuResult (result, paramId);
    });
}

void MoodAudioProcessorEditor::handleMidiMenuResult (int result, const juce::String& paramId)
{
    switch (result)
    {
        case 1: proc.beginMidiLearn (paramId); break;
        case 2: proc.clearMidiMapping (paramId); statusText = "Mapeamento removido"; statusUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0; break;
        case 3: proc.clearAllMidiMappings(); statusText = "Todos os mapeamentos MIDI removidos"; statusUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0; break;
        case 4: proc.resetParamToDefault (paramId); break;
        case 5: proc.cancelMidiLearn(); break;
        default: break;
    }
    repaint (0, 36, 640, 20);
}

void MoodAudioProcessorEditor::showFootswitchMenu (int which)
{
    const bool isWet = (which == 0);
    const char* freezeId = isWet ? ids::wetFreeze : ids::loopOverdub;
    const char* engageId = isWet ? ids::wetOn : ids::loopOn;
    const juce::String title = isWet ? "WET FOOTSWITCH" : "LOOP FOOTSWITCH";
    const juce::String freezeLabel = isWet ? "FREEZE" : "OVERDUB";
    const bool frozen = getBool (freezeId);
    const bool engaged = getBool (engageId);

    juce::PopupMenu m;
    m.addSectionHeader (title);
    m.addItem (1, engaged ? "Desligar" : "Ligar");
    m.addItem (2, (frozen ? "Desativar " : "Ativar ") + freezeLabel, engaged);
    m.addSeparator();
    // MIDI learn entries for the footswitch param
    const juce::String paramId = isWet ? juce::String (ids::fsWet) : juce::String (ids::fsLoop);
    const int cc = proc.getMappedCC (paramId);
    m.addItem (10, "MIDI Learn (mova um controle MIDI)");
    m.addItem (11, cc >= 0 ? "Remover mapeamento (CC " + juce::String (cc) + ")" : juce::String ("Remover mapeamento"), cc >= 0);

    juce::Component::SafePointer<MoodAudioProcessorEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, which, freezeId, engageId, paramId] (int result)
    {
        if (safe == nullptr || result == 0) return;
        switch (result)
        {
            case 1:
            {
                const bool on = ! safe->getBool (engageId);
                safe->proc.setBoolParam (engageId, on);
                if (! on) safe->proc.setBoolParam (freezeId, false);
                break;
            }
            case 2:
                safe->proc.setBoolParam (freezeId, ! safe->getBool (freezeId));
                break;
            case 10:
                safe->proc.beginMidiLearn (paramId);
                break;
            case 11:
                safe->proc.clearMidiMapping (paramId);
                safe->statusText = "Mapeamento removido";
                safe->statusUntil = juce::Time::getMillisecondCounterHiRes() + 3000.0;
                break;
            default: break;
        }
        safe->repaint();
    });
}

// ---------------------------------------------------------------------------------------------
void MoodAudioProcessorEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    presetEntries = proc.listPresets();
    bool headF = false, headU = false;
    for (size_t i = 0; i < presetEntries.size(); ++i)
    {
        if (presetEntries[i].factory && ! headF) { presetBox.addSectionHeading ("Factory"); headF = true; }
        if (! presetEntries[i].factory && ! headU) { presetBox.addSectionHeading ("User"); headU = true; }
        presetBox.addItem (presetEntries[i].name, (int) i + 1);
    }
    presetBox.setText (proc.getCurrentPresetName(), juce::dontSendNotification);
    shownPreset = proc.getCurrentPresetName();
}

void MoodAudioProcessorEditor::syncPresetBox()
{
    const auto name = proc.getCurrentPresetName();
    if (name != shownPreset)
    {
        shownPreset = name;
        if (! presetBox.hasKeyboardFocus (true)) presetBox.setText (name, juce::dontSendNotification);
    }
}

void MoodAudioProcessorEditor::timerCallback()
{
    const auto& t = proc.getTelemetry();
    meterIn = juce::jmax (t.inPeak.load(), meterIn * 0.88f);
    meterOut = juce::jmax (t.outPeak.load(), meterOut * 0.88f);
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (t.limitHits.load() > 0) limitHoldUntil = now + 500.0;
    syncPresetBox();

    const bool learning = proc.isMidiLearning();
    if (learning)
    {
        lastLearnName = proc.getParamDisplayName (proc.getLearningParamId());
        statusText = "MIDI LEARN: mova um knob / botao / fader do seu controlador para \"" + lastLearnName + "\"";
        statusUntil = now + 1.0e9;
    }
    else if (wasLearning)
    {
        const int cc = proc.getLastLearnedCC();
        statusText = cc >= 0 ? "Mapeado: \"" + lastLearnName + "\"  <-  CC " + juce::String (cc) : juce::String ("Aprendizado cancelado");
        statusUntil = now + 5000.0;
    }
    else if (now > statusUntil) statusText = kTip;
    wasLearning = learning;

    repaint (0, 690 + kDy, kW, 50);   // LEDs
    repaint (150, 226, 215, 38);      // medidores
    repaint (0, 36, 640, 20);         // status
}

void MoodAudioProcessorEditor::resized()
{
    // cabecalho
    presetBox.setBounds (190, 8, 220, 26);
    prevBtn.setBounds (416, 8, 28, 26);
    nextBtn.setBounds (448, 8, 28, 26);
    saveBtn.setBounds (482, 8, 56, 26);
    delBtn.setBounds (542, 8, 44, 26);
    dirBtn.setBounds (590, 8, 44, 26);
    hiddenBtn.setBounds (640, 8, 140, 26);
    trueBypassBtn.setBounds (650, 36, 130, 22);

    // dip switches
    for (int i = 0; i < 8; ++i)
    {
        dipCfg[i].setBounds (30 + i * 40, 76, 40, 62);
        dipRamp[i].setBounds (400 + i * 40, 76, 40, 62);
    }

    // faixa de extras
    swCtrl.setBounds (24, 162, 110, 52);
    sExp.setBounds (150, 152, 205, 22);
    sRamp.setBounds (150, 178, 205, 22);
    sRampAtt.setBounds (150, 204, 205, 22);
    sIn.setBounds (365, 152, 205, 22);
    sOut.setBounds (365, 178, 205, 22);
    sLowCut.setBounds (365, 204, 205, 22);
    sDegrade.setBounds (580, 152, 200, 22);
    sNoise.setBounds (580, 178, 200, 22);
    resetHiddenBtn.setBounds (640, 196 + kDy, 130, 26);

    // corpo do pedal
    const int cxL = 170, cxC = 400, cxR = 630;
    const juce::Rectangle<int> kb (0, 0, 118, 132);
    kTime.setBounds (kb.withCentre ({ cxL, 310 + kDy }));   hWidth.setBounds (kb.withCentre ({ cxL, 310 + kDy }));
    kMix.setBounds (kb.withCentre ({ cxC, 310 + kDy }));    hRampWave.setBounds (kb.withCentre ({ cxC, 310 + kDy }));
    kLength.setBounds (kb.withCentre ({ cxR, 310 + kDy })); hFade.setBounds (kb.withCentre ({ cxR, 310 + kDy }));
    kWetMod.setBounds (kb.withCentre ({ cxL, 480 + kDy })); hTone.setBounds (kb.withCentre ({ cxL, 480 + kDy }));
    kLoopMod.setBounds (kb.withCentre ({ cxR, 480 + kDy })); hDirect.setBounds (kb.withCentre ({ cxR, 480 + kDy }));
    const juce::Rectangle<int> big (0, 0, 156, 170);
    kClock.setBounds (big.withCentre ({ cxC, 490 + kDy })); hBalance.setBounds (big.withCentre ({ cxC, 490 + kDy }));

    const juce::Rectangle<int> sw (0, 0, 130, 78);
    swWet.setBounds (sw.withCentre ({ cxL, 618 + kDy }));   hSync.setBounds (sw.withCentre ({ cxL, 618 + kDy }));
    swRoute.setBounds (sw.withCentre ({ cxC, 618 + kDy })); hSpreadSolo.setBounds (sw.withCentre ({ cxC, 618 + kDy }));
    swLoop.setBounds (sw.withCentre ({ cxR, 618 + kDy }));  hLoopLen.setBounds (sw.withCentre ({ cxR, 618 + kDy }));

    fsWet.setBounds (juce::Rectangle<int> (0, 0, 96, 96).withCentre ({ 250, 800 + kDy }));
    fsLoop.setBounds (juce::Rectangle<int> (0, 0, 96, 96).withCentre ({ 550, 800 + kDy }));
    swPreset.setBounds (juce::Rectangle<int> (0, 0, 110, 70).withCentre ({ 410, 770 + kDy }));
    saveL.setBounds (335, 828 + kDy, 62, 24);
    saveR.setBounds (405, 828 + kDy, 62, 24);
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

    // ---- cabecalho ----
    g.setColour (kInk);
    g.setFont (24.f);
    g.drawText ("MOOD  MKII", 24, 6, 170, 30, juce::Justification::centredLeft);
    g.setFont (11.f);
    g.setColour (kInk.withAlpha (0.8f));
    g.drawText (statusText, 24, 38, 620, 16, juce::Justification::centredLeft);

    // ---- bancos de dip switches ----
    auto bank = [&] (int x, const char* title)
    {
        g.setColour (kRed);
        g.fillRoundedRectangle ((float) x - 8.f, 72.f, 8.f * 40.f + 16.f, 70.f, 6.f);
        g.setColour (kInk);
        g.setFont (10.f);
        g.drawText (title, x - 8, 59, 8 * 40 + 16, 12, juce::Justification::centredLeft);
    };
    bank (30, "DIP SWITCHES - CONFIG");
    bank (400, "DIP SWITCHES - RAMPING / EXTERNAL CONTROL");

    // ---- faixa de extras: rotulos + medidores ----
    g.setColour (kInk);
    g.setFont (10.f);
    g.drawText ("CONTROL SOURCE", 24, 148, 120, 12, juce::Justification::centredLeft);
    g.setFont (11.f);
    const float mdb[2] = { meterIn, meterOut };
    const char* mlab[2] = { "IN", "OUT" };
    for (int i = 0; i < 2; ++i)
    {
        const juce::Rectangle<float> r (186.f, 228.f + (float) i * 11.f, 128.f, 8.f);
        g.setColour (kInk.withAlpha (0.16f));
        g.fillRoundedRectangle (r, 3.f);
        const float dbv = 20.f * std::log10 (juce::jmax (mdb[i], 1.0e-5f));
        const float frac = juce::jlimit (0.f, 1.f, (dbv + 48.f) / 54.f);
        g.setColour (dbv > 0.f ? kRed : (dbv > -6.f ? juce::Colour (0xffffb347) : juce::Colour (0xff39d353)));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * frac), 3.f);
        g.setColour (kInk);
        g.drawText (mlab[i], 150, (int) r.getY() - 3, 34, 14, juce::Justification::centredLeft);
    }
    const bool limiting = juce::Time::getMillisecondCounterHiRes() < limitHoldUntil;
    drawLed (g, 342.f, 238.f, kRed, limiting);
    g.setColour (kInk);
    g.setFont (9.f);
    g.drawText ("LIMIT", 328, 248, 30, 10, juce::Justification::centred);

    // ---- corpo do pedal ----
    const juce::Rectangle<float> body (20.f, 240.f, 760.f, 704.f);
    juce::ColourGradient bg (hiddenOn ? kPlum.brighter (0.2f) : kBodyHi, 400.f, 240.f, hiddenOn ? kPlum : kBody, 400.f, 944.f, false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (body, 22.f);
    g.setColour (kInk);
    g.drawRoundedRectangle (body, 22.f, 2.f);

    // motivo dos dois circulos (canal wet = escuro, looper = rosa)
    g.setColour (juce::Colour (0x40361340));
    g.fillEllipse (255.f, (float) (690 + kDy), 190.f, 190.f);
    g.setColour (juce::Colour (0x40ffd6e2));
    g.fillEllipse (355.f, (float) (690 + kDy), 190.f, 190.f);

    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.setFont (30.f);
    g.drawText (hiddenOn ? "HIDDEN OPTIONS" : "M  O  O  D", 200, 252, 400, 40, juce::Justification::centred);
    g.setFont (11.f);
    g.drawText (hiddenOn ? "no pedal: segure os dois footswitches" : "MKII", 200, 288, 400, 16, juce::Justification::centred);

    // rotulos dos toggles
    g.setColour (kInk);
    g.setFont (11.f);
    const char* capN[3] = { "WET MODE", "ROUTING", "LOOP MODE" };
    const char* capH[3] = { "SYNC", "SPREAD SOLO", "LOOP LENGTH" };
    const int cx[3] = { 170, 400, 630 };
    for (int i = 0; i < 3; ++i)
        g.drawText (hiddenOn ? capH[i] : capN[i], cx[i] - 65, 568 + kDy, 130, 14, juce::Justification::centred);
    g.drawText ("PRESET", 355, 722 + kDy, 110, 14, juce::Justification::centred);

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
    drawLed (g, 250.f, (float) (712 + kDy), frozen ? juce::Colour (0xffffffff) : kRed, wetOn);
    const int sel = (int) std::lround (proc.apvts.getRawParameterValue (ids::presetSel)->load());
    drawLed (g, 410.f, (float) (706 + kDy), juce::Colour (0xffffb347), sel != 1);
    if (ls == 0) drawLed (g, 550.f, (float) (712 + kDy), juce::Colours::white, blinkOn);
    else drawLed (g, 550.f, (float) (712 + kDy), ls == 1 ? juce::Colour (0xff39d353) : kRed, true);
}
