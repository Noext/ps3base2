# La trace par branchement, et pourquoi la fenetre n'a pas marche

## Ce qui est en place

`bc_cond` instrumente sous `-DPS3BASE_BLOCK_TRACE=1`, avec les flags **aussi** sur les `mc_*.cpp` :

    for f in "$ROOT"/out/ppu/*.cpp; do
        "$EMCC" "${CXXFLAGS[@]}" -DPS3BASE_NO_RENDERER=1 \
            -DPS3BASE_CALL_TARGET_CHECK=1 -DPS3BASE_BLOCK_TRACE=1 -c "$f" -o "$o"
    done

Verifie : `201` branchements enregistres sur le boot (round 213).

## La tentative de fenetre, et son echec

La trace globale est trop bruyante. J'ai voulu l'ouvrir par une garde sur l'entree de `f_002da36c`, la
fonction qui lit l'objet nul.

    hooks::add_guard(0x2da36cu, [](ppu::Context&) { g_bt_on = true; return false; });

**Resultat : `window=0`, et `hooks installed` n'apparait plus.** La garde casse le boot : le run s'arrete avant
d'installer les hooks, donc **avant** que la garde elle-meme puisse servir.

Je l'ai retiree ; apres retrait, `entry544=1` revient et le run rejoue normalement.

## Ce que l'echec enseigne

Poser une garde sur `0x2da36c` n'est pas neutre : `add_guard` **arme `hook_impl`** et **pose le drapeau**, donc
le wrapper de cette fonction passe par `hook_call` au lieu d'executer son corps directement. Pour la plupart des
fonctions c'est sans consequence, mais celle-ci est appelee tres tot dans l'initialisation, et le detour change
l'ordre.

C'est la meme famille de probleme que les rounds 179 et 193 : **une garde modifie ce qu'elle observe**.

## Ce qui reste vrai

| element | etat |
|---|---|
| `bc_cond` transporte un crochet | **oui, teste** |
| les `mc_*.cpp` recoivent les `-D` | **oui, corrige ce round** |
| trace globale | **201 branchements** |
| fenetre par garde d'entree | **non** — casse le boot |

## Une autre voie pour la fenetre

Filtrer **a la sortie** plutot qu'a l'entree : la fonction de trace compare `c.lr` a la plage de
`f_002da36c`, sans rien modifier dans le runtime. C'est une ligne dans `ps3base_block_trace`, et cela ne
touche pas au chemin d'execution.

C'est la prochaine etape, et elle ne peut pas casser le boot.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |