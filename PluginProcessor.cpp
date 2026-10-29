#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
//  Préréglages d'usine
//  attack, release, sustain, mode, position, tone, glide, life, buzz, sens, dry, wet, space, output
//==============================================================================
const BowPreset InfiniteBowProcessor::factoryPresets[numFactoryPresets] =
{
    { "Classic Bow",     0.60f, 0.40f, 1.00f, 0, 0.25f, 0.50f,  40.0f, 0.30f, 0.00f, -45.0f, 0.00f, 0.80f, 0.25f, 0.0f, 0.00f },
    { "Violin Swell",    1.20f, 0.60f, 1.00f, 0, 0.12f, 0.68f,  60.0f, 0.55f, 0.00f, -45.0f, 0.00f, 0.80f, 0.35f, 0.0f, 0.00f },
    { "Ethereal Flute",  0.90f, 1.20f, 1.00f, 1, 0.50f, 0.22f,  80.0f, 0.40f, 0.00f, -45.0f, 0.00f, 0.75f, 0.60f, 0.0f, 0.00f },
    { "Octave Choir",    0.80f, 1.50f, 1.00f, 1, 0.30f, 0.45f,  50.0f, 0.50f, 0.00f, -45.0f, 0.15f, 0.70f, 0.70f, 0.0f, 0.00f },
    { "Fifth Shimmer",   0.70f, 1.00f, 1.00f, 2, 0.35f, 0.55f,  40.0f, 0.35f, 0.00f, -45.0f, 0.00f, 0.70f, 0.55f, 0.0f, 0.00f },
    { "Buzz Drone",      0.30f, 0.80f, 1.00f, 0, 0.04f, 0.90f,  20.0f, 0.20f, 0.70f, -45.0f, 0.00f, 0.75f, 0.20f, 0.0f, 0.00f },
    { "Slide Lead",      0.15f, 0.30f, 1.00f, 0, 0.20f, 0.72f, 250.0f, 0.40f, 0.10f, -45.0f, 0.25f, 0.80f, 0.20f, 0.0f, 0.00f },
    // larsen : la note jouée reste audible (DRY), puis "fleurit" vers l'harmonique qui accroche
    { "Larsen",          1.20f, 1.20f, 1.00f, 0, 0.15f, 0.75f,  30.0f, 0.45f, 0.15f, -45.0f, 0.40f, 0.85f, 0.30f, 0.0f, 0.85f },
};

//==============================================================================
//  MIDI Learn (v2.6) : toutes les commandes de la façade
//==============================================================================
const InfiniteBowProcessor::LearnDef& InfiniteBowProcessor::learnDef (int i)
{
    static const LearnDef defs[numLearn] =
    {
        { "attack",   "Attack",          -1, learnContinuous, 0 },
        { "release",  "Release",         -1, learnContinuous, 0 },
        { "sustain",  "Sustain",         -1, learnContinuous, 0 },
        { "sens",     "Sensitivity",     -1, learnContinuous, 0 },
        { "position", "Position",        -1, learnContinuous, 0 },
        { "buzz",     "Buzz",            -1, learnContinuous, 0 },
        { "tone",     "Tone",            -1, learnContinuous, 0 },
        { "life",     "Life",            -1, learnContinuous, 0 },
        { "glide",    "Glide",           -1, learnContinuous, 0 },
        { "wet",      "Bow Level",       -1, learnContinuous, 0 },
        { "dry",      "Dry",             -1, learnContinuous, 0 },
        { "space",    "Space",           -1, learnContinuous, 0 },
        { "output",   "Output",          -1, learnContinuous, 0 },
        { "layer",    "Mode button",     -1, learnToggle,     2 },   // >= 64 : réglages cyan
        { "mode",     "Voice",           -1, learnSelector,   3 },   // 0-42 Normal, 43-85 Harmonic, 86-127 Harmonic+
        { "preset",   "Preset",          -1, learnSelector,   numFactoryPresets },   // CC réparti sur les 8 presets
        { "bow",      "Bow footswitch",  -1, learnFootswitch, 0 },
        { "hold",     "Hold footswitch", 64, learnFootswitch, 0 },   // CC64 (sustain) = Hold, comme avant
    };
    return defs[juce::jlimit (0, numLearn - 1, i)];
}

int InfiniteBowProcessor::learnIndexOf (const juce::String& paramId)
{
    for (int i = 0; i < numLearn; ++i)
        if (paramId == learnDef (i).id) return i;
    return -1;
}

void InfiniteBowProcessor::resetMidiMap()
{
    for (int i = 0; i < numLearn; ++i) ccMap[i].store (learnDef (i).defaultCC);
}

//==============================================================================
static juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

juce::AudioProcessorValueTreeState::ParameterLayout InfiniteBowProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const auto& d = factoryPresets[0];

    auto secs = [] (float v, int) { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms"
                                                    : String (v, 2) + " s"; };

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack",
        NormalisableRange<float> (0.01f, 4.0f, 0.0f, 0.4f), d.attack,
        AudioParameterFloatAttributes().withStringFromValueFunction (secs)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release",
        NormalisableRange<float> (0.02f, 5.0f, 0.0f, 0.4f), d.release,
        AudioParameterFloatAttributes().withStringFromValueFunction (secs)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "sustain", 1 }, "Sustain",
        NormalisableRange<float> (0.0f, 1.0f), d.sustain,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (v >= 0.999f) return String ("Infinite");
            const float t = 0.3f + v * v * 20.0f;
            return String (t, 1) + " s";
        })));

    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Voice",
        StringArray { "Normal", "Harmonic", "Harmonic+" }, d.mode));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "position", 1 }, "Position",
        NormalisableRange<float> (0.0f, 1.0f), d.position,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "tone", 1 }, "Tone",
        NormalisableRange<float> (0.0f, 1.0f), d.tone,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Glide",
        NormalisableRange<float> (1.0f, 600.0f, 0.0f, 0.5f), d.glide,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (roundToInt (v)) + " ms"; })));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "life", 1 }, "Life",
        NormalisableRange<float> (0.0f, 1.0f), d.life,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "buzz", 1 }, "Buzz",
        NormalisableRange<float> (0.0f, 1.0f), d.buzz,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "sens", 1 }, "Sensitivity",
        NormalisableRange<float> (-70.0f, -20.0f), d.sens,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (v, 1) + " dB"; })));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "dry", 1 }, "Dry",
        NormalisableRange<float> (0.0f, 1.0f), d.dry,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "wet", 1 }, "Bow Level",
        NormalisableRange<float> (0.0f, 1.0f), d.wet,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "space", 1 }, "Space",
        NormalisableRange<float> (0.0f, 1.0f), d.space,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f), d.output,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; })));

    // larsen simulé : pas de potard sur la pédale, réglé par le preset Larsen (automatisable dans le DAW)
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "feedback", 1 }, "Feedback",
        NormalisableRange<float> (0.0f, 1.0f), d.feedback,
        AudioParameterFloatAttributes().withStringFromValueFunction (pct)));

    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "bow", 1 }, "Bow", true));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "hold", 1 }, "Hold", false));
    // bouton MODE de la pédale : choisit le réglage piloté par les potards doubles
    // (off = LED orange, réglages du haut ; on = LED cyan, réglages du bas)
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { "layer", 1 }, "Knob Mode", false,
        AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int)
        { return String (v ? "Cyan" : "Orange"); })));

    return { p.begin(), p.end() };
}

//==============================================================================
InfiniteBowProcessor::InfiniteBowProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "InfiniteBow", createLayout())
{
    pAttack   = apvts.getRawParameterValue ("attack");
    pRelease  = apvts.getRawParameterValue ("release");
    pSustain  = apvts.getRawParameterValue ("sustain");
    pMode     = apvts.getRawParameterValue ("mode");
    pPosition = apvts.getRawParameterValue ("position");
    pTone     = apvts.getRawParameterValue ("tone");
    pGlide    = apvts.getRawParameterValue ("glide");
    pLife     = apvts.getRawParameterValue ("life");
    pBuzz     = apvts.getRawParameterValue ("buzz");
    pSens     = apvts.getRawParameterValue ("sens");
    pDry      = apvts.getRawParameterValue ("dry");
    pWet      = apvts.getRawParameterValue ("wet");
    pSpace    = apvts.getRawParameterValue ("space");
    pOutput   = apvts.getRawParameterValue ("output");
    pBow      = apvts.getRawParameterValue ("bow");
    pHold     = apvts.getRawParameterValue ("hold");
    pFeedback = apvts.getRawParameterValue ("feedback");

    for (int i = 0; i < numLearn; ++i)
    {
        learnParam[i] = apvts.getParameter (learnDef (i).id);          // nullptr pour « preset » (pas un paramètre)
        learnRaw[i]   = apvts.getRawParameterValue (learnDef (i).id);
        pendingValue[i].store (0.0f);
        pendingSet[i].store (false);
    }
    resetMidiMap();
    startTimerHz (60);
}

bool InfiniteBowProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in  != juce::AudioChannelSet::mono() && in  != juce::AudioChannelSet::stereo()) return false;
    return in.size() <= out.size();
}

void InfiniteBowProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    lastSpace = -1.0f;
    wetBuffer.setSize (2, juce::jmax (1, samplesPerBlock));

    for (auto* s : { &dryGain, &wetGain, &outGain })
        s->reset (sampleRate, 0.03);
    dryGain.setCurrentAndTargetValue (pDry->load());
    wetGain.setCurrentAndTargetValue (pWet->load());
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    fxMix.reset (sampleRate, 0.02);
    fxMix.setCurrentAndTargetValue (pBow->load() > 0.5f ? 1.0f : 0.0f);
}

void InfiniteBowProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    // --- MIDI : commandes apprises (MIDI Learn), CC64 = Hold par défaut, affichage du dernier CC ---
    // anti-gel v2 : simples valeurs atomiques et file lue par le timer, aucun appel à l'hôte depuis le thread audio
    handleMidi (midi);

    // --- paramètres du moteur ---
    wiga::BowParams bp;
    bp.attackSec  = pAttack->load (std::memory_order_relaxed);
    bp.releaseSec = pRelease->load (std::memory_order_relaxed);
    bp.sustain    = pSustain->load (std::memory_order_relaxed);
    bp.mode       = juce::jlimit (0, 2, (int) std::lround (pMode->load (std::memory_order_relaxed)));
    bp.position   = pPosition->load (std::memory_order_relaxed);
    bp.tone       = pTone->load (std::memory_order_relaxed);
    bp.glideMs    = pGlide->load (std::memory_order_relaxed);
    bp.life       = pLife->load (std::memory_order_relaxed);
    bp.buzz       = pBuzz->load (std::memory_order_relaxed);
    bp.gateDb     = pSens->load (std::memory_order_relaxed);
    bp.bow        = pBow->load (std::memory_order_relaxed) > 0.5f;
    bp.hold       = pHold->load (std::memory_order_relaxed) > 0.5f || midiHold.load();
    bp.feedback   = pFeedback->load (std::memory_order_relaxed);
    engine.setParams (bp);

    dryGain.setTargetValue (pDry->load (std::memory_order_relaxed));
    wetGain.setTargetValue (pWet->load (std::memory_order_relaxed));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load (std::memory_order_relaxed)));
    fxMix.setTargetValue (bp.bow ? 1.0f : 0.0f);   // BOW relevé = bypass (fondu de 20 ms, sans clic)

    if (wetBuffer.getNumSamples() < n)
        wetBuffer.setSize (2, n, false, false, true);

    // --- archet (mono) ---
    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;
    float* wL = wetBuffer.getWritePointer (0);
    float* wR = wetBuffer.getWritePointer (1);

    for (int i = 0; i < n; ++i)
    {
        const float x = numIn > 1 ? 0.5f * (inL[i] + inR[i]) : inL[i];
        const float y = engine.process (x);
        wL[i] = y; wR[i] = y;
    }

    // --- espace (réverbération sur le son d'archet) ---
    const float space = pSpace->load (std::memory_order_relaxed);
    if (std::abs (space - lastSpace) > 0.001f)
    {
        juce::Reverb::Parameters rp;
        rp.roomSize   = 0.55f + 0.42f * space;
        rp.damping    = 0.45f;
        rp.wetLevel   = 0.85f * space;
        rp.dryLevel   = 1.0f - 0.45f * space;
        rp.width      = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        lastSpace = space;
    }
    if (space > 0.001f)
        reverb.processStereo (wL, wR, n);

    // --- mélange ---
    // BOW enfoncé : dry + archet, réglages DRY / BOW LEVEL / OUTPUT.
    // BOW relevé  : bypass, la guitare passe telle quelle (gain 1), aucun son coupé.
    float* o0 = buffer.getWritePointer (0);
    float* o1 = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        const float dg = dryGain.getNextValue();
        const float wg = wetGain.getNextValue();
        const float og = outGain.getNextValue();
        const float fx = fxMix.getNextValue();
        const float x0 = o0[i];
        const float x1 = (numIn > 1 && o1 != nullptr) ? o1[i] : x0;
        if (o1 == nullptr)
        {
            const float e = (dg * x0 + wg * 0.5f * (wL[i] + wR[i])) * og;
            o0[i] = fx * e + (1.0f - fx) * x0;
        }
        else
        {
            const float e0 = (dg * x0 + wg * wL[i]) * og;
            const float e1 = (dg * x1 + wg * wR[i]) * og;
            o0[i] = fx * e0 + (1.0f - fx) * x0;
            o1[i] = fx * e1 + (1.0f - fx) * x1;
        }
    }

    // --- télémétrie pour l'interface ---
    uiPitchHz.store (engine.getPitchHz());
    uiState.store (engine.getState());
    uiLevel.store (engine.getStringLevel());
    uiFeedback.store (engine.getFeedbackAmount());
    uiFbHarm.store (engine.getFeedbackHarmonic());
}

//==============================================================================
//  MIDI (thread audio) : aucune allocation, aucun appel à l'hôte, aucune recherche par chaîne
//==============================================================================
void InfiniteBowProcessor::queueParam (int i, float norm01)
{
    pendingValue[i].store (juce::jlimit (0.0f, 1.0f, norm01), std::memory_order_relaxed);
    pendingSet[i].store (true, std::memory_order_release);
}

void InfiniteBowProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    const bool toggleFs = midiFsToggle.load (std::memory_order_relaxed);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (! m.isController())
            continue;

        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        uiMidiCC.store (cc);
        uiMidiVal.store (v);

        // MIDI Learn : le premier CC reçu est affecté à la commande visée (il n'agit pas sur le son)
        const int lt = learnTarget.load();
        if (lt >= 0 && lt < numLearn && cc < 120)
        {
            for (int j = 0; j < numLearn; ++j)
                if (j != lt && ccMap[j].load() == cc) ccMap[j].store (-1);   // numéro déjà pris : l'autre commande est désassignée
            ccMap[lt].store (cc);
            prevFsValue[lt] = v;
            lastLearned.store (lt);
            learnTarget.store (-1);
            continue;
        }

        for (int i = 0; i < numLearn; ++i)
        {
            if (ccMap[i].load (std::memory_order_relaxed) != cc)
                continue;

            const auto& d = learnDef (i);
            if (d.type == learnContinuous)
                queueParam (i, (float) v / 127.0f);                       // lissage existant (DRY / BOW LEVEL / OUTPUT)
            else if (d.type == learnToggle)
                queueParam (i, v >= 64 ? 1.0f : 0.0f);
            else if (d.type == learnSelector)
            {
                const int pos = juce::jmin (d.positions - 1, v * d.positions / 128);
                if (i == learnPreset) pendingProgram.store (pos);
                else if (learnParam[i] != nullptr) queueParam (i, learnParam[i]->convertTo0to1 ((float) pos));
            }
            else   // footswitch : 127 = on / 0 = off, ou bascule à chaque appui (menu MIDI)
            {
                int& prev = prevFsValue[i];
                const bool press = v >= 64 && prev < 64;
                prev = v;
                if (i == learnHold)
                {
                    // Hold MIDI = pédale de sustain (comme le CC64 d'avant), ajouté au bouton HOLD
                    if (! toggleFs)  midiHold.store (v >= 64);
                    else if (press)  midiHold.store (! midiHold.load());
                }
                else if (! toggleFs) queueParam (i, v >= 64 ? 1.0f : 0.0f);
                else if (press)
                {
                    const float cur = pendingSet[i].load() ? pendingValue[i].load()
                                                           : (learnRaw[i] != nullptr ? learnRaw[i]->load() : 0.0f);
                    queueParam (i, cur > 0.5f ? 0.0f : 1.0f);
                }
            }
            break;
        }
    }
}

// thread de messages (60 Hz) : transmet au DAW et à la fenêtre les changements venus du MIDI
void InfiniteBowProcessor::timerCallback()
{
    for (int i = 0; i < numLearn; ++i)
        if (pendingSet[i].exchange (false, std::memory_order_acquire))
            if (auto* prm = learnParam[i])
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (pendingValue[i].load (std::memory_order_relaxed));
                prm->endChangeGesture();
            }

    const int pg = pendingProgram.exchange (-1);
    if (pg >= 0 && pg != currentPreset.load())
    {
        setCurrentProgram (pg);
        updateHostDisplay (juce::AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    }
}

//==============================================================================
void InfiniteBowProcessor::setCurrentProgram (int index)
{
    index = juce::jlimit (0, numFactoryPresets - 1, index);
    currentPreset.store (index);
    const auto& pr = factoryPresets[index];

    auto set = [this] (const char* id, float value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    };
    set ("attack", pr.attack);     set ("release", pr.release);
    set ("sustain", pr.sustain);   set ("mode", (float) pr.mode);
    set ("position", pr.position); set ("tone", pr.tone);
    set ("glide", pr.glide);       set ("life", pr.life);
    set ("buzz", pr.buzz);         set ("sens", pr.sens);
    set ("dry", pr.dry);           set ("wet", pr.wet);
    set ("space", pr.space);       set ("output", pr.output);
    set ("feedback", pr.feedback);
}

const juce::String InfiniteBowProcessor::getProgramName (int index)
{
    if (index >= 0 && index < numFactoryPresets)
        return factoryPresets[index].name;
    return {};
}

//==============================================================================
void InfiniteBowProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    state.setProperty ("uiScale", uiScale.load(), nullptr);
    for (int i = 0; i < numLearn; ++i)
        state.setProperty (juce::String ("cc_") + learnDef (i).id, ccMap[i].load(), nullptr);
    state.setProperty ("midiFsToggle", midiFsToggle.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void InfiniteBowProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentPreset.store (juce::jlimit (0, numFactoryPresets - 1, (int) state.getProperty ("preset", 0)));
            uiScale.store ((float) (double) state.getProperty ("uiScale", 0.6));
            // assignations MIDI Learn (ancien projet sans ces propriétés : Hold = CC64, le reste non assigné)
            for (int i = 0; i < numLearn; ++i)
                ccMap[i].store (juce::jlimit (-1, 127, (int) state.getProperty (juce::String ("cc_") + learnDef (i).id, learnDef (i).defaultCC)));
            midiFsToggle.store ((bool) state.getProperty ("midiFsToggle", false));
            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessorEditor* InfiniteBowProcessor::createEditor()
{
    return new InfiniteBowEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new InfiniteBowProcessor();
}
