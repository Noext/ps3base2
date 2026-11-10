# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| La plus grosse fonction est `f_006fe4d8`, 1 358 012 o | round 144 | oui |
| Elle n'a **pas de corps source** | round 144 | oui |
| **Aucune option de fusion de fonctions dans wasm-ld** | `wasm-ld --help` | **oui — la piste écartée** |

# La fusion ne vient pas du linker

## La vérification

`wasm-ld --help` ne propose que :

    --merge-data-segments      (fusion des SEGMENTS DE DONNÉES, par défaut)
    --no-merge-data-segments

**Aucune option de fusion de fonctions.** J'ai essayé `--no-merge-functions` : rejeté.

## Ce que ça signifie

Si wasm-ld ne fusionne pas de fonctions, `f_006fe4d8` vient d'**ailleurs** :

1. le compilateur (`emcc`), qui peut fusionner à `-Oz` ;
2. ou elle **existe** dans le code lifté sous un autre fichier que `mc_070.cpp`.

## Ce que je n'ai pas pu établir

La recherche de `f_006fe4d8_body` dans `mc_070.cpp` a donné 355 octets — mais j'ai cherché dans **un seul
fichier**. La fonction peut être dans un autre.

## Ce qu'il faut mesurer

Une recherche **globale** de `f_006fe4d8` dans `out/ppu/`. C'est trivial et je ne l'ai pas fait avant de
conclure à une fusion.

## Correction de méthode

J'ai conclu « c'est une fusion » après avoir cherché dans **un** fichier sur 170. C'est la **sixième fois**
dans cette campagne que je conclus d'un instrument trop étroit.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui (50,2 Mo)** |
| plus grosse fonction sous 1 Mo | non (1 358 ko) — **origine non établie** |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |