# La structure a 0xd72800 n'est pas `CellGcmContextData`

## La mesure, elargie

Probe sur tout `0x00d72800`-`0x00d72840` pendant le boot :

    [d7store] ea=0x00d7280c size=4 v=0x78f2bc00   -> valeur 0x00bcf278
    [d7store] ea=0x00d72810 size=4 v=0xa0f3bc00   -> valeur 0x00bcf3a0
    [d7store] ea=0x00d72814 size=4 v=0xa8f3bc00   -> valeur 0x00bcf3a8

**Trois ecritures, toutes apres `+0x0c`.**

| offset | ecrit ? | contenu |
|---|---|---|
| `+0x00` (`begin`) | **non** | 0 |
| `+0x04` (`end`) | **non** | 0 |
| `+0x08` (`current`) | **non** | 0 |
| `+0x0c` | oui | `0x00bcf278` |
| `+0x10` | oui | `0x00bcf3a0` |
| `+0x14` | oui | `0x00bcf3a8` |

Les trois valeurs sont des **pointeurs dans le code de `mc`** (`0x00bc????`), pas des adresses de tampon GCM.

## Ce que ca invalide

| affirmation | statut |
|---|---|
| la structure a `0xd72800` est `CellGcmContextData` | **faux** |
| `callback` occupe `+0x0c` | **faux** — trois champs distincts sont ecrits la |
| `f_330005ec` ecrit `begin`/`end` a `[r30+0]`/`[r30+4]` | **il ne les ecrit pas ici** — `r30` n'est pas `0xd72800` |

## Ce que la structure est vraisemblablement

Trois pointeurs consecutifs vers du code = une **table de fonctions** (descripteurs OPD ou un tableau de
methodes), pas une structure de donnees. `0x00bcf278`, `0x00bcf3a0` et `0x00bcf3a8` sont espaces de 0x128 et
0x8 octets — des OPD de 8 octets ou des fonctions.

## Ce que ca change pour la suite

Le `context = 0xd72800` passe a `_cellGcmInitBody` **n'est pas** un `CellGcmContextData`. La reference
attend `vm::pptr<CellGcmContextData>` ; le jeu passe autre chose, ou le wrapPER `f_005d27d0` charge la
mauvaise valeur depuis le TOC.

C'est la piste du round 228 — le wrapper charge `gpr[3] = ld32(r2 - 12164)` — qui redevient la plus
probable.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 737 751 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |

---

# Les trois pointeurs ne sont pas des entrees de table

    python3 -c "..."  sur table.cpp

| valeur | entree de table ? |
|---|---|
| `0x00bcf278` | **non** |
| `0x00bcf3a0` | **non** |
| `0x00bcf3a8` | **non** |

Ce ne sont donc ni des fonctions liftées ni des exports. Ce sont des adresses de **donnees** dans l'image `mc`
(qui s'etend jusqu'a `0x8af1ec` pour le texte, mais `0xbcxxxx` est dans sa zone de donnees).

Trois pointeurs consecutifs vers des donnees de `mc` a `0xd7280c`, `+0x10`, `+0x14` : c'est une structure du
**jeu**, pas du firmware GCM.

## Ce que ca implique pour la these du wrapper

Le wrapper `f_005d27d0` charge `gpr[3] = ld32(r2 - 12164)`, c'est-a-dire une valeur globale du jeu. Si cette
globale pointe vers une structure du jeu a `0xd72800`, alors **le jeu passe bien sa propre structure** et c'est
conforme : `_cellGcmInitBody` recoit un `CellGcmContextData` qui doit etre rempli par le jeu avant l'appel.

Or seuls trois champs **au-dela** de `+0x08` sont remplis. Les trois premiers — ceux que le corps GCM lit — ne
le sont pas.

## La question, resserree encore

Le jeu remplit `+0x0c`, `+0x10`, `+0x14` mais pas `+0x00`, `+0x04`, `+0x08`. Si le jeu devait remplir les six,
alors **l'ecriture des trois premiers est du code non atteint** — et c'est la piste de la decouverte
indirecte, jamais relancee depuis le round 187.
