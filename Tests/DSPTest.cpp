#include "../Source/Analysis.h"
#include "../Source/DSP.h"
#include <cstdio>
#include <random>
#include <vector>

using namespace vm;

static int failures = 0;
static void check(bool ok, const char* what, double value = 0)
{
    std::printf("%s  %s (%.2f)\n", ok ? "PASS" : "FAIL", what, value);
    if (!ok) ++failures;
}

static Settings neutral()
{
    Settings s;
    s.intensity = 0; s.resonance = 0; s.deEss = 0; s.autoGain = false;
    return s;
}

static double rmsDb(const std::vector<float>& x, size_t from)
{
    double e = 0;
    for (size_t i = from; i < x.size(); ++i) e += double(x[i]) * x[i];
    return 10 * std::log10(e / double(x.size() - from) + 1e-30);
}

/** Runs a mono signal through the engine as identical stereo channels. */
static std::vector<float> run(Engine& engine, const std::vector<float>& input, bool* stereoMatch = nullptr)
{
    std::vector<float> left = input, right = input;
    for (size_t start = 0; start < input.size(); start += 512)
    {
        const int n = int(std::min<size_t>(512, input.size() - start));
        float* channels[2] { left.data() + start, right.data() + start };
        engine.process(channels, 2, n);
    }
    if (stereoMatch) *stereoMatch = left == right;
    return left;
}

static std::vector<float> sine(double hz, double rate, double seconds, double amp = 0.1)
{
    std::vector<float> x(size_t(rate * seconds));
    for (size_t i = 0; i < x.size(); ++i) x[i] = float(amp * std::sin(2 * pi * hz * double(i) / rate));
    return x;
}

static std::vector<float> noise(double rate, double seconds, unsigned seed, double amp = 0.1)
{
    std::mt19937 gen(seed);
    std::normal_distribution<float> dist(0, float(amp));
    std::vector<float> x(size_t(rate * seconds));
    for (auto& v : x) v = dist(gen);
    return x;
}

static void filter(std::vector<float>& x, const Coefficients& c)
{
    Biquad f; f.c = c;
    for (auto& v : x) v = f.process(v, 0);
}

int main()
{
    // 1. Neutral settings keep the voice unchanged above the high-pass.
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (double hz : { 250.0, 1000.0, 3000.0, 9000.0 })
        {
            Engine e; e.prepare(rate); e.setSettings(neutral());
            auto x = sine(hz, rate, 0.5);
            auto y = run(e, x);
            const double change = rmsDb(y, x.size() / 2) - rmsDb(x, x.size() / 2);
            char label[96]; std::snprintf(label, sizeof label, "neutre a %.0f Hz, %.0f Hz d'echantillonnage", hz, rate);
            check(std::abs(change) < 0.25, label, change);
        }

    // 2. The drawn curve follows each profile's main settings.
    for (int p = 0; p < 4; ++p)
        for (int v = 0; v < 3; ++v)
        {
            Engine e; e.prepare(48000);
            Settings s = neutral(); s.profile = p; s.voiceType = v; s.intensity = 0.5f;
            e.setSettings(s);
            float buffer[2][64] {}; float* ch[2] { buffer[0], buffer[1] };
            e.process(ch, 2, 64);
            const auto& prof = profiles()[size_t(p)];
            const float shift = voiceTypes()[size_t(v)].shift;
            bool ok = true;
            for (int b = 0; b < 4; ++b)
                if (std::abs(prof.eq[size_t(b)]) >= 2)
                    ok = ok && (e.responseDb(bandFrequency[size_t(b)] * shift) * prof.eq[size_t(b)] > 0);
            char label[96]; std::snprintf(label, sizeof label, "courbe du profil %s, type %d", prof.name, v);
            check(ok, label);
        }

    // 3. Stable and finite output with everything at maximum, at several rates.
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Engine e; e.prepare(rate);
        Settings s; s.profile = 3; s.intensity = 1; s.resonance = 1; s.deEss = 1; s.analyzed = true;
        s.analysis = { 4, -4, 4, 4 }; s.manual = { 6, -6, 6, 6 };
        s.markerCount = 3; s.markerHz = { 900, 3200, 5800 }; s.markerDepth = { 1, 1, 1 }; s.markerOn = { true, true, true };
        e.setSettings(s);
        bool same = false;
        auto y = run(e, noise(rate, 4.0, 7, 0.3), &same);
        bool finite = true;
        for (float v : y) finite = finite && std::isfinite(v);
        // Once settings and Volume auto have settled, the level must not keep rising (no runaway).
        const std::vector<float> middle(y.begin() + long(y.size() / 2), y.begin() + long(3 * y.size() / 4));
        const std::vector<float> end(y.begin() + long(3 * y.size() / 4), y.end());
        const double growth = rmsDb(end, 0) - rmsDb(middle, 0);
        char label[96]; std::snprintf(label, sizeof label, "stable a %.0f Hz (evolution du niveau, dB)", rate);
        check(finite && same && growth < 1.0, label, growth);
    }

    // 4. Silence stays silent.
    {
        Engine e; e.prepare(48000);
        Settings s; s.intensity = 1; s.resonance = 1; s.deEss = 1; e.setSettings(s);
        auto y = run(e, std::vector<float>(48000, 0.0f));
        float peak = 0; for (float v : y) peak = std::max(peak, std::abs(v));
        check(peak == 0, "le silence reste silencieux", peak);
    }

    // 5. De-esser: 8 kHz bursts over a 300 Hz tone come out lower when it is turned up.
    {
        auto make = [](float deEss)
        {
            const double rate = 48000;
            std::vector<float> x(size_t(rate * 2));
            for (size_t i = 0; i < x.size(); ++i)
            {
                const bool burst = (i / 4800) % 4 == 1;
                x[i] = float(0.2 * std::sin(2 * pi * 300 * double(i) / rate) + (burst ? 0.25 * std::sin(2 * pi * 8000 * double(i) / rate) : 0));
            }
            Engine e; e.prepare(rate); Settings s = neutral(); s.deEss = deEss; e.setSettings(s);
            auto y = run(e, x);
            filter(y, Coefficients::highPass(rate, 6000)); filter(y, Coefficients::highPass(rate, 6000));
            return rmsDb(y, y.size() / 2);
        };
        const double reduction = make(0) - make(1);
        check(reduction > 3, "de-esser : sifflantes reduites (dB)", reduction);
    }

    // 6. Compression engages with intensity and stays off at 0 %.
    {
        auto meter = [](float intensity)
        {
            const double rate = 48000;
            auto x = noise(rate, 3, 3, 0.05);
            for (size_t i = 0; i < x.size(); ++i) x[i] *= (i / 9600) % 2 ? 4.0f : 1.0f;
            Engine e; e.prepare(rate); Settings s = neutral(); s.profile = 3; s.intensity = intensity; e.setSettings(s);
            float peak = 0;
            std::vector<float> l = x, r = x;
            for (size_t start = 0; start < x.size(); start += 512)
            {
                float* ch[2] { l.data() + start, r.data() + start };
                e.process(ch, 2, 512);
                if (start > x.size() / 2) peak = std::max(peak, e.gainReductionDb(0));
            }
            return peak;
        };
        check(meter(0) < 0.01, "compression inactive a 0 %", meter(0));
        const double on = meter(0.5);
        check(on > 2 && on < 12, "compression du corps a 50 % (dB)", on);
    }

    // 7. Volume auto brings the processed level back near the input level.
    {
        const double rate = 48000;
        auto x = noise(rate, 6, 11, 0.1);
        filter(x, Coefficients::lowPass(rate, 2000, 0.5));
        Engine e; e.prepare(rate); Settings s; s.profile = 3; s.intensity = 1; s.deEss = 0.3f; s.autoGain = true;
        s.manual = { 6, 0, 6, 6 }; e.setSettings(s);
        auto y = run(e, x);
        const double gap = rmsDb(y, x.size() / 2) - rmsDb(x, x.size() / 2);
        check(std::abs(gap) < 1.5, "volume auto : ecart de niveau (dB)", gap);
    }

    // 8. Analysis: a defect added on purpose moves the right band in the right direction.
    {
        const double rate = 48000;
        auto base = noise(rate, 12, 5, 0.1);
        filter(base, Coefficients::lowPass(rate, 3000, 0.5));
        for (size_t i = 0; i < base.size(); ++i) base[i] *= (i / 12000) % 5 ? 1.0f : 0.02f;
        const auto reference = analyse(base, rate, 1);
        auto boosted = base;
        filter(boosted, Coefficients::peak(rate, 420, 0.92, 6));
        const auto heavy = analyse(boosted, rate, 1);
        check(reference.valid && heavy.eq[1] < reference.eq[1], "analyse : bas-medium ajoute detecte",
              heavy.eq[1] - reference.eq[1]);
        auto darker = base;
        filter(darker, Coefficients::highShelf(rate, 8000, -9));
        const auto dull = analyse(darker, rate, 1);
        check(dull.eq[3] > reference.eq[3], "analyse : manque d'air detecte", dull.eq[3] - reference.eq[3]);

        auto ringing = base;
        auto tone = noise(rate, 12, 9, 0.05);
        filter(tone, Coefficients::bandPass(rate, 3000, 25)); filter(tone, Coefficients::bandPass(rate, 3000, 25));
        for (size_t i = 0; i < ringing.size(); ++i) ringing[i] += 8 * tone[i] * ((i / 12000) % 5 ? 1.0f : 0.02f);
        const auto res = analyse(ringing, rate, 1);
        bool found = false;
        for (int m = 0; m < res.markerCount; ++m) found = found || std::abs(std::log2(res.markerHz[size_t(m)] / 3000)) < 0.25;
        check(found, "analyse : resonance a 3 kHz trouvee", res.markerCount);
    }

    std::printf(failures ? "\n%d test(s) en echec\n" : "\nTous les tests passent\n", failures);
    return failures ? 1 : 0;
}
