#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "WindowMinSize.h"

//==============================================================================
//  ARC (Studio Wiga) - interface sur le visuel de la pédale
//  Toutes les coordonnées sont celles du visuel (1525 x 1227 px, même taille de pédale que Ring Think).
//==============================================================================
namespace arcui
{
    const juce::Colour orange   { 0xffff9a30 };   // arcs, mode orange (vif)
    const juce::Colour blue     { 0xff3ae4ff };   // arcs, mode bleu (couleur des étiquettes du bas, vive)
    const juce::Colour ledCyan  { 0xff2ff0ff };
    const juce::Colour textCol  { 0xffe9ecef };
    const juce::Colour noteOrg  { 0xffff8a1f };

    constexpr int imgW = 1525, imgH = 1227;
    constexpr float baseScale = 1023.0f / 1525.0f; // 100 % = 1023 x 823 px, 60 % = 614 x 494 (comme Ring Think)

    juce::Font font (float h, bool bold = false);
    juce::Font mono (float h);
}

//==============================================================================
class ArcLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

//==============================================================================
//  Zone cliquable posée sur un bouton dessiné dans le visuel
//==============================================================================
class HotSpot : public juce::Button
{
public:
    HotSpot (const juce::String& name, bool circular, bool toggle = false);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    bool hitTest (int x, int y) override;
private:
    bool circle;
};

//==============================================================================
class ArcPanel : public juce::Component, private juce::Timer
{
public:
    explicit ArcPanel (InfiniteBowProcessor&);
    ~ArcPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void editorResized() { resizeSettle = 4; }   // anti-gel v2 : cache du fond refait quand la taille sera stable
    void initCaches();                            // à l'ouverture de la fenêtre

private:
    struct KnobSlot
    {
        juce::String idA, idB;
        std::unique_ptr<juce::Slider> a, b;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attA, attB;
    };

    void timerCallback() override;
    void setLayer (bool blueLayer);
    void drawLed (juce::Graphics&, juce::Point<float> c, float lens, float bezel,
                  juce::Colour col, float intensity);
    void drawDisplay (juce::Graphics&);
    juce::String statusText() const;
    static juce::String noteName (float hz);

    InfiniteBowProcessor& proc;
    ArcLookAndFeel lnf;

    juce::Image background, scaledBackground;
    float scaledFor = -1.0f;

    // anti-gel v2 : cache du fond refait par le timer seulement, chien de garde des redessins
    void rebuildCaches();
    float targetScale() const;
    void askRepaint (juce::Rectangle<int> r);
    int resizeSettle = 0;
    double lastPaintMs = 0.0, repaintAskedMs = 0.0, lastScaleCheckMs = 0.0;

    std::vector<std::unique_ptr<KnobSlot>> knobs;

    HotSpot modeSpot { "MODE", true, true };
    HotSpot bowSpot  { "BOW",  true, true };
    HotSpot holdSpot { "HOLD", true, true };
    juce::OwnedArray<HotSpot> voiceSpots, presetSpots;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> modeAtt, bowAtt, holdAtt;
    std::unique_ptr<juce::ParameterAttachment> voiceAtt;

    bool layerB = false;
    int voiceIndex = 0;
    std::vector<float> lastValues;
    juce::String lastParamId { "attack" };
    int lastCC = -1, lastCCVal = 0;
    float shownLevel = 0.0f;
    double phase = 0.0;

    // dernier état réellement dessiné : on ne redessine une zone que si elle a changé
    juce::String drawnStatus, drawnDisplayKey;
    float drawnLevel = -1.0f, drawnPos = -1.0f;
    int drawnLayer = -1, drawnVoice = -1, drawnPreset = -1, drawnBowOn = -1, drawnHoldOn = -1;
};

//==============================================================================
class InfiniteBowEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit InfiniteBowEditor (InfiniteBowProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void applyScale (float s);
    void timerCallback() override;      // accroche le verrou de taille minimale à la fenêtre du DAW
    void parentHierarchyChanged() override;
    void attachMinSize();
    void visibilityChanged() override;
    friend class ArcPanel;              // son timer surveille le moteur de rendu (anti-gel v2)
    void useSoftwareRenderer();         // applique le moteur logiciel (Windows) ; repaint si la fenêtre hôte a changé
    void checkRenderer();               // seulement si la fenêtre a changé ou au plus 1 fois / s
    void resetRenderer();               // chien de garde : oublie la fenêtre mémorisée et réapplique tout
    void* lastPeer = nullptr;
    double lastRendererCheckMs = 0.0;
    int minW = 0, minH = 0;             // taille par défaut (60 %) = taille minimale
    int maxW = 0, maxH = 0;             // 100 % = taille maximale
    wiga::WindowMinSize minSize;

    InfiniteBowProcessor& proc;
    ArcPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfiniteBowEditor)
};
