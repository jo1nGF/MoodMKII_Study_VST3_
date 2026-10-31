#include "PluginProcessor.h"
#include "PluginEditor.h"

MoodAudioProcessor::MoodAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MOODMKII", createParameterLayout())
{
    auto r = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    raw.wetTime = r (ids::wetTime); raw.wetModify = r (ids::wetModify); raw.mix = r (ids::mix); raw.clock = r (ids::clock);
    raw.loopLength = r (ids::loopLength); raw.loopModify = r (ids::loopModify);
    raw.wetMode = r (ids::wetMode); raw.routing = r (ids::routing); raw.loopMode = r (ids::loopMode);
    raw.wetOn = r (ids::wetOn); raw.wetFreeze = r (ids::wetFreeze); raw.loopOn = r (ids::loopOn); raw.loopOverdub = r (ids::loopOverdub);
    raw.dipClassic = r (ids::dipClassic); raw.dipMiso = r (ids::dipMiso); raw.dipSpread = r (ids::dipSpread); raw.dipDryKill = r (ids::dipDryKill);
    raw.dipTrails = r (ids::dipTrails); raw.dipNoDub = r (ids::dipNoDub); raw.dipSmooth = r (ids::dipSmooth);
    raw.rmpTime = r (ids::rmpTime); raw.rmpWetModify = r (ids::rmpWetModify); raw.rmpClock = r (ids::rmpClock);
    raw.rmpLoopModify = r (ids::rmpLoopModify); raw.rmpLength = r (ids::rmpLength); raw.rmpBounce = r (ids::rmpBounce);
    raw.rmpSweepTop = r (ids::rmpSweepTop); raw.rmpRise = r (ids::rmpRise); raw.rmpSpeed = r (ids::rmpSpeed);
    raw.ctrlSource = r (ids::ctrlSource); raw.expCv = r (ids::expCv);
    raw.hStereoWidth = r (ids::hStereoWidth); raw.hDirectLoop = r (ids::hDirectLoop); raw.hTone = r (ids::hTone);
    raw.hFade = r (ids::hFade); raw.hBalance = r (ids::hBalance); raw.hRampWave = r (ids::hRampWave); raw.hSync = r (ids::hSync);
    raw.hSpreadSolo = r (ids::hSpreadSolo); raw.hLoopLen = r (ids::hLoopLen); raw.hTrueBypass = r (ids::hTrueBypass);
    raw.outLevel = r (ids::outLevel);
    raw.clockDegrade = r (ids::clockDegrade);

    scratchR.assign (4096, 0.f);
    slot[0] = slot[1] = liveSnapshot = captureParams();
    apvts.addParameterListener (ids::presetSel, this);
}

MoodAudioProcessor::~MoodAudioProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener (ids::presetSel, this);
}

void MoodAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    scratchR.assign ((size_t) juce::jmax (512, samplesPerBlock), 0.f);
    engine.prepare (sampleRate);
    lastOutGain = juce::Decibels::decibelsToGain (raw.outLevel->load());
}

bool MoodAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == layouts.getMainInputChannelSet();
}

mood::EngineParams MoodAudioProcessor::readParams() const
{
    mood::EngineParams e;
    auto f = [] (const std::atomic<float>* a) { return a->load(); };
    auto b = [] (const std::atomic<float>* a) { return a->load() > 0.5f; };
    auto i = [] (const std::atomic<float>* a) { return (int) std::lround (a->load()); };

    e.wetTime = f (raw.wetTime); e.wetModify = f (raw.wetModify); e.mix = f (raw.mix); e.clock = f (raw.clock);
    e.loopLength = f (raw.loopLength); e.loopModify = f (raw.loopModify);
    e.wetMode = i (raw.wetMode); e.routing = i (raw.routing); e.loopMode = i (raw.loopMode);
    e.wetOn = b (raw.wetOn); e.wetFreeze = b (raw.wetFreeze); e.loopOn = b (raw.loopOn); e.loopOverdub = b (raw.loopOverdub);
    e.classic = b (raw.dipClassic); e.miso = b (raw.dipMiso); e.spread = b (raw.dipSpread); e.dryKill = b (raw.dipDryKill);
    e.trails = b (raw.dipTrails); e.noDub = b (raw.dipNoDub); e.smoothClock = b (raw.dipSmooth);
    e.rampTime = b (raw.rmpTime); e.rampWetModify = b (raw.rmpWetModify); e.rampClock = b (raw.rmpClock);
    e.rampLoopModify = b (raw.rmpLoopModify); e.rampLength = b (raw.rmpLength);
    e.bounce = b (raw.rmpBounce); e.sweepTop = b (raw.rmpSweepTop); e.rise = b (raw.rmpRise); e.rampSpeed = f (raw.rmpSpeed);
    e.ctrlSource = i (raw.ctrlSource); e.expValue = f (raw.expCv);
    e.stereoWidth = f (raw.hStereoWidth); e.directLoop = f (raw.hDirectLoop); e.tone = f (raw.hTone); e.fade = f (raw.hFade);
    e.balance = f (raw.hBalance); e.rampWaveform = f (raw.hRampWave);
    e.sync = i (raw.hSync); e.spreadSolo = i (raw.hSpreadSolo); e.loopHalf = (i (raw.hLoopLen) == 0);
    e.trueBypass = b (raw.hTrueBypass);
    e.degrade = f (raw.clockDegrade);
    return e;
}

void MoodAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (numOut < 1 || buffer.getNumChannels() < 1) return;

    const mood::EngineParams ep = readParams();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    int done = 0;
    while (done < n)
    {
        const int m = juce::jmin (n - done, (int) scratchR.size());
        if (r != nullptr) engine.process (l + done, r + done, l + done, r + done, m, ep);
        else              engine.process (l + done, l + done, l + done, scratchR.data(), m, ep);   // entrada/saída mono
        done += m;
    }

    const float g = juce::Decibels::decibelsToGain (raw.outLevel->load());
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        buffer.applyGainRamp (ch, 0, n, lastOutGain, g);
    lastOutGain = g;
}

juce::AudioProcessorEditor* MoodAudioProcessor::createEditor() { return new MoodAudioProcessorEditor (*this); }

// ------------------------------------------------------------------------------------------------
// Presets: posição do toggle PRESET (Left / Live / Right). Left/Right recuperam o que foi salvo;
// voltar para Live restaura os knobs de antes (como no pedal, onde o meio = posição física dos knobs).
juce::String MoodAudioProcessor::captureParams() const
{
    juce::String s;
    for (auto* id : presetParamIds())
        if (auto* p = apvts.getParameter (id))
            s << id << ":" << juce::String (p->getValue(), 6) << ";";
    return s;
}

void MoodAudioProcessor::applyParams (const juce::String& s)
{
    for (const auto& tok : juce::StringArray::fromTokens (s, ";", ""))
    {
        const auto id = tok.upToFirstOccurrenceOf (":", false, false);
        const auto v = tok.fromFirstOccurrenceOf (":", false, false).getFloatValue();
        if (id.isNotEmpty())
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (v);
    }
}

void MoodAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (restoring || parameterID != ids::presetSel) return;
    pendingSel.store ((int) std::lround (newValue));
    triggerAsyncUpdate();   // aplica na thread de mensagens
}

void MoodAudioProcessor::handleAsyncUpdate()
{
    const int sel = pendingSel.load();
    if (sel == lastSel) return;
    if (lastSel == 1) liveSnapshot = captureParams();          // saindo de Live: guarda os knobs atuais
    if (sel == 1) applyParams (liveSnapshot);                  // voltando para Live
    else          applyParams (slot[sel == 0 ? 0 : 1]);        // Left / Right
    lastSel = sel;
}

void MoodAudioProcessor::savePresetSlot (int slotIndex)
{
    slot[juce::jlimit (0, 1, slotIndex)] = captureParams();
}

void MoodAudioProcessor::resetHiddenOptions()
{
    for (auto* id : hiddenParamIds())
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
}

void MoodAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetLeft", slot[0], nullptr);
    state.setProperty ("presetRight", slot[1], nullptr);
    state.setProperty ("presetLive", lastSel == 1 ? captureParams() : liveSnapshot, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void MoodAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto vt = juce::ValueTree::fromXml (*xml);
            slot[0] = vt.getProperty ("presetLeft", slot[0]).toString();
            slot[1] = vt.getProperty ("presetRight", slot[1]).toString();
            liveSnapshot = vt.getProperty ("presetLive", liveSnapshot).toString();
            restoring = true;
            apvts.replaceState (vt);
            restoring = false;
            lastSel = (int) std::lround (apvts.getRawParameterValue (ids::presetSel)->load());
            pendingSel.store (lastSel);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MoodAudioProcessor(); }
