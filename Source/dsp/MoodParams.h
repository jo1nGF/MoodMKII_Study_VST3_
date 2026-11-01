#pragma once
// MOOD MKII (estudo) - parâmetros "efetivos" do motor + mapeamentos dos knobs.
#include "MoodCommon.h"

namespace mood
{
// Os 11 passos do knob CLOCK (taxa de amostragem do motor) - alternam quarta/quinta justa => passos "harmonizados"
inline constexpr float kClockRates[11] = { 2000.f, 3000.f, 4000.f, 6000.f, 8000.f, 12000.f,
                                           16000.f, 24000.f, 32000.f, 48000.f, 64000.f };

// Divisões rítmicas usadas quando o canal Wet é sincronizado ao Micro-Looper (hidden option SYNC, posição direita)
inline constexpr float kSyncRatios[12] = { 1.f / 16.f, 1.f / 12.f, 1.f / 8.f, 1.f / 6.f, 3.f / 16.f, 1.f / 4.f,
                                           1.f / 3.f, 3.f / 8.f, 1.f / 2.f, 2.f / 3.f, 3.f / 4.f, 1.f };

struct EngineParams
{
    // ---- knobs (0..1) ----
    float wetTime = 0.5f, wetModify = 0.5f, mix = 0.5f, clock = 1.f, loopLength = 0.5f, loopModify = 0.69f;
    // ---- toggles ----
    int wetMode = 0;   // 0 Reverb, 1 Delay, 2 Slip
    int routing = 0;   // 0 IN, 1 IN+LOOP, 2 LOOP
    int loopMode = 1;  // 0 Env, 1 Tape, 2 Stretch
    // ---- footswitches ----
    bool wetOn = false, wetFreeze = false, loopOn = false, loopOverdub = false;
    // ---- dip switches (config) ----
    bool classic = false, miso = false, spread = false, dryKill = false, trails = false, noDub = false, smoothClock = false;
    // ---- dip switches (ramping / external control) ----
    bool rampTime = false, rampWetModify = false, rampClock = false, rampLoopModify = false, rampLength = false;
    bool bounce = true, sweepTop = true, rise = true;
    float rampSpeed = 0.5f;
    int ctrlSource = 0;      // 0 = ramping interno, 1 = expressão/CV
    float expValue = 0.f;    // posição do pedal de expressão / CV (0..1)
    // ---- hidden options ----
    float stereoWidth = 0.8f, directLoop = 0.f, tone = 1.f, fade = 1.f, balance = 0.5f, rampWaveform = 0.f;
    int sync = 0;            // 0 off, 1 looper sincronizado ao wet, 2 wet sincronizado ao looper
    int spreadSolo = 0;      // 0 ambos, 1 só wet, 2 só looper
    bool loopHalf = false;   // loop pela metade (resposta do MOOD original)
    bool trueBypass = false;
    // ---- extra (não existe no pedal) ----
    float degrade = 0.10f;    // 0 = clock "limpo" (anti-alias + reconstrução) ... 1 = aliasing bruto "de fábrica"
    float noise = 0.5f;       // ruído suave que cresce quando o CLOCK desce (0 = desligado)
    float wetLowCut = 0.35f;  // 0..1 -> 20 Hz .. ~790 Hz: passa-altas do canal wet
};

inline float clockHzFromKnob (float knob, bool smooth)
{
    knob = clampf (knob, 0.f, 1.f);
    if (smooth) return 2000.f * std::pow (32.f, knob);                  // 2k..64k contínuo (5 oitavas)
    return kClockRates[(int) std::lround (knob * 10.f)];
}

// Tape: 8 passos em oitavas, sem posição "parada": 4x..0.5x reverso | 0.5x..4x normal
inline float tapeSpeedFromKnob (float v)
{
    static constexpr float kSpd[8] = { -4.f, -2.f, -1.f, -0.5f, 0.5f, 1.f, 2.f, 4.f };
    return kSpd[std::min (7, std::max (0, (int) (v * 8.f)))];
}

// Slip: passos de semitom; meio = NEUTRO (1x); direita = frente até 2x; esquerda = reverso até 2x
inline float slipSpeedFromKnob (float v)
{
    const float d = (v - 0.5f) * 2.f;
    const int k = (int) std::lround (std::fabs (d) * 12.f);
    if (k == 0) return 1.f;
    const float s = std::pow (2.f, (float) k / 12.f);
    return d < 0.f ? -s : s;
}

// Stretch: velocidade de progressão da cabeça de leitura. Meio = congelado; extremos = 1x (sem efeito)
inline float stretchProgressFromKnob (float v)
{
    const float d = (v - 0.5f) * 2.f;
    if (std::fabs (d) < 0.03f) return 0.f;
    const float m = std::pow (std::fabs (d), 1.6f);
    return d < 0.f ? -m : m;
}

// Período "nominal" (em amostras do clock) de cada modo do Wet, usado pelo SYNC (looper -> wet)
inline float wetPeriodSamples (int mode, float t)
{
    if (mode == 0) return 4453.f * lerpf (0.30f, 1.35f, t);
    if (mode == 1) return 48.f + (65000.f - 48.f) * t * t;
    return 64.f * std::pow (512.f, t);
}
} // namespace mood
