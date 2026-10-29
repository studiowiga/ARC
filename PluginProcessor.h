#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "BowEngine.h"

// Préréglages d'usine
struct BowPreset
{
    const char* name;
    float attack, release, sustain;
    int   mode;
    float position, tone, glide, life, buzz, sens, dry, wet, space, output, feedback;
};

class InfiniteBowProcessor : public juce::AudioProcessor,
                             private juce::Timer
{
public:
    InfiniteBowProcessor();
    ~InfiniteBowProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Arc"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return numFactoryPresets; }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // --- partagé avec l'interface ---
    static constexpr int numFactoryPresets = 8;
    static const BowPreset factoryPresets[numFactoryPresets];

    std::atomic<float> uiPitchHz { 0.0f };
    std::atomic<int>   uiState   { 0 };
    std::atomic<float> uiLevel   { 0.0f };
    std::atomic<float> uiFeedback { 0.0f };   // part du larsen dans le son (0..1)
    std::atomic<int>   uiFbHarm  { 2 };
    std::atomic<int>   uiMidiCC  { -1 };     // dernier CC reçu (numéro)
    std::atomic<int>   uiMidiVal { 0 };      // et sa valeur 0..127
    std::atomic<bool>  midiHold  { false };  // pédale de sustain (CC64) = Hold
    std::atomic<int>   currentPreset { 0 };
    std::atomic<float> uiScale { 0.6f };     // taille de l'interface (60/75/100 %)

    // v2.6 MIDI Learn : toutes les commandes de la façade (clic droit sur la commande)
    // Hold a CC64 par défaut (pédale de sustain, comme avant) ; les autres commandes ne sont pas assignées
    enum LearnType { learnContinuous, learnToggle, learnSelector, learnFootswitch };
    struct LearnDef { const char* id; const char* name; int defaultCC; int type; int positions; };
    static constexpr int numLearn = 18;
    static constexpr int learnLayer = 13, learnVoice = 14, learnPreset = 15, learnBow = 16, learnHold = 17;
    static const LearnDef& learnDef (int i);
    static int learnIndexOf (const juce::String& paramId);
    std::atomic<int>  ccMap[numLearn];          // numéro de CC de chaque commande, -1 = non assigné
    std::atomic<int>  learnTarget { -1 };       // commande en attente d'un CC (-1 = pas de learn)
    std::atomic<int>  lastLearned { -1 };       // dernière commande apprise (lue et remise à -1 par la fenêtre)
    std::atomic<bool> midiFsToggle { false };   // footswitches MIDI : false = 127 on / 0 off, true = bascule à chaque appui
    void resetMidiMap();

private:
    // anti-gel : le MIDI (thread audio) met la valeur en file, le timer du processeur la transmet à l'hôte
    void timerCallback() override;
    void handleMidi (const juce::MidiBuffer& midi);
    void queueParam (int learnIdx, float norm01);
    std::atomic<float> pendingValue[numLearn];
    std::atomic<bool>  pendingSet[numLearn];
    std::atomic<int>   pendingProgram { -1 };
    juce::RangedAudioParameter* learnParam[numLearn] {};   // pointeurs mis en cache (aucune recherche par chaîne dans l'audio)
    std::atomic<float>* learnRaw[numLearn] {};
    int prevFsValue[numLearn] {};

    wiga::BowEngine engine;
    juce::Reverb reverb;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> dryGain, wetGain, outGain;
    juce::SmoothedValue<float> fxMix;      // 1 = effet actif, 0 = bypass (footswitch BOW relevé)

    std::atomic<float>* pAttack = nullptr;  std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pSustain = nullptr; std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pPosition = nullptr; std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pGlide = nullptr;  std::atomic<float>* pLife = nullptr;
    std::atomic<float>* pBuzz = nullptr;   std::atomic<float>* pSens = nullptr;
    std::atomic<float>* pDry = nullptr;    std::atomic<float>* pWet = nullptr;
    std::atomic<float>* pSpace = nullptr;  std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pBow = nullptr;    std::atomic<float>* pHold = nullptr;
    std::atomic<float>* pFeedback = nullptr;

    float lastSpace = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InfiniteBowProcessor)
};
