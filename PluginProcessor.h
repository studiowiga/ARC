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

class InfiniteBowProcessor : public juce::AudioProcessor
{
public:
    InfiniteBowProcessor();
    ~InfiniteBowProcessor() override = default;

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
    std::atomic<float> uiScale { 0.6f };     // taille de l'interface (50/60/75/100 %)

private:
    wiga::BowEngine engine;
    juce::Reverb reverb;
    juce::AudioBuffer<float> wetBuffer;
    juce::SmoothedValue<float> dryGain, wetGain, outGain;

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
