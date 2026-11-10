# Correction : les fragment programs sont bien soumis, mon hook était faux

## Ce que je viens de mesurer

Round 44 annonçait « 329 885 vertex programs soumis, **0 fragment programs** ». C'était
**faux**, et l'erreur était dans mon instrumentation.

J'avais accroché le compteur sur un registre (`0x0A04`) en supposant qu'une écriture de
registre déclenche la compilation du fragment program. En lisant le vrai renderer :

    rsx_renderer.cpp:2866:    VpEntry* vp = vertex_program();

Les deux programmes sont compilés **au moment du draw**, pas sur une écriture de registre.
Mon hook ne pouvait donc rien voir.

Accroché au draw, comme le renderer le fait :

    shaders vp=211553 fp=211553 fp_ok=211553
    shaders vp=225353 fp=225353 fp_ok=225353
    shaders vp=239130 fp=239130 fp_ok=239130

| | valeur |
|---|---|
| vertex programs | **239 130** |
| fragment programs | **239 130** |
| décompilés avec succès | **239 130** (100 %) |

**Un draw soumet un vertex program et un fragment program**, ce qui est exactement le
comportement attendu. Les deux compteurs sont identiques parce que mon stub appelle les deux
au même endroit — le vrai renderer fait de même.

## Ce que ça corrige dans la chaîne causale

Round 44 concluait :

    330k vertex programs soumis → aucun compilé → 93 % de flips vides

La première moitié est juste, la seconde était un artefact de mesure. La chaîne réelle est :

    239k draws, chacun avec un VP et un FP
      → ces programmes ne sont ni décompilés ni liés (le stub ne fait que compter)
        → les frames sortent vides
          → pas de contenu affiché
            → pas de ligne de menu

La conclusion générale ne change pas — le stub ne compile rien — mais le chiffre « 0 fragment
programs » était un défaut de mon instrument, pas une propriété du guest.

## Ce que ça confirme de solide

Le guest **soumet un travail de rendu parfaitement normal** : 239 130 draws, chacun avec ses
deux programmes. Il ne s'agit pas d'un jeu qui échoue à produire du travail graphique, mais
d'un backend graphique qui n'existe pas pour le consommer.

Et `fp_decompiler.cpp` — le générateur GLSL — **compile déjà pour wasm**. Le maillon
manquant reste la compilation GPU et la liaison aux pipelines, c'est-à-dire le jalon 2.

## Position

Le jalon 3 reste ouvert. Ce round produit une **correction de mesure** et non un progrès : le
chiffre `fp=0` du round 44 est retiré. La leçon est la même que dans les rounds précédents —
un hook placé au mauvais endroit produit un zéro qui ressemble à une découverte.
