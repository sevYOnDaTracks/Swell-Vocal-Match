#pragma once
#include "Profiles.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <string>
#include <vector>

// Voice analysis: compares the average spectrum of the sung passages with a target curve,
// proposes EQ offsets, and finds up to 3 narrow resonances. Runs off the audio thread.
namespace vm
{
struct AnalysisResult
{
    bool valid = false;
    std::array<float, numBands> eq {};
    int markerCount = 0;
    std::array<float, maxMarkers> markerHz {};
    std::array<float, maxMarkers> markerDepth {};
    std::array<float, maxMarkers> markerProminence {};
};

// Provisional target, in dB at third-octave points from 63 Hz to 16 kHz: the average of the
// three test voices, each aligned on its voice type. To be replaced by a larger set of voices.
constexpr std::array<float, 25> targetPoints { 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000,
                                               1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };
constexpr std::array<float, 25> targetCurve { -23.2f, -22.6f, -12.6f, -4.0f, 12.0f, 14.9f, 17.7f, 14.9f, 15.2f,
                                              16.4f, 13.8f, 10.7f, 9.2f, 7.1f, 2.7f, 0.8f, 1.3f, -0.8f, -3.3f,
                                              -8.1f, -7.6f, -8.2f, -12.7f, -15.0f, -18.6f };

constexpr float analysisStrength = 0.5f;
constexpr float analysisMaxDb = 4.0f;

inline void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2 * 3.141592653589793 / double(len);
        const std::complex<double> step(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1);
            for (size_t k = 0; k < len / 2; ++k, w *= step)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

/** Average power spectrum of the frames that contain voice (quiet frames are skipped). */
inline std::vector<double> averagePower(const std::vector<float>& x, double rate, size_t& fftSize)
{
    fftSize = 1;
    while (double(fftSize) < rate * 0.17) fftSize <<= 1;
    const size_t hop = fftSize / 2;
    std::vector<double> frameRms;
    for (size_t start = 0; start + fftSize <= x.size(); start += hop)
    {
        double e = 0;
        for (size_t i = 0; i < fftSize; ++i) e += double(x[start + i]) * x[start + i];
        frameRms.push_back(std::sqrt(e / double(fftSize)));
    }
    std::vector<double> power(fftSize / 2 + 1, 0.0);
    if (frameRms.empty()) return power;
    auto sorted = frameRms;
    std::sort(sorted.begin(), sorted.end());
    const double gate = 0.1 * sorted[size_t(double(sorted.size() - 1) * 0.95)];
    std::vector<std::complex<double>> buffer(fftSize);
    int used = 0;
    for (size_t f = 0; f < frameRms.size(); ++f)
    {
        if (frameRms[f] <= gate || frameRms[f] < 1e-5) continue;
        for (size_t i = 0; i < fftSize; ++i)
        {
            const double window = 0.5 - 0.5 * std::cos(2 * 3.141592653589793 * double(i) / double(fftSize - 1));
            buffer[i] = { double(x[f * hop + i]) * window, 0.0 };
        }
        fft(buffer);
        for (size_t k = 0; k < power.size(); ++k) power[k] += std::norm(buffer[k]);
        ++used;
    }
    if (used == 0) return std::vector<double>(fftSize / 2 + 1, 0.0);
    for (auto& p : power) p /= used;
    return power;
}

inline double meanPower(const std::vector<double>& power, double binHz, double low, double high)
{
    const size_t a = size_t(std::max(1.0, std::ceil(low / binHz)));
    const size_t b = std::min(power.size() - 1, size_t(std::floor(high / binHz)));
    double sum = 0;
    size_t count = 0;
    for (size_t k = a; k <= b; ++k) { sum += power[k]; ++count; }
    if (count == 0)
    {
        const size_t k = std::min(power.size() - 1, size_t(std::round(0.5 * (low + high) / binHz)));
        return power[k];
    }
    return sum / double(count);
}

/** Analyses mono voice audio recorded at the given rate, for one voice type. */
inline AnalysisResult analyse(const std::vector<float>& x, double rate, int voiceType)
{
    AnalysisResult result;
    size_t n = 0;
    const auto power = averagePower(x, rate, n);
    const double binHz = rate / double(n);
    double total = 0;
    for (auto p : power) total += p;
    if (total <= 0) return result;

    // Voice level per third octave (energy in the band), read on an axis aligned with the voice type.
    const double shift = voiceTypes()[size_t(std::clamp(voiceType, 0, 2))].shift;
    std::array<double, 25> diff {};
    double mean = 0;
    int counted = 0;
    for (size_t i = 0; i < targetPoints.size(); ++i)
    {
        const double f = targetPoints[i] * shift;
        if (f * std::pow(2.0, 1.0 / 6) >= rate / 2) { diff[i] = 0; continue; }
        const double level = 10 * std::log10(meanPower(power, binHz, f * std::pow(2.0, -1.0 / 6), f * std::pow(2.0, 1.0 / 6)) * f + 1e-30);
        diff[i] = targetCurve[i] - level;
    }
    for (size_t i = 0; i < targetPoints.size(); ++i)
        if (targetPoints[i] >= 150 && targetPoints[i] <= 12000) { mean += diff[i]; ++counted; }
    mean /= std::max(1, counted);

    for (int b = 0; b < numBands; ++b)
    {
        double weighted = 0, weights = 0;
        for (size_t i = 0; i < targetPoints.size(); ++i)
        {
            double d = std::log2(targetPoints[i] / bandFrequency[size_t(b)]);
            if (b == 3) d = std::min(d, 0.0);
            const double w = std::exp(-d * d / (2 * analysisWidth[size_t(b)] * analysisWidth[size_t(b)]));
            weighted += w * (diff[i] - mean);
            weights += w;
        }
        const double gain = std::clamp(analysisStrength * weighted / weights, double(-analysisMaxDb), double(analysisMaxDb));
        result.eq[size_t(b)] = float(std::round(gain * 2) / 2);
    }

    // Narrow bumps between 500 Hz and 6 kHz: a twelfth of an octave against an octave and a half.
    struct Peak { double hz, prominence; };
    std::vector<Peak> peaks;
    const int steps = 80;
    for (int s = 0; s < steps; ++s)
    {
        const double f = 500 * std::pow(12.0, double(s) / (steps - 1));
        if (f * std::pow(2.0, 0.75) >= rate / 2) break;
        const double narrow = meanPower(power, binHz, f * std::pow(2.0, -1.0 / 12), f * std::pow(2.0, 1.0 / 12));
        const double wide = meanPower(power, binHz, f * std::pow(2.0, -0.75), f * std::pow(2.0, 0.75));
        if (wide > 0) peaks.push_back({ f, 10 * std::log10(narrow / wide) });
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.prominence > b.prominence; });
    for (const auto& p : peaks)
    {
        if (p.prominence < 3.0 || result.markerCount >= maxMarkers) break;
        bool close = false;
        for (int m = 0; m < result.markerCount; ++m)
            close = close || std::abs(std::log2(p.hz / result.markerHz[size_t(m)])) < 0.5;
        if (close) continue;
        const auto m = size_t(result.markerCount++);
        result.markerHz[m] = float(p.hz);
        result.markerProminence[m] = float(p.prominence);
        result.markerDepth[m] = float(std::clamp(p.prominence / 6.0, 0.4, 1.0));
    }
    result.valid = true;
    return result;
}

/** Short findings shown after the analysis, in French. */
inline std::vector<std::string> describe(const AnalysisResult& r)
{
    std::vector<std::string> lines;
    if (!r.valid) return lines;
    if (r.eq[0] <= -1) lines.push_back("Corps chargé");
    if (r.eq[0] >= 1) lines.push_back("Corps léger");
    if (r.eq[1] <= -1) lines.push_back("Bas-médium chargé");
    if (r.eq[2] <= -1) lines.push_back("Présence trop forte");
    if (r.eq[2] >= 1) lines.push_back("Présence en retrait");
    if (r.eq[3] >= 1) lines.push_back("Manque d'air");
    if (r.eq[3] <= -1) lines.push_back("Trop d'air");
    for (int m = 0; m < r.markerCount; ++m)
    {
        const float hz = r.markerHz[size_t(m)];
        std::string label = hz < 1000 ? std::to_string(int(std::round(hz / 10) * 10)) + " Hz"
                                      : std::to_string(int(hz / 1000)) + "," + std::to_string(int(std::fmod(hz, 1000) / 100)) + " kHz";
        lines.push_back("Résonance vers " + label);
    }
    if (lines.empty()) lines.push_back("Voix équilibrée");
    return lines;
}
}
