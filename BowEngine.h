// =============================================================================
//  Infinite Bow (Studio Wiga) - moteur DSP
//  Simulation d'un archet électromagnétique (type EBow) :
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
        yinW = (int) std::lround (fsd * 0.022);                // ~22 ms d'intégration
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
        const float gk = 1.0f - (float) std::exp (-1.0 / (std::max (1.0f, params.glideMs) * 0.001 * sr));
        logCur += (logTarget - logCur) * gk;

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
        return out * swell * modeFade * wob * 0.6f;
    }

    // --------- télémétrie pour l'interface ---------
    float getPitchHz() const  { return havePitch ? (float) std::exp (logTarget) : 0.0f; }
    int   getState() const    { return (int) state; }
    float getStringLevel() const { return std::min (1.0f, aEnv / kTarget) * swell; }

private:
    static constexpr double kMinHz = 70.0, kMaxHz = 1400.0;
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

    // YIN (de Cheveigné & Kawahara 2002)
    void analyse (float gate)
    {
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
            // note liée (hammer-on, slide) : on confirme sur deux mesures
            const double fromCand = 12.0 * (lf - candidate) / std::log (2.0);
            changeCount = (changeCount > 0 && std::fabs (fromCand) < 0.35) ? changeCount + 1 : 1;
            candidate = lf;
            if (changeCount >= 2)
            {
                logTarget = lf; changeCount = 0;
                if (state == Bowing) injectSamples = (int) (0.04 * sr);
            }
        }
        else
        {
            changeCount = 0;
            logTarget += (lf - logTarget) * 0.35;   // suit le vibrato des doigts
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
    int changeCount = 0;
    double logTarget = std::log (220.0), logCur = std::log (220.0), candidate = 0.0;

    float swellLin = 0, swell = 0, susGain = 1.0f;
    int sinceOnset = 1 << 30, quietTime = 0, injectSamples = 0;

    int activeMode = 0; bool modeSwitching = false; float modeFade = 1.0f;

    double lfoPhase = 0.0;
    float driftA = 0, driftB = 0, driftTarget = 0;
    int driftCount = 0;
    uint32_t rng = 0x1234567u;
};

} // namespace wiga
