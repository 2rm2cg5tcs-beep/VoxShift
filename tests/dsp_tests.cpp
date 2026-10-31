// Self-contained DSP test-suite (no framework). Runs on Linux and Windows CI.
//   dsp_tests            → run everything, exit code = number of failures
//   dsp_tests --core in.f32 out.f32 quality pitch formant robot whisper vib dry denoise
//                       → reference-comparison helper (raw float32 mono 48 kHz)
#include "dsp/Fft.h"
#include "dsp/Presets.h"
#include "dsp/SpectralVoice.h"
#include "dsp/VoiceChain.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace vox;

static int g_failures = 0, g_checks = 0;
#define CHECK(cond, ...)                                                         \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("  FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond);       \
            std::printf(__VA_ARGS__);                                            \
            std::printf("\n");                                                   \
        }                                                                        \
    } while (0)

static constexpr int SR = 48000;
static constexpr double PI = 3.14159265358979323846;

static std::vector<float> vowel(double f0, double dur, double glide = 0.0) {
    const size_t n = size_t(SR * dur);
    std::vector<double> x(n, 0.0);
    double ph = 0.0;
    const double F[3] = {700, 1220, 2600}, B[3] = {80, 90, 120};
    const int H = int(8000 / f0);
    std::vector<double> amp(H + 1);
    for (int h = 1; h <= H; ++h) {
        const double fr = h * f0;
        double a = 0.02;
        for (int j = 0; j < 3; ++j) a += 1.0 / (1.0 + std::pow((fr - F[j]) / B[j], 2));
        amp[h] = a / std::sqrt(double(h));
    }
    for (size_t i = 0; i < n; ++i) {
        const double t = double(i) / SR;
        ph += 2 * PI * f0 * (1 + glide * std::sin(2 * PI * 0.5 * t)) / SR;
        double s = 0;
        for (int h = 1; h < H; ++h) s += amp[h] * std::sin(h * ph);
        x[i] = s;
    }
    double mx = 1e-9;
    for (double v : x) mx = std::max(mx, std::abs(v));
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = float(x[i] / mx * 0.3);
    return out;
}

static double rms(const std::vector<float>& x, size_t a, size_t b) {
    double s = 0;
    b = std::min(b, x.size());
    for (size_t i = a; i < b; ++i) s += double(x[i]) * x[i];
    return std::sqrt(s / std::max<size_t>(1, b - a));
}

// autocorrelation f0 estimate (first strong peak), skips the first 0.5 s
static double f0Estimate(const std::vector<float>& y) {
    const size_t start = SR / 2, n = std::min<size_t>(y.size() - start, SR);
    const int lo = SR / 600, hi = SR / 38;
    std::vector<double> c(hi + 1, 0.0);
    for (int lag = lo; lag <= hi; ++lag) {
        double s = 0;
        for (size_t i = 0; i + lag < n; ++i) s += double(y[start + i]) * y[start + i + lag];
        c[lag] = s;
    }
    double m = 0;
    for (int lag = lo; lag <= hi; ++lag) m = std::max(m, c[lag]);
    for (int lag = lo + 1; lag < hi; ++lag)
        if (c[lag] > 0.85 * m && c[lag] >= c[lag - 1] && c[lag] >= c[lag + 1]) return double(SR) / lag;
    return 0.0;
}

static double centroid(const std::vector<float>& y) {
    const size_t start = SR / 2, n = 32768;
    RealFft fft(n);
    std::vector<double> frame(n);
    std::vector<std::complex<double>> spec(n / 2 + 1);
    for (size_t i = 0; i < n; ++i)
        frame[i] = (start + i < y.size() ? y[start + i] : 0.0) * (0.5 - 0.5 * std::cos(2 * PI * double(i) / n));
    fft.forward(frame.data(), spec.data());
    double num = 0, den = 0;
    for (size_t k = 0; k < spec.size(); ++k) {
        const double m = std::abs(spec[k]);
        num += m * k * double(SR) / n;
        den += m;
    }
    return num / den;
}

template <class P>
static std::vector<float> runBlocks(P& proc, std::vector<float> x, size_t block = 480) {
    for (size_t i = 0; i < x.size(); i += block) proc.process(x.data() + i, std::min(block, x.size() - i));
    return x;
}

static void testFft() {
    std::printf("fft\n");
    for (size_t n : {256u, 1536u, 2048u}) {
        RealFft fft(n);
        std::mt19937 rng(1);
        std::normal_distribution<double> nd;
        std::vector<double> x(n), back(n);
        for (auto& v : x) v = nd(rng);
        std::vector<std::complex<double>> s(n / 2 + 1);
        fft.forward(x.data(), s.data());
        double err = 0;
        for (size_t k = 0; k < n / 2 + 1; k += 37) {
            std::complex<double> ref = 0;
            for (size_t i = 0; i < n; ++i) ref += x[i] * std::polar(1.0, -2 * PI * double(k * i) / n);
            err = std::max(err, std::abs(ref - s[k]));
        }
        fft.inverse(s.data(), back.data());
        double rt = 0;
        for (size_t i = 0; i < n; ++i) rt = std::max(rt, std::abs(back[i] - x[i]));
        CHECK(err < 1e-8 && rt < 1e-10, "n=%zu dft err %g roundtrip %g", n, err, rt);
    }
}

static void testIdentity() {
    std::printf("identity reconstruction\n");
    auto x = vowel(120, 2.0);
    for (Quality q : {Quality::Balanced, Quality::High}) {
        SpectralVoice v(SR, q);
        std::vector<float> y = x;
        std::mt19937 rng(0);
        size_t i = 0;
        while (i < y.size()) {
            size_t b = 1 + rng() % 1500;
            b = std::min(b, y.size() - i);
            v.process(y.data() + i, b);
            i += b;
        }
        const size_t L = v.latency();
        double err = 0;
        for (size_t k = L + SR / 4; k < y.size(); ++k) err = std::max(err, double(std::abs(y[k] - x[k - L])));
        CHECK(err < 1e-5, "quality %d err %g", int(q), err);
    }
}

static void testPitch() {
    std::printf("pitch accuracy\n");
    for (double f0 : {90.0, 120.0, 220.0})
        for (Quality q : {Quality::Balanced, Quality::High})
            for (int st : {-12, -7, -3, 3, 7, 12}) {
                SpectralVoice v(SR, q);
                CoreParams p;
                p.pitchRatio = semitonesToRatio(st);
                v.setParams(p);
                auto y = runBlocks(v, vowel(f0, 2.0));
                const double want = f0 * std::pow(2.0, st / 12.0), got = f0Estimate(y);
                CHECK(std::abs(got - want) / want < 0.02, "f0 %.0f q%d %+d st: got %.1f want %.1f", f0, int(q), st, got, want);
            }
}

static void testFormant() {
    std::printf("formants\n");
    auto x = vowel(120, 2.0);
    const double base = centroid(x);
    for (int st : {6, -6}) {
        SpectralVoice v(SR, Quality::Balanced);
        CoreParams p;
        p.formantRatio = semitonesToRatio(st);
        v.setParams(p);
        auto y = runBlocks(v, x);
        const double c = centroid(y);
        CHECK(st > 0 ? c > base * 1.15 : c < base * 0.85, "formant %+d centroid %.0f base %.0f", st, c, base);
        CHECK(std::abs(f0Estimate(y) - 120) < 3, "formant %+d moved pitch to %.1f", st, f0Estimate(y));
    }
    SpectralVoice v(SR, Quality::Balanced);
    CoreParams p;
    p.pitchRatio = 0.5;
    v.setParams(p);
    auto y = runBlocks(v, x);
    CHECK(std::abs(centroid(y) / base - 1) < 0.15, "pitch-only changed formants: %.0f vs %.0f", centroid(y), base);
}

static void testCoreEffects() {
    std::printf("robot / whisper / vibrato / denoise levels\n");
    auto x = vowel(110, 2.0, 0.2);
    const double rin = rms(x, 0, x.size());
    // (denoise is tested separately: a perfectly stationary test tone *is* "noise" to it)
    struct C { const char* name; CoreParams p; } cases[4];
    cases[0] = {"robot", {}}; cases[0].p.robot = 1;
    cases[1] = {"whisper", {}}; cases[1].p.whisper = 1;
    cases[2] = {"vibrato", {}}; cases[2].p.vibratoDepth = 1.5;
    cases[3] = {"dry+octave", {}}; cases[3].p.dry = 1; cases[3].p.pitchRatio = 0.5;
    for (auto& c : cases)
        for (Quality q : {Quality::Balanced, Quality::High}) {
            SpectralVoice v(SR, q);
            v.setParams(c.p);
            auto y = runBlocks(v, x);
            bool finite = true;
            for (float s : y) finite = finite && std::isfinite(s);
            const double ratio = rms(y, SR / 2, y.size()) / rin;
            CHECK(finite && ratio > 0.4 && ratio < 2.0, "%s q%d level ratio %.2f", c.name, int(q), ratio);
        }
}

static void testDenoise() {
    std::printf("denoise\n");
    std::vector<float> clean(SR * 3, 0.0f);
    for (double s : {0.3, 1.2, 2.1}) {
        auto w = vowel(115, 0.4, 0.1);
        for (size_t i = 0; i < w.size(); ++i) clean[size_t(s * SR) + i] = w[i];
    }
    std::mt19937 rng(1);
    std::normal_distribution<double> nd(0.0, 0.01);
    std::vector<float> noise(clean.size()), x(clean.size());
    for (size_t i = 0; i < x.size(); ++i) { noise[i] = float(nd(rng)); x[i] = clean[i] + noise[i]; }
    SpectralVoice v(SR, Quality::Balanced);
    CoreParams p;
    p.denoise = 0.7;
    v.setParams(p);
    auto y = runBlocks(v, x);
    const size_t L = v.latency(), a = size_t(2.55 * SR), b = size_t(2.95 * SR);
    const double nr = rms(y, a + L, b + L) / rms(noise, a, b);
    CHECK(nr < 0.32, "noise reduced only to %.2f", nr);
    double se = 0, sp = 0;
    for (size_t i = size_t(1.25 * SR); i < size_t(1.55 * SR); ++i) {
        se += std::pow(double(y[i + L]) - clean[i], 2);
        sp += double(clean[i]) * clean[i];
    }
    CHECK(10 * std::log10(se / sp) < -14, "speech distorted: %.1f dB", 10 * std::log10(se / sp));
}

static double clickiness(const std::vector<float>& y, size_t from) {
    std::vector<double> d(y.size(), 0.0);
    for (size_t i = from + 2; i < y.size(); ++i) d[i] = std::abs(double(y[i]) - 2.0 * y[i - 1] + y[i - 2]);
    double worst = 0, acc = 0;
    const size_t W = 480;
    for (size_t i = from; i < y.size(); ++i) {
        acc += d[i];
        if (i >= from + W) acc -= d[i - W];
        if (i >= from + W) worst = std::max(worst, d[i - W / 2] / (acc / W + 1e-6));
    }
    return worst;
}

static void testPresets() {
    std::printf("all presets through the full chain\n");
    auto x = vowel(115, 2.0, 0.15);
    const double rin = rms(x, SR / 2, x.size());
    double neutral = 0;
    for (const auto& p : builtinPresets()) {
        VoiceChain c(SR, Quality::Balanced);
        c.setVoice(p.full());
        auto t0 = std::chrono::steady_clock::now();
        auto y = runBlocks(c, x);
        double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        bool ok = true;
        float peak = 0;
        for (float s : y) { ok = ok && std::isfinite(s); peak = std::max(peak, std::abs(s)); }
        const double lvl = rms(y, SR / 2, y.size()) / rin;
        if (neutral == 0) neutral = lvl;
        const double db = 20 * std::log10(lvl / neutral);
        std::printf("    %-22s %+5.1f dB  peak %.2f  cpu %.1f%%\n", p.name, db, peak, dt / 2.0 * 100);
        CHECK(ok && peak <= 0.9 && std::abs(db) < 4.5, "%s: level %+.1f dB peak %.2f", p.name, db, peak);
    }
}

static void testSwitchingAndGarbage() {
    std::printf("live switching, toggling, garbage input\n");
    auto x = vowel(120, 3.0, 0.3);
    VoiceChain c(SR, Quality::Balanced);
    const char* names[] = {"Девушка", "Демон", "Без изменений", "Рация", "Бурундук", "Пещера"};
    std::vector<float> y = x;
    int k = 0;
    for (size_t i = 0; i < y.size(); i += 480, ++k) {
        if (k % 40 == 0)
            for (auto& p : builtinPresets())
                if (std::strcmp(p.name, names[(k / 40) % 6]) == 0) c.setVoice(p.full(), true);
        if (k % 57 == 0) c.setEnabled(k % 114 != 0);
        c.process(y.data() + i, std::min<size_t>(480, y.size() - i));
    }
    const double cl = clickiness(y, SR / 2), ref = clickiness(x, SR / 2);
    CHECK(cl < ref * 1.6 + 5, "click ratio %.1f (input %.1f)", cl, ref);

    VoiceChain g(SR, Quality::Balanced);
    VoiceValues v = defaultVoice();
    v[P_Pitch] = -5; v[P_ReverbMix] = 0.4;
    g.setVoice(v);
    std::vector<float> bad(480);
    bool finite = true;
    for (float val : {NAN, INFINITY, -INFINITY, 1e-38f, 100.0f}) {
        std::fill(bad.begin(), bad.end(), val);
        g.process(bad.data(), bad.size());
        for (float s : bad) finite = finite && std::isfinite(s) && std::abs(s) <= 1.0f;
    }
    CHECK(finite, "non-finite or clipped output on garbage input");
}

static void testIdleAndDisabled() {
    std::printf("idle mode, disabled passthrough\n");
    VoiceChain c(SR, Quality::Balanced);
    VoiceValues v = defaultVoice();
    v[P_Pitch] = 5; v[P_Formant] = 3; v[P_ReverbMix] = 0.3;
    c.setVoice(v);
    std::vector<float> silence(SR * 3);
    std::mt19937 rng(0);
    std::normal_distribution<double> nd(0, 1e-5);
    for (auto& s : silence) s = float(nd(rng));
    runBlocks(c, silence);
    CHECK(c.sleeping(), "did not go idle in silence");
    auto speech = vowel(120, 1.0);
    auto y = runBlocks(c, speech);
    CHECK(!c.sleeping(), "did not wake up");
    const size_t L = c.latencySamples();
    CHECK(rms(y, L + 2400, L + 7200) > 0.3 * rms(speech, 2400, 7200), "speech onset lost after idle");

    VoiceChain d(SR, Quality::Balanced);
    v[P_Robot] = 1; v[P_Pitch] = 7;
    d.setVoice(v);
    d.setEnabled(false);
    auto z = runBlocks(d, vowel(120, 2.0));
    CHECK(std::abs(f0Estimate(z) - 120) < 3, "disabled chain changed pitch: %.1f", f0Estimate(z));
}

static void testPerformance() {
    std::printf("performance\n");
    auto x = vowel(120, 3.0);
    for (Quality q : {Quality::Balanced, Quality::High}) {
        VoiceChain c(SR, q);
        for (auto& p : builtinPresets())
            if (std::strcmp(p.name, "Демон") == 0) c.setVoice(p.full());
        auto t0 = std::chrono::steady_clock::now();
        runBlocks(c, x);
        const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 3.0 * 100;
        std::printf("    Демон quality %d: %.2f%% of one core\n", int(q), cpu);
        CHECK(cpu < 25.0, "too slow: %.1f%%", cpu);
    }
}

static int coreTool(int argc, char** argv) {
    if (argc < 12) return 2;
    std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
    const std::streamsize bytes = in.tellg();
    in.seekg(0);
    std::vector<float> data(size_t(bytes) / 4);
    in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size() * 4));
    SpectralVoice v(SR, std::atoi(argv[4]) ? Quality::High : Quality::Balanced);
    CoreParams p;
    p.pitchRatio = std::atof(argv[5]); p.formantRatio = std::atof(argv[6]); p.robot = std::atof(argv[7]);
    p.whisper = std::atof(argv[8]); p.vibratoDepth = std::atof(argv[9]); p.dry = std::atof(argv[10]);
    p.denoise = std::atof(argv[11]);
    v.setParams(p);
    for (size_t i = 0; i < data.size(); i += 480) v.process(data.data() + i, std::min<size_t>(480, data.size() - i));
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<char*>(data.data()), std::streamsize(data.size() * 4));
    return 0;
}

// --render in.f32 outdir [quality]  → renders every built-in preset to outdir/NN.f32
static int renderTool(int argc, char** argv) {
    if (argc < 4) return 2;
    std::ifstream in(argv[2], std::ios::binary | std::ios::ate);
    const std::streamsize bytes = in.tellg();
    in.seekg(0);
    std::vector<float> x(size_t(bytes) / 4);
    in.read(reinterpret_cast<char*>(x.data()), std::streamsize(x.size() * 4));
    const Quality q = (argc > 4 && std::atoi(argv[4])) ? Quality::High : Quality::Balanced;
    int idx = 0;
    for (const auto& p : builtinPresets()) {
        VoiceChain c(SR, q);
        c.setVoice(p.full());
        std::vector<float> y = x;
        for (size_t i = 0; i < y.size(); i += 480) c.process(y.data() + i, std::min<size_t>(480, y.size() - i));
        char path[1024];
        std::snprintf(path, sizeof path, "%s/%02d.f32", argv[3], idx++);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(y.data()), std::streamsize(y.size() * 4));
    }
    std::printf("%d presets rendered, latency %d samples\n", idx, VoiceChain(SR, q).latencySamples());
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--core") == 0) return coreTool(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--render") == 0) return renderTool(argc, argv);
    testFft();
    testIdentity();
    testPitch();
    testFormant();
    testCoreEffects();
    testDenoise();
    testPresets();
    testSwitchingAndGarbage();
    testIdleAndDisabled();
    testPerformance();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}
