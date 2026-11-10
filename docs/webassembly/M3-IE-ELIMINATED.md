# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `interrupts_enabled = image_entry & 1` à l'init du thread SPU | `runtime/src/spu/thread.cpp:1351` | oui — origine de `ie` |
| Le natif reçoit `entry 0x848` (bit 0 = 0) | `/tmp/cmp.log` `thread_initialize` | **oui — et il réussit** |
| `ie 0` dans l'état final des deux côtés | `/tmp/cmp.log`, `/tmp/f4.log` | **oui — ce n'est pas la cause** |
| L'AOT est branché : `aot::find`, 170 fichiers compilés | `runtime/src/spu/thread.cpp:1814` | oui — vérifié sain |

# Une piste éliminée par la mesure

Le round 60 disait : « `ie = 0` ferme la seule sortie ». Ce round a vérifié.

## Le test

`interrupts_enabled` vient du **bit 0** de l'entrée du thread SPU :

    ctx.pc = image_entry & 0x3FFFC;
    ctx.interrupts_enabled = image_entry & 1;

Le natif journalise cette entrée :

    spu: thread_initialize 'MinecraftCellSpursKernel0' slot 0 group 0x4000100 entry 0x848 -> 0x100

**`0x848` a son bit 0 à zéro** : le natif démarre donc ses SPU avec `ie = 0` **aussi**.

Et dans l'état final :

| | natif | wasm |
|---|---|---|
| `ie` | **0** | **0** |

**Identique.** `ie = 0` n'est pas la cause de l'écart de 93 millions, et le round 60 avait tort
sur ce point.

## Ce que la mesure élimine

Trois hypothèses sont maintenant fermées :

1. **`ie = 0` bloque le SPU** — faux, le natif a la même valeur et réussit.
2. **Le décrémenteur ne tire jamais** — faux (round 61), il tire à t=32,8 s.
3. **L'AOT ne trouve pas les blocs** — la logique est saine (`aot::find`, 170 fichiers, cache par
   pc) et le SPU exécute bien ~12 000 instructions avant de s'arrêter.

## Ce que reste, chiffré

Les `insns` du wasm **oscillent entre 12 288 et 13 312** puis se figent. Le SPU fait donc environ
**mille instructions**, sauvegarde son contexte (les `stqd` lus à `0x11ac` sont un prologue
d'écriture de registres), puis s'arrête dans `block()`.

Le natif en fait **287 milliards**. L'écart n'est donc pas dans une porte fermée mais dans
l'**absence de travail à faire** : le noyau SPURS se met en attente de tâches qu'aucun PPU ne lui
soumet.

C'est cohérent avec ce que le round 57 avait localisé : le wasm s'arrête après le 17ᵉ thread PPU,
avant que les PPU ne soumettent du travail aux SPU.

## Une tentative qui n'a pas abouti

J'ai voulu compter les entrées dans `block()` pour distinguer « une entrée qui attend
éternellement » de « une entrée par tick d'inactivité ». Deux essais de sonde ont échoué sur des
erreurs de déclaration (statiques locales utilisées avant définition), et j'ai restauré le fichier
plutôt que d'insister.

**Le correctif du décrémenteur du round 61 est préservé** (il était commité, lignes 228 et 233) ;
seul le code de sonde expérimental est parti. Build natif à 0 erreur, `spurs` passe.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **élimine une hypothèse** (le round 60 avait tort sur `ie = 0`)
et resserre le diagnostic : l'écart SPU n'est pas une porte fermée mais une absence de travail
soumis, ce qui renvoie au blocage PPU localisé au round 57.
