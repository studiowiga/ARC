#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "WindowMinSize.h"

//==============================================================================
//  ARC (Studio Wiga) - interface sur le visuel de la pédale
//  Toutes les coordonnées sont celles du visuel (1603 x 1200 px, proportions de Ring Think).
//==============================================================================
namespace arcui
{
    const juce::Colour orange   { 0xffff9a30 };   // arcs, mode orange (vif)
    const juce::Colour blue     { 0xff3ae4ff };   // arcs, mode bleu (couleur des étiquettes du bas, vive)
    const juce::Colour ledCyan  { 0xff2ff0ff };
    const juce::Colour textCol  { 0xffe9ecef };
    const juce::Colour noteOrg  { 0xffff8a1f };

    constexpr int imgW = 1603, imgH = 1200;
    constexpr float baseScale = 1240.0f / 1603.0f; // 100 % = 1240 x 928 px (comme Ring Think), 60 % = 744 x 557

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
    int minW = 0, minH = 0;             // taille par défaut (60 %) = taille minimale
    int maxW = 0, maxH = 0;             // 100 % = taille maximale
    wiga::WindowMinSize minSize;

    InfiniteBowProcessor& proc;
    ArcPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfiniteBowEditor)
};
