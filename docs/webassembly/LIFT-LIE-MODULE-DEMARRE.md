# Le lift complet est en place et compile ; le module demarre

## Ce qui a ete fait

| etape | resultat |
|---|---|
| substituer `out/ppu` par le nouveau lift | 88 fichiers, table 730 218 -> **752 353** octets |
| compiler les 88 modules PPU | **0 echec**, 88 objets |
| lier le module wasm | **42065964 octets** de wasm, 626 ko de JS |
| executer | demarre, code guest execute, **mc charge** |

## Le progres mesurable

    mc  ancien lift : text 0x00010200-0x008aa674
    mc  NOUVEAU lift : text 0x00010200-0x008af1ec

**+19 448 octets de code guest**, ce qui correspond a la fonction ajoutee et a ses voisines.

## Les obstacles de liaison traverses

| obstacle | cause | correction |
|---|---|---|
| `duplicate symbol: ppu::hook_flags` | `r_ppu_ops.o` compile contre l'ancienne table | recompile |
| `duplicate symbol: main` | `r_main.o` en double | ecarte |
| `undefined spu::hle::g_first` | objets de sonde `j_*` perimes | ecartes |
| `duplicate edge::ovl::stats_on` | `e_ovl_probe.o` du round 169 | ecarte |

## L'obstacle restant

    loader: loaded mc (bin/mc/mc.elf) text 0x00010200-0x008af1ec
    loader: cannot read ./out-mc/gen/reloc/liblv2.elf

Le runtime lit les modules LLE a un chemin **relatif**, et le nouveau manifeste les place dans
`out-mc/gen/reloc/`. L'ancien manifeste les prenait dans `fw/4.91/reloc/`, qui est deja monte dans le disque.

Le disque est un `--preload-file` **fige au link** : un lien symbolique ajoute apres coup n'y entre pas.

## Ce que ca etablit

**Le lift complet est en place, compile et lie.** Ce n'est plus un projet de correction : les octets sont dans
l'arbre et dans le binaire.

Ce qui reste est un probleme de **chemin de fichier**, pas de code.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; **lift complet, lie, module demarre** |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |