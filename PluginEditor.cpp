#include "PluginEditor.h"
#include "BinaryData.h"

using namespace arcui;

juce::Font arcui::font (float h, bool bold)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font arcui::mono (float h)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain));
}

//==============================================================================
//  Géométrie relevée sur le visuel (pixels de l'image d'origine)
//==============================================================================
namespace
{
    struct KnobGeom { float acx, acy, R, px, py, len; const char* idA; const char* idB; bool white; };

    // centre de l'arc, rayon de l'arc, centre du chapeau (pivot de l'index), longueur de l'index
    const KnobGeom knobGeom[] =
    {
        {  280.0f, 276.3f, 137.7f,  263.2f, 265.0f, 83.0f, "attack",   "release", false },
        {  660.9f, 276.5f, 138.7f,  646.0f, 266.0f, 84.0f, "sustain",  "sens",    false },
        { 1050.4f, 276.1f, 137.5f, 1046.6f, 265.0f, 83.0f, "position", "buzz",    false },
        { 1423.1f, 276.3f, 137.5f, 1425.8f, 265.0f, 83.0f, "tone",     "",        true  },
        {  470.4f, 711.0f, 131.3f,  464.8f, 707.0f, 78.0f, "life",     "glide",   false },
        {  846.7f, 711.6f, 131.9f,  841.9f, 707.0f, 79.0f, "wet",      "dry",     false },
        { 1239.2f, 712.0f, 132.2f, 1236.9f, 707.0f, 79.0f, "space",    "output",  false },
    };

    struct Led { float x, y, lens, bezel; };
    const Led ledMode   { 288.0f, 1016.0f, 18.0f, 24.0f };
    const Led ledVoice[] = { { 559.0f, 1136.0f, 13.0f, 17.0f }, { 905.0f, 1136.0f, 13.0f, 17.0f }, { 1234.0f, 1136.0f, 13.0f, 17.0f } };
    const Led ledPreset[] =
    {
        { 219.5f, 1941.0f, 11.0f, 15.0f }, { 563.0f, 1941.0f, 11.0f, 15.0f }, { 907.5f, 1941.0f, 11.0f, 15.0f }, { 1246.0f, 1941.0f, 11.0f, 15.0f },
        { 219.0f, 2041.5f, 11.0f, 15.0f }, { 564.0f, 2041.5f, 11.0f, 15.0f }, { 908.5f, 2041.5f, 11.0f, 15.0f }, { 1246.5f, 2041.5f, 11.0f, 15.0f },
    };
    const Led ledBow  { 423.0f, 2136.0f, 17.0f, 23.0f };
    const Led ledHold { 1241.0f, 2136.0f, 17.0f, 23.0f };

    const juce::Rectangle<int> voiceRects[] = { { 516, 1102, 322, 74 }, { 869, 1102, 301, 74 }, { 1200, 1102, 277, 74 } };
    const juce::Rectangle<int> presetRects[] =
    {
        { 174, 1902, 319, 79 }, { 521, 1902, 314, 79 }, { 866, 1902, 308, 79 }, { 1202, 1902, 325, 79 },
        { 174, 2003, 319, 79 }, { 521, 2003, 314, 79 }, { 866, 2003, 308, 79 }, { 1202, 2003, 325, 79 },
    };
    const juce::Rectangle<float> lcdRect     { 176.0f, 1528.0f, 1332.0f, 68.0f };
    const juce::Rectangle<float> displayRect { 170.0f, 1628.0f, 1346.0f, 190.0f };
    const juce::Rectangle<float> presetName  { 437.0f, 1850.0f, 760.0f, 40.0f };

    const char* paramIds[] = { "attack", "release", "sustain", "sens", "position", "buzz", "tone",
                               "life", "glide", "wet", "dry", "space", "output",
                               "mode", "layer", "bow", "hold", "feedback" };
    constexpr int numParamIds = (int) (sizeof (paramIds) / sizeof (paramIds[0]));
}

//==============================================================================
//  Potards : arc de valeur (orange ou bleu selon MODE, blanc pour TONE) + index
//==============================================================================
void ArcLookAndFeel::drawRotarySlider (juce::Graphics& g, int, int, int w, int h, float pos,
                                       float startAngle, float endAngle, juce::Slider& s)
{
    const auto& p = s.getProperties();
    const float R   = (float) (double) p.getWithDefault ("R", 130.0);
    const float pdx = (float) (double) p.getWithDefault ("pdx", 0.0);
    const float pdy = (float) (double) p.getWithDefault ("pdy", 0.0);
    const float len = (float) (double) p.getWithDefault ("len", 80.0);
    const bool white = p.getWithDefault ("white", false);
    const bool blueArc = p.getWithDefault ("blue", false);

    const float cx = (float) w * 0.5f, cy = (float) h * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const juce::Colour col = white ? juce::Colour (0xffffffff) : (blueArc ? blue : orange);
    const auto rounded = [] (float wd) { return juce::PathStrokeType (wd, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

    // piste éteinte : sombre, pour que l'arc allumé ressorte
    juce::Path track;
    track.addCentredArc (cx, cy, R, R, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (track, rounded (14.0f));

    // arc de valeur : halo large + halo serré + trait vif + cœur clair (effet LED)
    if (pos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (cx, cy, R, R, 0.0f, startAngle, angle, true);
        g.setColour (col.withAlpha (0.16f));
        g.strokePath (value, rounded (22.0f));
        g.setColour (col);
        g.strokePath (value, rounded (13.0f));
        g.setColour (col.interpolatedWith (juce::Colours::white, 0.55f));
        g.strokePath (value, rounded (4.5f));
    }

    // index noir sur le chapeau
    const juce::Point<float> pivot (cx + pdx, cy + pdy);
    const auto tip = pivot.getPointOnCircumference (len, angle);
    const auto base = pivot.getPointOnCircumference (len * 0.06f, angle);
    g.setColour (juce::Colour (0xff0d0e10));
    g.drawLine ({ base, tip }, 10.0f);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (tip));
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (base));
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawLine ({ base.translated (-3.0f, 0.0f), tip.translated (-3.0f, 0.0f) }, 1.5f);
}

//==============================================================================
HotSpot::HotSpot (const juce::String& name, bool circular, bool toggle)
    : juce::Button (name), circle (circular)
{
    setClickingTogglesState (toggle);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

bool HotSpot::hitTest (int x, int y)
{
    if (! circle) return true;
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= (float) getWidth() * 0.5f;
}

void HotSpot::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // le bouton est déjà dessiné dans le visuel : on ne fait que l'ombrer quand on appuie
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const float alpha = down ? 0.28f : (highlighted ? 0.06f : 0.0f);
    if (alpha <= 0.0f) return;
    g.setColour (down ? juce::Colours::black.withAlpha (alpha) : juce::Colours::white.withAlpha (alpha));
    if (circle) g.fillEllipse (r);
    else        g.fillRoundedRectangle (r, 10.0f);
}

//==============================================================================
//  Panneau
//==============================================================================
ArcPanel::ArcPanel (InfiniteBowProcessor& p) : proc (p)
{
    background = juce::ImageCache::getFromMemory (BinaryData::background_jpg, BinaryData::background_jpgSize);
    setOpaque (true);
    auto& st = proc.apvts;

    auto makeSlider = [this, &st] (const KnobGeom& k, const juce::String& id, bool blueArc)
    {
        auto s = std::make_unique<juce::Slider>();
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        s->setMouseDragSensitivity (260);
        s->setLookAndFeel (&lnf);
        s->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        auto& pr = s->getProperties();
        pr.set ("R", k.R);
        pr.set ("pdx", k.px - k.acx);
        pr.set ("pdy", k.py - k.acy);
        pr.set ("len", k.len);
        pr.set ("white", k.white);
        pr.set ("blue", blueArc);
        if (auto* prm = st.getParameter (id))
            s->setDoubleClickReturnValue (true, (double) prm->convertFrom0to1 (prm->getDefaultValue()));
        return s;
    };

    for (const auto& k : knobGeom)
    {
        auto slot = std::make_unique<KnobSlot>();
        slot->idA = k.idA; slot->idB = k.idB;
        slot->a = makeSlider (k, k.idA, false);
        slot->attA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, k.idA, *slot->a);
        addAndMakeVisible (*slot->a);
        if (slot->idB.isNotEmpty())
        {
            slot->b = makeSlider (k, k.idB, true);
            slot->attB = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, k.idB, *slot->b);
            addChildComponent (*slot->b);
        }
        knobs.push_back (std::move (slot));
    }

    // bouton MODE
    addAndMakeVisible (modeSpot);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, "layer", modeSpot);
    modeSpot.onClick = [this] { setLayer (modeSpot.getToggleState()); };

    // VOICE
    for (int i = 0; i < 3; ++i)
    {
        auto* b = voiceSpots.add (new HotSpot ("VOICE " + juce::String (i + 1), false));
        b->onClick = [this, i] { if (voiceAtt != nullptr) voiceAtt->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    if (auto* vp = st.getParameter ("mode"))
    {
        voiceAtt = std::make_unique<juce::ParameterAttachment> (*vp, [this] (float v)
        {
            voiceIndex = juce::jlimit (0, 2, juce::roundToInt (v));
            repaint();
        });
        voiceAtt->sendInitialUpdate();
    }

    // presets
    for (int i = 0; i < InfiniteBowProcessor::numFactoryPresets; ++i)
    {
        auto* b = presetSpots.add (new HotSpot ("PRESET " + juce::String (i + 1), false));
        b->onClick = [this, i]
        {
            proc.setCurrentProgram (i);
            proc.updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
            repaint();
        };
        addAndMakeVisible (b);
    }

    // footswitches
    addAndMakeVisible (bowSpot);
    addAndMakeVisible (holdSpot);
    bowAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, "bow", bowSpot);
    holdAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, "hold", holdSpot);
    bowSpot.onClick  = [this] { repaint(); };
    holdSpot.onClick = [this] { repaint(); };

    lastValues.assign ((size_t) numParamIds, -1.0f);
    for (int i = 0; i < numParamIds; ++i)
        if (auto* prm = st.getParameter (paramIds[i]))
            lastValues[(size_t) i] = prm->getValue();

    layerB = ! modeSpot.getToggleState();   // force la mise à jour
    setLayer (modeSpot.getToggleState());

    setSize (imgW, imgH);
    startTimerHz (30);
}

ArcPanel::~ArcPanel()
{
    stopTimer();
    for (auto& k : knobs)
    {
        if (k->a) k->a->setLookAndFeel (nullptr);
        if (k->b) k->b->setLookAndFeel (nullptr);
    }
}

void ArcPanel::resized()
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto& k = knobGeom[i];
        const int half = (int) std::ceil (k.R + 24.0f);
        const juce::Rectangle<int> r ((int) std::lround (k.acx) - half, (int) std::lround (k.acy) - half, half * 2, half * 2);
        knobs[i]->a->setBounds (r);
        if (knobs[i]->b) knobs[i]->b->setBounds (r);
    }

    modeSpot.setBounds (juce::Rectangle<int> (100, 100).withCentre ({ 288, 1112 }));
    for (int i = 0; i < 3; ++i) voiceSpots[i]->setBounds (voiceRects[i]);
    for (int i = 0; i < presetSpots.size(); ++i) presetSpots[i]->setBounds (presetRects[i]);
    bowSpot.setBounds  (juce::Rectangle<int> (224, 224).withCentre ({ 423, 2290 }));
    holdSpot.setBounds (juce::Rectangle<int> (224, 224).withCentre ({ 1241, 2290 }));
}

void ArcPanel::setLayer (bool blueLayer)
{
    if (layerB == blueLayer) return;
    layerB = blueLayer;
    for (auto& k : knobs)
        if (k->b != nullptr)
        {
            k->a->setVisible (! layerB);
            k->b->setVisible (layerB);
        }
    repaint();
}

//==============================================================================
void ArcPanel::drawLed (juce::Graphics& g, juce::Point<float> c, float lens, float bezel,
                        juce::Colour col, float intensity)
{
    // halo : large lueur douce + lueur serrée et intense
    if (intensity > 0.01f)
    {
        const float hr = bezel * 3.0f;
        juce::ColourGradient halo (col.withAlpha (0.40f * intensity), c.x, c.y,
                                   col.withAlpha (0.0f), c.x + hr, c.y, true);
        halo.addColour (0.35, col.withAlpha (0.14f * intensity));
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (hr * 2.0f, hr * 2.0f).withCentre (c));

        const float hr2 = bezel * 1.6f;
        juce::ColourGradient core (col.withAlpha (0.60f * intensity), c.x, c.y,
                                   col.withAlpha (0.0f), c.x + hr2, c.y, true);
        g.setGradientFill (core);
        g.fillEllipse (juce::Rectangle<float> (hr2 * 2.0f, hr2 * 2.0f).withCentre (c));
    }

    // embase
    g.setColour (juce::Colour (0xff08090b));
    g.fillEllipse (juce::Rectangle<float> (bezel * 2.0f, bezel * 2.0f).withCentre (c));
    g.setColour (intensity > 0.01f ? col.withAlpha (0.55f * intensity) : juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (juce::Rectangle<float> (bezel * 2.0f, bezel * 2.0f).withCentre (c).reduced (0.5f), 1.6f);

    // lentille
    const auto lr = juce::Rectangle<float> (lens * 2.0f, lens * 2.0f).withCentre (c);
    if (intensity > 0.01f)
    {
        const auto hot = col.interpolatedWith (juce::Colours::white, 0.85f);
        juce::ColourGradient lit (hot, c.x, c.y, col.withMultipliedSaturation (1.2f), c.x + lens, c.y, true);
        lit.addColour (0.45, col.interpolatedWith (juce::Colours::white, 0.35f));
        g.setGradientFill (lit);
        g.fillEllipse (lr);
        if (intensity < 0.999f)
        {
            g.setColour (col.withMultipliedBrightness (0.20f).withAlpha (1.0f - intensity));
            g.fillEllipse (lr);
        }
    }
    else
    {
        // éteinte : lentille sombre, nettement moins lumineuse pour un contraste franc
        juce::ColourGradient off (col.withMultipliedBrightness (0.30f).withMultipliedSaturation (0.6f), c.x, c.y - lens,
                                  col.withMultipliedBrightness (0.08f), c.x, c.y + lens, false);
        g.setGradientFill (off);
        g.fillEllipse (lr);
    }
    // reflet
    g.setColour (juce::Colours::white.withAlpha (intensity > 0.01f ? 0.70f : 0.16f));
    g.fillEllipse (juce::Rectangle<float> (lens * 0.55f, lens * 0.4f).withCentre ({ c.x - lens * 0.35f, c.y - lens * 0.42f }));
}

static juce::Image downscale (const juce::Image& src, float scale)
{
    const int tw = juce::jmax (1, juce::roundToInt ((float) src.getWidth() * scale));
    const int th = juce::jmax (1, juce::roundToInt ((float) src.getHeight() * scale));
    juce::Image img = src;
    while (img.getWidth() > tw * 2)
        img = img.rescaled (img.getWidth() / 2, img.getHeight() / 2, juce::Graphics::highResamplingQuality);
    return img.rescaled (tw, th, juce::Graphics::highResamplingQuality);
}

void ArcPanel::paint (juce::Graphics& g)
{
    // fond : version réduite à la taille réelle à l'écran (net et sans crénelage)
    const float phys = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (background.isValid())
    {
        if (std::abs (phys - scaledFor) > 0.001f || ! scaledBackground.isValid())
        {
            scaledBackground = phys < 0.999f ? downscale (background, phys) : background;
            scaledFor = phys;
        }
        const float inv = (float) imgW / (float) scaledBackground.getWidth();
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageTransformed (scaledBackground, juce::AffineTransform::scale (inv));
    }
    else
        g.fillAll (juce::Colour (0xff2a2d36));

    const juce::Colour layerCol = layerB ? blue : orange;

    // LED du bouton MODE : orange ou bleue
    drawLed (g, { ledMode.x, ledMode.y }, ledMode.lens, ledMode.bezel, layerCol, 1.0f);

    // VOICE
    for (int i = 0; i < 3; ++i)
        drawLed (g, { ledVoice[i].x, ledVoice[i].y }, ledVoice[i].lens, ledVoice[i].bezel,
                 i == 0 ? orange : ledCyan, i == voiceIndex ? 1.0f : 0.0f);

    // presets
    const int cur = proc.currentPreset.load();
    for (int i = 0; i < InfiniteBowProcessor::numFactoryPresets; ++i)
        drawLed (g, { ledPreset[i].x, ledPreset[i].y }, ledPreset[i].lens, ledPreset[i].bezel,
                 i < 4 ? orange : ledCyan, i == cur ? 1.0f : 0.0f);

    // footswitches : BOW respire avec la corde, HOLD (bouton ou pédale CC64)
    const bool bowOn  = proc.apvts.getRawParameterValue ("bow")->load() > 0.5f;
    const bool holdOn = proc.apvts.getRawParameterValue ("hold")->load() > 0.5f || proc.midiHold.load();
    drawLed (g, { ledBow.x, ledBow.y }, ledBow.lens, ledBow.bezel, orange,
             bowOn ? 0.55f + 0.45f * shownLevel : 0.0f);
    drawLed (g, { ledHold.x, ledHold.y }, ledHold.lens, ledHold.bezel, ledCyan, holdOn ? 1.0f : 0.0f);

    // ligne d'état : couleur du mode
    g.setColour (layerCol);
    g.setFont (mono (34.0f));
    g.drawText (statusText(), lcdRect.reduced (24.0f, 0.0f), juce::Justification::centred, true);

    drawDisplay (g);
    drawLogo (g);

    // nom du preset
    g.setColour (textCol);
    g.setFont (font (31.0f));
    g.drawText (juce::String (cur + 1) + "  " + juce::String (InfiniteBowProcessor::factoryPresets[cur].name).toUpperCase(),
                presetName, juce::Justification::centredLeft, false);
}

void ArcPanel::drawDisplay (juce::Graphics& g)
{
    const float nutX = 210.0f, bridgeX = 1104.0f, cy = 1718.0f;

    // sillet et chevalet
    g.setColour (juce::Colour (0xffe6e7e9));
    g.fillRoundedRectangle (nutX, 1675.0f, 16.0f, 86.0f, 6.0f);
    g.fillRoundedRectangle (bridgeX, 1675.0f, 16.0f, 86.0f, 6.0f);

    // corde en onde stationnaire (1, 2 ou 3 ventres selon VOICE)
    const float x0 = nutX + 16.0f, x1 = bridgeX;
    const float k = (float) (voiceIndex + 1);
    const float amp = shownLevel * 34.0f;
    juce::Path p;
    for (int i = 0; i <= 160; ++i)
    {
        const float u = (float) i / 160.0f;
        const float shape = std::sin (juce::MathConstants<float>::pi * k * u);
        const float ripple = 0.18f * std::sin (juce::MathConstants<float>::pi * (2.0f * k + 1.0f) * u) * (float) std::sin (phase * 2.3);
        const float y = cy + amp * (shape * (float) std::sin (phase) + ripple);
        const float x = x0 + u * (x1 - x0);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (ledCyan.withAlpha (0.10f + 0.25f * shownLevel));
    g.strokePath (p, juce::PathStrokeType (16.0f));
    g.setColour (ledCyan);
    g.strokePath (p, juce::PathStrokeType (5.0f));

    // archet placé selon POSITION (depuis le chevalet)
    const float posFrac = 0.04f + 0.46f * proc.apvts.getRawParameterValue ("position")->load();
    const float bx = x1 - posFrac * (x1 - x0);
    const auto box = juce::Rectangle<float> (101.0f, 125.0f).withCentre ({ bx, cy });
    const bool bowing = proc.uiState.load() == 2;
    g.setColour (juce::Colour (0xff0b0c10).withAlpha (0.92f));
    g.fillRoundedRectangle (box, 12.0f);
    g.setColour (bowing ? noteOrg : noteOrg.withAlpha (0.45f));
    g.drawRoundedRectangle (box, 12.0f, bowing ? 5.0f : 3.0f);
    g.setFont (font (28.0f));
    g.drawText ("BOW", box, juce::Justification::centred, false);

    // séparateur
    g.setColour (juce::Colour (0xff2c2f36));
    g.fillRect (1159.0f, 1650.0f, 2.5f, 146.0f);

    // note détectée
    const float hz = proc.uiPitchHz.load();
    g.setColour (hz > 0.0f ? noteOrg : juce::Colour (0xff6c7380));
    g.setFont (font (74.0f, true));
    g.drawText (hz > 0.0f ? noteName (hz) : "--", juce::Rectangle<float> (1200.0f, 1648.0f, 300.0f, 72.0f),
                juce::Justification::centredLeft, false);
    g.setColour (textCol);
    g.setFont (mono (31.0f));
    g.drawText (hz > 0.0f ? juce::String (hz, 1) + " Hz" : "no pitch", juce::Rectangle<float> (1203.0f, 1722.0f, 300.0f, 36.0f),
                juce::Justification::centredLeft, false);
    const char* states[] = { "IDLE", "LISTENING", "BOWING", "RELEASE" };
    juce::String stateText = states[juce::jlimit (0, 3, proc.uiState.load())];
    const bool larsen = proc.uiFeedback.load() > 0.25f;
    if (larsen)
        stateText = "FEEDBACK x" + juce::String (proc.uiFbHarm.load());
    g.setColour (larsen ? noteOrg : (bowing ? ledCyan : juce::Colour (0xff6c7380)));
    g.drawText (stateText, juce::Rectangle<float> (1203.0f, 1762.0f, 300.0f, 36.0f),
                juce::Justification::centredLeft, false);
}

// Logo Studio Wiga entre les deux footswitches : (o)(o)  STUDIO WIGA, cercle gauche orange
void ArcPanel::drawLogo (juce::Graphics& g)
{
    const juce::Font big (juce::FontOptions (41.0f).withKerningFactor (0.10f));
    const float wBig = juce::GlyphArrangement::getStringWidth (big, "STUDIO WIGA");
    const float d = 38.0f, ring = 5.0f, overlap = d * 0.62f, gap = 17.0f;
    const float total = (overlap + d) + gap + wBig;
    const float cx = 832.0f, cy = 2290.0f;
    float x = cx - total * 0.5f;

    g.setColour (orange);
    g.drawEllipse (juce::Rectangle<float> (d, d).withPosition (x, cy - d * 0.5f).reduced (ring * 0.5f), ring);
    g.setColour (textCol.withAlpha (0.9f));
    g.drawEllipse (juce::Rectangle<float> (d, d).withPosition (x + overlap, cy - d * 0.5f).reduced (ring * 0.5f), ring);
    x += overlap + d + gap;

    g.setFont (big);
    g.drawText ("STUDIO WIGA", juce::Rectangle<float> (x, cy - 26.0f, wBig + 8.0f, 52.0f), juce::Justification::centredLeft, false);
}

juce::String ArcPanel::noteName (float hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int midi = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}

juce::String ArcPanel::statusText() const
{
    juce::String s;
    if (lastParamId == "layer")
        s << "MODE  " << (layerB ? "BLUE" : "ORANGE");
    else if (auto* prm = proc.apvts.getParameter (lastParamId))
        s << prm->getName (32).toUpperCase() << "  " << prm->getCurrentValueAsText()
          << "  (MIDI " << juce::roundToInt (prm->getValue() * 127.0f) << ")";
    if (lastCC >= 0)
        s << "  |  CC" << lastCC << "=" << lastCCVal;
    if (proc.midiHold.load())
        s << "  |  HOLD";
    return s;
}

void ArcPanel::timerCallback()
{
    // quel réglage vient de bouger (souris, automation, MIDI du DAW) ?
    for (int i = 0; i < numParamIds; ++i)
        if (auto* prm = proc.apvts.getParameter (paramIds[i]))
        {
            const float v = prm->getValue();
            if (std::abs (v - lastValues[(size_t) i]) > 1.0e-5f)
            {
                lastValues[(size_t) i] = v;
                lastParamId = paramIds[i];
            }
        }

    // MODE peut aussi venir du DAW (automation / MIDI)
    setLayer (proc.apvts.getRawParameterValue ("layer")->load() > 0.5f);

    lastCC = proc.uiMidiCC.load();
    lastCCVal = proc.uiMidiVal.load();

    shownLevel += (proc.uiLevel.load() - shownLevel) * 0.25f;
    phase += 0.9;
    if (phase > 1000.0) phase -= 1000.0;

    // zones animées : LED, ligne d'état, écran, nom du preset
    repaint (lcdRect.toNearestInt().expanded (4));
    repaint (displayRect.toNearestInt());
    repaint (presetName.toNearestInt());
    repaint (juce::Rectangle<int> (130, 130).withCentre ({ (int) ledMode.x, (int) ledMode.y }));
    repaint (juce::Rectangle<int> (1400, 120).withCentre ({ 900, (int) ledVoice[0].y }));
    repaint (juce::Rectangle<int> (1400, 180).withCentre ({ 860, 1991 }));
    repaint (juce::Rectangle<int> (150, 150).withCentre ({ (int) ledBow.x, (int) ledBow.y }));
    repaint (juce::Rectangle<int> (150, 150).withCentre ({ (int) ledHold.x, (int) ledHold.y }));
}

//==============================================================================
//  Éditeur : met la pédale à l'échelle (menu clic droit)
//==============================================================================
InfiniteBowEditor::InfiniteBowEditor (InfiniteBowProcessor& p)
    : AudioProcessorEditor (&p), proc (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.addMouseListener (this, true);

    // limites de taille imposées au DAW : de la taille par défaut (60 %) à 100 %, proportions fixes
    const int minW = juce::roundToInt ((float) imgW * baseScale * 0.6f), minH = juce::roundToInt ((float) imgH * baseScale * 0.6f);
    const int maxW = juce::roundToInt ((float) imgW * baseScale),        maxH = juce::roundToInt ((float) imgH * baseScale);
    setResizable (false, false);
    setResizeLimits (minW, minH, maxW, maxH);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) imgW / (double) imgH);

    applyScale (proc.uiScale.load());
}

void InfiniteBowEditor::applyScale (float s)
{
    s = juce::jlimit (0.6f, 1.0f, s);           // jamais plus petit que la taille par défaut
    proc.uiScale.store (s);
    const float total = baseScale * s;
    panel.setTransform (juce::AffineTransform::scale (total));
    setSize (juce::roundToInt ((float) imgW * total), juce::roundToInt ((float) imgH * total));
}

void InfiniteBowEditor::resized()
{
    panel.setBounds (0, 0, imgW, imgH);

    // si le DAW impose une taille, on s'y adapte sans jamais descendre sous la taille par défaut
    const float s = juce::jlimit (0.6f, 1.0f, (float) getWidth() / ((float) imgW * baseScale));
    panel.setTransform (juce::AffineTransform::scale (baseScale * s));
    proc.uiScale.store (s);
    const int minW = juce::roundToInt ((float) imgW * baseScale * 0.6f);
    if (getWidth() < minW - 1)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<InfiniteBowEditor> (this)]
                                         { if (safe != nullptr) safe->applyScale (0.6f); });
}

void InfiniteBowEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    const float cur = proc.uiScale.load();
    juce::PopupMenu m;
    m.addSectionHeader ("Window size");
    const float sizes[] = { 0.6f, 0.75f, 1.0f };
    const char* labels[] = { "60 % (default)", "75 %", "100 %" };
    for (int i = 0; i < 3; ++i)
        m.addItem (i + 1, labels[i], true, std::abs (cur - sizes[i]) < 0.01f);

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                     [safe = juce::Component::SafePointer<InfiniteBowEditor> (this), sizes] (int r)
                     {
                         if (safe != nullptr && r > 0)
                             safe->applyScale (sizes[r - 1]);
                     });
}
