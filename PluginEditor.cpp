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
        {   182.7f,  204.5f, 83.5f,   175.3f,  198.4f, 55.7f, "attack", "release", false },
        {   376.3f,  204.5f, 83.4f,   371.0f,  198.4f, 55.7f, "sustain", "sens", false },
        {   568.7f,  204.6f, 83.5f,   564.9f,  198.4f, 55.7f, "position", "buzz", false },
        {   763.0f,  201.1f, 82.5f,   763.3f,  198.4f, 55.7f, "tone", "", true },
        {   954.6f,  204.3f, 83.5f,   956.3f,  198.4f, 55.7f, "life", "glide", false },
        {  1147.5f,  204.4f, 83.0f,  1151.9f,  198.4f, 55.7f, "wet", "dry", false },
        {  1340.2f,  204.2f, 83.2f,  1345.8f,  198.4f, 55.7f, "space", "output", false },
    };

    struct Led { float x, y, lens, bezel; };
    const Led ledMode   { 183.0f, 433.0f, 12.0f, 16.0f };
    const Led ledVoice[] = { { 440.3f, 487.5f, 9.5f, 12.5f }, { 698.4f, 487.5f, 9.5f, 12.5f }, { 940.8f, 487.5f, 9.5f, 12.5f } };
    const Led ledPreset[] =
    {
        { 398.5f, 909.5f, 8.5f, 11.0f }, { 498.5f, 909.5f, 8.5f, 11.0f }, { 598.5f, 909.5f, 8.5f, 11.0f }, { 698.5f, 909.5f, 8.5f, 11.0f },
        { 798.5f, 909.5f, 8.5f, 11.0f }, { 898.5f, 909.5f, 8.5f, 11.0f }, { 998.5f, 909.5f, 8.5f, 11.0f }, { 1098.5f, 909.5f, 8.5f, 11.0f },
    };
    const Led ledBow  { 239.2f, 1007.5f, 12.5f, 16.5f };
    const Led ledHold { 1280.8f, 1010.0f, 12.5f, 16.5f };

    const juce::Rectangle<int> voiceRects[] = { { 409, 461, 235, 54 }, { 671, 461, 220, 54 }, { 915, 461, 210, 54 } };
    const juce::Rectangle<int> presetRects[] =   // LED + chiffre
    {
        { 380, 889, 62, 40 }, { 480, 889, 62, 40 }, { 580, 889, 62, 40 }, { 680, 889, 62, 40 },
        { 780, 889, 62, 40 }, { 880, 889, 62, 40 }, { 980, 889, 62, 40 }, { 1080, 889, 62, 40 },
    };
    const juce::Rectangle<float> lcdRect     { 264.0f, 594.0f, 995.0f, 41.0f };
    const juce::Rectangle<float> displayRect { 264.0f, 658.0f, 995.0f, 115.0f };

    const juce::Point<int> modeCentre { 183, 489 }, bowCentre { 240, 1090 }, holdCentre { 1283, 1090 };
    // écran de corde : décalage par rapport à la v2.0
    constexpr float dispDX = -40.0f, dispDY = -57.0f;

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

    // piste éteinte : sombre, discrète
    juce::Path track;
    track.addCentredArc (cx, cy, R, R, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.strokePath (track, rounded (9.0f));

    // arc de valeur : halo + trait vif + cœur clair (effet LED)
    if (pos > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (cx, cy, R, R, 0.0f, startAngle, angle, true);
        g.setColour (col.withAlpha (0.16f));
        g.strokePath (value, rounded (13.0f));
        g.setColour (col);
        g.strokePath (value, rounded (6.0f));
        g.setColour (col.interpolatedWith (juce::Colours::white, 0.55f));
        g.strokePath (value, rounded (2.0f));
    }

    // index blanc sur le chapeau (comme sur le visuel)
    const juce::Point<float> pivot (cx + pdx, cy + pdy);
    const auto tip = pivot.getPointOnCircumference (len, angle);
    const auto base = pivot.getPointOnCircumference (len * 0.06f, angle);
    juce::Path ptr;
    ptr.startNewSubPath (base);
    ptr.lineTo (tip);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (ptr, rounded (6.0f), juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour (juce::Colour (0xffeceef0));
    g.strokePath (ptr, rounded (4.0f));
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

    modeSpot.setBounds (juce::Rectangle<int> (72, 72).withCentre (modeCentre));
    for (int i = 0; i < 3; ++i) voiceSpots[i]->setBounds (voiceRects[i]);
    for (int i = 0; i < presetSpots.size(); ++i) presetSpots[i]->setBounds (presetRects[i]);
    bowSpot.setBounds  (juce::Rectangle<int> (132, 132).withCentre (bowCentre));
    holdSpot.setBounds (juce::Rectangle<int> (132, 132).withCentre (holdCentre));
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
    // halo : lueur qui décroît vite (comme une vraie LED sur une plaque mate), sans disque coloré net
    if (intensity > 0.01f)
    {
        const float hr = bezel * 2.5f;
        juce::ColourGradient bloom (col.withAlpha (0.34f * intensity), c.x, c.y,
                                    col.withAlpha (0.0f), c.x + hr, c.y, true);
        bloom.addColour (0.22, col.withAlpha (0.20f * intensity));
        bloom.addColour (0.45, col.withAlpha (0.07f * intensity));
        bloom.addColour (0.70, col.withAlpha (0.018f * intensity));
        g.setGradientFill (bloom);
        g.fillEllipse (juce::Rectangle<float> (hr * 2.0f, hr * 2.0f).withCentre (c));

        const float hr2 = bezel * 1.35f;
        juce::ColourGradient core (col.withAlpha (0.50f * intensity), c.x, c.y,
                                   col.withAlpha (0.0f), c.x + hr2, c.y, true);
        core.addColour (0.6, col.withAlpha (0.22f * intensity));
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
    g.setFont (mono (25.0f));
    g.drawText (statusText(), lcdRect.reduced (16.0f, 0.0f), juce::Justification::centred, true);

    drawDisplay (g);     // le logo Studio Wiga fait partie du visuel

    // (le nom du preset n'est plus affiché sur la pédale : il reste visible dans le DAW)
}

void ArcPanel::drawDisplay (juce::Graphics& g)
{
    // l'écran a été déplacé dans la v2.4 : on dessine dans le repère de la v2.0, décalé
    juce::Graphics::ScopedSaveState sss (g);
    g.addTransform (juce::AffineTransform::translation (dispDX, dispDY));

    const float nutX = 325.0f, bridgeX = 1005.0f, cy = 772.0f;

    // sillet et chevalet
    g.setColour (juce::Colour (0xffe6e7e9));
    g.fillRoundedRectangle (nutX, cy - 27.0f, 10.0f, 54.0f, 4.0f);
    g.fillRoundedRectangle (bridgeX, cy - 27.0f, 10.0f, 54.0f, 4.0f);

    // corde en onde stationnaire (1, 2 ou 3 ventres selon VOICE)
    const float x0 = nutX + 10.0f, x1 = bridgeX;
    const float k = (float) (voiceIndex + 1);
    const float amp = shownLevel * 21.0f;
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
    g.strokePath (p, juce::PathStrokeType (10.0f));
    g.setColour (ledCyan);
    g.strokePath (p, juce::PathStrokeType (3.2f));

    // archet placé selon POSITION (depuis le chevalet)
    const float posFrac = 0.04f + 0.46f * proc.apvts.getRawParameterValue ("position")->load();
    const float bx = x1 - posFrac * (x1 - x0);
    const auto box = juce::Rectangle<float> (64.0f, 78.0f).withCentre ({ bx, cy });
    const bool bowing = proc.uiState.load() == 2;
    g.setColour (juce::Colour (0xff0b0c10).withAlpha (0.92f));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (bowing ? noteOrg : noteOrg.withAlpha (0.45f));
    g.drawRoundedRectangle (box, 8.0f, bowing ? 3.2f : 2.0f);
    g.setFont (font (18.0f));
    g.drawText ("BOW", box, juce::Justification::centred, false);

    // séparateur
    g.setColour (juce::Colour (0xff2c2f36));
    g.fillRect (1040.0f, 727.0f, 1.8f, 91.0f);

    // note détectée
    const float hz = proc.uiPitchHz.load();
    g.setColour (hz > 0.0f ? noteOrg : juce::Colour (0xff6c7380));
    g.setFont (font (46.0f, true));
    g.drawText (hz > 0.0f ? noteName (hz) : "--", juce::Rectangle<float> (1062.0f, 721.0f, 230.0f, 48.0f),
                juce::Justification::centredLeft, false);
    g.setColour (textCol);
    g.setFont (mono (20.0f));
    g.drawText (hz > 0.0f ? juce::String (hz, 1) + " Hz" : "no pitch", juce::Rectangle<float> (1064.0f, 771.0f, 230.0f, 24.0f),
                juce::Justification::centredLeft, false);
    const char* states[] = { "IDLE", "LISTENING", "BOWING", "RELEASE" };
    juce::String stateText = states[juce::jlimit (0, 3, proc.uiState.load())];
    const bool bypassed = proc.apvts.getRawParameterValue ("bow")->load() < 0.5f;
    if (bypassed)
        stateText = "BYPASS";
    const bool larsen = ! bypassed && proc.uiFeedback.load() > 0.25f;
    if (larsen)
        stateText = "FEEDBACK x" + juce::String (proc.uiFbHarm.load());
    g.setColour (larsen ? noteOrg : (bowing ? ledCyan : juce::Colour (0xff6c7380)));
    g.drawText (stateText, juce::Rectangle<float> (1064.0f, 795.0f, 230.0f, 24.0f),
                juce::Justification::centredLeft, false);
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
    repaint (juce::Rectangle<int> (110, 110).withCentre ({ (int) ledMode.x, (int) ledMode.y }));
    repaint (juce::Rectangle<int> (820, 90).withCentre ({ 690, (int) ledVoice[0].y }));
    repaint (juce::Rectangle<int> (820, 80).withCentre ({ 748, (int) ledPreset[0].y }));
    repaint (juce::Rectangle<int> (110, 110).withCentre ({ (int) ledBow.x, (int) ledBow.y }));
    repaint (juce::Rectangle<int> (110, 110).withCentre ({ (int) ledHold.x, (int) ledHold.y }));
}

//==============================================================================
//  Éditeur : met la pédale à l'échelle (menu clic droit)
//==============================================================================
InfiniteBowEditor::InfiniteBowEditor (InfiniteBowProcessor& p)
    : AudioProcessorEditor (&p), proc (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.addMouseListener (this, true);

    // taille par défaut (60 %) = taille minimale ; 100 % = taille maximale.
    // Entre les deux, la fenêtre se redimensionne librement (en gardant les proportions),
    // à la souris ou avec le menu clic droit (60 / 75 / 100 %).
    // La fenêtre du DAW elle-même est verrouillée entre ces deux tailles (voir WindowMinSize).
    minW = juce::roundToInt ((float) imgW * baseScale * 0.6f);
    minH = juce::roundToInt ((float) imgH * baseScale * 0.6f);
    maxW = juce::roundToInt ((float) imgW * baseScale);
    maxH = juce::roundToInt ((float) imgH * baseScale);

    setResizable (true, false);                 // redimensionnable par le DAW…
    setResizeLimits (minW, minH, maxW, maxH);   // …entre 60 % et 100 %
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) imgW / (double) imgH);

    minSize.setMinimumSize (minW, minH);
    minSize.setMaximumSize (maxW, maxH);

    applyScale (proc.uiScale.load());
    startTimer (250);
}

void InfiniteBowEditor::applyScale (float s)
{
    s = juce::jlimit (0.6f, 1.0f, s);           // jamais plus petit que la taille par défaut
    proc.uiScale.store (s);
    const float total = baseScale * s;
    setSize (juce::roundToInt ((float) imgW * total), juce::roundToInt ((float) imgH * total));
    resized();
    minSize.enforceNow();
}

void InfiniteBowEditor::resized()
{
    panel.setBounds (0, 0, imgW, imgH);

    // la pédale remplit la fenêtre (proportions gardées), entre 60 % et 100 % ;
    // elle n'est réduite sous 60 % (jamais coupée) que si un DAW l'impose malgré tout
    const float fit = juce::jmin ((float) getWidth()  / ((float) imgW * baseScale),
                                  (float) getHeight() / ((float) imgH * baseScale));
    const float s = juce::jlimit (0.1f, 1.0f, fit);
    panel.setTransform (juce::AffineTransform::scale (baseScale * s));

    if (s >= 0.6f - 0.002f)
        proc.uiScale.store (juce::jlimit (0.6f, 1.0f, s));   // taille retenue avec le projet
}

void InfiniteBowEditor::parentHierarchyChanged()
{
    attachMinSize();
}

void InfiniteBowEditor::attachMinSize()
{
    if (auto* peer = getPeer())
    {
        void* handle = peer->getNativeHandle();
        if (handle != nullptr && ! minSize.isAttachedTo (handle))
        {
            auto* top = &peer->getComponent();
            minSize.attach (handle, [safe = juce::Component::SafePointer<juce::Component> (top)]
                                    { return safe != nullptr ? safe->getWidth() : 0; });
        }
    }
    else
    {
        minSize.detach();
    }
}

void InfiniteBowEditor::timerCallback()
{
    attachMinSize();        // le DAW peut recréer sa fenêtre (dock, ré-ouverture…)
    minSize.enforceNow();   // et la rouvrir à une taille mémorisée trop petite

    // si le DAW a quand même rétréci la pédale sous la taille par défaut, on la redemande
    if (getWidth() < minW - 1 || getHeight() < minH - 1)
        setSize (juce::jmax (getWidth(), minW), juce::jmax (getHeight(), minH));
}

void InfiniteBowEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);   // visible seulement si le DAW impose une forme différente
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
