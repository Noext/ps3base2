# La boucle de decouverte existe, mais le lift ne peut pas tourner ici

## Ce que j'ai trouve

tools/discover_loop.sh (66 lignes) fait exactement ce que le round 185 rendait possible :

    run -> seed -> relift -> rebuild -> repeat until nothing new

Son commentaire documente un cas reel et sa mesure :

> 0x2b0187a8 est un {code, toc} lu dans un champ de structure que le module remplit a l'execution.
> Aucun scanner statique ne peut le voir. Mesure : la boucle a converge en 2 rounds.

Il pilote tools/setup.py --steps ppu, qui accepte --images et lit indirect_seeds.txt.

## Le manifeste de notre jeu existe

    recomp/images.json  ->  mc (bin/mc/mc.elf), liblv2

bin/mc/mc.elf est present.

## Ce qui bloque

J'ai lance le lift avec le seed correct, dans out-mc/gen/indirect_seeds.txt. Reponse :

    exit=1
    missing bin/01.12/GOWA.patched.elf and bin/01.12/GOWA.elf

Le repertoire bin/01.12/ n'existe pas. Le firmware PS3 developpe (GOWA 01.12) n'est pas decrypte sur cette
machine.

| attendu | present |
|---|---|
| bin/01.12/GOWA.patched.elf | non |
| bin/01.12/GOWA.elf | non |
| bin/mc/ | oui |
| bin/gta/, bin/rdr/ | oui (autres titres) |

## Ce que ca signifie

Le lift PPU ne peut pas etre refait ici : il lui faut le firmware decrypte, une donnee que je ne peux pas
produire.

C'est le meme ordre de blocage que le binaire natif (pilote NVIDIA) : extérieur au code, mais cette fois
identifie en une commande au lieu de plusieurs rounds.

## Ce qui est acquis

| element | etat |
|---|---|
| le runtime enregistre les cibles manquantes | fait, verifie (round 185) |
| la boucle de decouverte existe | oui (tools/discover_loop.sh) |
| le manifeste du jeu existe | oui |
| le lifter accepte nos seeds | oui (il a lu le fichier) |
| le firmware requis | absent |

## Ce que ca donne a l'operateur

Une commande a executer quand le firmware sera disponible :

    tools/discover_loop.sh recomp/images.json build out-mc 10

Le chemin est trace de bout en bout ; il manque une entree.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; le chemin du monde exige un lift, le lift exige le firmware |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |