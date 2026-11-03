#pragma once
// Motor MOOD MKII (estudo): clock/sample-rate, roteamento Wet <-> Micro-Looper, mix, ramping/expressão.
#include "MoodCommon.h"
#include "MoodParams.h"
#include "MoodWet.h"
#include "MoodLooper.h"

namespace mood
{
// Automação de knobs (dips de RAMPING): bounce (contínuo, 5 formas que se "deformam") ou ramp (uma vez ao ligar)
struct Ramper
{
    float phase = 0.f, oneT = 1.f, rA = 0.f, rB = 0.f;
    bool running = false;
    Rng rng;

    void trigger() { oneT = 0.f; running = true; }

    float next (bool bounce, float speed, float waveform, bool rise, float fs)
    {
        const float hz = 0.03f * std::pow (400.f, clampf (speed, 0.f, 1.f));   // ~0.03 Hz .. 12 Hz
        if (!bounce)
        {
            if (running) { oneT += hz / fs; if (oneT >= 1.f) { oneT = 1.f; running = false; } }
            return rise ? oneT : 1.f - oneT;
        }
        phase += hz / fs;
        if (phase >= 1.f) { phase -= 1.f; rA = rB; rB = rng.unipolar(); }
        const float tri = 1.f - std::fabs (2.f * phase - 1.f);
        const float sq = phase < 0.5f ? 1.f : 0.f;
        const float sn = 0.5f - 0.5f * std::cos (kTwoPi * phase);
        const float sm = lerpf (rA, rB, 0.5f - 0.5f * std::cos (kPi * phase));
        const float shape[5] = { tri, sq, sn, rA, sm };   // triangular, quadrada, seno, random (S&H), random suave
        const float pos = clampf (waveform, 0.f, 1.f) * 4.f;
        const int i = std::min (3, (int) pos);
        const float s = lerpf (shape[i], shape[i + 1], pos - (float) i);
        return rise ? s : 1.f - s;
    }
};

class MoodEngine
{
public:
    struct Telemetry
    {
        std::atomic<float> loopPhase { 0.f }, loopPeriod { 1.f }, clockHz { 64000.f };
        std::atomic<int> loopState { 0 };   // 0 gravando (ouvindo), 1 tocando, 2 overdub
        std::atomic<bool> wetAudible { false };
        std::atomic<float> inPeak { 0.f }, outPeak { 0.f };   // picos do último bloco
        std::atomic<int> limitHits { 0 };                       // amostras em que o limitador interno atuou no último bloco
    };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        wet.init();
        looper.init();
        reset();
    }

    void reset()
    {
        wet.clearAll();
        looper.reset();
        for (int i = 0; i < 4; ++i) hl[i] = hr[i] = ohl[i] = ohr[i] = 0.f;
        tickPhase = 0.0;
        wetPrevL = wetPrevR = 0.f;
        needInit = true;
        duck = 1.f; duckState = 0;
        prevAnyOn = false;
        ramper = Ramper{};
        for (auto& f : aaIn) f.reset();
        for (auto& f : aaOut) f.reset();
        aaMix = 0.f; nzLvl = 0.f;
        for (auto& f : nzLp) f.reset();
    }

    // Pode ser chamado "in-place" (in == out).
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const EngineParams& p)
    {
        if (p.trueBypass)
        {
            for (int i = 0; i < n; ++i) { const float l = inL[i], r = inR[i]; outL[i] = l; outR[i] = r; }
            return;
        }
        const float aSm = timeCoef (0.015f, fs), aGain = timeCoef (0.005f, fs), aClk = timeCoef (0.03f, fs);
        const float duckStep = 1.f / (0.004f * fs);
        const bool anyOn = p.wetOn || p.loopOn;
        if (anyOn && ! prevAnyOn) ramper.trigger();   // "ramp" dispara quando o pedal é ligado
        prevAnyOn = anyOn;
        const bool anyKnob = p.rampTime || p.rampWetModify || p.rampClock || p.rampLoopModify || p.rampLength;
        const float kHead = 0.5f;   // headroom interno de 6 dB: o motor trabalha com metade da amplitude e a saída volta ao ganho original
        float blkIn = 0.f, blkOut = 0.f;
        int hits = 0;
        const float gW = std::min (1.f, 2.f * (1.f - p.balance));   // LEVEL BALANCE
        const float gL = std::min (1.f, 2.f * p.balance);

        for (int i = 0; i < n; ++i)
        {
            float dryL = inL[i], dryR = inR[i];
            if (p.miso) dryL = dryR = 0.5f * (dryL + dryR);

            // ---- ramping (interno) ou expressão/CV (externo) ----
            float s = 0.f;
            bool modOn;
            if (p.ctrlSource == 0) { modOn = anyKnob; if (anyKnob) s = ramper.next (p.bounce, p.rampSpeed, p.rampWaveform, p.rise, fs) * clampf (p.rampAtten, 0.f, 1.f); }
            else { modOn = true; s = p.rise ? p.expValue : 1.f - p.expValue; }
            auto mod = [&] (float knob, bool sel)
            {
                if (! sel || ! modOn) return knob;
                const float lo = p.sweepTop ? 0.f : knob, hi = p.sweepTop ? knob : 1.f;   // o knob define o topo ou o fundo da faixa
                return lerpf (lo, hi, s);
            };
            const float tEff = mod (p.wetTime, p.rampTime), mEff = mod (p.wetModify, p.rampWetModify);
            const float cEff = mod (p.clock, p.rampClock), lmEff = mod (p.loopModify, p.rampLoopModify);
            const float llEff = mod (p.loopLength, p.rampLength);
            const float mixEff = (p.ctrlSource == 1 && ! anyKnob) ? mod (p.mix, true) : p.mix;   // sem knobs atribuídos: EXP/CV controla o MIX

            if (needInit)
            {
                smTime = tEff; smModify = mEff; smLoopLen = llEff; smLoopMod = lmEff; smMix = mixEff;
                fc = clockHzFromKnob (cEff, p.smoothClock); fcLog = std::log2 (fc);
                activeWetMode = p.wetMode; activeLoopMode = p.loopMode;
                dryG = p.dryKill ? 0.f : 1.f; moodG = 0.f;
                smDeg = p.degrade; smNoise = p.noise; smLowCut = p.wetLowCut;
                needInit = false;
            }
            smTime += aSm * (tEff - smTime);
            smModify += aSm * (mEff - smModify);
            smLoopLen += aSm * (llEff - smLoopLen);
            smLoopMod += aSm * (lmEff - smLoopMod);
            smMix += aSm * (mixEff - smMix);
            smDeg += aSm * (p.degrade - smDeg);
            smNoise += aSm * (p.noise - smNoise);
            smLowCut += aSm * (p.wetLowCut - smLowCut);

            // ---- CLOCK: 11 passos harmonizados (ou contínuo com SMOOTH) ----
            const float fcT = clockHzFromKnob (cEff, p.smoothClock);
            if (p.smoothClock) { fcLog += aClk * (std::log2 (fcT) - fcLog); fc = std::exp2 (fcLog); }
            else { fc = fcT; fcLog = std::log2 (fc); }

            // ---- troca de modo: abaixa por ~4 ms, troca, sobe ----
            if (duckState == 0 && (p.wetMode != activeWetMode || p.loopMode != activeLoopMode)) duckState = 1;
            if (duckState == 1)
            {
                duck -= duckStep;
                if (duck <= 0.f)
                {
                    duck = 0.f;
                    if (p.wetMode != activeWetMode) wet.clearAll();
                    activeWetMode = p.wetMode; activeLoopMode = p.loopMode;
                    duckState = 2;
                }
            }
            else if (duckState == 2) { duck += duckStep; if (duck >= 1.f) { duck = 1.f; duckState = 0; } }

            // ---- SYNC (hidden option) ----
            float delayOverride = 0.f, tWet = smTime;
            if (p.sync == 2)   // Wet sincronizado ao looper: TIME anda em divisões rítmicas do loop
            {
                const int idx = std::min (11, (int) (tEff * 12.f));
                delayOverride = (float) looper.length() * kSyncRatios[idx];
                tWet = (float) idx / 11.f;
            }
            const int syncLen = (p.sync == 1) ? (int) std::lround (wetPeriodSamples (activeWetMode, tEff)) : 0;   // looper segue o TIME

            wpar.mode = activeWetMode; wpar.time = tWet; wpar.modify = smModify;
            wpar.engaged = p.wetOn; wpar.freeze = p.wetFreeze && p.wetOn;
            wpar.spread = p.spread && p.spreadSolo != 2;
            wpar.classic = p.classic; wpar.trails = p.trails; wpar.width = p.stereoWidth; wpar.tone = p.tone;
            wpar.fc = fc; wpar.delaySamplesOverride = delayOverride;
            wpar.lowCutHz = 20.f * std::exp2 (5.3f * clampf (smLowCut, 0.f, 1.f));
            {   // ruído suave ("hiss" de clock): cresce quando o CLOCK desce, é amostrado no clock do motor (textura digital) e passa pelo filtro de reconstrução
                const float cn = clampf (std::log2 (fc / 2000.f) / 5.f, 0.f, 1.f);
                nzLvl = clampf (smNoise, 0.f, 1.f) * 0.012f * kHead * std::pow (1.f - cn, 1.4f);
            }

            lpar.mode = activeLoopMode; lpar.length = smLoopLen; lpar.modify = smLoopMod;
            lpar.play = p.loopOn; lpar.overdub = p.loopOverdub;
            lpar.spread = p.spread && p.spreadSolo != 1;
            lpar.classic = p.classic; lpar.noDub = p.noDub; lpar.half = p.loopHalf; lpar.fade = p.fade;
            lpar.fc = fc; lpar.syncLen = syncLen;

            routingNow = p.routing; directLoop = p.directLoop; gWet = gW * duck; gLoop = gL * duck;

            // ---- DEGRADE: anti-alias de entrada + reconstrução de saída ----
            // DEGRADE=0 mantém o clock selecionado, mas usa filtragem de maior qualidade.
            // À medida que DEGRADE aumenta, o filtro abre até ficar praticamente bruto em 1.
            const float dg = clampf (smDeg, 0.f, 1.f);
            const bool degradeActive = dg > 0.001f;
            const float fcDeg = (fc < fs) ? clampf (fc, 2000.f, 8000.f) : fc;
            const float cutHz = 0.40f * std::exp2 (5.f * dg * dg) * fcDeg;
            const bool useAa = fcDeg < fs && cutHz < 0.45f * fs;
            aaMix += aGain * ((useAa ? 1.f : 0.f) - aaMix);
            aaIn[0].setCutoff (cutHz, fs); aaIn[1].setCutoff (cutHz, fs);
            const float fL = aaIn[0].process (dryL), fR = aaIn[1].process (dryR);
            const float xinL = dryL + aaMix * (fL - dryL), xinR = dryR + aaMix * (fR - dryR);

            // ---- ticks do motor (taxa = clock) ----
            hl[3] = hl[2]; hl[2] = hl[1]; hl[1] = hl[0]; hl[0] = xinL * kHead;
            hr[3] = hr[2]; hr[2] = hr[1]; hr[1] = hr[0]; hr[0] = xinR * kHead;
            blkIn = std::max (blkIn, std::max (std::fabs (dryL), std::fabs (dryR)));
            const double inc = (double) fc / (double) fs;
            const double ph = tickPhase + inc;
            const int ntAll = (int) ph;
            const int nt = std::min (ntAll, 4);
            for (int k = 0; k < nt; ++k)
            {
                const float u = clampf ((float) (((double) (k + 1) - tickPhase) / inc), 0.f, 1.f);
                tick (hermite (hl[3], hl[2], hl[1], hl[0], u), hermite (hr[3], hr[2], hr[1], hr[0], u));
            }
            tickPhase = ph - (double) ntAll;

            // ---- saída do motor: ZOH (imagens "degrau") se clock < fs e degrade ativo; interpolação caso contrário ----
            float yl, yr;
            if (fc < fs && degradeActive) { yl = ohl[0]; yr = ohr[0]; }
            else { const float t = (float) tickPhase; yl = hermite (ohl[3], ohl[2], ohl[1], ohl[0], t); yr = hermite (ohr[3], ohr[2], ohr[1], ohr[0], t); }

            aaOut[0].setCutoff (cutHz, fs); aaOut[1].setCutoff (cutHz, fs);
            const float oL = aaOut[0].process (yl), oR = aaOut[1].process (yr);
            yl += aaMix * (oL - yl);
            yr += aaMix * (oR - yr);

            if (p.classic)   // Classic: ruído que cresce conforme o CLOCK desce
            {
                const float cn = 1.f - clampf (std::log2 (fc / 2000.f) / 5.f, 0.f, 1.f);
                const float lvl = 0.004f * cn * std::sqrt (cn);
                noisePh += kTwoPi * (fc * 0.25f) / fs;
                if (noisePh > kTwoPi) noisePh -= kTwoPi;
                const float nz = (noiseRng.bipolar() + 0.6f * std::sin (noisePh)) * lvl;
                yl += nz * kHead; yr += nz * 0.9f * kHead;
            }
            if (std::fabs (yl) > 0.9f || std::fabs (yr) > 0.9f) ++hits;
            yl = softLimit (yl) / kHead; yr = softLimit (yr) / kHead;

            // ---- MIX (crossfade equal-power) + DRY KILL ----
            const bool active = wet.isAudible() || p.loopOn;
            const float tm = active ? std::sin (smMix * kPi * 0.5f) : 0.f;
            const float td = p.dryKill ? 0.f : (active ? std::cos (smMix * kPi * 0.5f) : 1.f);
            moodG += aGain * (tm - moodG);
            dryG += aGain * (td - dryG);
            const float oLv = dryL * dryG + yl * moodG, oRv = dryR * dryG + yr * moodG;
            outL[i] = oLv; outR[i] = oRv;
            blkOut = std::max (blkOut, std::max (std::fabs (oLv), std::fabs (oRv)));
        }

        telemetry.inPeak.store (blkIn);
        telemetry.outPeak.store (blkOut);
        telemetry.limitHits.store (hits);
        telemetry.loopPhase.store (looper.phase());
        telemetry.loopPeriod.store ((float) looper.length() / fc);
        telemetry.clockHz.store (fc);
        telemetry.wetAudible.store (wet.isAudible());
        telemetry.loopState.store (looper.isPlaying() ? (p.loopOverdub ? 2 : 1) : 0);
    }

    Telemetry telemetry;

private:
    void tick (float xl, float xr)
    {
        // O looper grava (no estado "ouvindo") o sinal + o Wet do tick anterior => ambiência "assada" no loop.
        const MicroLooper::Frame lo = looper.process (xl, xr, xl + wetPrevL, xr + wetPrevR, lpar);

        // ROUTING só vale com os dois canais ligados
        const int route = (looper.isPlaying() && wpar.engaged) ? routingNow : 0;
        const float wl = (route != 2 ? xl : 0.f) + (route != 0 ? lo.l : 0.f);
        const float wr = (route != 2 ? xr : 0.f) + (route != 0 ? lo.r : 0.f);
        float ywl, ywr;
        wet.process (wl, wr, ywl, ywr, wpar);
        wetPrevL = ywl; wetPrevR = ywr;

        const float dl = (route == 0) ? 1.f : directLoop;   // loop roteado pelo Wet vira 100% wet (salvo DIRECT MICRO-LOOP)
        const float nzL = nzLp[0].lp (noiseRng.bipolar(), 0.6f) * nzLvl, nzR = nzLp[1].lp (noiseRng.bipolar(), 0.6f) * nzLvl;
        const float mL = ywl * gWet + lo.l * dl * gLoop + nzL;
        const float mR = ywr * gWet + lo.r * dl * gLoop + nzR;
        ohl[3] = ohl[2]; ohl[2] = ohl[1]; ohl[1] = ohl[0]; ohl[0] = mL;
        ohr[3] = ohr[2]; ohr[2] = ohr[1]; ohr[1] = ohr[0]; ohr[0] = mR;
    }

    float fs = 48000.f;
    WetChannel wet;
    MicroLooper looper;
    WetChannel::Params wpar;
    MicroLooper::Params lpar;
    Ramper ramper;
    Rng noiseRng;
    LowPass4 aaIn[2], aaOut[2];
    float aaMix = 0.f;
    float smDeg = 0.1f, smNoise = 0.5f, smLowCut = 0.35f, nzLvl = 0.f;
    OnePole nzLp[2];

    double tickPhase = 0.0;
    float fc = 64000.f, fcLog = 15.97f;
    float hl[4] = {}, hr[4] = {}, ohl[4] = {}, ohr[4] = {};
    float wetPrevL = 0.f, wetPrevR = 0.f;
    float smTime = 0.5f, smModify = 0.5f, smLoopLen = 0.5f, smLoopMod = 0.5f, smMix = 0.5f;
    float dryG = 1.f, moodG = 0.f;
    float duck = 1.f;
    int duckState = 0, activeWetMode = 0, activeLoopMode = 1, routingNow = 0;
    float directLoop = 0.f, gWet = 1.f, gLoop = 1.f, noisePh = 0.f;
    bool needInit = true, prevAnyOn = false;
};
} // namespace mood
