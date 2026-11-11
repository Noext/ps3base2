# Le lift a ete refait, la fonction manquante existe

## Ce que le round 186 avait conclu a tort

J'avais conclu que le firmware etait absent. Il etait present, dans /root/Dev/GoWA-pc/bin/01.12/ -- un autre
checkout du meme projet. Un find sur la machine l'a trouve.

C'est la troisieme attribution retiree sur ce blocage.

## Ce que j'ai fait

| etape | commande |
|---|---|
| lier le firmware | ln -s /root/Dev/GoWA-pc/bin/01.12/GOWA*.elf bin/01.12/ |
| ecrire le seed | out-mc/gen/indirect_seeds.txt contient 0x000f656c mc |
| relancer le lift | setup.py --steps ppu --images recomp/images.json --out-root out-mc --gen-root out-mc/gen |

## Le resultat

    seeds: 1 indirect target(s) from out-mc/gen/indirect_seeds.txt
    exit=0
    total functions 23302

f_000f656c existe maintenant :

| verification | resultat |
|---|---|
| corps dans out-mc/ppu/mc_009.cpp | 1 548 octets |
| entree de table {0x000f656c, f_000f656c} | presente |
| declaration dans funcs.h | presente |
| total de fonctions | 22 685 -> 23 302 |

## Ce que ca etablit

Le mecanisme de decouverte fonctionne de bout en bout. Le round 185 avait rendu PS3BASE_DISCOVERY utilisable
sous wasm ; ce round a produit le seed et l'a fait lifter.

La fonction manquante qui bloquait le chemin du monde existe maintenant.

## Ce qui reste

Le nouveau out-mc/ppu doit etre compile et lie dans le module wasm, puis le run relance pour voir si le
chemin progresse au-dela de 0xf656c.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; le lift a ete complete, reste a le lier |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |