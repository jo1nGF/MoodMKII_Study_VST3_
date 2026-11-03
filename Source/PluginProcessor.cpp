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
    raw.rmpAtten = r (ids::rmpAtten);
    raw.ctrlSource = r (ids::ctrlSource); raw.expCv = r (ids::expCv);
    raw.hStereoWidth = r (ids::hStereoWidth); raw.hDirectLoop = r (ids::hDirectLoop); raw.hTone = r (ids::hTone);
    raw.hFade = r (ids::hFade); raw.hBalance = r (ids::hBalance); raw.hRampWave = r (ids::hRampWave); raw.hSync = r (ids::hSync);
    raw.hSpreadSolo = r (ids::hSpreadSolo); raw.hLoopLen = r (ids::hLoopLen); raw.hTrueBypass = r (ids::hTrueBypass);
    raw.outLevel = r (ids::outLevel); raw.inLevel = r (ids::inLevel);
    raw.clockDegrade = r (ids::clockDegrade); raw.clockNoise = r (ids::clockNoise); raw.wetLowCut = r (ids::wetLowCut);
    raw.fsWet = r (ids::fsWet); raw.fsLoop = r (ids::fsLoop);
    raw.actSaveLeft = r (ids::actSaveLeft); raw.actSaveRight = r (ids::actSaveRight);
    raw.actPrevPreset = r (ids::actPrevPreset); raw.actNextPreset = r (ids::actNextPreset);

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            allParams.push_back (rp);
    for (auto& a : ccToParam) a.store (-1);
    for (auto& a : ccValue) a.store (0.f);
    for (auto& a : ccDirty) a.store (false);

    scratchR.assign (4096, 0.f);
    slot[0] = slot[1] = liveSnapshot = captureParams();
    currentPreset = "Init (hi-fi)";
    apvts.addParameterListener (ids::presetSel, this);
    apvts.addParameterListener (ids::fsWet, this);
    apvts.addParameterListener (ids::fsLoop, this);
    getPresetDirectory();      // cria a pasta de presets do usuário
    startTimerHz (60);
}

MoodAudioProcessor::~MoodAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    apvts.removeParameterListener (ids::presetSel, this);
    apvts.removeParameterListener (ids::fsWet, this);
    apvts.removeParameterListener (ids::fsLoop, this);
}

void MoodAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    scratchR.assign ((size_t) juce::jmax (512, samplesPerBlock), 0.f);
    engine.prepare (sampleRate);
    recordingSampleRate = sampleRate;
    recordingChannels = getTotalNumOutputChannels() > 1 ? 2 : 1;
    recordingCapacity = juce::jmax (1, (int) std::min (sampleRate * 180.0, 180.0 * 96000.0));
    recordingData.assign ((size_t) recordingCapacity * 2u, 0);
    recordedSamples.store (0);
    recording.store (false);
    lastOutGain = juce::Decibels::decibelsToGain (raw.outLevel->load());
    lastInGain = juce::Decibels::decibelsToGain (raw.inLevel->load());
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
    e.rampAtten = f (raw.rmpAtten);
    e.ctrlSource = i (raw.ctrlSource); e.expValue = f (raw.expCv);
    e.stereoWidth = f (raw.hStereoWidth); e.directLoop = f (raw.hDirectLoop); e.tone = f (raw.hTone); e.fade = f (raw.hFade);
    e.balance = f (raw.hBalance); e.rampWaveform = f (raw.hRampWave);
    e.sync = i (raw.hSync); e.spreadSolo = i (raw.hSpreadSolo); e.loopHalf = (i (raw.hLoopLen) == 0);
    e.trueBypass = b (raw.hTrueBypass);
    e.degrade = f (raw.clockDegrade); e.noise = f (raw.clockNoise); e.wetLowCut = f (raw.wetLowCut);
    return e;
}

void MoodAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    for (const auto metadata : midi)
        handleMidi (metadata.getMessage());

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (numOut < 1 || buffer.getNumChannels() < 1) return;

    // ganho de entrada ("input gain" do pedal): vale para o dry e para o motor
    const float gin = juce::Decibels::decibelsToGain (raw.inLevel->load());
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        buffer.applyGainRamp (ch, 0, n, lastInGain, gin);
    lastInGain = gin;

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

    // A captura ocorre depois do processamento e do OUTPUT, portanto salva exatamente
    // o que sai do plugin. O buffer foi pré-alocado em prepareToPlay: sem alocação no áudio.
    if (recording.load (std::memory_order_relaxed))
    {
        int pos = recordedSamples.load (std::memory_order_relaxed);
        const int take = juce::jmin (n, recordingCapacity - pos);
        if (take > 0)
        {
            const float* outL = buffer.getReadPointer (0);
            const float* outR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : outL;
            for (int i = 0; i < take; ++i)
            {
                const auto qL = (int16_t) juce::jlimit (-32768, 32767, (int) std::lrint (juce::jlimit (-1.0f, 1.0f, outL[i]) * 32767.0f));
                const auto qR = (int16_t) juce::jlimit (-32768, 32767, (int) std::lrint (juce::jlimit (-1.0f, 1.0f, outR[i]) * 32767.0f));
                recordingData[(size_t) (pos + i) * 2u] = qL;
                recordingData[(size_t) (pos + i) * 2u + 1u] = qR;
            }
            recordedSamples.store (pos + take, std::memory_order_release);
        }
        if (pos + take >= recordingCapacity)
            recording.store (false, std::memory_order_release);
    }
}

bool MoodAudioProcessor::startRecording()
{
    if (recordingData.empty() || recordingCapacity <= 0) return false;
    recordedSamples.store (0, std::memory_order_release);
    recording.store (true, std::memory_order_release);
    return true;
}

void MoodAudioProcessor::stopRecording()
{
    recording.store (false, std::memory_order_release);
}

bool MoodAudioProcessor::saveRecording (const juce::File& destination, bool asMp3, juce::String& error)
{
    stopRecording();
    const int samples = juce::jlimit (0, recordingCapacity, recordedSamples.load (std::memory_order_acquire));
    if (samples <= 0) { error = "Nenhum áudio gravado."; return false; }
    if (destination.existsAsFile() && ! destination.deleteFile())
    { error = "Não foi possível substituir o arquivo de destino."; return false; }

    const auto wavFile = asMp3
        ? juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("mood_recording", ".wav", false)
        : destination;
    juce::WavAudioFormat wav;
    auto stream = wavFile.createOutputStream();
    if (stream == nullptr) { error = "Não foi possível criar o arquivo."; return false; }
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), recordingSampleRate,
                                                                           (unsigned int) recordingChannels, 16, {}, 0));
    if (! writer) { error = "Falha ao iniciar o gravador WAV."; return false; }

    constexpr int chunkSize = 2048;
    juce::AudioBuffer<float> writeBuffer (recordingChannels, chunkSize);
    int offset = 0;
    while (offset < samples)
    {
        const int count = juce::jmin (chunkSize, samples - offset);
        auto* left = writeBuffer.getWritePointer (0);
        auto* right = writeBuffer.getWritePointer (1);
        for (int i = 0; i < count; ++i)
        {
            const size_t at = (size_t) (offset + i) * 2u;
            left[i] = (float) recordingData[at] / 32768.0f;
            right[i] = (float) recordingData[at + 1u] / 32768.0f;
        }
        // AudioFormatWriter::write() expects integer sample pointers in this JUCE API.
        // This helper safely converts the float AudioBuffer to the writer's sample format.
        if (! writer->writeFromAudioSampleBuffer (writeBuffer, 0, count))
        { writer.reset(); wavFile.deleteFile(); error = "Erro ao gravar o WAV."; return false; }
        offset += count;
    }
    writer.reset();

    if (! asMp3) return true;

    // MP3 usa o encoder LAME instalado no sistema (128 kbit/s: qualidade média e arquivo leve).
    juce::ChildProcess encoder;
    const auto command = "lame -b 128 --silent " + wavFile.getFullPathName().quoted()
                       + " " + destination.getFullPathName().quoted();
    const bool started = encoder.start (command);
    const bool finished = started && encoder.waitForProcessToFinish (30000);
    const int exitCode = finished ? encoder.getExitCode() : -1;
    wavFile.deleteFile();
    if (! finished || exitCode != 0 || ! destination.existsAsFile())
    {
        destination.deleteFile();
        error = "Encoder MP3 não encontrado ou falhou. Instale o LAME e deixe 'lame' disponível no PATH. O WAV temporário foi removido.";
        return false;
    }
    return true;
}

juce::AudioProcessorEditor* MoodAudioProcessor::createEditor() { return new MoodAudioProcessorEditor (*this); }

// ================================================================================================
// Slots do pedal: Left / Live / Right. Left/Right recuperam o que foi salvo; voltar para Live restaura os knobs de antes.
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
    if (parameterID == ids::fsWet)  { if (newValue > 0.5f) ++fsWetDowns;  return; }
    if (parameterID == ids::fsLoop) { if (newValue > 0.5f) ++fsLoopDowns; return; }
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

void MoodAudioProcessor::savePresetSlot (int slotIndex) { slot[juce::jlimit (0, 1, slotIndex)] = captureParams(); }

void MoodAudioProcessor::resetHiddenOptions()
{
    for (auto* id : hiddenParamIds())
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
}

// ================================================================================================
// Gestos de footswitch (tap / hold) decodificados aqui => funcionam igual pela GUI, por automação ou por MIDI.
bool MoodAudioProcessor::getBoolParam (const char* id) const { return apvts.getRawParameterValue (id)->load() > 0.5f; }

void MoodAudioProcessor::setBoolParam (const char* id, bool v)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (v ? 1.f : 0.f);
        p->endChangeGesture();
    }
}

void MoodAudioProcessor::fsTap (int which)
{
    if (which == 0)
    {
        const bool on = ! getBoolParam (ids::wetOn);
        setBoolParam (ids::wetOn, on);
        if (! on) setBoolParam (ids::wetFreeze, false);
    }
    else
    {
        const bool on = ! getBoolParam (ids::loopOn);
        setBoolParam (ids::loopOn, on);
        if (! on) setBoolParam (ids::loopOverdub, false);
    }
}

void MoodAudioProcessor::fsHoldStart (int which)
{
    const bool latch = getBoolParam (ids::dipLatch);
    if (which == 0)
    {
        if (! getBoolParam (ids::wetOn)) setBoolParam (ids::wetOn, true);
        setBoolParam (ids::wetFreeze, latch ? ! getBoolParam (ids::wetFreeze) : true);
    }
    else
    {
        if (! getBoolParam (ids::loopOn)) return;
        setBoolParam (ids::loopOverdub, latch ? ! getBoolParam (ids::loopOverdub) : true);
    }
}

void MoodAudioProcessor::fsHoldEnd (int which)
{
    if (getBoolParam (ids::dipLatch)) return;
    setBoolParam (which == 0 ? ids::wetFreeze : ids::loopOverdub, false);
}

void MoodAudioProcessor::updateFootswitch (int which, FsState& st, const std::atomic<float>* rawParam, const std::atomic<int>& downs)
{
    const bool pressed = rawParam->load() > 0.5f;
    const int dc = downs.load();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! st.down)
    {
        if (pressed) { st.down = true; st.hold = false; st.t0 = now; st.seenDowns = dc; }
        else if (dc != st.seenDowns) { st.seenDowns = dc; fsTap (which); }   // toque tão rápido que passou entre dois ticks
    }
    else
    {
        if (pressed && ! st.hold && now - st.t0 > 450.0) { st.hold = true; fsHoldStart (which); }
        if (! pressed)
        {
            if (st.hold) fsHoldEnd (which); else fsTap (which);
            st.down = false; st.hold = false; st.seenDowns = dc;
        }
    }
}

void MoodAudioProcessor::handleActions()
{
    const bool a[4] = { raw.actSaveLeft->load() > 0.5f, raw.actSaveRight->load() > 0.5f,
                        raw.actPrevPreset->load() > 0.5f, raw.actNextPreset->load() > 0.5f };
    for (int i = 0; i < 4; ++i)
    {
        if (a[i] && ! actPrev[i])   // borda de subida
        {
            if (i == 0) savePresetSlot (0);
            else if (i == 1) savePresetSlot (1);
            else stepPreset (i == 2 ? -1 : 1);
        }
        actPrev[i] = a[i];
    }
}

void MoodAudioProcessor::timerCallback()
{
    updateFootswitch (0, fsWetState, raw.fsWet, fsWetDowns);
    updateFootswitch (1, fsLoopState, raw.fsLoop, fsLoopDowns);
    handleActions();
    applyMidiValues();
    const int pc = pendingProgram.exchange (-1);
    if (pc >= 0)
    {
        const auto list = listPresets();
        if (pc < (int) list.size()) loadPreset (list[(size_t) pc]);
    }
}

// ================================================================================================
// MIDI: CC -> qualquer parâmetro (MIDI Learn), Program Change -> presets (fábrica primeiro, depois usuário)
void MoodAudioProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isController())
    {
        const int cc = juce::jlimit (0, 127, m.getControllerNumber());
        const float v = (float) m.getControllerValue() / 127.f;
        const int learn = learnParam.load();
        if (learn >= 0)
        {
            for (auto& a : ccToParam) if (a.load() == learn) a.store (-1);   // um parâmetro <- um CC
            ccToParam[(size_t) cc].store (learn);
            lastLearnedCC.store (cc);
            learnParam.store (-1);
        }
        if (ccToParam[(size_t) cc].load() >= 0)
        {
            ccValue[(size_t) cc].store (v);
            ccDirty[(size_t) cc].store (true);
        }
    }
    else if (m.isProgramChange())
    {
        pendingProgram.store (m.getProgramChangeNumber());
    }
}

void MoodAudioProcessor::applyMidiValues()
{
    for (size_t cc = 0; cc < 128; ++cc)
    {
        if (! ccDirty[cc].exchange (false)) continue;
        const int pi = ccToParam[cc].load();
        if (pi < 0 || pi >= (int) allParams.size()) continue;
        auto* p = allParams[(size_t) pi];
        const float v = ccValue[cc].load();
        p->setValueNotifyingHost (p->convertTo0to1 (p->convertFrom0to1 (v)));   // encaixa em passos discretos (botões / toggles)
    }
}

int MoodAudioProcessor::indexOfParam (const juce::String& paramId) const
{
    for (size_t i = 0; i < allParams.size(); ++i)
        if (allParams[i]->getParameterID() == paramId)
            return (int) i;
    return -1;
}

void MoodAudioProcessor::beginMidiLearn (const juce::String& paramId)
{
    lastLearnedCC.store (-1);
    learnParam.store (indexOfParam (paramId));
}

juce::String MoodAudioProcessor::getLearningParamId() const
{
    const int pi = learnParam.load();
    if (pi < 0 || pi >= (int) allParams.size()) return {};
    return allParams[(size_t) pi]->getParameterID();
}

int MoodAudioProcessor::getMappedCC (const juce::String& paramId) const
{
    const int pi = indexOfParam (paramId);
    if (pi < 0) return -1;
    for (size_t cc = 0; cc < 128; ++cc)
        if (ccToParam[cc].load() == pi) return (int) cc;
    return -1;
}

void MoodAudioProcessor::clearMidiMapping (const juce::String& paramId)
{
    const int pi = indexOfParam (paramId);
    for (auto& a : ccToParam) if (a.load() == pi) a.store (-1);
}

void MoodAudioProcessor::clearAllMidiMappings() { for (auto& a : ccToParam) a.store (-1); }

juce::String MoodAudioProcessor::getParamDisplayName (const juce::String& paramId) const
{
    const int pi = indexOfParam (paramId);
    return pi >= 0 ? allParams[(size_t) pi]->getName (64) : paramId;
}

void MoodAudioProcessor::resetParamToDefault (const juce::String& paramId)
{
    const int pi = indexOfParam (paramId);
    if (pi < 0) return;
    auto* p = allParams[(size_t) pi];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getDefaultValue());
    p->endChangeGesture();
}

juce::String MoodAudioProcessor::midiMapToString() const
{
    juce::String s;
    for (size_t cc = 0; cc < 128; ++cc)
    {
        const int pi = ccToParam[cc].load();
        if (pi >= 0 && pi < (int) allParams.size())
            s << (int) cc << ":" << allParams[(size_t) pi]->getParameterID() << ";";
    }
    return s;
}

void MoodAudioProcessor::midiMapFromString (const juce::String& s)
{
    for (auto& a : ccToParam) a.store (-1);
    for (const auto& tok : juce::StringArray::fromTokens (s, ";", ""))
    {
        const int cc = tok.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int pi = indexOfParam (tok.fromFirstOccurrenceOf (":", false, false));
        if (pi >= 0 && cc >= 0 && cc < 128) ccToParam[(size_t) cc].store (pi);
    }
}

// ================================================================================================
// Presets: fábrica (Presets.h) + usuário (arquivos .moodpreset em %APPDATA%\MOOD MKII Study\Presets)
juce::File MoodAudioProcessor::getPresetDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("MOOD MKII Study").getChildFile ("Presets");
    if (! dir.exists()) dir.createDirectory();
    return dir;
}

juce::StringArray MoodAudioProcessor::listUserPresetNames() const
{
    juce::StringArray names;
    for (const auto& f : getPresetDirectory().findChildFiles (juce::File::findFiles, false, "*.moodpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

std::vector<MoodAudioProcessor::PresetEntry> MoodAudioProcessor::listPresets() const
{
    std::vector<PresetEntry> out;
    const auto& fp = factoryPresets();
    for (size_t i = 0; i < fp.size(); ++i)
    {
        PresetEntry e; e.name = fp[i].name; e.factory = true; e.factoryIndex = (int) i;
        out.push_back (e);
    }
    for (const auto& n : listUserPresetNames())
    {
        PresetEntry e; e.name = n; e.factory = false; e.factoryIndex = -1;
        out.push_back (e);
    }
    return out;
}

void MoodAudioProcessor::forceLiveSelection()
{
    if (auto* p = apvts.getParameter (ids::presetSel))
    {
        restoring = true;
        p->setValueNotifyingHost (p->convertTo0to1 (1.f));
        restoring = false;
    }
    lastSel = 1;
    pendingSel.store (1);
}

void MoodAudioProcessor::resetUserParamsToDefault()
{
    for (auto* id : userPresetParamIds())
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
}

void MoodAudioProcessor::applyFactoryPreset (int index)
{
    const auto& fp = factoryPresets();
    if (index < 0 || index >= (int) fp.size()) return;
    forceLiveSelection();
    resetUserParamsToDefault();
    for (const auto& kv : fp[(size_t) index].values)
        if (auto* p = apvts.getParameter (kv.first))
            p->setValueNotifyingHost (p->convertTo0to1 (kv.second));
    currentPreset = fp[(size_t) index].name;
    currentProgram = index;
}

bool MoodAudioProcessor::loadUserPreset (const juce::String& name)
{
    const auto file = getPresetDirectory().getChildFile (name + ".moodpreset");
    if (! file.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("MOODPRESET")) return false;
    forceLiveSelection();
    resetUserParamsToDefault();
    for (auto* c : xml->getChildIterator())
        if (auto* p = apvts.getParameter (c->getStringAttribute ("id")))
            p->setValueNotifyingHost ((float) c->getDoubleAttribute ("v", (double) p->getDefaultValue()));
    currentPreset = name;
    return true;
}

bool MoodAudioProcessor::loadPreset (const PresetEntry& e)
{
    if (e.factory) { applyFactoryPreset (e.factoryIndex); return true; }
    return loadUserPreset (e.name);
}

bool MoodAudioProcessor::saveUserPreset (const juce::String& nameIn)
{
    const auto name = juce::File::createLegalFileName (nameIn.trim());
    if (name.isEmpty()) return false;
    juce::XmlElement xml ("MOODPRESET");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", 1);
    for (auto* id : userPresetParamIds())
        if (auto* p = apvts.getParameter (id))
        {
            auto* c = xml.createNewChildElement ("P");
            c->setAttribute ("id", juce::String (id));
            c->setAttribute ("v", (double) p->getValue());
        }
    const auto file = getPresetDirectory().getChildFile (name + ".moodpreset");
    if (! file.replaceWithText (xml.toString())) return false;
    currentPreset = name;
    return true;
}

bool MoodAudioProcessor::deleteUserPreset (const juce::String& nameIn)
{
    const auto file = getPresetDirectory().getChildFile (juce::File::createLegalFileName (nameIn.trim()) + ".moodpreset");
    return file.existsAsFile() && file.deleteFile();
}

void MoodAudioProcessor::stepPreset (int delta)
{
    const auto list = listPresets();
    if (list.empty()) return;
    const int n = (int) list.size();
    int idx = -1;
    for (int i = 0; i < n; ++i)
        if (list[(size_t) i].name == currentPreset) { idx = i; break; }
    idx = (idx < 0) ? (delta > 0 ? 0 : n - 1) : (idx + delta + n) % n;
    loadPreset (list[(size_t) idx]);
}

void MoodAudioProcessor::setCurrentProgram (int index) { applyFactoryPreset (index); }

const juce::String MoodAudioProcessor::getProgramName (int index)
{
    const auto& fp = factoryPresets();
    return (index >= 0 && index < (int) fp.size()) ? juce::String (fp[(size_t) index].name) : juce::String();
}

// ================================================================================================
void MoodAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("presetLeft", slot[0], nullptr);
    state.setProperty ("presetRight", slot[1], nullptr);
    state.setProperty ("presetLive", lastSel == 1 ? captureParams() : liveSnapshot, nullptr);
    state.setProperty ("midiMap", midiMapToString(), nullptr);
    state.setProperty ("currentPreset", currentPreset, nullptr);
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
            midiMapFromString (vt.getProperty ("midiMap", "").toString());
            currentPreset = vt.getProperty ("currentPreset", currentPreset).toString();
            restoring = true;
            apvts.replaceState (vt);
            restoring = false;
            lastSel = (int) std::lround (apvts.getRawParameterValue (ids::presetSel)->load());
            pendingSel.store (lastSel);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MoodAudioProcessor(); }
