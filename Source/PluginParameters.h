#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <vector>
#include "dsp/MoodParams.h"

// IDs dos parâmetros (estáveis: usados para automação e presets do host)
namespace ids
{
    // knobs
    inline constexpr const char* wetTime = "wet_time";
    inline constexpr const char* wetModify = "wet_modify";
    inline constexpr const char* mix = "mix";
    inline constexpr const char* clock = "clock";
    inline constexpr const char* loopLength = "loop_length";
    inline constexpr const char* loopModify = "loop_modify";
    // toggles
    inline constexpr const char* wetMode = "wet_mode";
    inline constexpr const char* routing = "routing";
    inline constexpr const char* loopMode = "loop_mode";
    inline constexpr const char* presetSel = "preset_sel";
    // footswitches (estado)
    inline constexpr const char* wetOn = "wet_on";
    inline constexpr const char* wetFreeze = "wet_freeze";
    inline constexpr const char* loopOn = "loop_on";
    inline constexpr const char* loopOverdub = "loop_overdub";
    // dip switches (config)
    inline constexpr const char* dipClassic = "dip_classic";
    inline constexpr const char* dipMiso = "dip_miso";
    inline constexpr const char* dipSpread = "dip_spread";
    inline constexpr const char* dipDryKill = "dip_dry_kill";
    inline constexpr const char* dipTrails = "dip_trails";
    inline constexpr const char* dipLatch = "dip_latch";
    inline constexpr const char* dipNoDub = "dip_no_dub";
    inline constexpr const char* dipSmooth = "dip_smooth";
    // dip switches (ramping / external control)
    inline constexpr const char* rmpTime = "rmp_time";
    inline constexpr const char* rmpWetModify = "rmp_wet_modify";
    inline constexpr const char* rmpClock = "rmp_clock";
    inline constexpr const char* rmpLoopModify = "rmp_loop_modify";
    inline constexpr const char* rmpLength = "rmp_length";
    inline constexpr const char* rmpBounce = "rmp_bounce";
    inline constexpr const char* rmpSweepTop = "rmp_sweep_top";
    inline constexpr const char* rmpRise = "rmp_rise";
    inline constexpr const char* rmpSpeed = "rmp_speed";
    inline constexpr const char* rmpAtten = "rmp_atten";
    inline constexpr const char* ctrlSource = "ctrl_source";
    inline constexpr const char* expCv = "exp_cv";
    // hidden options
    inline constexpr const char* hStereoWidth = "h_stereo_width";
    inline constexpr const char* hDirectLoop = "h_direct_loop";
    inline constexpr const char* hTone = "h_tone";
    inline constexpr const char* hFade = "h_fade";
    inline constexpr const char* hBalance = "h_balance";
    inline constexpr const char* hRampWave = "h_ramp_wave";
    inline constexpr const char* hSync = "h_sync";
    inline constexpr const char* hSpreadSolo = "h_spread_solo";
    inline constexpr const char* hLoopLen = "h_loop_len";
    inline constexpr const char* hTrueBypass = "h_true_bypass";
    // extra (não existe no pedal)
    inline constexpr const char* outLevel = "out_level";
    inline constexpr const char* clockDegrade = "clock_degrade";
    inline constexpr const char* inLevel = "in_level";
    inline constexpr const char* clockNoise = "clock_noise";
    inline constexpr const char* wetLowCut = "wet_lowcut";
    inline constexpr const char* limiterThreshold = "limiter_threshold";
    // gestos de footswitch e ações (momentâneos): mapeáveis em qualquer controlador / MIDI
    inline constexpr const char* fsWet = "fs_wet";
    inline constexpr const char* fsLoop = "fs_loop";
    inline constexpr const char* actSaveLeft = "act_save_left";
    inline constexpr const char* actSaveRight = "act_save_right";
    inline constexpr const char* actPrevPreset = "act_prev_preset";
    inline constexpr const char* actNextPreset = "act_next_preset";
}

// Parâmetros guardados nos presets (esquerda/direita). No pedal: knobs, toggles e dip switches.
inline const std::vector<const char*>& presetParamIds()
{
    static const std::vector<const char*> v = {
        ids::wetTime, ids::wetModify, ids::mix, ids::clock, ids::loopLength, ids::loopModify,
        ids::wetMode, ids::routing, ids::loopMode,
        ids::dipClassic, ids::dipMiso, ids::dipSpread, ids::dipDryKill, ids::dipTrails, ids::dipLatch, ids::dipNoDub, ids::dipSmooth,
        ids::rmpTime, ids::rmpWetModify, ids::rmpClock, ids::rmpLoopModify, ids::rmpLength, ids::rmpBounce, ids::rmpSweepTop,
        ids::rmpRise, ids::rmpSpeed, ids::rmpAtten, ids::ctrlSource,
        ids::hStereoWidth, ids::hDirectLoop, ids::hTone, ids::hFade, ids::hBalance, ids::hRampWave, ids::hSync, ids::hSpreadSolo, ids::hLoopLen };
    return v;
}

// Presets de usuário (arquivos): tudo que sai do som, inclusive os extras de "limpeza" do plugin
inline const std::vector<const char*>& userPresetParamIds()
{
    static const std::vector<const char*> v = [] {
        std::vector<const char*> r = presetParamIds();
        r.push_back (ids::clockDegrade); r.push_back (ids::clockNoise); r.push_back (ids::wetLowCut);
        return r; }();
    return v;
}

inline const std::vector<const char*>& hiddenParamIds()
{
    static const std::vector<const char*> v = { ids::hStereoWidth, ids::hDirectLoop, ids::hTone, ids::hFade, ids::hBalance,
                                                ids::hRampWave, ids::hSync, ids::hSpreadSolo, ids::hLoopLen };
    return v;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto addF = [&] (const char* id, const String& name, float def, const AudioParameterFloatAttributes& attr = {})
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.f, 1.f), def, attr));
    };
    auto addB = [&] (const char* id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto addC = [&] (const char* id, const String& name, const StringArray& choices, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def));
    };

    const auto clockAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
    {
        const int idx = (int) std::lround (jlimit (0.f, 1.f, v) * 10.f);
        return String ((int) (mood::kClockRates[idx] / 1000.f)) + " kHz";
    });

    // ---- knobs ----
    addF (ids::wetTime,    "Wet TIME",     0.5f);
    addF (ids::wetModify,  "Wet MODIFY",   0.5f);
    addF (ids::mix,        "MIX",          0.5f);
    addF (ids::clock,      "CLOCK",        1.0f, clockAttr);
    addF (ids::loopLength, "Loop LENGTH",  1.0f);
    addF (ids::loopModify, "Loop MODIFY",  0.69f);
    // ---- toggles ----
    addC (ids::wetMode,  "Wet MODE",  StringArray { "Reverb", "Delay", "Slip" }, 0);
    addC (ids::routing,  "ROUTING",   StringArray { "In", "In + Loop", "Loop" }, 0);
    addC (ids::loopMode, "Loop MODE", StringArray { "Env", "Tape", "Stretch" }, 1);
    addC (ids::presetSel, "PRESET",   StringArray { "Left", "Live", "Right" }, 1);
    // ---- footswitches ----
    addB (ids::wetOn,       "Wet ENGAGE",   false);
    addB (ids::wetFreeze,   "Wet FREEZE",   false);
    addB (ids::loopOn,      "Loop PLAY",    false);
    addB (ids::loopOverdub, "Loop OVERDUB", false);
    // ---- dip switches ----
    addB (ids::dipClassic, "Dip CLASSIC",  false);
    addB (ids::dipMiso,    "Dip MISO",     false);
    addB (ids::dipSpread,  "Dip SPREAD",   false);
    addB (ids::dipDryKill, "Dip DRY KILL", false);
    addB (ids::dipTrails,  "Dip TRAILS",   false);
    addB (ids::dipLatch,   "Dip LATCH",    false);
    addB (ids::dipNoDub,   "Dip NO DUB",   false);
    addB (ids::dipSmooth,  "Dip SMOOTH",   false);
    // ---- ramping / external control ----
    addB (ids::rmpTime,       "Ramp Wet TIME",     false);
    addB (ids::rmpWetModify,  "Ramp Wet MODIFY",   false);
    addB (ids::rmpClock,      "Ramp CLOCK",        false);
    addB (ids::rmpLoopModify, "Ramp Loop MODIFY",  false);
    addB (ids::rmpLength,     "Ramp Loop LENGTH",  false);
    addB (ids::rmpBounce,     "Ramp BOUNCE (off = ramp)", true);
    addB (ids::rmpSweepTop,   "Ramp SWEEP Top (off = Bottom)", true);
    addB (ids::rmpRise,       "Ramp POLARITY Rise (off = Fall)", true);
    addF (ids::rmpSpeed,      "Ramp SPEED (MIX knob)", 0.5f);
    addF (ids::rmpAtten,      "Ramp ATTENUATOR",       1.0f);
    addC (ids::ctrlSource,    "Control Source", StringArray { "Ramp", "EXP / CV" }, 0);
    addF (ids::expCv,         "EXP / CV", 0.f);
    // ---- hidden options ----
    addF (ids::hStereoWidth, "Hidden Stereo Width",   0.8f);
    addF (ids::hDirectLoop,  "Hidden Direct Micro-Loop", 0.f);
    addF (ids::hTone,        "Hidden Tone",           1.f);
    addF (ids::hFade,        "Hidden Fade",           1.f);
    addF (ids::hBalance,     "Hidden Level Balance",  0.5f);
    addF (ids::hRampWave,    "Hidden Ramp Waveform",  0.f);
    addC (ids::hSync,        "Hidden Sync",           StringArray { "Unsynced", "Loop to Wet", "Wet to Loop" }, 0);
    addC (ids::hSpreadSolo,  "Hidden Spread Solo",    StringArray { "Both", "Wet only", "Loop only" }, 0);
    addC (ids::hLoopLen,     "Hidden Loop Length",    StringArray { "Half", "Full" }, 1);
    addB (ids::hTrueBypass,  "Hidden True Bypass",    false);
    // ---- extra ----
    addF (ids::clockDegrade, "Clock Degrade (extra)", 0.10f);
    addF (ids::clockNoise,   "Clock Noise (extra)",   0.5f);
    addF (ids::wetLowCut,    "Wet Low Cut (extra)",   0.35f);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::inLevel, 1 }, "Input Level (dB)",
                                                       NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f));
    // gestos de footswitch (tap / hold) e ações: botões momentâneos, mapeáveis em controladores
    addB (ids::fsWet,         "Footswitch Wet (tap / hold)",  false);
    addB (ids::fsLoop,        "Footswitch Loop (tap / hold)", false);
    addB (ids::actSaveLeft,   "Action: Save Preset LEFT",     false);
    addB (ids::actSaveRight,  "Action: Save Preset RIGHT",    false);
    addB (ids::actPrevPreset, "Action: Previous Preset",      false);
    addB (ids::actNextPreset, "Action: Next Preset",          false);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::outLevel, 1 }, "Output Level (dB)",
                                                       NormalisableRange<float> (-24.f, 12.f, 0.1f), 0.f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ids::limiterThreshold, 1 }, "Limiter Threshold (dB)",
                                                       NormalisableRange<float> (-12.f, 0.f, 0.1f), -1.f));
    return layout;
}
