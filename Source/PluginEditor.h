#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace colours
{
const juce::Colour background { 0xff0d0e10 }, panel { 0xff15171b }, line { 0xff2a2e35 }, raised { 0xff22252b },
                   text { 0xfff2f3f5 }, soft { 0xffd5d8dd }, muted { 0xffa3a8b2 }, accent { 0xffff9a4d },
                   blue { 0xff7fb2ff };
}

class SwellLook final : public juce::LookAndFeel_V4
{
public:
    SwellLook();
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
};

class VocalMatchEditor;

/** Spectrum, correction curve, zones and resonance markers. */
class GraphView final : public juce::Component
{
public:
    explicit GraphView(VocalMatchEditor& owner) : editor(owner) {}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    juce::Rectangle<float> zoneButton(int zone) const;

private:
    juce::Rectangle<float> plot() const;
    float xOf(float hz) const;
    float yOfDb(float db) const;
    juce::Point<float> markerPoint(int marker) const;
    VocalMatchEditor& editor;
};

class VocalMatchEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalMatchEditor(VocalMatchProcessor&);
    ~VocalMatchEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int numPoints = 220;
    static float frequencyAt(int index) { return 20.0f * std::pow(1000.0f, float(index) / float(numPoints - 1)); }

    VocalMatchProcessor& processor;
    vm::Settings settings;
    std::array<float, numPoints> curve {}, spectrumIn {}, spectrumOut {};
    float progress = 0;
    bool advanced = false;

private:
    void timerCallback() override;
    void updateSpectrum(bool processed, std::array<float, numPoints>& target, std::vector<float>& history);
    void setChoice(const juce::String& id, int index);
    juce::String statusText() const;
    void setupKnob(juce::Slider&, const juce::String& id, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>&);
    void setupToggle(juce::TextButton&, juce::Colour on);

    SwellLook look;
    juce::Image logo;
    GraphView graph { *this };
    juce::TextButton slotA { "A" }, slotB { "B" }, autoGain { "Volume auto" }, listenRemoved, bypass { "Bypass" }, advancedButton;
    std::array<juce::TextButton, 3> voiceButtons, zoneButtons;
    juce::ComboBox profileBox;
    juce::TextButton analyse, reference;
    juce::Slider resonanceKnob, deEssKnob, intensityKnob, outputKnob;
    std::array<juce::Slider, vm::numBands> manualSliders;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment, bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> profileAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> resonanceAttachment, deEssAttachment, intensityAttachment, outputAttachment;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, vm::numBands> manualAttachments;

    vm::Engine preview;
    double previewRate = 0;
    juce::dsp::FFT fft { 12 };
    std::vector<float> historyIn, historyOut, fftData;
    std::vector<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalMatchEditor)
};
