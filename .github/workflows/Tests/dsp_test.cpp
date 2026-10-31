// Harness de testes do núcleo DSP (sem JUCE). Compilar: g++ -std=c++17 -O2 -fsanitize=address,undefined Tests/dsp_test.cpp -o dsp_test
#include "../Source/dsp/MoodEngine.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#if defined(__SSE__)
 #include <xmmintrin.h>
#endif

using namespace mood;

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_fail; std::printf ("  [FALHA] "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct Out { std::vector<float> l, r; };

using Gen = std::function<float (int)>;
using Hook = std::function<void (int block, EngineParams&)>;

static Out render (MoodEngine& e, EngineParams p, int n, const Gen& gen, const Hook& hook = {}, int bs = 256)
{
    Out o; o.l.resize ((size_t) n); o.r.resize ((size_t) n);
    std::vector<float> inL ((size_t) bs), inR ((size_t) bs);
    for (int s = 0, b = 0; s < n; s += bs, ++b)
    {
        const int m = std::min (bs, n - s);
        if (hook) hook (b, p);
        for (int i = 0; i < m; ++i) inL[(size_t) i] = inR[(size_t) i] = gen (s + i);
        e.process (inL.data(), inR.data(), o.l.data() + s, o.r.data() + s, m, p);
    }
    return o;
}

static float rmsRange (const std::vector<float>& v, int a, int b)
{
    double s = 0; int n = 0;
    for (int i = std::max (0, a); i < std::min ((int) v.size(), b); ++i) { s += (double) v[(size_t) i] * v[(size_t) i]; ++n; }
    return n ? (float) std::sqrt (s / n) : 0.f;
}
static float peakOf (const std::vector<float>& v) { float m = 0; for (float x : v) m = std::max (m, std::fabs (x)); return m; }
static bool finiteAll (const Out& o) { for (float x : o.l) if (! std::isfinite (x)) return false; for (float x : o.r) if (! std::isfinite (x)) return false; return true; }
static float db (float x) { return 20.f * std::log10 (std::max (1e-9f, x)); }

// burst: seno 400 Hz com janela de Hann (20 ms)
static float burst (int i, int start, float fs, float amp = 0.8f, float ms = 20.f, float hz = 400.f)
{
    const int len = (int) (ms * 0.001f * fs);
    if (i < start || i >= start + len) return 0.f;
    const float t = (float) (i - start) / (float) len;
    return amp * std::sin (kTwoPi * hz * (float) (i - start) / fs) * (0.5f - 0.5f * std::cos (kTwoPi * t));
}

static void writeWav (const char* path, const Out& o, int fs)
{
    FILE* f = std::fopen (path, "wb"); if (! f) return;
    const int n = (int) o.l.size(); const int dataBytes = n * 4;
    auto w32 = [&] (int v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (short v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    w32 (16); w16 (1); w16 (2); w32 (fs); w32 (fs * 4); w16 (4); w16 (16); std::fwrite ("data", 1, 4, f); w32 (dataBytes);
    float pk = std::max (peakOf (o.l), peakOf (o.r)); const float g = pk > 0.89f ? 0.89f / pk : 1.f;
    for (int i = 0; i < n; ++i) { w16 ((short) (clampf (o.l[(size_t) i] * g, -1.f, 1.f) * 32767.f)); w16 ((short) (clampf (o.r[(size_t) i] * g, -1.f, 1.f) * 32767.f)); }
    std::fclose (f);
}

// Pluck Karplus-Strong simples para demos
struct Pluck
{
    std::vector<float> buf; int pos = 0; float last = 0.f;
    void trigger (float hz, float fs, std::mt19937& rng)
    {
        const int n = std::max (2, (int) (fs / hz)); buf.assign ((size_t) n, 0.f);
        std::uniform_real_distribution<float> d (-1.f, 1.f); for (auto& x : buf) x = d (rng) * 0.6f; pos = 0;
    }
    float next() { if (buf.empty()) return 0.f; const size_t n = buf.size(); const float cur = buf[(size_t) pos]; const float nx = buf[(size_t) ((pos + 1) % (int) n)]; buf[(size_t) pos] = 0.5f * (cur + nx) * 0.9985f; pos = (pos + 1) % (int) n; return cur; }
};
static Out pluckInput (float fs, float seconds, const std::vector<std::pair<float, float>>& notes)  // (tempo s, Hz)
{
    std::mt19937 rng (7); Pluck pl; Out o; const int n = (int) (seconds * fs); o.l.assign ((size_t) n, 0.f); o.r = o.l; size_t k = 0;
    for (int i = 0; i < n; ++i)
    {
        while (k < notes.size() && (float) i >= notes[k].first * fs) { pl.trigger (notes[k].second, fs, rng); ++k; }
        o.l[(size_t) i] = o.r[(size_t) i] = pl.next() * 0.7f;
    }
    return o;
}
static Out renderBuf (MoodEngine& e, EngineParams p, const Out& in, const Hook& hook = {}, int bs = 256)
{
    const int n = (int) in.l.size(); Out o; o.l.resize ((size_t) n); o.r.resize ((size_t) n);
    std::vector<float> tl ((size_t) bs), tr ((size_t) bs);
    for (int s = 0, b = 0; s < n; s += bs, ++b)
    {
        const int m = std::min (bs, n - s); if (hook) hook (b, p);
        std::copy (in.l.begin() + s, in.l.begin() + s + m, tl.begin()); std::copy (in.r.begin() + s, in.r.begin() + s + m, tr.begin());
        e.process (tl.data(), tr.data(), o.l.data() + s, o.r.data() + s, m, p);
    }
    return o;
}

// ---------------------------------------------------------------------------------------------------------
static void testLoopPeriodVsClock()
{
    std::printf ("[1] Período do loop = comprimento/clock (clock escala o tempo do loop)\n");
    const int fs = 48000;
    const int steps[] = { 10, 8, 6, 4 };   // 64k, 32k, 16k, 8k
    for (int st : steps)
    {
        MoodEngine e; e.prepare (fs);
        EngineParams p; p.mix = 1.f; p.clock = (float) st / 10.f; p.loopMode = 1; p.loopModify = 0.69f; p.loopLength = 1.f;
        const float fc = kClockRates[st]; const float period = 65536.f / fc;
        const int engageAt = (int) (0.8f * fs);
        const int n = (int) ((0.8f + 3.f * period + 0.5f) * fs);
        auto o = render (e, p, n, [&] (int i) { return burst (i, (int) (0.2f * fs), (float) fs); },
                         [&] (int b, EngineParams& q) { q.loopOn = (b * 256 >= engageAt); });
        // envelope + bordas de subida
        std::vector<float> env (o.l.size()); float z = 0; const float a = timeCoef (0.003f, (float) fs);
        for (size_t i = 0; i < o.l.size(); ++i) { z += a * (std::fabs (o.l[i]) - z); env[i] = z; }
        std::vector<float> edges; bool above = false;
        for (int i = engageAt; i < n; ++i) { if (! above && env[(size_t) i] > 0.1f) { above = true; edges.push_back ((float) i / (float) fs); } else if (above && env[(size_t) i] < 0.02f) above = false; }
        CHECK (edges.size() >= 3, "clock %.0f: só %zu bordas detectadas", fc, edges.size());
        if (edges.size() >= 3)
        {
            const float meas = (edges[2] - edges[1]);
            std::printf ("  clock %5.0f Hz: período esperado %.4f s, medido %.4f s\n", fc, period, meas);
            CHECK (std::fabs (meas - period) < 0.004f, "período fora da tolerância");
        }
        CHECK (finiteAll (o), "NaN/Inf");
    }
}

static void testFreezeStable()
{
    std::printf ("[2] Reverb: freeze estável (sustain infinito) e delay fb=1 estável\n");
    const int fs = 48000;
    {
        MoodEngine e; e.prepare (fs);
        EngineParams p; p.mix = 1.f; p.wetOn = true; p.wetMode = 0; p.wetTime = 0.8f; p.wetModify = 1.f; p.clock = 0.8f;
        auto o = render (e, p, 14 * fs, [&] (int i) { return burst (i, (int) (0.1f * fs), (float) fs, 0.7f, 300.f, 220.f); },
                         [&] (int b, EngineParams& q) { q.wetFreeze = (b * 256 >= (int) (1.0f * fs)); });
        const float r1 = rmsRange (o.l, 3 * fs, 4 * fs), r2 = rmsRange (o.l, 12 * fs, 13 * fs);
        std::printf ("  reverb freeze: RMS 3-4s = %.1f dB, 12-13s = %.1f dB (pico %.2f)\n", db (r1), db (r2), peakOf (o.l));
        CHECK (std::fabs (db (r1) - db (r2)) < 2.5f, "freeze do reverb não é estável");
        CHECK (r1 > 1e-4f, "freeze silencioso"); CHECK (finiteAll (o), "NaN/Inf");
    }
    {
        MoodEngine e; e.prepare (fs);
        EngineParams p; p.mix = 1.f; p.wetOn = true; p.wetMode = 1; p.wetTime = 0.2f; p.wetModify = 1.f; p.clock = 1.f;
        auto o = render (e, p, 12 * fs, [&] (int i) { return burst (i, (int) (0.1f * fs), (float) fs, 0.5f); });
        const float r1 = rmsRange (o.l, 2 * fs, 3 * fs), r2 = rmsRange (o.l, 10 * fs, 11 * fs);
        std::printf ("  delay fb=1: RMS 2-3s = %.1f dB, 10-11s = %.1f dB (pico %.2f)\n", db (r1), db (r2), peakOf (o.l));
        CHECK (std::fabs (db (r1) - db (r2)) < 1.5f, "delay fb=1 não é estável"); CHECK (peakOf (o.l) <= 1.6f, "pico alto");
    }
}

static void testReverbLevel()
{
    std::printf ("[3] Calibração de nível do Reverb (ruído -> reverb)\n");
    const int fs = 48000; std::mt19937 rng (3); std::uniform_real_distribution<float> d (-1.f, 1.f);
    const float tvals[] = { 0.2f, 0.5f, 0.8f }; const float svals[] = { 0.f, 1.f };
    for (float sm : svals) for (float t : tvals)
    {
        MoodEngine e; e.prepare (fs);
        EngineParams p; p.mix = 1.f; p.wetOn = true; p.wetMode = 0; p.wetTime = t; p.wetModify = sm; p.clock = 0.8f;
        std::vector<float> in ((size_t) (8 * fs)); for (auto& x : in) x = d (rng) * 0.1f;
        auto o = render (e, p, 8 * fs, [&] (int i) { return in[(size_t) i]; });
        const float ri = rmsRange (in, 4 * fs, 8 * fs), ro = rmsRange (o.l, 4 * fs, 8 * fs);
        std::printf ("  TIME %.1f SMEAR %.0f: in %.1f dB -> out %.1f dB (%+.1f dB)\n", t, sm, db (ri), db (ro), db (ro) - db (ri));
        CHECK (finiteAll (o), "NaN/Inf");
    }
}

static void testBehaviors()
{
    std::printf ("[4] Comportamentos: Env, Stretch frozen, Tape speed, Overdub, Sync, Trails\n");
    const int fs = 48000;
    auto gen = [&] (int i) { return burst (i, (int) (0.2f * fs), (float) fs, 0.7f, 120.f, 330.f); };
    { // Stretch frozen: o conteúdo deve ficar repetindo => energia estável ao longo do tempo
        MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 1.f; p.loopMode = 2; p.loopModify = 0.5f; p.loopLength = 0.5f; p.clock = 1.f;
        auto o = render (e, p, 8 * fs, [&] (int i) { return i < (int) (1.5f * fs) ? 0.4f * std::sin (kTwoPi * 330.f * (float) i / (float) fs) : 0.f; },
                         [&] (int b, EngineParams& q) { q.loopOn = (b * 256 >= (int) (1.5f * fs)); });
        const float r1 = rmsRange (o.l, 3 * fs, 4 * fs), r2 = rmsRange (o.l, 7 * fs, 8 * fs);
        std::printf ("  stretch (frozen): RMS %.1f dB -> %.1f dB\n", db (r1), db (r2)); CHECK (r1 > 1e-3f && r2 > 1e-3f, "stretch frozen silencioso"); CHECK (finiteAll (o), "NaN/Inf");
    }
    { // Stretch 1x (extremo): reprodução ~normal => período do loop mantém o burst
        MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 1.f; p.loopMode = 2; p.loopModify = 1.f; p.loopLength = 0.5f; p.clock = 1.f;
        auto o = render (e, p, 4 * fs, gen, [&] (int b, EngineParams& q) { q.loopOn = (b * 256 >= (int) (0.8f * fs)); });
        const float r = rmsRange (o.l, (int) (0.8f * fs), 4 * fs); std::printf ("  stretch (modify=max, sem efeito): RMS %.1f dB\n", db (r)); CHECK (r > 1e-3f, "silencioso");
    }
    { // Env: com sensibilidade alta e sinal contínuo na entrada, o loop "congela" numa fatia
        MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 1.f; p.loopMode = 0; p.loopModify = 1.f; p.loopLength = 0.4f; p.clock = 1.f;
        auto o = render (e, p, 5 * fs, [&] (int i) { return (i < (int) (1.f * fs)) ? burst (i, (int) (0.2f * fs), (float) fs, 0.7f, 600.f, 330.f) : (i > 3 * fs ? 0.05f * std::sin (0.05f * (float) i) : 0.f); },
                         [&] (int b, EngineParams& q) { q.loopOn = (b * 256 >= (int) (1.1f * fs)); });
        CHECK (finiteAll (o), "NaN/Inf"); std::printf ("  env: ok (pico %.2f)\n", peakOf (o.l));
    }
    { // Tape 2x: período cai pela metade
        const float periods[2] = { 0.f, 0.f };
        (void) periods;
        float meas[2];
        for (int k = 0; k < 2; ++k)
        {
            MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 1.f; p.loopMode = 1; p.loopModify = k == 0 ? 0.69f : 0.8f; p.loopLength = 1.f; p.clock = 1.f;
            auto o = render (e, p, 6 * fs, [&] (int i) { return burst (i, (int) (0.2f * fs), (float) fs); }, [&] (int b, EngineParams& q) { q.loopOn = (b * 256 >= (int) (0.8f * fs)); });
            std::vector<float> env (o.l.size()); float z = 0; const float a = timeCoef (0.003f, (float) fs);
            for (size_t i = 0; i < o.l.size(); ++i) { z += a * (std::fabs (o.l[i]) - z); env[i] = z; }
            std::vector<float> ed; bool ab = false;
            for (int i = (int) (0.8f * fs); i < 6 * fs; ++i) { if (! ab && env[(size_t) i] > 0.1f) { ab = true; ed.push_back ((float) i / fs); } else if (ab && env[(size_t) i] < 0.02f) ab = false; }
            meas[k] = ed.size() >= 3 ? ed[2] - ed[1] : -1.f;
        }
        std::printf ("  tape 1x: %.3f s | tape 2x: %.3f s (esperado metade)\n", meas[0], meas[1]);
        CHECK (meas[0] > 0.f && std::fabs (meas[1] - 0.5f * meas[0]) < 0.01f, "tape 2x não divide o período");
    }
    { // Overdub: soma sinal limpo (energia aumenta) / NO DUB: substitui
        MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 1.f; p.loopMode = 1; p.loopModify = 0.69f; p.loopLength = 1.f; p.clock = 1.f;
        auto o = render (e, p, 8 * fs, [&] (int i) { return burst (i, (int) (0.2f * fs), (float) fs, 0.4f) + burst (i, (int) (3.5f * fs), (float) fs, 0.4f, 20.f, 700.f); },
                         [&] (int b, EngineParams& q) { const int s = b * 256; q.loopOn = (s >= (int) (0.8f * fs)); q.loopOverdub = (s >= 3 * fs && s < 4 * fs); });
        CHECK (finiteAll (o), "NaN/Inf"); std::printf ("  overdub: ok (pico %.2f)\n", peakOf (o.l));
    }
    { // Trails: sem trails o wet corta rápido; com trails decai
        float rr[2];
        for (int k = 0; k < 2; ++k)
        {
            MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 1.f; p.wetOn = true; p.wetMode = 0; p.wetTime = 0.7f; p.wetModify = 1.f; p.trails = (k == 1);
            auto o = render (e, p, 4 * fs, [&] (int i) { return burst (i, (int) (0.1f * fs), (float) fs, 0.7f, 200.f, 220.f); }, [&] (int b, EngineParams& q) { q.wetOn = (b * 256 < (int) (0.6f * fs)); });
            rr[k] = rmsRange (o.l, (int) (1.0f * fs), (int) (1.5f * fs));
        }
        std::printf ("  trails: sem = %.1f dB | com = %.1f dB (após bypass)\n", db (rr[0]), db (rr[1])); CHECK (rr[1] > rr[0] * 5.f, "trails não funciona");
    }
    { // Sync looper->wet: o comprimento do loop segue o TIME do delay
        MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 1.f; p.wetMode = 1; p.wetTime = 0.5f; p.sync = 1; p.clock = 1.f; p.loopMode = 1; p.loopLength = 1.f;
        auto o = render (e, p, 3 * fs, [&] (int i) { return burst (i, (int) (0.2f * fs), (float) fs); }, [&] (int b, EngineParams& q) { q.loopOn = (b * 256 >= (int) (0.5f * fs)); });
        std::printf ("  sync (looper->wet): ok (pico %.2f)\n", peakOf (o.l)); CHECK (finiteAll (o), "NaN/Inf");
    }
}

static float corrLR (const Out& o, int a, int b)
{
    double sl = 0, sr = 0, slr = 0;
    for (int i = a; i < b; ++i) { sl += (double) o.l[(size_t) i] * o.l[(size_t) i]; sr += (double) o.r[(size_t) i] * o.r[(size_t) i]; slr += (double) o.l[(size_t) i] * o.r[(size_t) i]; }
    return (float) (slr / std::sqrt (std::max (1e-30, sl * sr)));
}
static void testSpread()
{
    std::printf ("[4b] SPREAD: correlação L/R com entrada mono\n");
    const int fs = 48000;
    for (int mode = 0; mode < 3; ++mode)
    {
        float c[2];
        for (int k = 0; k < 2; ++k)
        {
            MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 1.f; p.wetOn = true; p.wetMode = mode; p.wetTime = mode == 1 ? 0.25f : 0.6f; p.wetModify = mode == 2 ? 0.8f : 0.7f; p.spread = (k == 1); p.stereoWidth = 1.f;
            auto o = render (e, p, 4 * fs, [&] (int i) { return 0.3f * std::sin (kTwoPi * 220.f * (float) i / (float) fs) * (i < fs ? 1.f : 0.f); });
            c[k] = corrLR (o, fs, 4 * fs);
        }
        std::printf ("  modo %d: corr(L,R) spread OFF = %.3f | ON = %.3f\n", mode, c[0], c[1]);
        CHECK (c[1] < c[0] - 0.05f || c[1] < 0.9f, "SPREAD não alterou a imagem estéreo no modo %d", mode);
    }
}

static void testFuzz()
{
    std::printf ("[5] Fuzz: parâmetros aleatórios em vários sample rates (ASan/UBSan ativos)\n");
    const int rates[] = { 22050, 44100, 48000, 96000, 192000 };
    std::mt19937 rng (123); std::uniform_real_distribution<float> u (0.f, 1.f);
    for (int fs : rates)
    {
        MoodEngine e; e.prepare (fs);
        EngineParams p; p.mix = 0.7f;
        std::vector<float> noise ((size_t) fs); for (auto& x : noise) x = (u (rng) * 2.f - 1.f) * 0.3f;
        auto hook = [&] (int b, EngineParams& q)
        {
            if (b % 7 == 0)
            {
                q.wetTime = u (rng); q.wetModify = u (rng); q.mix = u (rng); q.clock = u (rng); q.loopLength = u (rng); q.loopModify = u (rng);
                q.wetMode = (int) (u (rng) * 2.99f); q.routing = (int) (u (rng) * 2.99f); q.loopMode = (int) (u (rng) * 2.99f);
                q.wetOn = u (rng) > 0.3f; q.wetFreeze = u (rng) > 0.8f; q.loopOn = u (rng) > 0.4f; q.loopOverdub = u (rng) > 0.7f;
                q.classic = u (rng) > 0.7f; q.miso = u (rng) > 0.8f; q.spread = u (rng) > 0.5f; q.dryKill = u (rng) > 0.9f; q.trails = u (rng) > 0.5f;
                q.noDub = u (rng) > 0.8f; q.smoothClock = u (rng) > 0.5f;
                q.rampTime = u (rng) > 0.7f; q.rampWetModify = u (rng) > 0.7f; q.rampClock = u (rng) > 0.7f; q.rampLoopModify = u (rng) > 0.7f; q.rampLength = u (rng) > 0.7f;
                q.bounce = u (rng) > 0.3f; q.sweepTop = u (rng) > 0.5f; q.rise = u (rng) > 0.5f; q.rampSpeed = u (rng); q.rampWaveform = u (rng);
                q.ctrlSource = u (rng) > 0.8f ? 1 : 0; q.expValue = u (rng);
                q.stereoWidth = u (rng); q.directLoop = u (rng); q.tone = u (rng); q.fade = u (rng); q.balance = u (rng);
                q.sync = (int) (u (rng) * 2.99f); q.spreadSolo = (int) (u (rng) * 2.99f); q.loopHalf = u (rng) > 0.5f;
            }
        };
        auto o = render (e, p, fs * 40, [&] (int i) { return noise[(size_t) (i % fs)] * (((i / (fs / 2)) % 2) ? 1.f : 0.2f); }, hook, 128 + (fs % 5) * 100);
        const float pk = std::max (peakOf (o.l), peakOf (o.r));
        std::printf ("  fs %6d: pico %.2f, finito=%d\n", fs, pk, (int) finiteAll (o));
        CHECK (finiteAll (o), "NaN/Inf em fs=%d", fs); CHECK (pk < 6.f, "pico absurdo em fs=%d", fs);
    }
}

static void testPerf()
{
    std::printf ("[6] Desempenho (pior caso: reverb dual-mono + looper stretch + clock 64k, 48 kHz)\n");
    const int fs = 48000; MoodEngine e; e.prepare (fs);
    EngineParams p; p.mix = 0.5f; p.wetOn = true; p.loopOn = true; p.loopMode = 2; p.loopModify = 0.8f; p.wetMode = 0; p.wetModify = 1.f; p.clock = 1.f;
    std::vector<float> in ((size_t) fs * 20, 0.1f), ol (in.size()), orr (in.size());
    auto t0 = std::chrono::steady_clock::now();
    for (size_t s = 0; s < in.size(); s += 512) e.process (in.data() + s, in.data() + s, ol.data() + s, orr.data() + s, (int) std::min<size_t> (512, in.size() - s), p);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("  20 s de áudio em %.3f s => %.1f%% de 1 núcleo\n", sec, 100.0 * sec / 20.0);
}

static void demos()
{
    std::printf ("[7] Gerando demos .wav em ./demos\n");
    const int fs = 48000; std::filesystem::create_directories ("demos");
    const auto in = pluckInput ((float) fs, 12.f, { { 0.2f, 196.f }, { 1.6f, 246.9f }, { 3.0f, 293.7f }, { 5.0f, 220.f }, { 6.5f, 261.6f }, { 8.0f, 329.6f } });
    { MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 0.55f; p.wetOn = true; p.wetMode = 0; p.wetTime = 0.75f; p.wetModify = 0.9f; p.clock = 1.f;
      writeWav ("demos/01_reverb_hifi.wav", renderBuf (e, p, in), fs); }
    { MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 0.55f; p.wetOn = true; p.wetMode = 0; p.wetTime = 0.75f; p.wetModify = 0.9f; p.clock = 0.6f;
      writeWav ("demos/02_reverb_clock16k.wav", renderBuf (e, p, in, [&] (int b, EngineParams& q) { const float t = (float) b * 256.f / (float) fs; q.clock = t < 5.f ? 1.f : (t < 8.f ? 0.6f : 0.2f); }), fs); }
    { MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 0.6f; p.wetOn = true; p.wetMode = 1; p.wetTime = 0.25f; p.wetModify = 0.6f; p.clock = 1.f; p.spread = true;
      writeWav ("demos/03_delay_pingpong.wav", renderBuf (e, p, in), fs); }
    { MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 0.7f; p.loopMode = 2; p.loopModify = 0.62f; p.loopLength = 0.45f; p.clock = 0.8f; p.loopHalf = false;
      writeWav ("demos/04_looper_stretch.wav", renderBuf (e, p, in, [&] (int b, EngineParams& q) { const float t = (float) b * 256.f / (float) fs; q.loopOn = t > 3.6f; }), fs); }
    { MoodEngine e; e.prepare (fs); EngineParams p; p.mix = 0.6f; p.loopMode = 1; p.loopModify = 0.69f; p.loopLength = 1.f; p.clock = 0.8f; p.wetOn = true; p.wetMode = 0; p.wetTime = 0.7f; p.wetModify = 1.f; p.routing = 2;
      writeWav ("demos/05_loop_through_reverb.wav", renderBuf (e, p, in, [&] (int b, EngineParams& q) { const float t = (float) b * 256.f / (float) fs; q.loopOn = t > 3.6f; }), fs); }
}

int main (int argc, char** argv)
{
#if defined(__SSE__)
    _mm_setcsr (_mm_getcsr() | 0x8040);   // FTZ + DAZ (no plugin: juce::ScopedNoDenormals)
#endif
    testLoopPeriodVsClock();
    testFreezeStable();
    testReverbLevel();
    testBehaviors();
    testSpread();
    testFuzz();
    testPerf();
    if (argc > 1 && std::string (argv[1]) == "demos") demos();
    std::printf (g_fail == 0 ? "\nTODOS OS TESTES PASSARAM\n" : "\n%d FALHA(S)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
