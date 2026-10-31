// Real FFT wrapper around pocketfft (header-only, BSD licence).
// Plans are created once; exec() never allocates, so it is safe on the audio thread.
#pragma once

#define POCKETFFT_NO_MULTITHREADING
#include "pocketfft/pocketfft_hdronly.h"

#include <complex>
#include <vector>

namespace vox {

class RealFft {
public:
    explicit RealFft(size_t n) : n_(n), plan_(n), buf_(n) {}

    size_t size() const { return n_; }
    size_t bins() const { return n_ / 2 + 1; }

    // out must hold n/2+1 values (n is even). Unnormalised, like numpy.fft.rfft.
    void forward(const double* in, std::complex<double>* out) {
        for (size_t i = 0; i < n_; ++i) buf_[i] = in[i];
        plan_.exec(buf_.data(), 1.0, true);
        // FFTPACK half-complex order: r0, r1, i1, r2, i2, ..., r(n/2)
        out[0] = {buf_[0], 0.0};
        const size_t h = n_ / 2;
        for (size_t k = 1; k < h; ++k) out[k] = {buf_[2 * k - 1], buf_[2 * k]};
        out[h] = {buf_[n_ - 1], 0.0};
    }

    // Inverse with 1/n normalisation, like numpy.fft.irfft.
    void inverse(const std::complex<double>* in, double* out) {
        const size_t h = n_ / 2;
        buf_[0] = in[0].real();
        for (size_t k = 1; k < h; ++k) {
            buf_[2 * k - 1] = in[k].real();
            buf_[2 * k] = in[k].imag();
        }
        buf_[n_ - 1] = in[h].real();
        plan_.exec(buf_.data(), 1.0 / double(n_), false);
        for (size_t i = 0; i < n_; ++i) out[i] = buf_[i];
    }

private:
    size_t n_;
    pocketfft::detail::pocketfft_r<double> plan_;
    std::vector<double> buf_;
};

}  // namespace vox
