# Jalon 4 : le ring audio pour l'AudioWorklet

## Ce que le prompt demande

> AudioWorklet alimente par memoire partagee.

## Ce qui existait

L'arm wasm de cellAudio.cpp jetait les echantillons. Le commentaire disait que le jalon 4 brancherait un
AudioWorklet sur cette couture.

## Ce que j'ai ecrit

| fichier | role |
|---|---|
| runtime/include/runtime/audio_ring.h | ring stereo f32, producteur/consommateur |
| runtime/src/gpu/audio_ring.cpp | l'implementation |
| tests/audio_ring.cpp | la suite |

## Le choix de conception qui compte

Le ring jette au lieu de bloquer. Le callback audio ne doit jamais attendre : si le ring est plein, les
echantillons les plus recents sont abandonnes et comptes.

| comportement | ce qu'il revele |
|---|---|
| jette chaque bloc pendant une seconde | emulateur affame |
| ne jette jamais | worklet qui ne consomme pas |

Les deux sont visibles dans les compteurs, ce qui est la seule facon de les distinguer de l'exterieur.

## Le raccordement

Deux symboles C exposes par host.cpp, remplis par la page avant le demarrage :
g_audio_ring_memory et g_audio_ring_frames.

Zero signifie sans peripherique : le HLE compte et jette, exactement comme avant. C'est ce qui permet au
run Node sans worklet de continuer a fonctionner.

## Le test

    audio_ring: 0 failures

Couverture : aller-retour stereo exact, ring plein qui jette et compte sans ecraser, wrap dans les deux sens
sur 200 rounds, lecture courte qui laisse le buffer de l'appelant intact.

## Le resultat du run

    menu: 1
    crash: 0
    t=118s

Le module tourne, la ligne de menu reste emise, aucun crash.

## Ce que je n'ai pas fait

Je n'ai pas ecrit le code JavaScript de l'AudioWorklet. Le seam C++ est pret ; cote page, il faut un
AudioWorkletProcessor qui lit le ring dans process(). C'est du JavaScript, non testable ici sans navigateur.

## Position

| critere du jalon 4 | etat |
|---|---|
| ring de commandes | fait, teste |
| boucle qui cede | fait, mesure |
| ring audio | fait, teste, raccorde |
| AudioWorklet JavaScript | non |
| worker / WebGPU dans la page | non |
| page dans Chrome | non |