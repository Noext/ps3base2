# I1 — La regle refs/ etait morte : le lien n'existait pas

## Ce que j'ai trouve en appliquant AGENTS.md

AGENTS.md:114-137 impose `grep -Rn "<symptome>" refs/` **avant** toute hypothese. J'ai applique la
regle pour le blocage des entrees. Resultat : **`refs/` ne contenait rien**.

    $ ls -la refs 2>/dev/null   -> (le repertoire n'existait pas dans wasm-base)
    $ ls -la /root/Dev/ps3base2/refs -> refs -> /root/Dev/ps3base/refs
    $ ls /root/Dev/ps3base/refs     -> (vide, 0 entree)

Trois liens casses en cascade : `wasm-base/refs` absent, `ps3base2/refs` pointe vers
`/root/Dev/ps3base/refs` qui est un repertoire VIDE.

**Tous les `grep -R refs/` de cette session ont donc rendu zero, et je l'ai lu comme « aucun
antecedent ».** C'est exactement le mode d'echec que l'AGENTS.md decrit pour `grep -r` au lieu de
`grep -R` : la commande rend vide et la regle echoue en silence. Ici la cause etait differente — le
lien — mais l'effet identique.

## Repare

    $ mkdir -p /root/Dev/wasm-base/refs && cd refs
    $ ln -s /root/Dev/port-flow flow
    $ ln -s /root/Dev/port-saintsrow2 saintsrow2
    $ ln -s /root/Dev/port-allstars-ps3 allstars-ps3
    $ ln -s /root/Dev/midway-ps3 midway-ps3
    $ ln -s /root/Dev/rpcs3 rpcs3

Verifie : `grep -Rl cellPadGetData refs/` rend maintenant des fichiers. `refs` est dans
`.gitignore`, donc l'ajout ne pollue pas les diffs.

## Le premier antecedent exploitable, enfin

`refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPad.cpp:695` :

    error_code cellPadGetData(u32 port_no, vm::ptr<CellPadData> data)

RPCS3 implemente `cellPadGetData` et `cellPadGetDataExtra` (ligne 843). Notre table d'imports liste
`cellPadGetData` (NID `0x8B72CDA1`) mais un run de 148 s ne l'appelle **jamais**.

## Ce que cela change

Les pistes qui suivent doivent refaire leur grep d'antecedents **maintenant que refs/ fonctionne**.
Les rapports qui disent « 0 ligne dans refs/ » avant 21:53 sont a rejouer : le resultat etait
l'absence du lien, pas l'absence d'antecedent.