# Round 203 : le worktree compilait l'arbre de l'autre agent

## La cause de vingt rounds de mesures etranges

`tools/build_wasm.sh` contenait :

    ROOT="${1:-/root/Dev/ps3base2}"

**Un chemin absolu code en dur.** Depuis `/root/Dev/ps3base2-wasm`, chaque `bash tools/build_wasm.sh`
compilait donc les sources de **l'arbre principal**, pas celles du worktree.

Toutes mes editions de `ppu_ops.cpp`, `ppu_runtime.cpp`, `hooks.cpp`, `wasm_entry.cpp` depuis la creation du
worktree etaient **compilees puis ignorees** : le script lisait les fichiers de `/root/Dev/ps3base2`.

C'est pour cela que :

| symptome | explication |
|---|---|
| les gardes ne tiraient jamais | mon code n'etait pas dans le binaire |
| le module faisait toujours 44 365 073 octets | c'etait le meme arbre source |
| `grep 'hookcall' game.wasm` = 0 | l'objet lie venait de l'autre arbre |
| relancer le build ne changeait rien | il recompilait l'autre arbre |

## La correction

    ROOT="${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"

Le script derive sa racine de **sa propre position**. Verifie : apres correction, le module a change de taille
(44 366 348 au lieu de 44 365 073) et mes probes ont tire pour la premiere fois.

## Ce que les probes ont revele

| probe | resultat |
|---|---|
| `[hookcall] addr=0x005d2b44 impl=armed` | **le wrapper s'execute**, `hook_impl` est arme |
| `[impl-entry] addr=0x005d2b44 in_map=1` | **le dispatcher recoit l'adresse**, le Hook existe |

Le mecanisme fonctionne donc **de bout en bout** dans le vrai runtime. Mes rounds 194-201 cherchaient un
defaut qui n'existait pas, dans un binaire qui n'etait pas le mien.

## Ce que j'ai appris

Le test isole du round 202 (`HOOK-MECHANISM-OK`) etait **juste** : il avait raison avant moi. Je l'ai cru
quand il a contredit ma piste, et c'est ce qui m'a fait chercher pourquoi le vrai run differait.

## Ce qui reste

Le module construit depuis le worktree s'arrete plus tot qu'avant (pas de crash, pas de probe). Le merge de
`main` a apporte 870 lignes de l'autre agent, dont un `cellSysutil.cpp` que j'ai du completer en copiant un
en-tete non commite. L'etat exact demande une mesure propre, pas une supposition.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |