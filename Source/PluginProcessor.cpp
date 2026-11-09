#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ResizableEditor.h"

namespace
{
constexpr double captureSeconds = 12.0;
}

VocalMatchProcessor::VocalMatchProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, nullptr, "VocalMatch", layout())
{
    for (auto& v : analysisEq) v = 0;
    for (auto& v : markerHz) v = 1000;
    for (auto& v : markerDepth) v = 0;
    for (auto& v : markerOn) v = true;
    for (auto& v : meters) v = 0;
}

VocalMatchProcessor::~VocalMatchProcessor() { pool.removeAllJobs(true, 4000); }

juce::AudioProcessorValueTreeState::ParameterLayout VocalMatchProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    StringArray voices, names;
    for (const auto& v : vm::voiceTypes()) voices.add(String::fromUTF8(v.name));
    for (const auto& p : vm::profiles()) names.add(String::fromUTF8(p.name));
    auto percent = [](float v, int) { return String(roundToInt(v)) + " %"; };
    auto decibels = [](float v, int) { return (v > 0 ? "+" : "") + String(v, 1).replaceCharacter('.', ',') + " dB"; };
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::voiceType, 1 }, "Type de voix", voices, 1));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::profile, 1 }, "Profil", names, 0));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::intensity, 1 }, String::fromUTF8("Intensité"),
                                                NormalisableRange<float>(0, 100, 1), 50.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(percent)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::resonance, 1 }, String::fromUTF8("Résonances"),
                                                NormalisableRange<float>(0, 100, 1), 50.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(percent)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::deEss, 1 }, "De-esser", NormalisableRange<float>(0, 100, 1), 30.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(percent)));
    l.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::output, 1 }, "Sortie", NormalisableRange<float>(-12, 12, 0.5f), 0.0f,
                                                AudioParameterFloatAttributes().withStringFromValueFunction(decibels)));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { ids::autoGain, 1 }, "Volume auto", true));
    l.add(std::make_unique<AudioParameterBool>(ParameterID { ids::bypass, 1 }, "Bypass", false));
    l.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::solo, 1 }, String::fromUTF8("Écoute"),
                                                 StringArray { "Son complet", "Corps", String::fromUTF8("Présence"), "Air",
                                                               String::fromUTF8("Retiré") }, 0));
    const char* bandNames[] { "Corps", "Bas-médium", "Présence", "Air" };
    for (int b = 0; b < vm::numBands; ++b)
        l.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::manual(b), 1 }, String::fromUTF8(bandNames[b]),
                                                    NormalisableRange<float>(-6, 6, 0.5f), 0.0f,
                                                    AudioParameterFloatAttributes().withStringFromValueFunction(decibels)));
    return l;
}

bool VocalMatchProcessor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

void VocalMatchProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    rate = sampleRate;
    engine.setSettings(currentSettings());
    engine.prepare(sampleRate);
    dry.setSize(2, std::max(samplesPerBlock, 4096));
    bypassMix.reset(sampleRate, 0.02);
    bypassMix.setCurrentAndTargetValue(state.getRawParameterValue(ids::bypass)->load() > 0.5f ? 1.0f : 0.0f);
    // Allocated here, never on the audio thread, and never while the analysis reads it.
    if (captureState.load() == 1) captureState = 0;
    if (captureState.load() == 0 && capture.size() != size_t(sampleRate * captureSeconds))
        capture.assign(size_t(sampleRate * captureSeconds), 0.0f);
}

vm::Settings VocalMatchProcessor::currentSettings() const
{
    vm::Settings s;
    auto raw = [this](const juce::String& id) { return state.getRawParameterValue(id)->load(); };
    s.voiceType = juce::roundToInt(raw(ids::voiceType));
    s.profile = juce::roundToInt(raw(ids::profile));
    s.intensity = raw(ids::intensity) / 100.0f;
    s.resonance = raw(ids::resonance) / 100.0f;
    s.deEss = raw(ids::deEss) / 100.0f;
    s.outputDb = raw(ids::output);
    s.autoGain = raw(ids::autoGain) > 0.5f;
    s.solo = juce::roundToInt(raw(ids::solo));
    for (int b = 0; b < vm::numBands; ++b)
    {
        s.manual[size_t(b)] = raw(ids::manual(b));
        s.analysis[size_t(b)] = analysisEq[size_t(b)].load();
    }
    s.analyzed = analyzed.load();
    s.markerCount = s.analyzed ? markerCount.load() : 0;
    for (int m = 0; m < vm::maxMarkers; ++m)
    {
        s.markerHz[size_t(m)] = markerHz[size_t(m)].load();
        s.markerDepth[size_t(m)] = markerDepth[size_t(m)].load();
        s.markerOn[size_t(m)] = markerOn[size_t(m)].load();
    }
    if (s.solo == 4 && s.markerCount == 0) s.solo = 0;
    return s;
}

void VocalMatchProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { if (! licence->isUnlocked()) { buffer.clear(); return; }  render(buffer, false); }
void VocalMatchProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { render(buffer, true); }

void VocalMatchProcessor::render(juce::AudioBuffer<float>& buffer, bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = std::min(buffer.getNumChannels(), 2);
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());
    if (channels == 0) return;
    engine.setSettings(currentSettings());
    bypassMix.setTargetValue(forceBypass || state.getRawParameterValue(ids::bypass)->load() > 0.5f ? 1.0f : 0.0f);

    for (int start = 0; start < buffer.getNumSamples(); start += dry.getNumSamples())
    {
        const int n = std::min(dry.getNumSamples(), buffer.getNumSamples() - start);
        float* data[2] {};
        for (int c = 0; c < channels; ++c)
        {
            data[c] = buffer.getWritePointer(c, start);
            dry.copyFrom(c, 0, data[c], n);
        }

        // Dry voice for the analysis and the "before" spectrum.
        float energy = 0;
        int pre1 = 0, size1 = 0, pre2 = 0, size2 = 0;
        scopeFifo[0].prepareToWrite(n, pre1, size1, pre2, size2);
        for (int i = 0; i < n; ++i)
        {
            float mono = 0;
            for (int c = 0; c < channels; ++c) mono += data[c][i];
            mono /= float(channels);
            energy += mono * mono;
            if (i < size1) scopeData[0][size_t(pre1 + i)] = mono;
            else if (i - size1 < size2) scopeData[0][size_t(pre2 + i - size1)] = mono;
        }
        scopeFifo[0].finishedWrite(size1 + size2);
        if (captureState.load() == 1 && energy / float(n) > 1.0e-7f)
        {
            int pos = capturePosition.load();
            for (int i = 0; i < n && pos < int(capture.size()); ++i, ++pos)
            {
                float mono = 0;
                for (int c = 0; c < channels; ++c) mono += dry.getSample(c, i);
                capture[size_t(pos)] = mono / float(channels);
            }
            capturePosition = pos;
            if (pos >= int(capture.size()))
            {
                captureState = 2;
                const int voice = captureVoiceType.load();
                pool.addJob([this, voice]
                {
                    const auto result = vm::analyse(capture, rate.load(), voice);
                    {
                        const juce::SpinLock::ScopedLockType lock(resultLock);
                        pendingResult = result;
                    }
                    captureState = 3;
                });
            }
        }

        engine.process(data, channels, n);

        for (int i = 0; i < n; ++i)
        {
            const float mix = bypassMix.getNextValue();
            for (int c = 0; c < channels; ++c) data[c][i] = data[c][i] * (1 - mix) + dry.getSample(c, i) * mix;
        }

        scopeFifo[1].prepareToWrite(n, pre1, size1, pre2, size2);
        for (int i = 0; i < size1 + size2; ++i)
        {
            float mono = 0;
            for (int c = 0; c < channels; ++c) mono += data[c][i];
            scopeData[1][size_t(i < size1 ? pre1 + i : pre2 + i - size1)] = mono / float(channels);
        }
        scopeFifo[1].finishedWrite(size1 + size2);
    }
    if (buffer.getNumChannels() > 1 && getTotalNumInputChannels() == 1) buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    for (int b = 0; b < vm::numCompBands; ++b) meters[size_t(b)] = engine.gainReductionDb(b);
}

int VocalMatchProcessor::readScope(bool processed, float* dest, int n)
{
    auto& fifo = scopeFifo[processed ? 1 : 0];
    auto& data = scopeData[processed ? 1 : 0];
    const int ready = fifo.getNumReady();
    // Drop what does not fit, keep the newest samples.
    if (ready > n)
    {
        int a, b, c, d;
        fifo.prepareToRead(ready - n, a, b, c, d);
        fifo.finishedRead(b + d);
    }
    int pre1, size1, pre2, size2;
    fifo.prepareToRead(std::min(n, fifo.getNumReady()), pre1, size1, pre2, size2);
    for (int i = 0; i < size1; ++i) dest[i] = data[size_t(pre1 + i)];
    for (int i = 0; i < size2; ++i) dest[size1 + i] = data[size_t(pre2 + i)];
    fifo.finishedRead(size1 + size2);
    return size1 + size2;
}

void VocalMatchProcessor::startAnalysis()
{
    if (isAnalysing() || capture.empty()) return;
    captureVoiceType = juce::roundToInt(state.getRawParameterValue(ids::voiceType)->load());
    capturePosition = 0;
    captureState = 1;
}

float VocalMatchProcessor::analysisProgress() const
{
    if (capture.empty()) return 0;
    return captureState.load() >= 2 ? 1.0f : float(capturePosition.load()) / float(capture.size());
}

void VocalMatchProcessor::pollAnalysis()
{
    if (captureState.load() != 3) return;
    vm::AnalysisResult result;
    {
        const juce::SpinLock::ScopedLockType lock(resultLock);
        result = pendingResult;
    }
    captureState = 0;
    if (result.valid) applyResult(result);
}

void VocalMatchProcessor::applyResult(const vm::AnalysisResult& r)
{
    for (int b = 0; b < vm::numBands; ++b) analysisEq[size_t(b)] = r.eq[size_t(b)];
    for (int m = 0; m < vm::maxMarkers; ++m)
    {
        markerHz[size_t(m)] = m < r.markerCount ? r.markerHz[size_t(m)] : 1000.0f;
        markerDepth[size_t(m)] = m < r.markerCount ? r.markerDepth[size_t(m)] : 0.0f;
        markerOn[size_t(m)] = true;
    }
    markerCount = r.markerCount;
    analyzed = true;
    storeAnalysisInState();
}

vm::AnalysisResult VocalMatchProcessor::analysisResult() const
{
    vm::AnalysisResult r;
    r.valid = analyzed.load();
    for (int b = 0; b < vm::numBands; ++b) r.eq[size_t(b)] = analysisEq[size_t(b)].load();
    r.markerCount = markerCount.load();
    for (int m = 0; m < vm::maxMarkers; ++m)
    {
        r.markerHz[size_t(m)] = markerHz[size_t(m)].load();
        r.markerDepth[size_t(m)] = markerDepth[size_t(m)].load();
    }
    return r;
}

void VocalMatchProcessor::setMarkerOn(int marker, bool on)
{
    if (marker < 0 || marker >= vm::maxMarkers) return;
    markerOn[size_t(marker)] = on;
    storeAnalysisInState();
}

void VocalMatchProcessor::storeAnalysisInState()
{
    auto& tree = state.state;
    tree.setProperty("analyzed", analyzed.load(), nullptr);
    for (int b = 0; b < vm::numBands; ++b) tree.setProperty("analysisEq" + juce::String(b), analysisEq[size_t(b)].load(), nullptr);
    tree.setProperty("markerCount", markerCount.load(), nullptr);
    for (int m = 0; m < vm::maxMarkers; ++m)
    {
        tree.setProperty("markerHz" + juce::String(m), markerHz[size_t(m)].load(), nullptr);
        tree.setProperty("markerDepth" + juce::String(m), markerDepth[size_t(m)].load(), nullptr);
        tree.setProperty("markerOn" + juce::String(m), markerOn[size_t(m)].load(), nullptr);
    }
}

void VocalMatchProcessor::loadAnalysisFromState()
{
    const auto& tree = state.state;
    for (int b = 0; b < vm::numBands; ++b) analysisEq[size_t(b)] = float(tree.getProperty("analysisEq" + juce::String(b), 0.0f));
    markerCount = juce::jlimit(0, vm::maxMarkers, int(tree.getProperty("markerCount", 0)));
    for (int m = 0; m < vm::maxMarkers; ++m)
    {
        markerHz[size_t(m)] = float(tree.getProperty("markerHz" + juce::String(m), 1000.0f));
        markerDepth[size_t(m)] = float(tree.getProperty("markerDepth" + juce::String(m), 0.0f));
        markerOn[size_t(m)] = bool(tree.getProperty("markerOn" + juce::String(m), true));
    }
    analyzed = bool(tree.getProperty("analyzed", false));
}

juce::ValueTree VocalMatchProcessor::captureSlot() const
{
    juce::ValueTree slotTree("Slot");
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (withId->paramID != ids::bypass && withId->paramID != ids::solo)
                slotTree.setProperty(withId->paramID, p->getValue(), nullptr);
    for (int i = 0; i < state.state.getNumProperties(); ++i)
    {
        const auto name = state.state.getPropertyName(i);
        slotTree.setProperty(name, state.state.getProperty(name), nullptr);
    }
    return slotTree;
}

void VocalMatchProcessor::applySlot(const juce::ValueTree& slotTree)
{
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
            if (slotTree.hasProperty(withId->paramID))
                p->setValueNotifyingHost(float(slotTree.getProperty(withId->paramID)));
    auto copyProperty = [&](const juce::String& key, const juce::var& fallback)
    { state.state.setProperty(key, slotTree.getProperty(key, fallback), nullptr); };
    copyProperty("analyzed", false);
    copyProperty("markerCount", 0);
    for (int b = 0; b < vm::numBands; ++b) copyProperty("analysisEq" + juce::String(b), 0.0);
    for (int m = 0; m < vm::maxMarkers; ++m)
    {
        copyProperty("markerHz" + juce::String(m), 1000.0);
        copyProperty("markerDepth" + juce::String(m), 0.0);
        copyProperty("markerOn" + juce::String(m), true);
    }
    loadAnalysisFromState();
}

void VocalMatchProcessor::switchSlot(int newSlot)
{
    newSlot = juce::jlimit(0, 1, newSlot);
    if (newSlot == slot) return;
    slots[slot] = captureSlot();
    if (!slots[newSlot].isValid())
    {
        // B starts from A with another profile, so the comparison is useful straight away.
        slots[newSlot] = slots[slot].createCopy();
        if (auto* p = state.getParameter(ids::profile)) slots[newSlot].setProperty(ids::profile, p->convertTo0to1(3), nullptr);
    }
    applySlot(slots[newSlot]);
    slot = newSlot;
}

void VocalMatchProcessor::getStateInformation(juce::MemoryBlock& out)
{
    storeAnalysisInState();
    auto copy = state.copyState();
    juce::ValueTree slotTrees("Slots");
    slotTrees.setProperty("active", slot, nullptr);
    for (int i = 0; i < 2; ++i)
        if (slots[i].isValid() && i != slot) slotTrees.appendChild(slots[i].createCopy().setProperty("index", i, nullptr), nullptr);
    copy.appendChild(slotTrees, nullptr);
    if (auto xml = copy.createXml()) copyXmlToBinary(*xml, out);
}

void VocalMatchProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (!xml || !xml->hasTagName(state.state.getType())) return;
    auto tree = juce::ValueTree::fromXml(*xml);
    auto slotTrees = tree.getChildWithName("Slots");
    tree.removeChild(slotTrees, nullptr);
    state.replaceState(tree);
    loadAnalysisFromState();
    slots[0] = slots[1] = {};
    slot = slotTrees.isValid() ? juce::jlimit(0, 1, int(slotTrees.getProperty("active", 0))) : 0;
    for (const auto& child : slotTrees)
    {
        const int index = juce::jlimit(0, 1, int(child.getProperty("index", 0)));
        if (index != slot) slots[index] = child.createCopy();
    }
}

juce::AudioProcessorEditor* VocalMatchProcessor::createEditor()
{
    return swell::LicenceGuard::attach (new ResizableEditor(*this, new VocalMatchEditor(*this)));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VocalMatchProcessor(); }
