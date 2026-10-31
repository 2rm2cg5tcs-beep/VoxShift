#include "VocalTract.h"

#include <algorithm>
#include <cmath>

namespace vox {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kSound = 35000.0;  // cm/s, warm humid air
}  // namespace

TractMeter::TractMeter(double sampleRate) : fs_(sampleRate) {
    buf_.assign(size_t(sampleRate * 0.05) + 8, 0.0);
    cosT_.resize(size_t(kGrid) * (kOrder + 1));
    sinT_.resize(cosT_.size());
    for (int k = 0; k < kGrid; ++k)
        for (int m = 0; m <= kOrder; ++m) {
            const double w = kPi * double(k) / kGrid * m;
            cosT_[size_t(k) * (kOrder + 1) + size_t(m)] = std::cos(w);
            sinT_[size_t(k) * (kOrder + 1) + size_t(m)] = std::sin(w);
        }
    env_.assign(kGrid, 0.0);
    hist_.assign(kBins, 0.0);
}

double TractMeter::measure(const float* x, int n) {
    n = std::min<int>(n, int(buf_.size()));
    if (n < 64) return 0.0;
    // pre-emphasis + Hamming window
    for (int i = 0; i < n; ++i) {
        const double pe = double(x[i]) - (i ? 0.95 * double(x[i - 1]) : 0.0);
        buf_[size_t(i)] = pe * (0.54 - 0.46 * std::cos(2.0 * kPi * i / (n - 1)));
    }
    double r[kOrder + 1];
    for (int lag = 0; lag <= kOrder; ++lag) {
        double s = 0.0;
        for (int i = lag; i < n; ++i) s += buf_[size_t(i)] * buf_[size_t(i - lag)];
        r[lag] = s;
    }
    if (r[0] < 1e-12) return 0.0;
    r[0] *= 1.0 + 1e-9;
    // Levinson–Durbin
    double a[kOrder + 1] = {1.0}, tmp[kOrder + 1];
    double err = r[0];
    for (int i = 1; i <= kOrder; ++i) {
        double acc = r[i];
        for (int j = 1; j < i; ++j) acc += a[j] * r[i - j];
        const double k = -acc / err;
        for (int j = 0; j <= i; ++j) tmp[j] = a[j];
        for (int j = 1; j < i; ++j) a[j] = tmp[j] + k * tmp[i - j];
        a[i] = k;
        err *= 1.0 - k * k;
        if (err <= 0.0) return 0.0;
    }
    // envelope in dB on a 0..Nyquist grid
    for (int k = 0; k < kGrid; ++k) {
        double re = 0.0, im = 0.0;
        const double* c = &cosT_[size_t(k) * (kOrder + 1)];
        const double* s = &sinT_[size_t(k) * (kOrder + 1)];
        for (int m = 0; m <= kOrder; ++m) {
            re += a[m] * c[m];
            im -= a[m] * s[m];
        }
        env_[size_t(k)] = -10.0 * std::log10(re * re + im * im + 1e-30);
    }
    // the first four peaks above 200 Hz with ≥ 2 dB prominence
    double F[4];
    int found = 0;
    const double hz = fs_ * 0.5 / kGrid;
    for (int k = 1; k < kGrid - 1 && found < 4; ++k) {
        const double v = env_[size_t(k)];
        if (!(v > env_[size_t(k - 1)] && v >= env_[size_t(k + 1)])) continue;
        if (k * hz < 200.0) continue;
        double lmin = v, rmin = v;
        for (int j = k - 1; j >= 0; --j) {
            if (env_[size_t(j)] > v) break;
            lmin = std::min(lmin, env_[size_t(j)]);
        }
        for (int j = k + 1; j < kGrid; ++j) {
            if (env_[size_t(j)] > v) break;
            rmin = std::min(rmin, env_[size_t(j)]);
        }
        if (v - std::max(lmin, rmin) < 2.0) continue;
        const double y0 = env_[size_t(k - 1)], y1 = v, y2 = env_[size_t(k + 1)];
        const double den = y0 - 2.0 * y1 + y2;
        const double off = std::abs(den) > 1e-12 ? std::clamp(0.5 * (y0 - y2) / den, -0.5, 0.5) : 0.0;
        F[found++] = (k + off) * hz;
    }
    if (found < 4) return 0.0;
    if (!(F[0] < 1100.0 && F[1] > 500.0 && F[1] < 3000.0 && F[2] > 1500.0 && F[2] < 4200.0 && F[3] > 2500.0 &&
          F[3] < 5200.0))
        return 0.0;
    double num = 0.0, den = 0.0;
    for (int i = 0; i < 4; ++i) {
        const double c = (2.0 * i + 1.0) / 2.0;
        num += F[i] * c;
        den += c * c;
    }
    const double dF = num / den;
    return kSound / (2.0 * dF);
}

void TractMeter::learn(double cm) {
    if (!(cm > kBase && cm < kBase + kBins * kStep)) return;
    const double keep = 1.0 - 1.0 / 3000.0;  // ~30 s of voiced speech
    for (double& h : hist_) h *= keep;
    total_ = total_ * keep + 1.0;
    hist_[size_t((cm - kBase) / kStep)] += 1.0;
    count_ += 1.0;
    if (total_ < 30.0) return;
    double run = 0.0;
    const double half = 0.5 * total_;
    int k = 0;
    for (; k < kBins - 1; ++k) {
        if (run + hist_[size_t(k)] >= half) break;
        run += hist_[size_t(k)];
    }
    const double frac = hist_[size_t(k)] > 0.0 ? (half - run) / hist_[size_t(k)] : 0.5;
    const double median = kBase + (k + frac) * kStep;
    // quick while learning, then at most ~0.2 cm per second
    const double maxStep = count_ < 300.0 ? 100.0 : 0.002;
    est_ = est_ <= 0.0 ? median : est_ + std::clamp(median - est_, -maxStep, maxStep);
    cm_.store(est_, std::memory_order_relaxed);
    conf_.store(count_, std::memory_order_relaxed);
}

void TractMeter::set(double cm, double confidence) {
    if (!(cm > kBase && cm < kBase + kBins * kStep)) return;
    std::fill(hist_.begin(), hist_.end(), 0.0);
    const double w = std::min(confidence, 1000.0);
    const int c = int((cm - kBase) / kStep);
    double sum = 0.0;
    for (int d = -6; d <= 6; ++d) {
        const int k = c + d;
        if (k < 0 || k >= kBins) continue;
        const double g = std::exp(-0.5 * (d / 3.0) * (d / 3.0));
        hist_[size_t(k)] = g;
        sum += g;
    }
    for (double& h : hist_) h *= w / sum;
    total_ = w;
    count_ = std::max(0.0, confidence);
    est_ = cm;
    cm_.store(cm, std::memory_order_relaxed);
    conf_.store(count_, std::memory_order_relaxed);
}

}  // namespace vox
