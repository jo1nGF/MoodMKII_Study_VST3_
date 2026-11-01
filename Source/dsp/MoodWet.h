#pragma once
// Canal WET (estilo Old Blood Noise Endeavors): Reverb / Delay / Slip + Freeze, Trails, Tone, Spread.
#include "MoodCommon.h"
#include "MoodParams.h"
#include "MoodReverb.h"

namespace mood
{
// Delay com crossfade entre duas posições de leitura (MKII: troca de tempo "limpa", sem pitch-bend)
// ou com glide do ponteiro de leitura (Classic: "borrachudo").
struct DelayUnit
{
    DelayLine line;
    float cur = 4000.f, next = 4000.f, slew = 4000.f, xf = 1.f, xfInc = 0.f;

    void init() { line.init (65536); }
    void clear() { line.clear(); xf = 1.f; }
    inline void write (float x) { line.write (x); }

    inline float read (float target, bool classic, int xfTicks, float slewCoef)
    {
        if (classic)
        {
            slew += slewCoef * (target - slew);
            cur = slew;
            xf = 1.f;
            return line.read (slew);
        }
        slew = cur;
        if (xf >= 1.f && std::fabs (target - cur) > 2.f)
        {
            next = target;
            xf = 0.f;
            xfInc = 1.f / (float) std::max (1, xfTicks);
        }
        const float a = line.read (cur);
        if (xf < 1.f)
        {
            const float b = line.read (next);
            const float y = a + (b - a) * xf;
            xf += xfInc;
            if (xf >= 1.f) { xf = 1.f; cur = next; }
            return y;
        }
        return a;
    }
};

// "Auto-sampler": grava continuamente e reproduz janelas a velocidade/direção escolhidas
// (pitch-shifter de linha de atraso com 2 cabeças + janela seno²). Freeze = para de gravar e repete a janela.
struct SlipUnit
{
    DelayLine line;
    float phase = 0.f, neutral = 1.f;

    void init() { line.init (65536); }
    void clear() { line.clear(); phase = 0.f; neutral = 1.f; }

    inline float process (float in, float S, float r, bool frozen, bool classic, float neutralCoef)
    {
        const bool isNeutral = !frozen && std::fabs (1.f - r) < 1e-3f;
        neutral += neutralCoef * ((isNeutral ? 1.f : 0.f) - neutral);
        if (!frozen) line.write (in);
        const float wInc = frozen ? 0.f : 1.f;
        phase += (wInc - r) / S;
        phase -= std::floor (phase);
        float phB = phase + 0.5f;
        if (phB >= 1.f) phB -= 1.f;
        const float dA = 2.f + phase * S, dB = 2.f + phB * S;
        float a, b;
        if (classic) { a = line.at ((int) dA); b = line.at ((int) dB); }   // Classic: sem interpolação => "crocante"
        else         { a = line.read (dA);     b = line.read (dB); }
        const float sA = std::sin (kPi * phase), sB = std::sin (kPi * phB);
        const float shifted = a * sA * sA + b * sB * sB;
        return lerpf (shifted, in, neutral);
    }
};

class WetChannel
{
public:
    static constexpr int kMaxDelay = 65000;

    struct Params
    {
        int mode = 0;                     // 0 Reverb, 1 Delay, 2 Slip
        float time = 0.5f, modify = 0.5f; // 0..1
        bool engaged = false, freeze = false, spread = false, classic = false, trails = false;
        float width = 0.8f, tone = 1.f;
        float fc = 32000.f;               // taxa do motor (Hz)
        float delaySamplesOverride = 0.f; // >0: tempo do delay vindo do SYNC (wet -> looper)
        float lowCutHz = 20.f;            // passa-altas (2 polos) na saída do wet (extra do plugin)
    };

    void init()
    {
        revA.init(); revB.init();
        for (auto& d : dl) d.init();
        for (auto& s : sl) s.init();
        for (auto& r : ring) r.init (4096);
        clearAll();
        outGain = inGain = frz = 0.f;
        armed = false;
    }

    void clearAll()
    {
        revA.clear(); revB.clear();
        for (auto& d : dl) d.clear();
        for (auto& s : sl) s.clear();
        for (auto& r : ring) r.clear();
        for (auto& t : tone) for (auto& f : t) f.reset();
        for (auto& t : lc) for (auto& f : t) f.reset();
    }

    bool isAudible() const { return outGain > 0.0005f; }

    void process (float inL, float inR, float& outL, float& outR, const Params& p)
    {
        const float fc = p.fc;
        const float fast = timeCoef (0.004f, fc);

        if (p.engaged)
        {
            inGain += fast * (1.f - inGain);
            outGain += fast * (1.f - outGain);
            armed = true;
        }
        else
        {
            inGain += fast * (0.f - inGain);
            if (p.trails) outGain *= std::exp (-1.f / (0.45f * fc));   // fade suave (~3 s até -60 dB)
            else          outGain += fast * (0.f - outGain);
            if (armed && outGain < 1e-4f) { clearAll(); outGain = 0.f; inGain = 0.f; frz = 0.f; armed = false; }
        }
        if (!armed) { outL = outR = 0.f; return; }

        frz += fast * ((p.freeze ? 1.f : 0.f) - frz);
        const float xl = inL * inGain, xr = inR * inGain;
        float yl = 0.f, yr = 0.f;

        if (p.mode == 0) reverbMode (xl, xr, yl, yr, p);
        else if (p.mode == 1) delayMode (xl, xr, yl, yr, p);
        else slipMode (xl, xr, yl, yr, p);

        if (p.tone < 0.995f)   // hi-cut simples (hidden option TONE): 2 polos
        {
            const float a = onePoleCoef (500.f * std::pow (36.f, p.tone), fc);
            yl = tone[0][1].lp (tone[0][0].lp (yl, a), a);
            yr = tone[1][1].lp (tone[1][0].lp (yr, a), a);
        }
        if (p.lowCutHz > 25.f)   // low cut do wet (2 estágios de 1 polo)
        {
            const float a = onePoleCoef (p.lowCutHz, fc);
            const float h1l = yl - lc[0][0].lp (yl, a), h1r = yr - lc[1][0].lp (yr, a);
            yl = h1l - lc[0][1].lp (h1l, a);
            yr = h1r - lc[1][1].lp (h1r, a);
        }
        outL = yl * outGain;
        outR = yr * outGain;
    }

private:
    static inline void applyWidth (float& l, float& r, float wf)
    {
        const float m = 0.5f * (l + r), s = 0.5f * (l - r) * wf;
        l = m + s;
        r = m - s;
    }

    float ringProc (int c, float y, float fc)
    {
        const int D = std::max (8, (int) (0.0045f * fc));
        const float r = ring[c].read ((float) D);
        const float o = y + 0.35f * r;
        ring[c].write (o);
        return o;
    }

    void reverbMode (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        const float t = p.time;
        Reverb::Params rp;
        rp.scale = lerpf (0.30f, 1.35f, t);                                    // TIME = tamanho...
        // decay contínuo até 0.998 (sustain praticamente infinito, sem estourar). O ganho de entrada acompanha o decay
        // (lei calibrada ~ (1-g²)^0.275) => nível de regime ~constante em todo o curso do TIME, sem "penhasco" de volume.
        const float dec = lerpf (0.28f, 0.998f, std::pow (t, 1.5f));                     // ...e decay
        const float dc = std::min (dec, 0.99f);   // acima de 0.99 o regime leva minutos: não compensa além disso
        const float comp = std::sqrt ((1.f - dc * dc) / 0.36f);   // compensação plena: energia de regime ~constante (medida com ruído ROSA)
        rp.decay = lerpf (dec, 1.f, frz);
        rp.smear = p.modify;                                                   // MODIFY = smear (0 multi-tap -> 1 reverb)
        rp.damp = lerpf (lerpf (0.85f, 0.62f, p.modify), 1.f, frz);   // SMEAR baixo = ecos nítidos; alto = cauda mais suave
        rp.modDepth = p.classic ? 0.f : 1.f;                                   // Classic: sem modulação => "ringy"
        rp.inputGain = 0.28f * comp * (1.f - frz);
        rp.outGain = lerpf (2.2f, 1.0f, std::pow (p.modify, 0.6f));    // ecos (multi-tap) com presença: SMEAR 0 soa tão forte quanto o reverb
        rp.hpHz = lerpf (120.f, 4.f, frz);   // no FREEZE o passa-altas do tanque sai de cena (senão os graves decairiam: sustain infinito)
        float al, ar, bl, br;
        if (!p.spread)   // 2 reverbs mono independentes: preserva a imagem estéreo de entrada
        {
            revA.process (xl, al, ar, rp, p.fc);
            revB.process (xr, bl, br, rp, p.fc);
            yl = 0.5f * (al + ar) * 1.41f;
            yr = 0.5f * (bl + br) * 1.41f;
        }
        else             // SPREAD: reflexões "espalhadas" no campo estéreo
        {
            revA.process (0.5f * (xl + xr), al, ar, rp, p.fc);
            revB.process (0.f, bl, br, rp, p.fc);
            yl = al; yr = ar;
            applyWidth (yl, yr, clampf (p.width * 1.25f, 0.f, 1.25f));
        }
        if (p.classic) { yl = ringProc (0, yl, p.fc); yr = ringProc (1, yr, p.fc); }
    }

    void delayMode (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        float target = p.delaySamplesOverride > 0.f ? p.delaySamplesOverride
                                                    : 48.f + ((float) kMaxDelay - 48.f) * p.time * p.time;
        target = clampf (target, 8.f, (float) kMaxDelay);
        float fb = std::min (1.f, p.modify / 0.97f);   // máx = 1.0 => repetições estáveis (acumulam como looper)
        fb = lerpf (fb, 1.f, frz);
        const float inG = 1.f - frz;
        const int xfTicks = std::max (24, (int) (0.025f * p.fc));
        const float slewC = timeCoef (0.12f, p.fc);
        const float oL = dl[0].read (target, p.classic, xfTicks, slewC);
        const float oR = dl[1].read (target, p.classic, xfTicks, slewC);
        if (!p.spread)
        {
            dl[0].write (xl * inG + fb * softLimit (oL));
            dl[1].write (xr * inG + fb * softLimit (oR));
            yl = oL; yr = oR;
        }
        else   // ping-pong
        {
            const float m = 0.5f * (xl + xr) * inG;
            dl[0].write (m + fb * softLimit (oR));
            dl[1].write (fb * softLimit (oL));
            yl = oL; yr = oR;
            applyWidth (yl, yr, clampf (p.width * 1.25f, 0.f, 1.25f));
        }
    }

    void slipMode (float xl, float xr, float& yl, float& yr, const Params& p)
    {
        const float S = 64.f * std::pow (512.f, p.time);   // tamanho da amostragem (refresh)
        const float r = slipSpeedFromKnob (p.modify);
        const float nc = timeCoef (0.02f, p.fc);
        const bool fz = frz > 0.5f;
        if (!p.spread)
        {
            yl = sl[0].process (xl, S, r, fz, p.classic, nc);
            yr = sl[1].process (xr, S, r, fz, p.classic, nc);
        }
        else   // SPREAD: pan suave, velocidade ligada a TIME (e CLOCK)
        {
            const float m = sl[0].process (0.5f * (xl + xr), S, r, fz, p.classic, nc);
            const float hz = lerpf (8.f, 0.15f, p.time) * (p.fc / 32000.f);
            panPhase += hz / p.fc;
            if (panPhase >= 1.f) panPhase -= 1.f;
            const float th = clampf (kPi * 0.25f * (1.f + clampf (p.width * 1.25f, 0.f, 1.25f) * std::sin (kTwoPi * panPhase)), 0.f, kPi * 0.5f);
            yl = m * std::cos (th) * 1.414f;
            yr = m * std::sin (th) * 1.414f;
        }
    }

    Reverb revA, revB;
    DelayUnit dl[2];
    SlipUnit sl[2];
    DelayLine ring[2];
    OnePole tone[2][2], lc[2][2];
    float inGain = 0.f, outGain = 0.f, frz = 0.f, panPhase = 0.f;
    bool armed = false;
};
} // namespace mood
