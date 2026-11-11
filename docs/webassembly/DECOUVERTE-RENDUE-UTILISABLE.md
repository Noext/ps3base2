# Le runtime sait deja decouvrir les cibles manquantes

## Ce que j'ai trouve dans notre propre code

`runtime/src/ppu_runtime.cpp:325-360` contient un **mode decouverte** deja ecrit :

    const char* disc = std::getenv("PS3BASE_DISCOVERY");
    if (disc && *disc && in_image) {
        ... fprintf(f, "0x%08x %s\n", addr, image::images[0].name);
        LOG_WARN("ppu", "discovery: stopping so the lift can add 0x%08x", addr);
        std::exit(3);
    }

Il enregistre chaque cible indirecte absente, **verifie qu'elle est dans une image** (pour ne pas enregistrer
un pointeur corrompu), puis s'arrete proprement au lieu de planter.

## Le probleme que j'ai corrige

`getenv` est **inerte sous wasm** — le mode etait donc inutilisable dans l'environnement qui en a besoin.

J'ai ajoute une variante compile-time :

    #if defined(PS3BASE_DISCOVERY_FILE)
            const char* disc = PS3BASE_DISCOVERY_FILE;
    #else
            const char* disc = std::getenv("PS3BASE_DISCOVERY");
    #endif

## La verification

    W 27.869405 ppu: discovery: indirect target 0x000f656c in [0x00010200,0x008aa674) recorded to ...
    W 27.869525 ppu: discovery: stopping so the lift can add 0x000f656c

**Le mecanisme fonctionne** : il identifie `0x000f656c`, confirme qu'elle est dans l'image du jeu, et sort en
code 3 au lieu de planter.

## Ce qui reste

Le fichier est ecrit dans le **systeme de fichiers virtuel d'Emscripten** (`MEMFS`), pas sur l'hote : il
n'apparait pas dans `/tmp`. L'adresse reste capturable par le log, qui la nomme explicitement.

Pour extraire le fichier il faudrait `FS.syncfs` ou un montage `NODEFS` — du JavaScript, hors du chemin C++.

## Ce que ca donne pour la suite

La decouverte est **iterative par construction** : `exit(3)` s'arrete a la premiere adresse manquante, le
lifter l'ajoute, on relance, l'adresse suivante apparait.

C'est un outil qui existe, qui fonctionne, et dont l'usage sous wasm est maintenant possible.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; le chemin du monde exige du code non lifte, et le runtime sait le designer |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |