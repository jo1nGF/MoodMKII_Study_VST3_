#pragma once
// MOOD MKII (estudo) - utilitários DSP comuns. C++17 puro, sem dependência do JUCE.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mood
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf (float a, float b, float t)    { return a + (b - a) * t; }

// Limitador suave: transparente até 0.9, satura suavemente em 1.0.
inline float softLimit (float x)
{
    const float ax = std::fabs (x);
    if (ax <= 0.9f) return x;
    const float y = 0.9f + 0.1f * std::tanh ((ax - 0.9f) * 10.f);
    return x < 0.f ? -y : y;
}

inline float onePoleCoef (float hz, float fs)  { return 1.f - std::exp (-kTwoPi * clampf (hz / fs, 1e-6f, 0.49f)); }
inline float timeCoef (float seconds, float fs) { return 1.f - std::exp (-1.f / std::max (1e-6f, seconds * fs)); }

// Interpolação de Hermite (Catmull-Rom): interpola entre y0 e y1, t em [0,1)
inline float hermite (float ym1, float y0, float y1, float y2, float t)
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    float bipolar() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
    float unipolar() { return 0.5f + 0.5f * bipolar(); }
};

struct OnePole
{
    float z = 0.f;
    inline float lp (float x, float a) { z += a * (x - z); return z; }
    void reset() { z = 0.f; }
};

struct DcBlock
{
    float x1 = 0.f, y1 = 0.f;
    inline float process (float x, float r = 0.9995f) { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset() { x1 = y1 = 0.f; }
};

// Linha de atraso circular (tamanho potência de 2). at(0) = última amostra escrita.
struct DelayLine
{
    std::vector<float> buf;
    uint32_t mask = 0, w = 0;

    void init (int minSize)
    {
        uint32_t n = 8;
        while ((int) n < minSize) n <<= 1;
        buf.assign (n, 0.f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.f); w = 0; }
    int size() const { return (int) buf.size(); }
    inline void write (float x) { buf[w] = x; w = (w + 1) & mask; }
    inline float at (int d) const { return buf[(w - 1u - (uint32_t) d) & mask]; }
    // leitura fracionária (Hermite). d = atraso em amostras (>= 1)
    inline float read (float d) const
    {
        d = clampf (d, 1.f, (float) (buf.size() - 4));
        const int i = (int) d;
        const float f = d - (float) i;
        return hermite (at (i - 1), at (i), at (i + 1), at (i + 2), f);
    }
};
// Passa-baixas Butterworth de 4ª ordem (2 biquads). Usado como anti-alias de entrada / reconstrução de saída.
// Coeficientes só são recalculados quando o corte muda.
struct LowPass4
{
    float b0[2] = {}, b1[2] = {}, b2[2] = {}, a1[2] = {}, a2[2] = {}, z1[2] = {}, z2[2] = {};
    float lastCut = -1.f;
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.f; }
    void setCutoff (float hz, float fs)
    {
        hz = clampf (hz, 20.f, 0.45f * fs);
        if (std::fabs (hz - lastCut) < 0.002f * hz) return;
        lastCut = hz;
        const float K = std::tan (kPi * hz / fs);
        const float q[2] = { 0.5411961f, 1.3065630f };   // Butterworth 4ª ordem
        for (int i = 0; i < 2; ++i)
        {
            const float norm = 1.f / (1.f + K / q[i] + K * K);
            b0[i] = K * K * norm; b1[i] = 2.f * b0[i]; b2[i] = b0[i];
            a1[i] = 2.f * (K * K - 1.f) * norm;
            a2[i] = (1.f - K / q[i] + K * K) * norm;
        }
    }
    inline float process (float x)
    {
        for (int i = 0; i < 2; ++i)
        {
            const float y = b0[i] * x + z1[i];
            z1[i] = b1[i] * x - a1[i] * y + z2[i];
            z2[i] = b2[i] * x - a2[i] * y;
            x = y;
        }
        return x;
    }
};
} // namespace mood
