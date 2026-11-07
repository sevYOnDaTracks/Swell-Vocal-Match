// Offline render through the real engine: reads raw stereo float32 at 48 kHz on stdin,
// analyses the voice, applies the chosen profile and writes raw stereo float32 on stdout.
// Usage: RenderVoice <voiceType 0-2> <profile 0-3>
#include "../Source/Analysis.h"
#include "../Source/DSP.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    const int voice = std::atoi(argv[1]), profile = std::atoi(argv[2]);
    const double rate = 48000;
    std::vector<float> interleaved;
    float frame[2];
    while (std::fread(frame, sizeof(float), 2, stdin) == 2) { interleaved.push_back(frame[0]); interleaved.push_back(frame[1]); }
    const size_t n = interleaved.size() / 2;
    std::vector<float> left(n), right(n), mono(n);
    for (size_t i = 0; i < n; ++i)
    {
        left[i] = interleaved[2 * i]; right[i] = interleaved[2 * i + 1];
        mono[i] = 0.5f * (left[i] + right[i]);
    }

    const auto result = vm::analyse(mono, rate, voice);
    vm::Settings s;
    s.voiceType = voice;
    s.profile = profile;
    s.intensity = 0.5f;
    s.analyzed = result.valid;
    s.analysis = result.eq;
    s.markerCount = result.markerCount;
    s.markerHz = result.markerHz;
    s.markerDepth = result.markerDepth;
    s.markerOn = { true, true, true };
    s.autoGain = true;
    std::fprintf(stderr, "eq %.1f %.1f %.1f %.1f markers %d:", result.eq[0], result.eq[1], result.eq[2], result.eq[3], result.markerCount);
    for (int m = 0; m < result.markerCount; ++m) std::fprintf(stderr, " %.0f Hz", result.markerHz[size_t(m)]);
    std::fprintf(stderr, "\n");

    vm::Engine engine;
    engine.setSettings(s);
    engine.prepare(rate);
    // Two passes: the first lets the level matching settle, the second is written out.
    for (int pass = 0; pass < 2; ++pass)
    {
        std::vector<float> l = left, r = right;
        float peak[3] {};
        for (size_t start = 0; start < n; start += 512)
        {
            float* ch[2] { l.data() + start, r.data() + start };
            engine.process(ch, 2, int(std::min<size_t>(512, n - start)));
            for (int b = 0; b < 3; ++b) peak[b] = std::max(peak[b], engine.gainReductionDb(b));
        }
        if (pass == 1) std::fprintf(stderr, "compression max %.1f %.1f %.1f dB | ", peak[0], peak[1], peak[2]);
        if (pass == 1)
            for (size_t i = 0; i < n; ++i) { frame[0] = l[i]; frame[1] = r[i]; std::fwrite(frame, sizeof(float), 2, stdout); }
    }
    std::fprintf(stderr, "compression %.1f %.1f %.1f dB\n", engine.gainReductionDb(0), engine.gainReductionDb(1), engine.gainReductionDb(2));
    return 0;
}
