# ARC (Studio Wiga)

Plugin VST3 (Windows / macOS) et AU (macOS) qui simule un archet électronique :
sustain infini, attaque en « swell » sans médiator, modes harmoniques, position de l'archet sur la corde.
Dans le DAW, il apparaît sous le nom **Arc**, fabricant **Studio Wiga**.

v2.6.0 : MIDI Learn au clic droit sur chaque commande (Clear MIDI, assignations sauvegardées avec le projet)
v2.5.1 : plus de "poc" ni de note qui décroche à chaque coup de médiator (la corde virtuelle n'est plus coupée à la réattaque, même note = son continu, autre note = changement sans silence ; pas de double déclenchement ; étouffement confirmé sur 20 ms)
v2.5.0 : anti-gel v2 (JUCE 8.0.15, caches refaits par le timer, chien de garde, aucun appel à l'hôte depuis le thread audio)

## Principe

1. **Suivi de la note** : le plugin détecte la hauteur de la note jouée (YIN, monophonique), les attaques et les étouffements.
2. **Corde virtuelle** : une corde modélisée, accordée sur la note, est excitée par la vraie corde puis entretenue par une boucle
   « capteur → ampli saturé → excitateur » avec contrôle automatique de gain.
3. **VOICE** : Normal, Harm (la corde vibre sur son 2e harmonique, une octave au-dessus) ou Harm+ (3e harmonique, octave + quinte).
4. **POSITION** : un filtre en peigne reproduit le placement de l'archet le long de la corde.

## La pédale

L'interface est le visuel ARC. Les potards, les LED, la ligne d'état et l'écran de corde sont de vrais contrôles animés.

### Bouton MODE

- **LED orange** : les potards règlent les réglages écrits en orange. Leurs arcs sont orange et la ligne d'état s'affiche en orange.
- **LED bleue** : les potards règlent les réglages écrits en bleu. Leurs arcs passent au bleu et la ligne d'état s'affiche en bleu.

Chaque réglage garde sa propre valeur. En changeant de mode, le potard se place sur la valeur de l'autre réglage.

| Potard | Orange | Bleu |
|---|---|---|
| 1 | ATTACK : montée du son | RELEASE : extinction |
| 2 | SUSTAIN : infini au maximum | SENS : seuil de détection |
| 3 | POSITION : du chevalet au milieu de la corde | BUZZ : la corde frise contre l'archet |
| 4 | TONE : brillance (arc blanc, même réglage dans les deux modes) | TONE |
| 5 | LIFE : vibrato et instabilités naturelles | GLIDE : portamento entre notes liées (sauts de frette uniquement) |
| 6 | BOW LEVEL : niveau du son d'archet | DRY : niveau du son direct |
| 7 | SPACE : réverbération | OUTPUT : volume de sortie |

Un double-clic sur un potard le remet à sa valeur par défaut.

### LED

| LED | Comportement |
|---|---|
| MODE | Orange ou bleue selon le mode |
| NORMAL / HARM / HARM+ | Seule la voix choisie est allumée |
| PRESETS 1-8 | Seul le preset chargé est allumé (orange pour 1-4, cyan pour 5-8) |
| BOW | Allumée quand l'archet est posé ; sa luminosité suit le son de la corde |
| HOLD | Allumée quand HOLD est actif, par le footswitch ou la pédale de sustain MIDI (CC64) |

### Bends et vibrato

Les bends et le vibrato des doigts sont suivis en 15 ms environ, quel que soit GLIDE. GLIDE ne s'applique qu'aux sauts de note (hammer-on, pull-off, slide).

### Preset 8 « Larsen »

La note tenue s'épanouit au bout d'environ une seconde vers une harmonique qui dépend de la note jouée : celle qui tombe le plus près des résonances d'un ampli et d'une pièce virtuels. Deux notes différentes à la suite ne prennent jamais la même harmonique. Pendant le larsen, un bend fait vaciller le larsen puis le fait sauter d'harmonique (parfois plusieurs fois sur un long bend) ; un vibrato large entre deux zones le fait papillonner ; en ramenant la corde à sa hauteur de départ, l'harmonique d'origine revient, sans réattaquer. Les bends restent suivis jusqu'à environ 30 dB sous SENS (ronflement ignoré), et une attaque forte fait accrocher le larsen plus vite. Le niveau reste celui du son normal et rien n'est renvoyé vers l'extérieur : aucun vrai larsen ne peut se former. La quantité est le paramètre caché « Feedback » (85 % dans le preset), automatisable dans le DAW. L'écran affiche `FEEDBACK x5` (par exemple) quand l'harmonique a pris le dessus.

### Écran et ligne d'état

- **Ligne d'état** : le dernier réglage touché, sa valeur, son équivalent MIDI (0-127) et le dernier CC reçu. Le texte est orange ou bleu selon le mode.
- **Écran de corde** : la corde vibre avec 1, 2 ou 3 ventres selon VOICE. Le bloc BOW se place selon POSITION. L'écran affiche aussi la note détectée, sa fréquence et l'état (IDLE, LISTENING, BOWING, RELEASE).

### Taille de la fenêtre

Clic droit sur la pédale pour choisir 60 % (par défaut, 614 × 494 px), 75 % ou 100 % (1023 × 823 px), comme Ring Think ; on peut aussi tirer le bord de la fenêtre entre 60 % et 100 %. La fenêtre ne peut pas être plus petite que sa taille par défaut.

## MIDI

- Les 13 réglages, VOICE, MODE (« Knob Mode »), BOW, HOLD et Feedback sont automatisables et assignables séparément en MIDI dans le DAW.
- La pédale de sustain (CC64) active HOLD tant qu'elle est enfoncée (assignation par défaut du MIDI Learn, réassignable).
- **MIDI Learn** : clic droit sur une commande (potard, MODE, VOICE, preset, BOW, HOLD) -> « MIDI Learn », puis bouger la pédale ou appuyer sur le footswitch MIDI. « Clear MIDI » retire l'assignation ; le sous-menu MIDI liste toutes les commandes, « Reset MIDI to defaults » et le mode des footswitches MIDI (127/0 ou bascule à chaque appui). Les assignations sont sauvegardées avec le projet.

## Conseils de jeu

- Jouez une note à la fois, comme avec un archet électronique.
- Placez le plugin en premier dans la chaîne. Les accordages graves sont suivis jusqu'au G#1 (52 Hz) : baryton, 7 cordes, drop. Un micro manche et un son clair donnent le suivi de note le plus fiable.
- Hammer-ons et slides sans repincer : la corde suit, avec le portamento réglé par GLIDE.
- Pour arrêter la note, étouffez la corde avec la paume.
- Footswitch BOW éteint = bypass : la guitare passe telle quelle (niveau d'origine, sans clic ni coupure), l'écran affiche BYPASS.

## Compilation via GitHub

1. Copiez tous les fichiers à la racine du dépôt, y compris `background.jpg` et le dossier caché `.github`.
2. L'onglet **Actions** lance « Build Arc » à chaque push.
3. Téléchargez les **Artifacts** : `Arc-Windows-VST3` et `Arc-macOS-VST3-AU`.

## Installation

**Windows**

Copiez `Arc.vst3` dans `C:\Program Files\Common Files\VST3\`, puis relancez le scan des plugins dans le DAW.

**macOS**

- VST3 : `~/Library/Audio/Plug-Ins/VST3/`
- AU : `~/Library/Audio/Plug-Ins/Components/`
- Si macOS bloque le plugin, exécutez dans le Terminal `xattr -cr` suivi du chemin du plugin.

## Fichiers

| Fichier | Contenu |
|---|---|
| `BowEngine.h` | Moteur DSP (indépendant de JUCE) |
| `PluginProcessor.*` | Paramètres, presets, MIDI, réverbération, mixage |
| `PluginEditor.*` | Interface sur le visuel ARC |
| `WindowMinSize.*` | Verrou : la fenêtre du DAW ne peut pas être plus petite que la pédale (Windows `.cpp`, macOS `.mm`) |
| `background.jpg` | Visuel ARC v2.4 (1525 × 1227, même taille de pédale que Ring Think), plaque grise lisse, écritures refaites, presets en LED, nettoyé : arcs, index, LED et écrans sont dessinés en direct |
| `ARC_Manual_EN_FR.pdf` | Notice complète, anglais puis français |
| `CMakeLists.txt` | Projet JUCE 8 (téléchargé automatiquement) |
| `.github/workflows/build.yml` | Compilation Windows (windows-2022) et macOS |
