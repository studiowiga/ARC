// =============================================================================
//  Infinite Bow (Studio Wiga) - moteur DSP
//  Simulation d'un archet électronique :
//   1. suivi de hauteur monophonique (YIN) et détection d'attaque / d'étouffement
//   2. corde virtuelle (guide d'onde) accordée sur la note jouée, excitée par la
//      vraie corde puis entretenue par une boucle "capteur -> ampli -> excitateur"
//      avec contrôle automatique de gain (sustain infini)
//   3. mode Harmonic : la corde virtuelle vibre sur son 2e (ou 3e) harmonique
//   4. position de l'archet sur la corde (filtre en peigne), buzz, "vie"
//  Aucune dépendance JUCE : testable hors DAW.
// =============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <complex>
#include <cstdint>
#include <algorithm>

namespace wiga
{

struct BowParams
{
    float attackSec  = 0.6f;   // montée du son (distance archet/corde)
    float releaseSec = 0.4f;   // extinction quand on lève l'archet / étouffe
    float sustain    = 1.0f;   // 1 = infini ; < 1 = le son s'éteint après la corde réelle
    int   mode       = 0;      // 0 Normal, 1 Harmonic (octave), 2 Harmonic+ (octave + quinte)
    float position   = 0.25f;  // 0 = près du chevalet (nasal) .. 1 = milieu de corde (creux)
    float tone       = 0.5f;   // brillance de la corde virtuelle
    float glideMs    = 40.0f;  // portamento entre notes liées
    float life       = 0.3f;   // vibrato et instabilités organiques
    float buzz       = 0.0f;   // corde qui frise contre l'archet
    float gateDb     = -45.0f; // seuil de détection
    bool  bow        = true;   // archet posé (on) / levé (off)
    bool  hold       = false;  // maintien infini, ignore l'étouffement
    float feedback   = 0.0f;   // larsen simulé : 0 = off, 1 = bascule rapide et franche
};

class BowEngine
{
public:
    enum State { Idle = 0, Waiting, Bowing, Releasing };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        decim = std::max (1, (int) std::lround (sr / 24000.0));
        fsd = sr / decim;

        // --- analyse de hauteur (signal décimé) ---
        yinSize = 2048;
        yinRing.assign ((size_t) yinSize, 0.0f);
        yinW = (int) std::lround (fsd * 0.032);                // ~32 ms d'intégration (au moins 1,5 période à 48 Hz)
        tauMax = std::min ((int) (fsd / kMinHz) + 2, yinSize - yinW - 4);
        tauMin = std::max (2, (int) (fsd / kMaxHz));
        frame.assign ((size_t) (yinW + tauMax + 2), 0.0f);
        dBuf.assign ((size_t) (tauMax + 2), 0.0f);
        hopD = std::max (32, (int) std::lround (fsd * 0.005)); // analyse toutes les ~5 ms

        // --- corde virtuelle ---
        int maxDelay = (int) (sr / 30.0) + 16;
        int sz = 1;
        while (sz < maxDelay) sz <<= 1;
        str.assign ((size_t) sz, 0.0f);
        hist.assign ((size_t) sz, 0.0f);
        mask = sz - 1;

        fastA = coef (0.0005); fastR = coef (0.015);
        slowA = coef (0.040);  slowR = coef (0.400);
        agcK  = 1.0f - coef (0.030);
        dcR   = (float) std::exp (-6.283185307 * 2.0 / sr);   // anti-DC à 2 Hz (peu de dispersion)

        reset();
    }

    void reset()
    {
        std::fill (yinRing.begin(), yinRing.end(), 0.0f);
        std::fill (str.begin(), str.end(), 0.0f);
        std::fill (hist.begin(), hist.end(), 0.0f);
        yinW_idx = 0; decimCount = 0; decimAcc = 0.0f; hopCount = 0;
        wIdx = 0; hIdx = 0;
        fastEnv = slowEnv = 0.0f;
        lpState = dcX1 = dcY1 = 0.0f; bpZ1 = bpZ2 = 0.0f;
        aEnv = 0.0f;
        state = Idle;
        havePitch = false; changeCount = 0;
        swellLin = 0.0f; swell = 0.0f; susGain = 1.0f;
        sinceOnset = 1 << 30; quietTime = 0;
        modeFade = 1.0f; activeMode = params.mode; modeSwitching = false;
        lfoPhase = 0.0; driftA = driftB = driftTarget = 0.0f; driftCount = 0;
        injectSamples = 0;
        lastFrameLevel = 0.0f;
        fbMix = 0.0f; fbTime = 0; fbPhase = 0.0; throbPhase = 0.0; fbHarm = 2; lastHarm = 0; lastHarmF0 = 0.0; stableHarm = 0; stableF0 = 0.0;
        rawAge = 100; glideActive = false;
        for (int i = 0; i < kNumRes; ++i) resDrift[i] = resTarget[i] = 0.0f;
        baseHarm = 2; baseLog = std::log (220.0); swGain = 1.0f; swDown = false; pendingHarm = 2; evalCount = 0;
        for (int i = 0; i < kNumRes; ++i) resSnap[i] = kRes[i];
        updateFilters (220.0);
    }

    void setParams (const BowParams& p) { params = p; }

    // traite un échantillon mono, renvoie le signal "archet" (wet)
    float process (float x)
    {
        // ---------------- enveloppes ----------------
        const float ax = std::fabs (x);
        fastEnv += (ax - fastEnv) * (1.0f - (ax > fastEnv ? fastA : fastR));
        slowEnv += (ax - slowEnv) * (1.0f - (ax > slowEnv ? slowA : slowR));
        const float gate = dbToLin (params.gateDb);
        if (sinceOnset < (1 << 30)) ++sinceOnset;

        // ---------------- analyse de hauteur ----------------
        decimAcc += x;
        if (++decimCount >= decim)
        {
            yinRing[(size_t) yinW_idx] = decimAcc / (float) decim;
            yinW_idx = (yinW_idx + 1) % yinSize;
            decimAcc = 0.0f; decimCount = 0;
            if (++hopCount >= hopD) { hopCount = 0; analyse (gate); }
        }

        // ---------------- attaque (nouvelle note pincée) ----------------
        if (params.bow
            && fastEnv > 1.8f * slowEnv + 1.0e-6f
            && fastEnv > gate * 1.5f
            && sinceOnset > (int) (0.08 * sr))
            onset();

        // ---------------- étouffement (paume / main gauche) ----------------
        if (state == Bowing && ! params.hold
            && sinceOnset > (int) (0.10 * sr)
            && slowEnv > gate * 2.0f
            && fastEnv < 0.12f * slowEnv)
            state = Releasing;

        if (! params.bow && (state == Bowing || state == Waiting))
            state = Releasing;

        // ---------------- machine d'états ----------------
        if (state == Waiting)
        {
            if (havePitch && freshPitch) startBowing();
            else if (sinceOnset > (int) (0.15 * sr))
                state = havePitch ? (startBowing(), Bowing) : Idle;
        }

        if (state == Idle) { pushHist (0.0f); return 0.0f; }

        // sustain : la corde réelle s'est tue
        if (state == Bowing)
        {
            if (slowEnv < gate) ++quietTime; else { quietTime = 0; susGain = 1.0f; }
            if (! params.hold && params.sustain < 0.999f && quietTime > 0)
            {
                const double t = 0.3 + (double) params.sustain * params.sustain * 20.0;
                susGain *= (float) std::exp (-1.0 / (t * sr));
                if (susGain < 1.0e-3f) state = Releasing;
            }
        }

        // enveloppe de sortie (montée façon archet / extinction)
        if (state == Bowing)
        {
            swellLin = std::min (1.0f, swellLin + (float) (1.0 / (std::max (0.005f, params.attackSec) * sr)));
            swell = swellLin * swellLin;
        }
        else if (state == Releasing)
        {
            swell *= (float) std::exp (-6.9 / (std::max (0.01f, params.releaseSec) * sr));
            swellLin = std::sqrt (swell);
            if (swell < 1.0e-5f) { goIdle(); pushHist (0.0f); return 0.0f; }
        }

        // changement de mode : fondu court pour masquer le saut
        if (params.mode != activeMode && ! modeSwitching) modeSwitching = true;
        if (modeSwitching)
        {
            modeFade -= (float) (1.0 / (0.015 * sr));
            if (modeFade <= 0.0f)
            {
                modeFade = 0.0f; activeMode = params.mode; modeSwitching = false;
                injectSamples = (int) (0.08 * sr);
            }
        }
        else modeFade = std::min (1.0f, modeFade + (float) (1.0 / (0.03 * sr)));

        // ---------------- hauteur courante (glide + vie) ----------------
        // GLIDE pour les sauts de note, suivi rapide (6 ms) pour les bends et le vibrato
        const float tauMs = glideActive ? std::max (1.0f, params.glideMs) : 6.0f;
        const float gk = 1.0f - (float) std::exp (-1.0 / (tauMs * 0.001 * sr));
        logCur += (logTarget - logCur) * gk;
        if (glideActive && std::fabs (logTarget - logCur) < 0.004) glideActive = false;

        lfoPhase += 5.3 / sr;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        if (++driftCount >= (int) (sr / 20.0))
        {
            driftCount = 0;
            rng = rng * 1664525u + 1013904223u;
            driftTarget = ((float) (rng >> 8) / 8388608.0f) - 1.0f;
        }
        const float dk = 1.0f - coef (0.35);
        driftA += (driftTarget - driftA) * dk;
        driftB += (driftA - driftB) * dk;

        const float cents = params.life * (10.0f * (float) std::sin (6.283185307 * lfoPhase) + 6.0f * driftB);
        const double hz = std::exp (logCur) * std::pow (2.0, cents / 1200.0);
        const int div = activeMode == 0 ? 1 : (activeMode == 1 ? 2 : 3);
        const double loopHz = std::min (hz * div, sr * 0.2);

        if (++filterCount >= 32) { filterCount = 0; updateFilters (loopHz); }

        // ---------------- corde virtuelle ----------------
        const double period = sr / loopHz;
        const double readD = std::clamp (period - filtDelay, 2.0, (double) mask - 4.0);
        const float v = readFrac (readD);

        lpState += lpA * (v - lpState);
        float s = lpState - dcX1 + dcR * dcY1;
        dcX1 = lpState; dcY1 = s;

        const float A = kTarget * susGain;
        if (params.buzz > 0.001f)
        {
            const float th = kTarget * (1.15f - 0.85f * params.buzz);
            if (s > th) s = th + (s - th) * 0.12f;
        }

        aEnv += (std::fabs (s) - aEnv) * agcK;
        float g = 1.0f + 0.06f * (A - aEnv) / kTarget;
        g = std::clamp (g, 0.97f, 1.05f);

        // injection de la vraie corde (verrouillage de phase + excitation)
        float inj = 0.0f;
        if (slowEnv > gate * 0.5f)
        {
            const float norm = kTarget / std::max (slowEnv, gate);
            const float amt = injectSamples > 0 ? 0.35f : 0.03f;
            inj = x * norm * amt;
        }
        if (injectSamples > 0) --injectSamples;

        // l'ampli de l'archet sature : il réinjecte des harmoniques à chaque passage
        const float sat = kTarget * fastTanh (s * 2.5f / kTarget) / 2.5f;
        // léger surcroît de gain sur le mode le plus grave de la boucle :
        // c'est lui qui "gagne" (fondamentale en Normal, harmonique visé en Harmonic)
        const float bp = bpB0 * s + bpZ1;
        bpZ1 = bpZ2 - bpA1 * bp;            // forme transposée directe II (b1 = 0, b2 = -b0)
        bpZ2 = -bpB0 * s - bpA2 * bp;
        float w = g * ((1.0f - kSat) * s + kSat * sat + kFund * bp) + inj;
        w = kClip * fastTanh (w / kClip);
        str[(size_t) wIdx] = w;
        wIdx = (wIdx + 1) & mask;
        pushHist (s);

        // ---------------- position de l'archet ----------------
        const double posFrac = 0.04 + 0.46 * (double) params.position;
        const float  delayed = readHist (posFrac * period);
        const float  norm = 1.0f / std::max (0.35f, (float) (2.0 * std::sin (3.14159265 * posFrac)));
        float out = (s - delayed) * norm;

        const float wob = 1.0f + params.life * 0.08f * driftA;
        // ---------------- larsen simulé ----------------
        // La note tenue "fleurit" vers une harmonique choisie selon la note (résonances ampli + pièce).
        // Rien n'est réinjecté vers l'extérieur : l'amplitude reste bornée, aucun vrai larsen possible.
        float m = 0.0f, voice = 0.0f;
        if (params.feedback > 0.001f || fbMix > 1.0e-4f)
        {
            if (state == Bowing && params.feedback > 0.001f)
            {
                ++fbTime;
                if (fbTime == (long long) (0.3 * sr)) { stableHarm = baseHarm; stableF0 = std::exp (baseLog); }
                const double bloom = 2.6 - 1.8 * (double) params.feedback;          // délai avant que ça "prenne"
                const float target = (double) fbTime > bloom * sr ? params.feedback : 0.0f;
                const double t = target > fbMix ? 1.6 - 0.8 * (double) params.feedback : 0.06;
                fbMix += (target - fbMix) * (float) (1.0 - std::exp (-1.0 / (t * sr)));
            }
            else
                fbMix *= (float) std::exp (-1.0 / (0.25 * sr));

            // dérive lente des résonances (le même doigté ne sonne jamais exactement pareil)
            if (++resCount >= (int) (sr * 0.5))
            {
                resCount = 0;
                for (int i = 0; i < kNumRes; ++i)
                {
                    rng = rng * 1664525u + 1013904223u;
                    resTarget[i] = ((float) (rng >> 8) / 8388608.0f) - 1.0f;
                }
            }
            for (int i = 0; i < kNumRes; ++i) resDrift[i] += (resTarget[i] - resDrift[i]) * (1.0f / (float) (sr * 2.0));

            // bends : le larsen peut sauter sur une autre harmonique, et revient sur la première
            // dès que la corde retrouve sa hauteur de départ (sans réattaque)
            if (state == Bowing && ++evalCount >= 256)
            {
                evalCount = 0;
                const double bendSt = 12.0 * (logTarget - baseLog) / std::log (2.0);
                int want = fbHarm;
                if (std::fabs (bendSt) < 0.3)
                    want = baseHarm;
                else
                {
                    const double f0 = std::exp (logTarget);
                    double bestScore = 1.0e9; int best = fbHarm;
                    for (int h = 2; h <= kMaxHarm; ++h)
                    {
                        if (f0 * h > 2800.0) break;
                        const double sc = harmScore (f0, h, resSnap);
                        if (sc < bestScore) { bestScore = sc; best = h; }
                    }
                    // seuil : on ne change que si la nouvelle harmonique accroche nettement mieux
                    if (best != fbHarm && bestScore < harmScore (f0, fbHarm, resSnap) - 0.04)
                        want = best;
                }
                if (want != fbHarm && ! swDown) { pendingHarm = want; swDown = true; }
                else if (swDown && want == fbHarm) swDown = false;   // annulé avant le saut
            }
            // transition d'harmonique : creux bref (60 ms) puis reprise (0,25 s), comme un vrai larsen
            if (swDown)
            {
                swGain -= (float) (1.0 / (0.06 * sr));
                if (swGain <= 0.2f) { swGain = 0.2f; fbHarm = pendingHarm; swDown = false; }
            }
            else
                swGain = std::min (1.0f, swGain + (float) (0.8 / (0.25 * sr)));

            const double fbHz = std::min (hz * fbHarm, sr * 0.4);
            fbPhase += fbHz / sr;
            if (fbPhase >= 1.0) fbPhase -= 1.0;
            throbPhase += (0.35 + 0.25 * (double) driftB) / sr;                 // respiration du larsen
            if (throbPhase >= 1.0) throbPhase -= 1.0;
            const float sn = (float) std::sin (6.283185307 * fbPhase);
            voice = fastTanh (1.8f * sn) / 0.9468f;
            voice *= 1.0f + 0.14f * fbMix * (float) std::sin (6.283185307 * throbPhase);
            m = fbMix * fbMix * (3.0f - 2.0f * fbMix) * swGain;                // montée en S
        }
        const float mixed = out * (1.0f - 0.85f * m) + voice * 0.16f * susGain * m;

        const float y = mixed * swell * modeFade * wob * 0.6f;
        return 0.95f * fastTanh (y / 0.95f);                                   // garde-fou de niveau
    }

    // --------- télémétrie pour l'interface ---------
    float getPitchHz() const  { return havePitch ? (float) std::exp (logTarget) : 0.0f; }
    int   getState() const    { return (int) state; }
    float getStringLevel() const { return std::min (1.0f, aEnv / kTarget) * swell; }
    float getFeedbackAmount() const { return fbMix * swell; }
    int   getFeedbackHarmonic() const { return fbHarm; }

private:
    static constexpr double kMinHz = 48.0, kMaxHz = 1400.0;   // du G#1 (52 Hz, drop G#) à environ F6
    static constexpr float kTarget = 0.25f, kClip = 1.0f, kSat = 0.45f, kFund = 0.05f;

    float coef (double sec) const { return (float) std::exp (-1.0 / (sec * sr)); }
    static float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }
    static float fastTanh (float x)
    {
        x = std::clamp (x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    void pushHist (float s) { hist[(size_t) hIdx] = s; hIdx = (hIdx + 1) & mask; }
    float readHist (double d) const
    {
        d = std::clamp (d, 1.0, (double) mask - 2.0);
        const int i = (int) d; const float f = (float) (d - i);
        const float a = hist[(size_t) ((hIdx - i) & mask)];
        const float b = hist[(size_t) ((hIdx - i - 1) & mask)];
        return a + (b - a) * f;
    }

    // lecture fractionnaire, interpolation de Lagrange d'ordre 3
    float readFrac (double D) const
    {
        const int i = (int) std::floor (D) - 1;
        const float f = (float) (D - i);           // dans [1, 2)
        const float h0 = -(f - 1) * (f - 2) * (f - 3) / 6.0f;
        const float h1 =  f * (f - 2) * (f - 3) / 2.0f;
        const float h2 = -f * (f - 1) * (f - 3) / 2.0f;
        const float h3 =  f * (f - 1) * (f - 2) / 6.0f;
        auto tap = [this] (int k) { return str[(size_t) ((wIdx - k) & mask)]; };
        return h0 * tap (i) + h1 * tap (i + 1) + h2 * tap (i + 2) + h3 * tap (i + 3);
    }

    void updateFilters (double loopHz)
    {
        // brillance relative à la note (6x à 96x la fréquence de boucle) : timbre homogène sur le manche
        const double fc = std::min (loopHz * 6.0 * std::pow (2.0, params.tone * 4.0), sr * 0.45);
        lpA = (float) (1.0 - std::exp (-6.283185307 * fc / sr));
        // retard de phase de la boucle (passe-bas + anti-DC) à la fréquence jouée
        const double w = 6.283185307 * loopHz / sr;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> H = ((double) lpA / (1.0 - (1.0 - lpA) * z1))
                                     * ((1.0 - z1) / (1.0 - (double) dcR * z1));
        filtDelay = -std::arg (H) / w;

        // passe-bande RBJ (gain 0 dB au centre, Q = 2) centré sur le mode visé
        const double Q = 2.0, alpha = std::sin (w) / (2.0 * Q), a0 = 1.0 + alpha;
        bpB0 = (float) (alpha / a0);
        bpA1 = (float) (-2.0 * std::cos (w) / a0);
        bpA2 = (float) ((1.0 - alpha) / a0);
    }

    void onset()
    {
        sinceOnset = 0;
        fbTime = 0; rawAge = 100; glideActive = false;
        state = Waiting;
        freshPitch = false;
        changeCount = 0;
        swellLin = 0.0f; swell = 0.0f;
        susGain = 1.0f; quietTime = 0;
        std::fill (str.begin(), str.end(), 0.0f);
        lpState = dcX1 = dcY1 = 0.0f; aEnv = 0.0f; bpZ1 = bpZ2 = 0.0f;
    }

    void startBowing()
    {
        fbTime = 0;
        chooseHarmonic (std::exp (logTarget));
        state = Bowing;
        logCur = logTarget;
        activeMode = params.mode; modeSwitching = false; modeFade = 1.0f;
        injectSamples = (int) (0.06 * sr);
        updateFilters (std::exp (logCur));
    }

    void goIdle()
    {
        state = Idle;
        swell = swellLin = 0.0f;
        std::fill (str.begin(), str.end(), 0.0f);
        lpState = dcX1 = dcY1 = 0.0f; aEnv = 0.0f; bpZ1 = bpZ2 = 0.0f;
    }

    // nouvelle note liée : le larsen retombe et repartira sur l'harmonique de la nouvelle note
    void noteChanged()
    {
        fbTime = 0;
        fbMix *= 0.25f;
        chooseHarmonic (std::exp (logTarget));
    }

    // Résonances virtuelles de l'ampli, du baffle et de la pièce (Hz)
    static constexpr int kNumRes = 7, kMaxHarm = 12;
    static constexpr double kRes[kNumRes] = { 500.0, 640.0, 820.0, 1050.0, 1340.0, 1720.0, 2200.0 };

    static double harmScore (double f0, int h, const double* res)
    {
        double d = 1.0e9;
        for (int i = 0; i < kNumRes; ++i) d = std::min (d, std::fabs (std::log (f0 * h / res[i])));
        return d + 0.03 * (h - 2);
    }

    // Harmonique qui "accroche" : celle qui tombe le plus près d'une résonance de l'ampli / de la pièce.
    // Elle dépend donc de la note jouée, et deux notes différentes successives ne prennent jamais la même.
    // Les résonances sont figées pendant toute la note tenue : un bend relâché retrouve toujours l'harmonique de départ.
    void chooseHarmonic (double f0)
    {
        for (int i = 0; i < kNumRes; ++i) resSnap[i] = kRes[i] * (1.0 + 0.035 * resDrift[i]);
        int best = 2, second = 0;
        double bestScore = 1.0e9, secondScore = 1.0e9;
        for (int h = 2; h <= kMaxHarm; ++h)
        {
            if (f0 * h > 2800.0) break;
            const double score = harmScore (f0, h, resSnap);
            if (score < bestScore) { second = best; secondScore = bestScore; best = h; bestScore = score; }
            else if (score < secondScore) { second = h; secondScore = score; }
        }
        // parmi les harmoniques qui accrochent bien, la note elle-même départage
        const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (f0 / 440.0));
        int pick = best;
        if (second >= 2 && secondScore < bestScore + 0.22 && ((midi * 7 + 3) % 3) == 0)
            pick = second;
        // une nouvelle note ne reprend jamais l'harmonique de la note tenue précédente
        // (les hauteurs transitoires de l'attaque ne comptent pas : seule une note tenue 0,3 s sert de référence)
        const bool otherNote = stableF0 > 0.0 && std::fabs (std::log (f0 / stableF0)) > 0.03;
        if (otherNote && pick == stableHarm && second >= 2 && secondScore < 1.0e8)
            pick = (pick == best ? second : best);
        fbHarm = pick; lastHarm = pick; lastHarmF0 = f0;
        baseHarm = pick; baseLog = std::log (f0);
        swGain = 1.0f; swDown = false; pendingHarm = pick;
    }

    // YIN (de Cheveigné & Kawahara 2002)
    void analyse (float gate)
    {
        ++rawAge;
        const int n = yinW + tauMax;
        int start = (yinW_idx - n + yinSize * 4) % yinSize;
        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = yinRing[(size_t) ((start + i) % yinSize)];
            frame[(size_t) i] = v;
            if (i >= tauMax) energy += (double) v * v;
        }
        lastFrameLevel = (float) std::sqrt (energy / yinW);
        if (lastFrameLevel < gate * 0.7f) return;
        // juste après une attaque, la trame contient encore l'ancienne note
        if (state == Waiting && sinceOnset < (int) (0.025 * sr)) return;

        // fonction de différence, mise en forme sur la fin de la trame
        const float* xs = frame.data() + tauMax;
        dBuf[0] = 1.0f;
        double running = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            const float* xt = xs - tau;
            for (int j = 0; j < yinW; ++j) { const float df = xs[j] - xt[j]; d += df * df; }
            running += d;
            dBuf[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (dBuf[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && dBuf[(size_t) tau + 1] < dBuf[(size_t) tau]) ++tau;
                best = tau; break;
            }
        }
        if (best < 0) return;                      // pas de hauteur fiable

        const float a = dBuf[(size_t) best - 1], b = dBuf[(size_t) best], c = dBuf[(size_t) best + 1];
        const float den = a - 2.0f * b + c;
        const double shift = std::fabs (den) > 1.0e-9f ? 0.5 * (a - c) / den : 0.0;
        const double f0 = fsd / ((double) best + std::clamp (shift, -0.5, 0.5));
        if (f0 < kMinHz * 0.95 || f0 > kMaxHz) return;

        const double lf = std::log (f0);
        const bool rawRecent = rawAge <= 4;                 // mesure précédente il y a moins de ~20 ms
        const double jump = rawRecent ? 12.0 * (lf - lastRaw) / std::log (2.0) : 99.0;
        lastRaw = lf; rawAge = 0;

        if (! havePitch || ! freshPitch)
        {
            // première mesure après une attaque : on saute directement
            if (state == Waiting || ! havePitch)
            {
                logTarget = lf; havePitch = true; freshPitch = true; changeCount = 0;
                return;
            }
        }

        const double semis = 12.0 * (lf - logTarget) / std::log (2.0);
        if (std::fabs (semis) > 0.45)
        {
            const double fromCand = 12.0 * (lf - candidate) / std::log (2.0);
            if (changeCount > 0 && std::fabs (fromCand) < 0.35)
            {
                // saut de note confirmé (hammer-on, pull-off, slide, nouvelle frette) : GLIDE s'applique
                logTarget = lf; changeCount = 0; glideActive = true;
                if (state == Bowing) { injectSamples = (int) (0.04 * sr); noteChanged(); }
            }
            else if (std::fabs (jump) < 0.6)
            {
                // la hauteur glisse en continu : c'est un bend, suivi immédiat sans GLIDE
                logTarget = lf; changeCount = 0; glideActive = false;
            }
            else
            {
                changeCount = 1; candidate = lf;          // saut brusque : on attend une confirmation
            }
        }
        else
        {
            changeCount = 0;
            logTarget += (lf - logTarget) * 0.6;        // vibrato et petits bends
        }
    }

    BowParams params;
    double sr = 48000.0, fsd = 24000.0;
    int decim = 2;

    std::vector<float> yinRing, frame, dBuf;
    int yinSize = 2048, yinW = 512, tauMax = 343, tauMin = 17, hopD = 120;
    int yinW_idx = 0, decimCount = 0, hopCount = 0;
    float decimAcc = 0.0f, lastFrameLevel = 0.0f;

    std::vector<float> str, hist;
    int mask = 0, wIdx = 0, hIdx = 0;

    float fastEnv = 0, slowEnv = 0, fastA = 0, fastR = 0, slowA = 0, slowR = 0;
    float lpA = 0.5f, lpState = 0, dcX1 = 0, dcY1 = 0;
    double filtDelay = 0.0;
    int filterCount = 0;
    float aEnv = 0, agcK = 0.001f, dcR = 0.9997f;
    float bpB0 = 0, bpA1 = 0, bpA2 = 0, bpZ1 = 0, bpZ2 = 0;

    State state = Idle;
    bool havePitch = false, freshPitch = false;
    int changeCount = 0, rawAge = 100;
    bool glideActive = false;
    double lastRaw = 0.0;
    double logTarget = std::log (220.0), logCur = std::log (220.0), candidate = 0.0;

    float swellLin = 0, swell = 0, susGain = 1.0f;
    int sinceOnset = 1 << 30, quietTime = 0, injectSamples = 0;

    int activeMode = 0; bool modeSwitching = false; float modeFade = 1.0f;

    double lfoPhase = 0.0;
    float driftA = 0, driftB = 0, driftTarget = 0;
    int driftCount = 0;
    uint32_t rng = 0x1234567u;

    // larsen
    float fbMix = 0.0f; long long fbTime = 0; double fbPhase = 0.0, throbPhase = 0.0;
    int fbHarm = 2, lastHarm = 0, resCount = 0; double lastHarmF0 = 0.0;
    float resDrift[7] {}, resTarget[7] {};
    double resSnap[7] { 500.0, 640.0, 820.0, 1050.0, 1340.0, 1720.0, 2200.0 };
    int baseHarm = 2, pendingHarm = 2, evalCount = 0, stableHarm = 0; double baseLog = 0.0, stableF0 = 0.0;
    float swGain = 1.0f; bool swDown = false;
};

} // namespace wiga
