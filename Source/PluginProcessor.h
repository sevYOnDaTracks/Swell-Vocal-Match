#pragma once
#include "Analysis.h"
#include "DSP.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

namespace ids
{
inline const juce::String voiceType = "voiceType", profile = "profile", intensity = "intensity", resonance = "resonance",
                          deEss = "deEss", output = "output", autoGain = "autoGain", bypass = "bypass", solo = "solo";
inline juce::String manual(int band) { return "manual" + juce::String(band); }
}

class VocalMatchProcessor final : public juce::AudioProcessor
{
public:
    VocalMatchProcessor();
    ~VocalMatchProcessor() override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return state.getParameter(ids::bypass); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Swell Vocal Match"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Settings shared by the audio thread and the interface.
    vm::Settings currentSettings() const;
    double currentRate() const { return rate.load(); }

    // Analysis: records the dry voice, then analyses it on a background thread.
    void startAnalysis();
    bool isRecording() const { return captureState.load() == 1; }
    bool isAnalysing() const { return captureState.load() == 1 || captureState.load() == 2; }
    float analysisProgress() const;
    /** Called by the interface timer: applies a finished analysis on the message thread. */
    void pollAnalysis();
    bool isAnalysed() const { return analyzed.load(); }
    vm::AnalysisResult analysisResult() const;
    void setMarkerOn(int marker, bool on);

    // A/B: two complete sets of settings.
    int activeSlot() const { return slot; }
    void switchSlot(int newSlot);

    float gainReductionDb(int band) const { return meters[size_t(band)].load(); }
    /** Reads up to n of the newest mono samples before (pre) or after processing. */
    int readScope(bool processed, float* dest, int n);

    juce::AudioProcessorValueTreeState state;

private:
    void render(juce::AudioBuffer<float>&, bool forceBypass);
    juce::ValueTree captureSlot() const;
    void applySlot(const juce::ValueTree&);
    void applyResult(const vm::AnalysisResult&);
    void storeAnalysisInState();
    void loadAnalysisFromState();

    vm::Engine engine;
    std::atomic<double> rate { 48000 };
    juce::AudioBuffer<float> dry;
    juce::SmoothedValue<float> bypassMix;

    std::vector<float> capture;
    std::atomic<int> captureState { 0 };  // 0 idle, 1 recording, 2 analysing, 3 done
    std::atomic<int> capturePosition { 0 };
    std::atomic<int> captureVoiceType { 1 };
    juce::ThreadPool pool { 1 };
    juce::SpinLock resultLock;
    vm::AnalysisResult pendingResult;

    std::atomic<bool> analyzed { false };
    std::array<std::atomic<float>, vm::numBands> analysisEq {};
    std::array<std::atomic<float>, vm::maxMarkers> markerHz {}, markerDepth {};
    std::array<std::atomic<bool>, vm::maxMarkers> markerOn {};
    std::atomic<int> markerCount { 0 };

    std::array<std::atomic<float>, vm::numCompBands> meters {};
    juce::AbstractFifo scopeFifo[2] { juce::AbstractFifo(16384), juce::AbstractFifo(16384) };
    std::vector<float> scopeData[2] { std::vector<float>(16384), std::vector<float>(16384) };

    juce::ValueTree slots[2];
    int slot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalMatchProcessor)
};
