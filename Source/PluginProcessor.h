#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <cstdint>
#include <atomic>
#include <vector>
#include "PluginParameters.h"
#include "Presets.h"
#include "dsp/MoodEngine.h"

class MoodAudioProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer
{
public:
    struct PresetEntry { juce::String name; bool factory = false; int factoryIndex = -1; };

    MoodAudioProcessor();
    ~MoodAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    // "Programs" do host = presets de fábrica
    int getNumPrograms() override { return (int) factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------- slots do pedal (Left / Right) ----------
    void savePresetSlot (int slotIndex);          // 0 = esquerda, 1 = direita
    void resetHiddenOptions();

    // ---------- presets (fábrica + arquivos do usuário) ----------
    std::vector<PresetEntry> listPresets() const;
    bool loadPreset (const PresetEntry& e);
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);
    void stepPreset (int delta);
    juce::File getPresetDirectory() const;
    juce::String getCurrentPresetName() const { return currentPreset; }

    // ---------- MIDI learn (qualquer parâmetro) ----------
    void beginMidiLearn (const juce::String& paramId);
    void cancelMidiLearn() { learnParam.store (-1); }
    bool isMidiLearning() const { return learnParam.load() >= 0; }
    juce::String getLearningParamId() const;
    int getLastLearnedCC() const { return lastLearnedCC.load(); }
    int getMappedCC (const juce::String& paramId) const;   // -1 se não mapeado
    void clearMidiMapping (const juce::String& paramId);
    void clearAllMidiMappings();
    juce::String getParamDisplayName (const juce::String& paramId) const;
    void resetParamToDefault (const juce::String& paramId);
    void setBoolParam (const char* id, bool v);

    // Gravador da saída processada: PCM 16-bit, duração limitada apenas pela memória disponível.
    bool startRecording();
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    int getRecordedSamples() const { return recordedSamples.load(); }
    double getRecordingSampleRate() const { return recordingSampleRate; }
    bool saveRecording (const juce::File& destination, bool asMp3, juce::String& error);

    const mood::MoodEngine::Telemetry& getTelemetry() const { return engine.telemetry; }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct FsState { bool down = false, hold = false; double t0 = 0.0; int seenDowns = 0; };

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    void timerCallback() override;

    juce::String captureParams() const;
    void applyParams (const juce::String& s);
    mood::EngineParams readParams() const;

    void handleMidi (const juce::MidiMessage& m);
    void applyMidiValues();
    int indexOfParam (const juce::String& paramId) const;
    juce::String midiMapToString() const;
    void midiMapFromString (const juce::String& s);

    void updateFootswitch (int which, FsState& st, const std::atomic<float>* rawParam, const std::atomic<int>& downs);
    void fsTap (int which);
    void fsHoldStart (int which);
    void fsHoldEnd (int which);
    void handleActions();
    bool getBoolParam (const char* id) const;

    void forceLiveSelection();
    void resetUserParamsToDefault();
    void applyFactoryPreset (int index);
    bool loadUserPreset (const juce::String& name);
    juce::StringArray listUserPresetNames() const;

    struct Raw
    {
        std::atomic<float> *wetTime, *wetModify, *mix, *clock, *loopLength, *loopModify;
        std::atomic<float> *wetMode, *routing, *loopMode;
        std::atomic<float> *wetOn, *wetFreeze, *loopOn, *loopOverdub;
        std::atomic<float> *dipClassic, *dipMiso, *dipSpread, *dipDryKill, *dipTrails, *dipNoDub, *dipSmooth;
        std::atomic<float> *rmpTime, *rmpWetModify, *rmpClock, *rmpLoopModify, *rmpLength, *rmpBounce, *rmpSweepTop, *rmpRise, *rmpSpeed, *rmpAtten;
        std::atomic<float> *ctrlSource, *expCv;
        std::atomic<float> *hStereoWidth, *hDirectLoop, *hTone, *hFade, *hBalance, *hRampWave, *hSync, *hSpreadSolo, *hLoopLen, *hTrueBypass;
        std::atomic<float> *outLevel, *inLevel, *clockDegrade, *clockNoise, *wetLowCut, *limiterThreshold;
        std::atomic<float> *fsWet, *fsLoop, *actSaveLeft, *actSaveRight, *actPrevPreset, *actNextPreset;
    } raw {};

    mood::MoodEngine engine;
    std::vector<float> scratchR;
    float lastOutGain = 1.f, lastInGain = 1.f, limiterGain = 1.f;

    // Buffer intercalado int16 para manter o consumo de memória baixo.
    std::vector<int16_t> recordingData;
    int recordingCapacity = 0;
    std::atomic<int> recordedSamples { 0 };
    std::atomic<bool> recording { false };
    double recordingSampleRate = 44100.0;
    int recordingChannels = 2;

    // slots Left / Right + snapshot de "Live"
    juce::String slot[2], liveSnapshot;
    std::atomic<int> pendingSel { 1 };
    int lastSel = 1;
    bool restoring = false;

    // presets
    juce::String currentPreset;
    int currentProgram = 0;

    // footswitches / ações
    FsState fsWetState, fsLoopState;
    std::atomic<int> fsWetDowns { 0 }, fsLoopDowns { 0 };
    bool actPrev[4] = { false, false, false, false };

    // MIDI
    std::vector<juce::RangedAudioParameter*> allParams;
    std::array<std::atomic<int>, 128> ccToParam;
    std::array<std::atomic<float>, 128> ccValue;
    std::array<std::atomic<bool>, 128> ccDirty;
    std::atomic<int> learnParam { -1 }, lastLearnedCC { -1 }, pendingProgram { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoodAudioProcessor)
};
