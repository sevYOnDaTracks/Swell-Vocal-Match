#pragma once
#include "Profiles.h"
#include <algorithm>
#include <array>
#include <cmath>

// Zero-latency vocal engine: high-pass, tonal EQ, dynamic resonance cuts,
// 3-band compression with a de-esser on the air band, then automatic level matching.
// No allocation and no locks in process(). Independent of JUCE so it can be tested alone.
namespace vm
{
constexpr double pi = 3.141592653589793;

inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
inline float gainToDb(float gain) { return 20.0f * std::log10(std::max(gain, 1.0e-9f)); }

struct Coefficients
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static Coefficients peak(double rate, double hz, double q, double db)
    {
        const double a = std::pow(10.0, db / 40.0), w = 2 * pi * hz / rate, alpha = std::sin(w) / (2 * q);
        const double norm = 1 + alpha / a;
        return { (1 + alpha * a) / norm, -2 * std::cos(w) / norm, (1 - alpha * a) / norm,
                 -2 * std::cos(w) / norm, (1 - alpha / a) / norm };
    }

    static Coefficients highShelf(double rate, double hz, double db)
    {
        const double a = std::pow(10.0, db / 40.0), w = 2 * pi * hz / rate;
        const double alpha = std::sin(w) / 2 * std::sqrt(2.0), c = std::cos(w), s = 2 * std::sqrt(a) * alpha;
        const double norm = (a + 1) - (a - 1) * c + s;
        return { a * ((a + 1) + (a - 1) * c + s) / norm, -2 * a * ((a - 1) + (a + 1) * c) / norm,
                 a * ((a + 1) + (a - 1) * c - s) / norm, 2 * ((a - 1) - (a + 1) * c) / norm,
                 ((a + 1) - (a - 1) * c - s) / norm };
    }

    static Coefficients lowPass(double rate, double hz, double q = 0.7071067811865476)
    {
        const double w = 2 * pi * hz / rate, alpha = std::sin(w) / (2 * q), c = std::cos(w), norm = 1 + alpha;
        return { (1 - c) / 2 / norm, (1 - c) / norm, (1 - c) / 2 / norm, -2 * c / norm, (1 - alpha) / norm };
    }

    static Coefficients highPass(double rate, double hz, double q = 0.7071067811865476)
    {
        const double w = 2 * pi * hz / rate, alpha = std::sin(w) / (2 * q), c = std::cos(w), norm = 1 + alpha;
        return { (1 + c) / 2 / norm, -(1 + c) / norm, (1 + c) / 2 / norm, -2 * c / norm, (1 - alpha) / norm };
    }

    static Coefficients bandPass(double rate, double hz, double q)
    {
        const double w = 2 * pi * hz / rate, alpha = std::sin(w) / (2 * q), norm = 1 + alpha;
        return { alpha / norm, 0, -alpha / norm, -2 * std::cos(w) / norm, (1 - alpha) / norm };
    }

    /** Magnitude in dB at a frequency, for the curve drawn by the interface. */
    double magnitudeDb(double rate, double hz) const
    {
        const double w = 2 * pi * hz / rate;
        const double cr = std::cos(w), ci = -std::sin(w), c2r = std::cos(2 * w), c2i = -std::sin(2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        return 10 * std::log10((nr * nr + ni * ni) / std::max(dr * dr + di * di, 1e-30));
    }
};

/** Transposed direct form II, one state per channel. */
struct Biquad
{
    Coefficients c;
    double z1[2] {}, z2[2] {};

    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }

    float process(float input, int channel)
    {
        const double x = input, y = c.b0 * x + z1[channel];
        z1[channel] = c.b1 * x - c.a1 * y + z2[channel];
        z2[channel] = c.b2 * x - c.a2 * y;
        return float(y);
    }
};

/** Fourth-order Linkwitz-Riley split: two cascaded Butterworth sections per side. */
struct Crossover
{
    Biquad low[2], high[2];

    void prepare(double rate, double hz)
    {
        for (auto& f : low) { f.c = Coefficients::lowPass(rate, hz); f.reset(); }
        for (auto& f : high) { f.c = Coefficients::highPass(rate, hz); f.reset(); }
    }

    void split(float input, int channel, float& lowOut, float& highOut)
    {
        lowOut = low[1].process(low[0].process(input, channel), channel);
        highOut = high[1].process(high[0].process(input, channel), channel);
    }
};

struct Settings
{
    int voiceType = 1;
    int profile = 0;
    float intensity = 0.5f;          // 0..1, 0.5 = profile values
    std::array<float, numBands> manual {};
    std::array<float, numBands> analysis {};
    bool analyzed = false;
    std::array<float, maxMarkers> markerHz {};
    std::array<float, maxMarkers> markerDepth {};
    std::array<bool, maxMarkers> markerOn {};
    int markerCount = 0;
    float resonance = 0.5f;          // 0..1, full scale = 6 dB at the deepest marker
    float deEss = 0.3f;              // 0..1, full scale = 10 dB on sibilants
    float outputDb = 0;
    bool autoGain = true;
    int solo = 0;                    // 0 none, 1 corps, 2 présence, 3 air, 4 removed by resonance cuts
};

class Engine
{
public:
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        lowSplit.prepare(rate, crossoverLow);
        highSplit.prepare(rate, crossoverHigh);
        lowAllPass.prepare(rate, crossoverHigh);
        sibilanceBand.c = Coefficients::bandPass(rate, 7000, 1.0);
        for (auto& m : markers) { m.narrow.c = Coefficients::bandPass(rate, 1000, 4.3); m.wide.c = Coefficients::bandPass(rate, 1000, 0.9); }
        auto coefficient = [this](double seconds) { return float(std::exp(-1.0 / (seconds * rate))); };
        fastAttack = coefficient(0.002); fastRelease = coefficient(0.060);
        compAttack = { coefficient(0.015), coefficient(0.008), coefficient(0.004) };
        compRelease = { coefficient(0.160), coefficient(0.110), coefficient(0.070) };
        referenceUp = coefficient(0.05); referenceDown = coefficient(2.0);
        levelCoefficient = coefficient(1.0);
        smoothCoefficient = coefficient(0.02);
        markerSlow = coefficient(2.0 / 16);  // updated once every 16 samples
        duckAttack = coefficient(0.001); duckRelease = coefficient(0.05);
        autoGainStep = 1.0f - coefficient(0.5);
        reset();
        applied = {};
        updateFilters(true);
    }

    void reset()
    {
        highPass[0].reset(); highPass[1].reset();
        for (auto& f : eq) f.reset();
        for (auto& m : markers) { m.cut.reset(); m.narrow.reset(); m.wide.reset(); m.narrowEnv = m.wideEnv = 0; m.average = 0; m.gainDb = 0; }
        lowSplit.prepare(rate, crossoverLow); highSplit.prepare(rate, crossoverHigh); lowAllPass.prepare(rate, crossoverHigh);
        sibilanceBand.reset();
        for (auto& b : bands) { b.env = 0; b.reference = -70; b.gainDb = 0; }
        fullEnv = sibEnv = 0; airDuck = 1; inLevel = outLevel = 0; autoGainLinear = 1;
        counter = 0;
    }

    void setSettings(const Settings& s) { target = s; }

    /** Applies the settings at once, without the gradual steps: for drawing the curve. */
    void applyNow() { updateFilters(true); }

    /** Curve the interface draws: static EQ plus the resonance cuts at their average depth. */
    float responseDb(float hz) const
    {
        double total = 0;
        for (const auto& f : designed) total += f.magnitudeDb(rate, hz);
        for (int i = 0; i < target.markerCount; ++i)
            if (target.markerOn[size_t(i)])
                total += Coefficients::peak(rate, target.markerHz[size_t(i)], 4.3,
                                            -0.5 * maxCut(i)).magnitudeDb(rate, hz);
        return float(total);
    }

    float gainReductionDb(int band) const { return bands[size_t(band)].meter; }

    void process(float* const* channels, int numChannels, int numSamples)
    {
        numChannels = std::min(numChannels, 2);
        updateFilters(false);
        const auto& s = target;
        const auto& profile = profiles()[size_t(std::clamp(s.profile, 0, 3))];
        const float k = 2.0f * s.intensity;
        std::array<float, numCompBands> threshold {}, compTarget {};
        for (int b = 0; b < numCompBands; ++b)
        {
            compTarget[size_t(b)] = profile.comp[size_t(b)] * k;
            threshold[size_t(b)] = bands[size_t(b)].reference - compTarget[size_t(b)] * 1.5f;
        }
        const float deEssFloor = 1.0f - dbToGain(-10.0f * s.deEss);
        const float output = dbToGain(s.outputDb);
        std::array<float, numCompBands> peakReduction {};

        for (int i = 0; i < numSamples; ++i)
        {
            if ((counter++ & 15) == 0) updateMarkers();

            float pre[2] {}, post[2] {}, band[3][2] {};
            float fullPeak = 0, sibPeak = 0, inEnergy = 0;
            std::array<float, numCompBands> bandPeak {};
            for (int c = 0; c < numChannels; ++c)
            {
                float x = channels[c][i];
                float y = highPass[1].process(highPass[0].process(x, c), c);
                for (auto& f : eq) y = f.process(y, c);
                pre[c] = y;
                for (int m = 0; m < s.markerCount; ++m)
                {
                    auto& marker = markers[size_t(m)];
                    const float n = marker.narrow.process(y, c), w = marker.wide.process(y, c);
                    marker.narrowEnv += n * n;
                    marker.wideEnv += w * w;
                    y = marker.cut.process(y, c);
                }
                post[c] = y;
                float low, rest, mid, high, lowA, lowB;
                lowSplit.split(y, c, low, rest);
                highSplit.split(rest, c, mid, high);
                lowAllPass.split(low, c, lowA, lowB);
                band[0][c] = lowA + lowB;
                band[1][c] = mid;
                band[2][c] = high;
                for (int b = 0; b < 3; ++b) bandPeak[size_t(b)] = std::max(bandPeak[size_t(b)], band[b][c] * band[b][c]);
                fullPeak = std::max(fullPeak, std::abs(y));
                sibPeak = std::max(sibPeak, std::abs(sibilanceBand.process(y, c)));
                inEnergy += x * x / float(numChannels);
            }
            inLevel = levelCoefficient * inLevel + (1 - levelCoefficient) * inEnergy;

            // Compression: the threshold follows the loud passages of each band, so the
            // gain reduction on those passages lands near the profile value.
            std::array<float, numCompBands> gain {};
            for (int b = 0; b < numCompBands; ++b)
            {
                auto& st = bands[size_t(b)];
                const float a = bandPeak[size_t(b)] > st.env ? compAttack[size_t(b)] : compRelease[size_t(b)];
                st.env = a * st.env + (1 - a) * bandPeak[size_t(b)];
                const float level = 10.0f * std::log10(st.env + 1e-12f);
                // The air band of a voice is often 30 to 50 dB below the full signal: the gate stays low.
                if (level > -90.0f)
                {
                    const float r = level > st.reference ? referenceUp : referenceDown;
                    st.reference = r * st.reference + (1 - r) * level;
                }
                float reduction = 0;
                if (compTarget[size_t(b)] > 0.05f)
                {
                    const float over = level - threshold[size_t(b)], knee = 4.0f;
                    if (over > knee / 2) reduction = over * (1 - 1 / 3.0f);
                    else if (over > -knee / 2) reduction = (1 - 1 / 3.0f) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
                    // Never far beyond what the profile asks for, even on a very dynamic voice.
                    reduction = std::min(reduction, 2.5f * compTarget[size_t(b)]);
                }
                st.gainDb = smoothCoefficient * st.gainDb + (1 - smoothCoefficient) * reduction;
                peakReduction[size_t(b)] = std::max(peakReduction[size_t(b)], st.gainDb);
                gain[size_t(b)] = dbToGain(-st.gainDb);
            }

            // De-esser: sibilants rise in the 7 kHz band compared with the whole voice.
            const float fa = fullPeak > fullEnv ? fastAttack : fastRelease;
            fullEnv = fa * fullEnv + (1 - fa) * fullPeak;
            const float sa = sibPeak > sibEnv ? fastAttack : fastRelease;
            sibEnv = sa * sibEnv + (1 - sa) * sibPeak;
            const float sibilance = std::clamp((sibEnv / (fullEnv + 0.003f) - 0.30f) / 0.25f, 0.0f, 1.0f);
            const float duckTarget = 1.0f - deEssFloor * sibilance;
            airDuck = (duckTarget < airDuck ? duckAttack : duckRelease) * (airDuck - duckTarget) + duckTarget;
            gain[2] *= airDuck;

            float outEnergy = 0, outSamples[2] {};
            for (int c = 0; c < numChannels; ++c)
            {
                outSamples[c] = band[0][c] * gain[0] + band[1][c] * gain[1] + band[2][c] * gain[2];
                outEnergy += outSamples[c] * outSamples[c] / float(numChannels);
            }
            outLevel = levelCoefficient * outLevel + (1 - levelCoefficient) * outEnergy;
            if (s.autoGain && inLevel > 1e-7f)
            {
                const float wanted = std::clamp(std::sqrt(inLevel / std::max(outLevel, 1e-12f)), 0.25f, 4.0f);
                autoGainLinear += autoGainStep * (wanted - autoGainLinear);
            }
            else if (!s.autoGain)
                autoGainLinear += 10 * autoGainStep * (1.0f - autoGainLinear);
            const float makeup = autoGainLinear * output;

            for (int c = 0; c < numChannels; ++c)
            {
                float y = outSamples[c];
                if (s.solo >= 1 && s.solo <= 3) y = band[s.solo - 1][c] * gain[size_t(s.solo - 1)];
                else if (s.solo == 4) y = pre[c] - post[c];
                channels[c][i] = y * makeup;
            }
        }
        for (int b = 0; b < numCompBands; ++b) bands[size_t(b)].meter = peakReduction[size_t(b)];
    }

private:
    struct Marker
    {
        Biquad cut, narrow, wide;
        float narrowEnv = 0, wideEnv = 0, average = 0, gainDb = 0, hz = 0;
    };
    struct CompBand { float env = 0, reference = -70, gainDb = 0, meter = 0; };

    float maxCut(int i) const
    {
        return 6.0f * target.resonance * target.markerDepth[size_t(i)];
    }

    void updateFilters(bool force)
    {
        const auto& s = target;
        const auto& profile = profiles()[size_t(std::clamp(s.profile, 0, 3))];
        const auto& voice = voiceTypes()[size_t(std::clamp(s.voiceType, 0, 2))];
        std::array<float, numBands + 1> wanted {};
        for (int b = 0; b < numBands; ++b)
            wanted[size_t(b)] = (profile.eq[size_t(b)] + (s.analyzed ? s.analysis[size_t(b)] : 0.0f)) * 2.0f * s.intensity
                                + s.manual[size_t(b)];
        wanted[numBands] = voice.shift;
        bool changed = force || voice.highPass != appliedHighPass;
        for (size_t b = 0; b < wanted.size(); ++b)
        {
            // Small steps per block avoid zipper noise when a setting jumps.
            const float step = b < numBands ? 0.5f : 1.0f;
            const float next = applied[b] + std::clamp(wanted[b] - applied[b], -step, step);
            if (force) applied[b] = wanted[b];
            else if (next != applied[b]) { applied[b] = next; changed = true; }
        }
        if (!changed) return;
        appliedHighPass = voice.highPass;
        const double shift = applied[numBands] > 0 ? applied[numBands] : 1.0;
        auto q = [](double octaves) { const double p = std::pow(2.0, octaves); return std::sqrt(p) / (p - 1); };
        designed[0] = Coefficients::highPass(rate, voice.highPass);
        designed[1] = Coefficients::highPass(rate, voice.highPass);
        for (int b = 0; b < 3; ++b)
            designed[size_t(b + 2)] = Coefficients::peak(rate, bandFrequency[size_t(b)] * shift, q(bandOctaves[size_t(b)]), applied[size_t(b)]);
        designed[5] = Coefficients::highShelf(rate, std::min(bandFrequency[3] * shift, rate * 0.45), applied[3]);
        highPass[0].c = designed[0]; highPass[1].c = designed[1];
        for (int b = 0; b < 4; ++b) eq[size_t(b)].c = designed[size_t(b + 2)];
    }

    void updateMarkers()
    {
        const auto& s = target;
        for (int i = 0; i < s.markerCount; ++i)
        {
            auto& m = markers[size_t(i)];
            const float hz = std::min(s.markerHz[size_t(i)], float(rate * 0.45));
            if (hz != m.hz)
            {
                m.hz = hz;
                m.narrow.c = Coefficients::bandPass(rate, hz, 4.3);
                m.wide.c = Coefficients::bandPass(rate, hz, 0.9);
            }
            // Ratio of the narrow band to its surroundings, compared with its own recent average:
            // the cut deepens only when the resonance stands out more than usual.
            float wanted = 0;
            if (s.markerOn[size_t(i)] && m.wideEnv > 1e-10f)
            {
                const float ratio = 10.0f * std::log10((m.narrowEnv + 1e-12f) / m.wideEnv);
                m.average = markerSlow * m.average + (1 - markerSlow) * ratio;
                wanted = maxCut(i) * std::clamp(0.5f + (ratio - m.average) / 6.0f, 0.0f, 1.0f);
            }
            m.narrowEnv = m.wideEnv = 0;
            m.gainDb += 0.2f * (wanted - m.gainDb);
            m.cut.c = Coefficients::peak(rate, hz, 4.3, -m.gainDb);
        }
    }

    double rate = 48000;
    Settings target;
    std::array<float, numBands + 1> applied {};
    float appliedHighPass = 0;
    std::array<Coefficients, 6> designed;
    Biquad highPass[2];
    std::array<Biquad, 4> eq;
    std::array<Marker, maxMarkers> markers;
    Crossover lowSplit, highSplit, lowAllPass;
    Biquad sibilanceBand;
    std::array<CompBand, numCompBands> bands;
    std::array<float, numCompBands> compAttack {}, compRelease {};
    float fastAttack = 0, fastRelease = 0, referenceUp = 0, referenceDown = 0, levelCoefficient = 0;
    float smoothCoefficient = 0, markerSlow = 0, duckAttack = 0, duckRelease = 0, autoGainStep = 0;
    float fullEnv = 0, sibEnv = 0, airDuck = 1, inLevel = 0, outLevel = 0, autoGainLinear = 1;
    unsigned counter = 0;
};
}
