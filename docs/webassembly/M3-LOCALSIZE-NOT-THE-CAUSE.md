# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Profil RSX : 0 surface, 0 texture | `M3-WRITES-PROFILE.md` (round 92) | oui |
| Assets et bibliothèques identiques | `M3-ASSETS-IDENTICAL.md` (round 93) | oui |
| Hypothèse : `cellGcmGetConfiguration` renvoie de mauvaises valeurs | round 93 | **testée** |

# La correction appliquée, et son absence d'effet

## Ce que RPCS3 déclare

    gcm_cfg.local_size = 0xf900000;              // 249 MB
    gcm_cfg.local_addr = rsx::constants::local_mem_base;   // 0xC0000000

## Ce que nous déclarions

    vm::write32(p + 0, 0xC0000000u);   /* localAddress  -- CORRECT */
    vm::write32(p + 4, 0x00200000u);   /* localSize     -- 2 MB, 124x trop petit */

L'adresse était juste, la **taille était fausse** : 2 Mo au lieu de 249 Mo.

## La correction

    vm::write32(p + 4, 0x0F900000u);   /* localSize, as RPCS3 reports it */

## Le résultat

    texmask=0x0  shaders vp=242098 fp=242098 | fb tris=4057659
    (aucune ligne 'surface regs' avec des valeurs non nulles)

**Aucun changement.** `texmask` reste à zéro, aucune surface n'est créée.

## Ce que ça établit

La valeur de `localSize` **n'est pas** ce qui empêche le jeu de configurer une surface. L'hypothèse du
round 93 est **éliminée**.

La correction est **conservée** : elle aligne notre HLE sur la référence, ce qui est juste
indépendamment de son effet. Un HLE qui annonce 2 Mo de mémoire vidéo quand la console en a 249 est faux,
même si ce n'est pas la cause du blocage.

## Ce que ça élimine de plus

| hypothèse | statut |
|---|---|
| liaison de texture (rounds 79-90) | en aval, pas la cause première |
| allocation de surface (round 91) | réelle, mais le jeu ne la demande pas |
| ordre du travail (round 93) | décrit, pas expliqué |
| **`localSize` trop petit (round 93)** | **éliminée par la mesure** |

## Où en est le diagnostic

Le guest écrit : 430k sémaphores, 2,9 M draws, 0 surface, 0 texture. Il ne demande **jamais** de cible
de rendu, ni avec 2 Mo ni avec 249 Mo de mémoire vidéo annoncée.

Donc le jeu n'atteint pas le code qui configure une surface — et ce n'est pas une question de valeur
renvoyée par le HLE RSX.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **teste une hypothèse et l'élimine**, tout en corrigeant une valeur
fausse de notre HLE (2 Mo au lieu de 249 Mo).
