#pragma once
// Micro-Looper "sempre ouvindo" (estilo Drolo FX): Env / Tape / Stretch + overdub, fade, classic, spread.
// O loop vive em "amostras do clock": mexer no CLOCK escala tempo/pitch, e os overdubs ficam no lugar.
#include "MoodCommon.h"
#include "MoodParams.h"

namespace mood
{
class MicroLooper
{
public:
    static constexpr int kSize = 65536;

    struct Frame { float l = 0.f, r = 0.f; };

    struct Params
    {
        int mode = 1;                       // 0 Env, 1 Tape, 2 Stretch
        float length = 0.5f, modify = 0.69f;
        bool play = false, overdub = false, spread = false, classic = false, noDub = false, half = false;
        float fade = 1.f;                   // 1 = sem fade (infinito)
        float fc = 32000.f;
        int syncLen = 0;                    // >0: comprimento do loop vindo do SYNC (looper -> wet)
    };

    void init()
    {
        bl.assign (kSize, 0.f);
        br.assign (kSize, 0.f);
        reset();
    }

    void reset()
    {
        std::fill (bl.begin(), bl.end(), 0.f);
        std::fill (br.begin(), br.end(), 0.f);
        L = kSize; wr = 0; loopStart = 0; playing = false; playOff = 0.f;
        rd = XReader{}; rd2 = XReader{};
        envF = 0.f; repeatStart = 0.f; trig = false;
        chopCount = 0; chopStart = 0.f;
        head = 0.f; grainInit = false;
        panPhase = 0.f; panAmt = 0.f; degL = degR = 0.f;
    }

    bool isPlaying() const { return playing; }
    int length() const { return L; }
    float phase() const
    {
        if (!playing) return (float) wr / (float) L;
        const float o = playOff - (float) L * std::floor (playOff / (float) L);
        return clampf (o / (float) L, 0.f, 1.f);
    }

    Frame process (float xl, float xr, float recL, float recR, const Params& p)
    {
        const int xfTicks = std::max (8, (int) (0.003f * p.fc));

        if (!playing)   // comprimento do loop só muda fora da reprodução
        {
            int target = p.half ? kSize / 2 : kSize;
            if (p.syncLen > 0) target = std::max (256, std::min (kSize, p.syncLen));
            if (target != L) { L = target; if (wr >= L) wr %= L; }
        }

        if (p.play && !playing) engage();
        else if (!p.play && playing) { wr = wrapIdx (loopStart + (int) playOff); playing = false; }

        Frame out;
        if (!playing)   // "ouvindo sempre": grava continuamente por cima do que há (a ordem circular define o loop)
        {
            bl[wr] = recL;
            br[wr] = recR;
            if (++wr >= L) wr = 0;
            return out;
        }

        float pan = 0.f;
        if (p.mode == 1) out = tapeTick (p, xfTicks);
        else if (p.mode == 0) out = envTick (xl, xr, p, xfTicks, pan);
        else out = stretchTick (p, pan);

        if (p.spread && p.mode != 1)   // Env/Stretch + SPREAD: pan automático
        {
            if (pan > 0.f) out.l *= 1.f - pan; else out.r *= 1.f + pan;
        }

        float outScale = 1.f;
        const int wi = wrapIdx (loopStart + (int) std::lround (playOff));
        if (p.overdub)
        {
            // Só o sinal limpo é gravado (nunca o efeito Wet) para não criar realimentação.
            float nl, nr;
            if (p.noDub) { nl = xl; nr = xr; }                      // NO DUB: feedback 0 => "refresh"
            else
            {
                const float f = p.fade >= 0.97f ? 1.f : 0.25f + 0.75f * std::pow (p.fade / 0.97f, 0.7f);  // FADE
                nl = bl[wi] * f + xl;
                nr = br[wi] * f + xr;
            }
            bl[wi] = softLimit (nl);
            br[wi] = softLimit (nr);
            if (p.noDub && p.classic) outScale = 0.f;               // Classic: saída muda durante o overdub (MKI)
        }
        else if (p.classic)   // Classic: o loop se degrada aos poucos (perda de geração)
        {
            degL += 0.985f * (bl[wi] - degL);
            degR += 0.985f * (br[wi] - degR);
            bl[wi] = degL * 0.9998f + rng.bipolar() * 2e-5f;
            br[wi] = degR * 0.9998f + rng.bipolar() * 2e-5f;
        }
        out.l *= outScale;
        out.r *= outScale;
        return out;
    }

private:
    struct XReader
    {
        float pos = 0.f, old = 0.f;
        int xf = 0, xfLen = 1;
        void jump (float np, int ticks) { old = pos; pos = np; xf = ticks; xfLen = std::max (1, ticks); }
        void advance (float d) { pos += d; if (xf > 0) { old += d; --xf; } }
    };
    struct Grain { float g = 0.f, h0 = 0.f, dir = 1.f; };

    inline int wrapIdx (int i) const { i %= L; return i < 0 ? i + L : i; }

    inline Frame sampleAt (float off) const
    {
        float idx = (float) loopStart + off;
        const float fl = (float) L;
        idx -= fl * std::floor (idx / fl);
        int i1 = (int) idx;
        if (i1 >= L) i1 = L - 1;
        if (i1 < 0) i1 = 0;
        const float f = idx - (float) i1;
        const int i0 = i1 > 0 ? i1 - 1 : L - 1;
        const int i2 = i1 + 1 < L ? i1 + 1 : 0;
        const int i3 = i2 + 1 < L ? i2 + 1 : 0;
        Frame o;
        o.l = hermite (bl[i0], bl[i1], bl[i2], bl[i3], f);
        o.r = hermite (br[i0], br[i1], br[i2], br[i3], f);
        return o;
    }

    inline Frame readX (const XReader& r) const
    {
        Frame a = sampleAt (r.pos);
        if (r.xf > 0)
        {
            const Frame b = sampleAt (r.old);
            const float g = 1.f - (float) r.xf / (float) r.xfLen;
            a.l = lerpf (b.l, a.l, g);
            a.r = lerpf (b.r, a.r, g);
        }
        return a;
    }

    void engage()
    {
        loopStart = wr;   // amostra mais antiga = início; o "agora" = fim do loop
        playing = true;
        playOff = 0.f;
        rd = XReader{}; rd2 = XReader{};
        rd2.pos = (float) L - 1.f;
        head = 0.f; grainInit = false;
        chopCount = 0; chopStart = 0.f;
        trig = false; envF = 0.f; repeatStart = 0.f;
    }

    // ---------- TAPE: LENGTH = encurta o loop (Classic: taxa de "chop"); MODIFY = velocidade/direção em oitavas ----------
    Frame tapeTick (const Params& p, int xfTicks)
    {
        const float spd = tapeSpeedFromKnob (p.modify);
        int Le = L;
        float regionStart = 0.f;
        if (!p.classic)
        {
            Le = std::max (128, (int) ((float) L * (0.004f + 0.996f * std::pow (clampf (p.length, 0.f, 1.f), 2.5f))));
            Le = std::min (Le, L);
            regionStart = (float) (L - Le);   // o loop encurtado mantém o material mais recente
        }
        auto fix = [&] (XReader& r, float dir)
        {
            if (r.pos < regionStart || r.pos >= (float) L) r.jump (dir >= 0.f ? regionStart : (float) L - 1.f, xfTicks);
        };
        fix (rd, spd);
        fix (rd2, -spd);

        Frame out;
        if (p.spread)   // SPREAD: direita = cópia normal, esquerda = cópia reversa
        {
            const Frame f = readX (rd), b = readX (rd2);
            out.l = 0.5f * (b.l + b.r);
            out.r = 0.5f * (f.l + f.r);
        }
        else out = readX (rd);
        playOff = rd.pos;

        rd.advance (spd);
        rd2.advance (-spd);

        if (p.classic)   // Classic: LENGTH "pica" o loop (repete um trecho)
        {
            const int cp = std::max (64, (int) ((float) L * (0.01f + 0.99f * p.length * p.length)));
            if (cp < L)
            {
                if (chopCount == 0) chopStart = rd.pos;
                if (++chopCount >= cp) { chopCount = 0; rd.jump (chopStart, xfTicks); }
            }
        }
        auto wrap = [&] (XReader& r)
        {
            if (r.pos >= (float) L) r.jump (r.pos - (float) Le, xfTicks);
            else if (r.pos < regionStart) r.jump (r.pos + (float) Le, xfTicks);
        };
        wrap (rd);
        wrap (rd2);
        return out;
    }

    // ---------- ENV: corta o loop em fatias; enquanto houver som na entrada, repete a fatia atual ----------
    Frame envTick (float xl, float xr, const Params& p, int xfTicks, float& pan)
    {
        const float len = clampf (p.length, 0.f, 1.f);
        const float G = std::max (16.f, 32.f * std::pow (((float) L * 0.25f) / 32.f, len));   // tamanho da fatia
        const float ax = std::max (std::fabs (xl), std::fabs (xr));
        envF = ax > envF ? ax : envF + timeCoef (0.04f, p.fc) * (ax - envF);
        const float thr = 0.5f * std::pow (0.004f, clampf (p.modify, 0.f, 1.f));               // sensibilidade
        const bool prev = trig;
        if (envF > thr) trig = true; else if (envF < thr * 0.6f) trig = false;
        if (trig && !prev) repeatStart = std::floor (rd.pos / G) * G;

        const Frame out = readX (rd);
        playOff = rd.pos;
        rd.advance (1.f);
        if (trig && rd.pos >= repeatStart + G) rd.jump (repeatStart + (rd.pos - (repeatStart + G)), xfTicks);
        if (rd.pos >= (float) L) rd.jump (rd.pos - (float) L, xfTicks);

        if (p.spread)
        {
            panAmt += timeCoef (0.05f, p.fc) * ((trig ? 1.f : 0.f) - panAmt);
            panPhase += lerpf (12.f, 0.3f, len) * (p.fc / 32000.f) / p.fc;
            if (panPhase >= 1.f) panPhase -= 1.f;
            pan = panAmt * std::sin (kTwoPi * panPhase);
        }
        return out;
    }

    // ---------- STRETCH: granular (grãos com janela seno², hop = G/2). MODIFY = velocidade da cabeça (meio = congelado) ----------
    Frame stretchTick (const Params& p, float& pan)
    {
        const float len = clampf (p.length, 0.f, 1.f);
        const float G = std::max (64.f, 128.f * std::pow (((float) L * 0.5f) / 128.f, len));   // tamanho da fatia
        const float prog = stretchProgressFromKnob (p.modify);
        const float dir = prog < 0.f ? -1.f : 1.f;
        if (!grainInit)
        {
            gr[0].g = 0.f;          gr[0].h0 = head;                     gr[0].dir = dir;
            gr[1].g = 0.5f * G;     gr[1].h0 = head - dir * 0.5f * G;    gr[1].dir = dir;
            grainInit = true;
        }
        float accL = 0.f, accR = 0.f;
        for (Grain& g : gr)
        {
            if (g.g >= G) { g.g = std::fmod (g.g, G); g.h0 = head; g.dir = dir; }
            const float w = std::sin (kPi * g.g / G);
            const Frame f = sampleAt (g.h0 + g.dir * g.g);
            accL += w * w * f.l;
            accR += w * w * f.r;
            g.g += 1.f;
        }
        playOff = head;
        head += prog;
        const float fl = (float) L;
        head -= fl * std::floor (head / fl);
        if (p.spread)
        {
            panPhase += lerpf (0.1f, 3.f, std::fabs (prog)) * (p.fc / 32000.f) / p.fc;
            if (panPhase >= 1.f) panPhase -= 1.f;
            pan = std::sin (kTwoPi * panPhase);
        }
        Frame out;
        out.l = accL;
        out.r = accR;
        return out;
    }

    std::vector<float> bl, br;
    int L = kSize, wr = 0, loopStart = 0;
    bool playing = false;
    float playOff = 0.f;
    XReader rd, rd2;
    float envF = 0.f, repeatStart = 0.f;
    bool trig = false;
    int chopCount = 0;
    float chopStart = 0.f;
    float head = 0.f;
    Grain gr[2];
    bool grainInit = false;
    float panPhase = 0.f, panAmt = 0.f;
    float degL = 0.f, degR = 0.f;
    Rng rng;
};
} // namespace mood
