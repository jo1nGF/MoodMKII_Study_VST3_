#pragma once
// Reverb "plate" de Dattorro (tanque em 8) com difusão variável.
// SMEAR=0 -> allpasses viram atrasos puros => cluster de ecos (multi-tap); SMEAR=1 -> reverb borrado.
#include "MoodCommon.h"

namespace mood
{
struct Allpass
{
    DelayLine line;
    void init (int n) { line.init (n); }
    void clear() { line.clear(); }
    inline float process (float x, float g, float delay)
    {
        const float d = line.read (delay - 1.f);
        const float v = x + g * d;
        line.write (v);
        return d - g * v;
    }
};

class Reverb
{
public:
    struct Params
    {
        float scale = 1.f;      // tamanho (escala dos atrasos)
        float decay = 0.7f;     // ganho do tanque (1 = sustain infinito)
        float smear = 1.f;      // difusão 0..1
        float damp = 0.55f;     // passa-baixas do tanque (1 = sem amortecimento)
        float modDepth = 1.f;   // modulação dos allpasses (0 = desligada)
        float inputGain = 0.5f;
        float outGain = 0.6f;
        float hpHz = 150.f;     // passa-altas dentro do tanque (evita acúmulo de graves / "lama")
    };

    void init()
    {
        for (auto& a : inAp) a.init (160 + 8);
        for (int s = 0; s < 2; ++s)
        {
            side[s].ap1.init (cap (kAp1[s]));
            side[s].d1.init (cap (kD1[s]));
            side[s].ap2.init (cap (kAp2[s]));
            side[s].d2.init (cap (kD2[s]));
        }
        inAp[2].init (400);
        inAp[3].init (300);
        clear();
    }

    void clear()
    {
        for (auto& a : inAp) a.clear();
        inBw.reset();
        for (auto& s : side) { s.ap1.clear(); s.ap2.clear(); s.d1.clear(); s.d2.clear(); s.damp.reset(); s.dc.reset(); s.fb = 0.f; }
    }

    void process (float in, float& outL, float& outR, const Params& p, float fc)
    {
        float x = inBw.lp (in, 0.9f);
        const float g1 = 0.75f * p.smear, g2 = 0.625f * p.smear;
        x = inAp[0].process (x, g1, (float) kIn[0]);
        x = inAp[1].process (x, g1, (float) kIn[1]);
        x = inAp[2].process (x, g2, (float) kIn[2]);
        x = inAp[3].process (x, g2, (float) kIn[3]);
        x *= p.inputGain;

        const float hpR = clampf (1.f - kTwoPi * p.hpHz / fc, 0.5f, 0.9995f);
        const float dg1 = 0.7f * p.smear;
        const float dg2 = clampf (p.decay + 0.15f, 0.25f, 0.5f) * p.smear;

        lfo += 0.9f / 32000.f;   // LFO proporcional ao clock (como um programa fixo): o desvio de pitch fica igual em todos os clocks
        if (lfo >= 1.f) lfo -= 1.f;
        const float mod[2] = { std::sin (kTwoPi * lfo) * 8.f * p.modDepth, std::cos (kTwoPi * lfo) * 8.f * p.modDepth };
        const float inS[2] = { x + side[1].fb, x + side[0].fb };

        for (int s = 0; s < 2; ++s)
        {
            Side& S = side[s];
            const float a1 = S.ap1.process (inS[s], dg1, (float) kAp1[s] * p.scale + mod[s]);
            S.d1.write (a1);
            float d1o = S.d1.read ((float) kD1[s] * p.scale);
            d1o = S.dc.process (d1o, hpR);
            const float dm = S.damp.lp (d1o, p.damp) * p.decay;
            const float a2 = S.ap2.process (dm, dg2, (float) kAp2[s] * p.scale);
            S.d2.write (a2);
            S.fb = S.d2.read ((float) kD2[s] * p.scale);
        }

        auto tp = [&] (const DelayLine& l, int off) { return l.read ((float) off * p.scale); };
        const Side& A = side[0];
        const Side& B = side[1];
        outL = tp (B.d1, 266) + tp (B.d1, 2974) - tp (B.ap2.line, 1913) + tp (B.d2, 1996)
             - tp (A.d1, 1990) - tp (A.ap2.line, 187) - tp (A.d2, 1066);
        outR = tp (A.d1, 353) + tp (A.d1, 3627) - tp (A.ap2.line, 1228) + tp (A.d2, 2673)
             - tp (B.d1, 2111) - tp (B.ap2.line, 335) - tp (B.d2, 121);
        outL *= p.outGain;
        outR *= p.outGain;
    }

private:
    static constexpr float kMaxScale = 1.7f;
    static int cap (int len) { return (int) ((float) len * kMaxScale) + 48; }

    static constexpr int kIn[4] = { 142, 107, 379, 277 };
    static constexpr int kAp1[2] = { 672, 908 };
    static constexpr int kD1[2] = { 4453, 4217 };
    static constexpr int kAp2[2] = { 1800, 2656 };
    static constexpr int kD2[2] = { 3720, 3163 };

    struct Side { Allpass ap1, ap2; DelayLine d1, d2; OnePole damp; DcBlock dc; float fb = 0.f; };

    Allpass inAp[4];
    OnePole inBw;
    Side side[2];
    float lfo = 0.f;
};
} // namespace mood
