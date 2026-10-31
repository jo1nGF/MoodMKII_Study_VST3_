#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginParameters.h"
#include "dsp/MoodEngine.h"

class MoodAudioProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
{
public:
    MoodAudioProcessor();
    ~MoodAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- usado pelo editor ----
    void savePresetSlot (int slot);          // 0 = esquerda, 1 = direita (guarda os knobs/toggles/dips atuais)
    void resetHiddenOptions();
    const mood::MoodEngine::Telemetry& getTelemetry() const { return engine.telemetry; }

    juce::AudioProcessorValueTreeState apvts;

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
    juce::String captureParams() const;
    void applyParams (const juce::String& s);
    mood::EngineParams readParams() const;

    struct Raw
    {
        std::atomic<float> *wetTime, *wetModify, *mix, *clock, *loopLength, *loopModify;
        std::atomic<float> *wetMode, *routing, *loopMode;
        std::atomic<float> *wetOn, *wetFreeze, *loopOn, *loopOverdub;
        std::atomic<float> *dipClassic, *dipMiso, *dipSpread, *dipDryKill, *dipTrails, *dipNoDub, *dipSmooth;
        std::atomic<float> *rmpTime, *rmpWetModify, *rmpClock, *rmpLoopModify, *rmpLength, *rmpBounce, *rmpSweepTop, *rmpRise, *rmpSpeed;
        std::atomic<float> *ctrlSource, *expCv;
        std::atomic<float> *hStereoWidth, *hDirectLoop, *hTone, *hFade, *hBalance, *hRampWave, *hSync, *hSpreadSolo, *hLoopLen, *hTrueBypass;
        std::atomic<float> *outLevel, *clockDegrade;
    } raw {};

    mood::MoodEngine engine;
    std::vector<float> scratchR;
    float lastOutGain = 1.f;

    // presets (esquerda / direita) + snapshot da posição "live"
    juce::String slot[2], liveSnapshot;
    std::atomic<int> pendingSel { 1 };
    int lastSel = 1;
    bool restoring = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoodAudioProcessor)
};
