#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
juce::String fr(const char* text) { return juce::String::fromUTF8(text); }

juce::String formatDb(float v)
{
    const auto sign = v > 0.05f ? "+" : v < -0.05f ? fr("−") : juce::String();
    return sign + juce::String(std::abs(v), 1).replaceCharacter('.', ',') + " dB";
}

juce::String formatHz(float hz)
{
    if (hz < 1000) return juce::String(juce::roundToInt(hz / 10) * 10) + " Hz";
    return juce::String(hz / 1000.0f, 1).replaceCharacter('.', ',') + " kHz";
}

constexpr float zoneEdges[] { 80.0f, 400.0f, 5000.0f, 20000.0f };
const char* zoneNames[] { "CORPS", "PRÉSENCE", "AIR" };
}

// ---------------------------------------------------------------------------------------------
SwellLook::SwellLook()
{
    setColour(juce::PopupMenu::backgroundColourId, colours::panel);
    setColour(juce::PopupMenu::textColourId, colours::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::line);
    setColour(juce::PopupMenu::highlightedTextColourId, colours::text);
    setColour(juce::ComboBox::textColourId, colours::text);
    setColour(juce::ComboBox::arrowColourId, colours::soft);
    setColour(juce::Slider::trackColourId, colours::accent);
    setColour(juce::Slider::backgroundColourId, colours::background);
    setColour(juce::Slider::thumbColourId, colours::text);
}

void SwellLook::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(6);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) / 2 - 4;
    const auto centre = bounds.getCentre();
    const auto colour = s.findColour(juce::Slider::rotarySliderFillColourId);
    const bool bipolar = s.getMinimum() < 0;
    const float zeroAngle = bipolar ? start + (end - start) * float((0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) : start;
    const float angle = start + pos * (end - start);
    juce::Path track, arc;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
    arc.addCentredArc(centre.x, centre.y, radius, radius, 0, std::min(zeroAngle, angle), std::max(zeroAngle, angle), true);
    g.setColour(colours::line);
    g.strokePath(track, juce::PathStrokeType(6, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colour);
    g.strokePath(arc, juce::PathStrokeType(6, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto face = juce::Rectangle<float>(radius * 1.52f, radius * 1.52f).withCentre(centre);
    g.setColour(colours::raised);
    g.fillEllipse(face);
    g.setColour(juce::Colour(0xff3a3f48));
    g.drawEllipse(face, 1);
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(bipolar ? 13.0f : 15.0f, juce::Font::bold));
    g.drawText(s.getTextFromValue(s.getValue()), face.toNearestInt(), juce::Justification::centred);
    if (s.hasKeyboardFocus(true))
    {
        g.setColour(colours::text.withAlpha(0.6f));
        g.drawEllipse(bounds.reduced(1), 1.5f);
    }
}

void SwellLook::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = b.getToggleState();
    auto fill = b.findColour(on ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    if (b.isEnabled() && (over || down)) fill = fill.brighter(0.12f);
    g.setColour(fill);
    g.fillRoundedRectangle(r, 8);
    auto outline = b.findColour(juce::ComboBox::outlineColourId);
    if (b.getProperties().contains("dashed"))
    {
        juce::Path p; p.addRoundedRectangle(r, 8);
        juce::Path dashed; const float dashes[] { 4, 4 };
        juce::PathStrokeType(1).createDashedStroke(dashed, p, dashes, 2);
        g.setColour(juce::Colour(0xff3a3f48)); g.fillPath(dashed);
    }
    else
    {
        g.setColour(outline);
        g.drawRoundedRectangle(r, 8, 1);
    }
    if (b.hasKeyboardFocus(false)) { g.setColour(colours::text); g.drawRoundedRectangle(r.expanded(1.5f), 9, 1.5f); }
}

juce::Font SwellLook::getTextButtonFont(juce::TextButton& b, int)
{
    return juce::FontOptions(float(b.getProperties().getWithDefault("fontSize", 13.0)), juce::Font::bold);
}

void SwellLook::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float>(0, 0, float(w), float(h)).reduced(0.5f);
    g.setColour(colours::panel); g.fillRoundedRectangle(r, 10);
    g.setColour(box.hasKeyboardFocus(true) ? colours::text : colours::line); g.drawRoundedRectangle(r, 10, 1);
    juce::Path arrow;
    const float cx = float(w) - 25, cy = float(h) / 2;
    arrow.startNewSubPath(cx - 6, cy - 3); arrow.lineTo(cx, cy + 3); arrow.lineTo(cx + 6, cy - 3);
    g.setColour(colours::soft);
    g.strokePath(arrow, juce::PathStrokeType(2, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font SwellLook::getComboBoxFont(juce::ComboBox&) { return juce::FontOptions(16.0f, juce::Font::bold); }

void SwellLook::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(10, 0, box.getWidth() - 50, box.getHeight());
    label.setFont(getComboBoxFont(box));
}

// ---------------------------------------------------------------------------------------------
juce::Rectangle<float> GraphView::plot() const
{
    // Same proportions as the mock-up: 1080 x 340 with the plot from x 40 to 1060 and y 16 to 316.
    const float sx = float(getWidth()) / 1080.0f, sy = float(getHeight()) / 340.0f;
    return { 40 * sx, 16 * sy, 1020 * sx, 300 * sy };
}

float GraphView::xOf(float hz) const
{
    const auto p = plot();
    return p.getX() + p.getWidth() * std::log10(juce::jlimit(20.0f, 20000.0f, hz) / 20.0f) / 3.0f;
}

float GraphView::yOfDb(float db) const
{
    const auto p = plot();
    return p.getCentreY() - p.getHeight() / 30.0f * juce::jlimit(-15.0f, 15.0f, db);
}

juce::Rectangle<float> GraphView::zoneButton(int zone) const
{
    const float left = xOf(zoneEdges[zone]), right = xOf(std::min(zoneEdges[zone + 1], 20000.0f));
    return { left, plot().getBottom() - 46, right - left, 44 };
}

juce::Point<float> GraphView::markerPoint(int marker) const
{
    const float hz = editor.settings.markerHz[size_t(marker)];
    const int index = juce::jlimit(0, VocalMatchEditor::numPoints - 1,
                                   juce::roundToInt(std::log(hz / 20.0f) / std::log(1000.0f) * float(VocalMatchEditor::numPoints - 1)));
    return { xOf(hz), yOfDb(editor.curve[size_t(index)]) };
}

void GraphView::paint(juce::Graphics& g)
{
    const auto& s = editor.settings;
    const auto p = plot();
    const bool on = !(editor.processor.state.getRawParameterValue(ids::bypass)->load() > 0.5f);
    g.setColour(colours::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 12);

    // Zones: Corps and Air lightly shaded; with a solo, the chosen zone lights up and the rest darkens.
    for (int z = 0; z < 3; ++z)
    {
        const auto area = juce::Rectangle<float>(xOf(zoneEdges[z]), p.getY(), xOf(zoneEdges[z + 1]) - xOf(zoneEdges[z]), p.getHeight());
        if (s.solo == 0) { if (z != 1) { g.setColour(juce::Colours::white.withAlpha(0.045f)); g.fillRect(area); } }
        else if (s.solo == z + 1) { g.setColour(juce::Colours::white.withAlpha(0.14f)); g.fillRect(area); }
        else { g.setColour(juce::Colours::black.withAlpha(0.38f)); g.fillRect(area); }
    }

    g.setColour(juce::Colour(0xff252930));
    for (float hz : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f }) g.drawVerticalLine(juce::roundToInt(xOf(hz)), p.getY(), p.getBottom());
    for (float db : { -12.f, -6.f, 6.f, 12.f }) g.drawHorizontalLine(juce::roundToInt(yOfDb(db)), p.getX(), p.getRight());
    g.setColour(juce::Colour(0xff3a3f48));
    g.drawHorizontalLine(juce::roundToInt(yOfDb(0)), p.getX(), p.getRight());

    g.setFont(juce::FontOptions(11.0f));
    g.setColour(colours::muted);
    for (float db : { 12.f, 6.f, 0.f, -6.f, -12.f })
        g.drawText(db > 0 ? "+" + juce::String(int(db)) : db < 0 ? fr("−") + juce::String(int(-db)) : "0",
                   juce::Rectangle<float>(0, yOfDb(db) - 7, p.getX() - 6, 14), juce::Justification::centredRight);
    const std::pair<float, const char*> labels[] { { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" }, { 1000, "1k" },
                                                   { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k Hz" } };
    for (const auto& [hz, text] : labels)
        g.drawText(text, juce::Rectangle<float>(xOf(hz) - 30, p.getBottom() + 4, 60, 14), juce::Justification::centred);

    // Spectrum: dashed grey before, blue after.
    auto levelY = [&](float level) { return p.getY() + p.getHeight() * juce::jlimit(0.0f, 1.0f, (-12.0f - level) / 84.0f); };
    juce::Path before, after, fill;
    for (int i = 0; i < VocalMatchEditor::numPoints; ++i)
    {
        const float x = xOf(VocalMatchEditor::frequencyAt(i));
        const float yIn = levelY(editor.spectrumIn[size_t(i)]), yOut = levelY(editor.spectrumOut[size_t(i)]);
        if (i == 0) { before.startNewSubPath(x, yIn); after.startNewSubPath(x, yOut); fill.startNewSubPath(x, p.getBottom()); }
        else { before.lineTo(x, yIn); after.lineTo(x, yOut); }
        fill.lineTo(x, yOut);
    }
    fill.lineTo(p.getRight(), p.getBottom());
    fill.closeSubPath();
    g.setColour(colours::blue.withAlpha(0.16f)); g.fillPath(fill);
    g.setColour(colours::blue); g.strokePath(after, juce::PathStrokeType(1.5f));
    juce::Path dashedBefore; const float dashes[] { 3, 4 };
    juce::PathStrokeType(1).createDashedStroke(dashedBefore, before, dashes, 2);
    g.setColour(colours::muted); g.fillPath(dashedBefore);

    // Correction curve and band points.
    juce::Path curve;
    for (int i = 0; i < VocalMatchEditor::numPoints; ++i)
    {
        const float x = xOf(VocalMatchEditor::frequencyAt(i)), y = yOfDb(on ? editor.curve[size_t(i)] : 0.0f);
        if (i == 0) curve.startNewSubPath(x, y); else curve.lineTo(x, y);
    }
    const auto curveColour = on ? colours::accent : juce::Colour(0xff5a5f68);
    g.setColour(curveColour);
    g.strokePath(curve, juce::PathStrokeType(3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float shift = vm::voiceTypes()[size_t(juce::jlimit(0, 2, s.voiceType))].shift;
    auto curveAt = [&](float hz)
    {
        const int index = juce::jlimit(0, VocalMatchEditor::numPoints - 1,
                                       juce::roundToInt(std::log(hz / 20.0f) / std::log(1000.0f) * float(VocalMatchEditor::numPoints - 1)));
        return on ? editor.curve[size_t(index)] : 0.0f;
    };
    for (int b = 0; b < vm::numBands; ++b)
    {
        const float hz = vm::bandFrequency[size_t(b)] * shift;
        auto dot = juce::Rectangle<float>(14, 14).withCentre({ xOf(hz), yOfDb(curveAt(hz)) });
        g.setColour(curveColour); g.fillEllipse(dot);
        g.setColour(colours::panel); g.drawEllipse(dot, 2);
    }

    // Resonance markers (after an analysis): click to switch one off or back on.
    if (s.analyzed && on && !editor.processor.isAnalysing())
        for (int m = 0; m < s.markerCount; ++m)
        {
            const auto point = markerPoint(m);
            juce::Path diamond;
            diamond.addRectangle(-6, -6, 12, 12);
            diamond.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi / 4).translated(point));
            g.setColour(s.markerOn[size_t(m)] ? colours::accent : colours::panel); g.fillPath(diamond);
            g.setColour(s.markerOn[size_t(m)] ? colours::accent : colours::muted); g.strokePath(diamond, juce::PathStrokeType(2));
            g.setColour(s.markerOn[size_t(m)] ? colours::text : colours::muted);
            g.setFont(juce::FontOptions(11.0f));
            g.drawText(formatHz(s.markerHz[size_t(m)]), juce::Rectangle<float>(point.x - 40, point.y + 11, 80, 14), juce::Justification::centred);
        }

    // Legend.
    g.setFont(juce::FontOptions(12.0f));
    float lx = p.getX() + 12;
    const std::tuple<juce::Colour, const char*, float> legend[] { { colours::muted, "Voix d'entrée", 1 }, { colours::blue, "Après traitement", 2 },
                                                                  { colours::accent, "Correction", 3 } };
    for (const auto& [colour, text, thickness] : legend)
    {
        g.setColour(colour); g.fillRect(lx, p.getY() + 9, 14.0f, thickness);
        g.setColour(colours::soft);
        const auto label = fr(text);
        const float width = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), label);
        g.drawText(label, juce::Rectangle<float>(lx + 20, p.getY() + 2, width + 4, 16), juce::Justification::centredLeft);
        lx += 20 + width + 18;
    }

    // Progress while the voice is recorded.
    if (editor.processor.isAnalysing())
    {
        const auto bar = juce::Rectangle<float>(p.getX(), p.getBottom() - 2, p.getWidth(), 4);
        g.setColour(colours::line); g.fillRoundedRectangle(bar, 2);
        g.setColour(colours::accent); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * editor.progress), 2);
    }

    // Findings box.
    if (s.analyzed && on && !editor.processor.isAnalysing())
    {
        const auto lines = vm::describe(editor.processor.analysisResult());
        const auto box = juce::Rectangle<float>(float(getWidth()) - 230, 12, 216, 34.0f + 18.0f * float(lines.size()));
        g.setColour(colours::background); g.fillRoundedRectangle(box, 9);
        g.setColour(colours::line); g.drawRoundedRectangle(box, 9, 1);
        g.setColour(colours::accent); g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText("Analyse de ta voix", box.reduced(12, 8).withHeight(16), juce::Justification::centredLeft);
        g.setColour(colours::text); g.setFont(juce::FontOptions(12.0f));
        for (size_t i = 0; i < lines.size(); ++i)
            g.drawText(fr("• ") + juce::String::fromUTF8(lines[i].c_str()), juce::Rectangle<float>(box.getX() + 12, box.getY() + 28 + 18.0f * float(i), box.getWidth() - 20, 16),
                       juce::Justification::centredLeft);
    }
}

void GraphView::mouseDown(const juce::MouseEvent& e)
{
    const auto& s = editor.settings;
    const auto pos = e.position;
    if (s.analyzed && !editor.processor.isAnalysing())
        for (int m = 0; m < s.markerCount; ++m)
            if (pos.getDistanceFrom(markerPoint(m)) < 16)
            {
                editor.processor.setMarkerOn(m, !s.markerOn[size_t(m)]);
                return;
            }
}

void GraphView::mouseMove(const juce::MouseEvent& e)
{
    bool hot = false;
    if (editor.settings.analyzed)
        for (int m = 0; m < editor.settings.markerCount; ++m) hot = hot || e.position.getDistanceFrom(markerPoint(m)) < 16;
    setMouseCursor(hot ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

// ---------------------------------------------------------------------------------------------
VocalMatchEditor::VocalMatchEditor(VocalMatchProcessor& p)
    : AudioProcessorEditor(p), processor(p), historyIn(4096, 0.0f), historyOut(4096, 0.0f), fftData(8192, 0.0f), scratch(16384, 0.0f)
{
    setLookAndFeel(&look);
    logo = juce::ImageCache::getFromMemory(BinaryData::swellmark_png, BinaryData::swellmark_pngSize);
    spectrumIn.fill(-120); spectrumOut.fill(-120);

    addAndMakeVisible(graph);

    for (auto* b : { &slotA, &slotB })
    {
        setupToggle(*b, colours::text);
        b->setColour(juce::TextButton::textColourOnId, colours::background);
        b->getProperties().set("fontSize", 14.0);
        addAndMakeVisible(*b);
    }
    slotA.onClick = [this] { processor.switchSlot(0); };
    slotB.onClick = [this] { processor.switchSlot(1); };

    setupToggle(autoGain, colours::panel);
    autoGain.setClickingTogglesState(true);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, ids::autoGain, autoGain);

    listenRemoved.setButtonText(fr("Écouter le retiré"));
    setupToggle(listenRemoved, colours::panel);
    listenRemoved.onClick = [this] { setChoice(ids::solo, settings.solo == 4 ? 0 : 4); };

    setupToggle(bypass, colours::text);
    bypass.setColour(juce::TextButton::textColourOnId, colours::background);
    bypass.setClickingTogglesState(true);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, ids::bypass, bypass);

    advancedButton.setButtonText(fr("Réglages avancés"));
    setupToggle(advancedButton, colours::line);
    advancedButton.onClick = [this] { advanced = !advanced; resized(); repaint(); };

    for (int v = 0; v < 3; ++v)
    {
        auto& b = voiceButtons[size_t(v)];
        b.setButtonText(fr(vm::voiceTypes()[size_t(v)].name));
        setupToggle(b, colours::text);
        b.setColour(juce::TextButton::textColourOnId, colours::background);
        b.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.getProperties().set("fontSize", 14.0);
        b.onClick = [this, v] { setChoice(ids::voiceType, v); };
        addAndMakeVisible(b);
    }

    for (int z = 0; z < 3; ++z)
    {
        auto& b = zoneButtons[size_t(z)];
        b.setButtonText(fr(zoneNames[z]));
        b.setTitle(fr("Écouter la zone ") + fr(zoneNames[z]).toLowerCase() + fr(" seule"));
        b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b.setColour(juce::TextButton::textColourOffId, colours::text);
        b.setColour(juce::TextButton::textColourOnId, colours::accent);
        b.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        b.getProperties().set("fontSize", 14.0);
        b.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        b.onClick = [this, z] { setChoice(ids::solo, settings.solo == z + 1 ? 0 : z + 1); };
        addAndMakeVisible(b);
    }

    for (int i = 0; i < 4; ++i) profileBox.addItem(fr(vm::profiles()[size_t(i)].name), i + 1);
    profileBox.setTitle("Profil");
    addAndMakeVisible(profileBox);
    profileAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state, ids::profile, profileBox);

    analyse.setColour(juce::TextButton::buttonColourId, colours::accent);
    analyse.setColour(juce::TextButton::textColourOffId, colours::background);
    analyse.setColour(juce::ComboBox::outlineColourId, colours::accent);
    analyse.getProperties().set("fontSize", 15.0);
    analyse.onClick = [this] { processor.startAnalysis(); };
    addAndMakeVisible(analyse);

    reference.setButtonText(fr("Charger une référence · V2"));
    reference.setEnabled(false);
    reference.getProperties().set("dashed", true);
    reference.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    reference.setColour(juce::TextButton::textColourOffId, colours::muted);
    addAndMakeVisible(reference);

    setupKnob(resonanceKnob, ids::resonance, resonanceAttachment);
    setupKnob(deEssKnob, ids::deEss, deEssAttachment);
    setupKnob(intensityKnob, ids::intensity, intensityAttachment);
    setupKnob(outputKnob, ids::output, outputAttachment);
    resonanceKnob.textFromValueFunction = [](double v) { return formatDb(-6.0f * float(v) / 100.0f); };
    deEssKnob.setColour(juce::Slider::rotarySliderFillColourId, colours::blue);
    outputKnob.setColour(juce::Slider::rotarySliderFillColourId, colours::text);
    outputKnob.textFromValueFunction = [](double v) { return formatDb(float(v)); };
    for (auto* k : { &resonanceKnob, &deEssKnob, &intensityKnob, &outputKnob }) k->updateText();

    for (int b = 0; b < vm::numBands; ++b)
    {
        auto& s = manualSliders[size_t(b)];
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setDoubleClickReturnValue(true, 0);
        addChildComponent(s);
        manualAttachments[size_t(b)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, ids::manual(b), s);
    }

    setSize(1120, 758);
    timerCallback();
    startTimerHz(30);
}

VocalMatchEditor::~VocalMatchEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void VocalMatchEditor::setupToggle(juce::TextButton& b, juce::Colour on)
{
    b.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    b.setColour(juce::TextButton::buttonOnColourId, on);
    b.setColour(juce::TextButton::textColourOffId, colours::soft);
    b.setColour(juce::TextButton::textColourOnId, colours::text);
    b.setColour(juce::ComboBox::outlineColourId, colours::line);
    addAndMakeVisible(b);
}

void VocalMatchEditor::setupKnob(juce::Slider& k, const juce::String& id, std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment)
{
    k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    k.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k.setColour(juce::Slider::rotarySliderFillColourId, colours::accent);
    k.setTitle(processor.state.getParameter(id)->getName(64));
    addAndMakeVisible(k);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state, id, k);
    k.setDoubleClickReturnValue(true, double(processor.state.getParameter(id)->convertFrom0to1(processor.state.getParameter(id)->getDefaultValue())));
}

void VocalMatchEditor::setChoice(const juce::String& id, int index)
{
    if (auto* param = processor.state.getParameter(id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(float(index)));
        param->endChangeGesture();
    }
}

void VocalMatchEditor::updateSpectrum(bool processed, std::array<float, numPoints>& target, std::vector<float>& history)
{
    const int got = processor.readScope(processed, scratch.data(), int(history.size()));
    if (got > 0)
    {
        std::move(history.begin() + got, history.end(), history.begin());
        std::copy(scratch.begin(), scratch.begin() + got, history.end() - got);
    }
    const size_t n = history.size();
    std::fill(fftData.begin(), fftData.end(), 0.0f);
    for (size_t i = 0; i < n; ++i)
        fftData[i] = history[i] * (0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * float(i) / float(n - 1)));
    fft.performFrequencyOnlyForwardTransform(fftData.data());
    const double rate = processor.currentRate();
    for (int i = 0; i < numPoints; ++i)
    {
        const float hz = frequencyAt(i);
        const float bin = hz / float(rate) * float(n);
        const int b0 = juce::jlimit(1, int(n / 2) - 2, int(bin));
        const float frac = juce::jlimit(0.0f, 1.0f, bin - float(b0));
        const float mag = fftData[size_t(b0)] * (1 - frac) + fftData[size_t(b0 + 1)] * frac;
        // dBFS for a full-scale sine, plus a 3 dB/octave tilt so a voice reads naturally.
        const float level = juce::Decibels::gainToDecibels(mag * 4.0f / float(n), -120.0f) + 3.0f * std::log2(hz / 1000.0f);
        auto& v = target[size_t(i)];
        v = level > v ? 0.6f * level + 0.4f * v : std::max(level, v - 1.5f);
    }
}

void VocalMatchEditor::timerCallback()
{
    processor.pollAnalysis();
    settings = processor.currentSettings();
    progress = processor.analysisProgress();

    const double rate = processor.currentRate();
    if (rate != previewRate) { previewRate = rate; preview.setSettings(settings); preview.prepare(rate); }
    preview.setSettings(settings);
    preview.applyNow();
    for (int i = 0; i < numPoints; ++i) curve[size_t(i)] = preview.responseDb(frequencyAt(i));

    updateSpectrum(false, spectrumIn, historyIn);
    updateSpectrum(true, spectrumOut, historyOut);

    slotA.setToggleState(processor.activeSlot() == 0, juce::dontSendNotification);
    slotB.setToggleState(processor.activeSlot() == 1, juce::dontSendNotification);
    for (int v = 0; v < 3; ++v) voiceButtons[size_t(v)].setToggleState(settings.voiceType == v, juce::dontSendNotification);
    for (int z = 0; z < 3; ++z) zoneButtons[size_t(z)].setToggleState(settings.solo == z + 1, juce::dontSendNotification);
    advancedButton.setToggleState(advanced, juce::dontSendNotification);

    const bool hasMarkers = settings.analyzed && settings.markerCount > 0 && !processor.isAnalysing();
    listenRemoved.setEnabled(hasMarkers);
    listenRemoved.setToggleState(settings.solo == 4, juce::dontSendNotification);
    listenRemoved.setColour(juce::TextButton::textColourOffId, hasMarkers ? colours::soft : colours::muted);
    listenRemoved.setColour(juce::ComboBox::outlineColourId, settings.solo == 4 ? colours::accent : colours::line);
    listenRemoved.setColour(juce::TextButton::textColourOnId, colours::accent);
    autoGain.setColour(juce::ComboBox::outlineColourId, autoGain.getToggleState() ? colours::blue : colours::line);
    autoGain.setColour(juce::TextButton::textColourOnId, colours::blue);

    analyse.setEnabled(!processor.isAnalysing());
    analyse.setButtonText(processor.isRecording() ? fr("Analyse… ") + juce::String(juce::roundToInt(progress * 100)) + " %"
                          : processor.isAnalysing() ? fr("Calcul…")
                          : settings.analyzed ? fr("Relancer l’analyse") : fr("Analyser ma voix"));
    graph.repaint();
    repaint(juce::Rectangle<int>(0, 66, getWidth(), 112));
    repaint(juce::Rectangle<int>(0, 520, getWidth(), getHeight() - 520));
}

juce::String VocalMatchEditor::statusText() const
{
    const bool bypassed = processor.state.getRawParameterValue(ids::bypass)->load() > 0.5f;
    if (processor.isRecording()) return fr("Analyse : chante ou lance ta piste voix. Seuls les passages avec du son sont comptés.");
    if (processor.isAnalysing()) return fr("Calcul de l’analyse…");
    if (bypassed) return fr("Bypass actif : le son passe sans traitement.");
    if (settings.solo == 4) return fr("Écoute du retiré : si tu y entends le cœur de la voix, baisse Résonances.");
    if (settings.solo >= 1) return fr("Écoute solo : ") + fr(zoneNames[settings.solo - 1]) + fr(". Reclique pour revenir au son complet.");
    if (settings.analyzed) return fr("Réglages proposés à partir de ta voix. Dose-les avec Intensité.");
    return fr("Choisis ton type de voix et ton profil, puis lance l’analyse.");
}

void VocalMatchEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::background);

    // Header.
    g.drawImageWithin(logo, 18, 18, 34, 30, juce::RectanglePlacement::centred);
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("S W E L L", 62, 18, 110, 30, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff3a3f48));
    g.fillRect(176, 21, 1, 24);
    g.setColour(colours::text);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("Vocal Match", 190, 16, 180, 34, juce::Justification::centredLeft);
    g.setColour(colours::line);
    g.fillRect(18, 66, getWidth() - 36, 1);

    // Labels and panels of the control row.
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(colours::muted);
    g.drawText("TYPE DE VOIX", 18, 80, 236, 20, juce::Justification::centredLeft);
    g.drawText("PROFIL", 272, 80, 120, 20, juce::Justification::centredLeft);
    g.drawText(fr(vm::profiles()[size_t(juce::jlimit(0, 3, settings.profile))].description), 392, 80, 180, 20, juce::Justification::centredRight);
    g.setColour(colours::panel);
    g.fillRoundedRectangle(18, 104, 236, 54, 10);
    g.setColour(colours::line);
    g.drawRoundedRectangle(18.5f, 104.5f, 235, 53, 10, 1);

    // Bottom panels.
    auto panel = [&](juce::Rectangle<float> r) { g.setColour(colours::panel); g.fillRoundedRectangle(r, 12); g.setColour(colours::line); g.drawRoundedRectangle(r.reduced(0.5f), 12, 1); };
    panel({ 18, 529, 500, 172 });
    const char* knobTitles[] { "RÉSONANCES", "DE-ESSER (S)", "INTENSITÉ", "SORTIE" };
    for (int k = 0; k < 4; ++k)
    {
        const float x = 532.0f + float(k) * 146.0f;
        panel({ x, 529, 132, 172 });
        g.setColour(colours::muted);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(fr(knobTitles[k]), juce::Rectangle<float>(x, 545, 132, 16), juce::Justification::centred);
    }

    g.setFont(juce::FontOptions(12.0f));
    g.setColour(colours::muted);
    if (!advanced)
    {
        g.drawText("COMPRESSION PAR BANDE", 36, 543, 300, 18, juce::Justification::centredLeft);
        const char* names[] { "Corps", "Présence", "Air" };
        const char* ranges[] { "80–400 Hz", "400 Hz–5 kHz", "5–20 kHz" };
        for (int b = 0; b < 3; ++b)
        {
            const int y = 573 + b * 38;
            const float reduction = processor.gainReductionDb(b);
            g.setColour(colours::text); g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
            g.drawText(fr(names[b]), 36, y, 80, 28, juce::Justification::centredLeft);
            g.setColour(colours::muted); g.setFont(juce::FontOptions(11.0f));
            g.drawText(fr(ranges[b]), 112, y, 90, 28, juce::Justification::centredLeft);
            const auto track = juce::Rectangle<float>(210, float(y) + 9, 200, 10);
            g.setColour(colours::background); g.fillRoundedRectangle(track, 5);
            g.setColour(colours::accent);
            g.fillRoundedRectangle(track.withWidth(track.getWidth() * juce::jlimit(0.0f, 1.0f, reduction / 12.0f)), 5);
            g.setColour(colours::text); g.setFont(juce::FontOptions(13.0f));
            g.drawText(formatDb(-reduction), 416, y, 86, 28, juce::Justification::centredRight);
        }
    }
    else
    {
        g.drawText("CORRECTION MANUELLE", 36, 543, 300, 18, juce::Justification::centredLeft);
        const char* names[] { "Corps", "Bas-médium", "Présence", "Air" };
        for (int b = 0; b < vm::numBands; ++b)
        {
            const int y = 566 + b * 32;
            g.setColour(colours::text); g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText(fr(names[b]), 36, y, 100, 28, juce::Justification::centredLeft);
            g.setFont(juce::FontOptions(13.0f));
            g.drawText(formatDb(float(manualSliders[size_t(b)].getValue())), 416, y, 86, 28, juce::Justification::centredRight);
        }
    }

    // Footer.
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(colours::muted);
    g.drawText(statusText(), 18, 715, 760, 20, juce::Justification::centredLeft);
    g.setColour(colours::text);
    g.drawText("v" JucePlugin_VersionString "  /  by @sevyondatracks", getWidth() - 318, 715, 300, 20, juce::Justification::centredRight);
}

void VocalMatchEditor::resized()
{
    int x = getWidth() - 18;
    auto placeRight = [&](juce::Component& c, int width) { x -= width; c.setBounds(x, 11, width, 44); x -= 8; };
    placeRight(advancedButton, 150);
    placeRight(bypass, 84);
    placeRight(listenRemoved, 140);
    placeRight(autoGain, 110);
    placeRight(slotB, 44);
    x += 8;
    placeRight(slotA, 44);

    for (int v = 0; v < 3; ++v) voiceButtons[size_t(v)].setBounds(23 + v * 76, 109, 74, 44);
    profileBox.setBounds(272, 104, 300, 54);
    analyse.setBounds(590, 104, 252, 54);
    reference.setBounds(850, 104, 252, 54);
    graph.setBounds(18, 172, 1084, 343);
    for (int z = 0; z < 3; ++z)
        zoneButtons[size_t(z)].setBounds(graph.zoneButton(z).toNearestInt().translated(graph.getX(), graph.getY()));

    juce::Slider* knobs[] { &resonanceKnob, &deEssKnob, &intensityKnob, &outputKnob };
    for (int k = 0; k < 4; ++k) knobs[k]->setBounds(532 + k * 146 + 8, 566, 116, 116);

    for (int b = 0; b < vm::numBands; ++b)
    {
        manualSliders[size_t(b)].setBounds(140, 566 + b * 32, 270, 28);
        manualSliders[size_t(b)].setVisible(advanced);
    }
}
