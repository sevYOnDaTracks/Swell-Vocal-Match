#pragma once
#include <array>

// Data shared by the audio engine, the analysis and the interface.
// Profile values are the settings at 50 % intensity; they are starting points to validate by ear.
namespace vm
{
constexpr int numBands = 4;      // Corps, Bas-médium, Présence, Air
constexpr int numCompBands = 3;  // Corps, Présence, Air
constexpr int maxMarkers = 3;

struct Profile
{
    const char* name;
    const char* description;
    std::array<float, numBands> eq;
    std::array<float, numCompBands> comp;
};

inline const std::array<Profile, 4>& profiles()
{
    static const std::array<Profile, 4> list {{
        { "Late Night", "Posé sur les basses", { -2.0f, -3.0f, 1.0f, 1.0f }, { 2.0f, 1.0f, 1.0f } },
        { "Neon Auto", "Dense, pour l'Auto-Tune", { 0.0f, -2.0f, -1.0f, 2.0f }, { 4.0f, 4.0f, 2.0f } },
        { "Diva Power", "Puissant et en avant", { -1.0f, -3.0f, 3.0f, 2.0f }, { 2.0f, 3.0f, 2.0f } },
        { "Sharp Flow", "Net et très présent", { -2.0f, -3.0f, 2.0f, 3.0f }, { 5.0f, 3.0f, 3.0f } },
    }};
    return list;
}

struct VoiceType
{
    const char* name;
    float shift;     // moves the EQ bands with the register of the voice
    float highPass;  // Hz
};

inline const std::array<VoiceType, 3>& voiceTypes()
{
    static const std::array<VoiceType, 3> list {{
        { "Grave", 0.8f, 70.0f },
        { "Médium", 1.0f, 90.0f },
        { "Aiguë", 1.25f, 120.0f },
    }};
    return list;
}

// EQ band centres (Hz, before the voice-type shift) and widths (octaves, for the analysis weights).
constexpr std::array<float, numBands> bandFrequency { 180.0f, 420.0f, 3000.0f, 11000.0f };
constexpr std::array<float, numBands> bandOctaves { 2.1f, 1.6f, 2.1f, 0.0f };
constexpr std::array<float, numBands> analysisWidth { 0.9f, 0.7f, 0.9f, 1.1f };

constexpr float crossoverLow = 400.0f;
constexpr float crossoverHigh = 5000.0f;
}
