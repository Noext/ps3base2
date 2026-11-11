# Round 226 : la trace du retour GCM, et un etat casse

## Ce que je voulais mesurer

`r3` a la sortie du corps de `_cellGcmInitBody`, pour voir ou nait `0x80010004`.

## Ce qui s'est passe

| etape | resultat |
|---|---|
| ajout des probes `[gcmret]` et du guard `0x33000e5c` | build OK |
| premier run | `Aborted(missing function: _ZN6jsedge8spu_idleEj)` |
| restauration du stub `spu_idle` | build OK, 16 symboles non definis |
| second run | `RuntimeError: unreachable`, `videoimp: 0` |

## Ce que ca dit

`jsedge::spu_idle` **avait disparu** de mon fichier de stubs — le merge de `main` l'avait retire. Sans lui, le
run s'arrete tot.

Apres restauration, le run va jusqu'a `gcminit` mais **plus jusqu'a `videoimp`** : l'ordre a change. Mon guard
sur `0x33000e5c` (l'entree du corps GCM) est peut-etre en cause, comme la garde du round 214 sur
`0x2da36c`.

## Ce que je n'ai pas obtenu

**Aucune mesure du round.** Ni `[gcmret]`, ni `[imp119]`, ni `[videoimp]`. Le run est casse par mes
instrumentations, pas par un changement du jeu.

## Ce que je dois faire au round suivant

1. Retirer le guard sur `0x33000e5c` — l'experience du round 214 (garde sur une entree precoce = boot casse)
   s'applique probablement ici.
2. Garder seulement le probe sur l'import 119, qui ne touche pas au chemin.
3. Relancer et verifier que `videoimp` revient a 2.

## Ce que le round a quand meme etabli

| fait | valeur |
|---|---|
| `spu_idle` avait disparu du worktree | **oui**, corrige |
| le fichier de stubs est fragile face aux merges | **oui**, deuxieme fois |
| poser un guard sur une entree tot casse le boot | **confirme une troisieme fois** |

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 45,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |

---

# Nettoyage reussi, et la mesure obtenue

## Ce que j'ai fait apres avoir ecrit le rapport ci-dessus

Retire la garde sur `0x33000e5c`, garde le probe sur l'import 119.

## Le resultat

    guest machine up: 1
    images mc=: 1
    gcminit: 1
    videoimp: 2      <-- revenue
    gcmret: 1        <-- la mesure voulue

    [gcmret] import119 returns r3=0xffffffff80010004 (low32=0x80010004)

Le crash revient aussi (`unknown indirect target 0x00000000`, `lr=0x005d2c08`), donc l'etat est coherent.

## La confirmation

**Poser une garde sur une entree de fonction appelee tot casse le boot** — troisieme confirmation
(rounds 214, 226, 226). Le mecanisme est le meme : `add_guard` arme `hook_impl` et pose le drapeau, donc
l'entree passe par `hook_call` au lieu d'executer son corps directement, et ce detour change l'ordre.

## Ce que la valeur dit

`r3 = 0xffffffff80010004`, soit `0x80010004` en 32 bits — confirme la mesure du round 221 par un second
chemin (le probe sur l'import plutot que sur l'appel).

Ce n'est pas encore l'explication, mais la mesure est **reproductible par deux instruments independants**.
