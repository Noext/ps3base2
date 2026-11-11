# Ce que RPCS3 fait et que nous ne faisons pas

## L'antecedent (regle `refs/` d'abord)

| constat | source | s'applique ici ? |
|---|---|---|
| RPCS3 valide la cible d'un appel indirect avant de l'appeler | `refs/rpcs3/rpcs3/Emu/Cell/PPUThread.cpp:903` — `if (from_opd && !vm::check_addr<sizeof(ppu_func_opd_t)>(direct_target))` | **oui** — c'est exactement la situation |
| RPCS3 evite de lire une memoire non mappee sous mutex | meme ligne, commentaire | oui, en esprit |
| notre `call_indirect` ne valide rien | `runtime/src/ppu_runtime.cpp:245` | **c'est la lacune** |

## La mesure

    [null-call-source] target=0x00000000 lr=0x005d2c08 ctr=0x00000000
                      r3=0x0 r9=0x0 r31=0x0

Les trois registres qui portent l'objet sont **nuls**. La cible est nulle. Le `lr` nomme la fonction
appelante.

## La difference avec RPCS3

| | RPCS3 | nous |
|---|---|---|
| cible non mappee | `check_addr` echoue, l'appel est refuse et signale | **on appelle quand meme** |
| acces memoire non mappe | faute traduite en erreur VM | **rend zero** (`calloc` de 2304 Mo) |
| consequence | erreur au point fautif | erreur **2000 instructions plus loin**, sans rapport apparent |

## Pourquoi c'est important pour le portage

Le vrai defaut n'est pas que le jeu lise une zone non allouee — c'est que **notre backend memoire ne le dit
pas**. Un `calloc` de 2304 Mo rend toute adresse valide, donc :

- un pointeur nul passe silencieusement,
- un champ non initialise passe silencieusement,
- une allocation manquante passe silencieusement,

et l'erreur surgit ailleurs. C'est la cause des vingt rounds que j'ai passes a chercher au mauvais endroit.

## Ce qu'il faudrait, et que le prompt autorise

Le prompt du jalon 5 interdit de modifier le runtime sauf **derriere une interface**. Une validation de cible
dans `call_indirect` est exactement cela : un point de controle, pas un changement de comportement correct.

La forme minimale, calquee sur RPCS3 :

    if (addr == 0 || !vm::range_mapped(addr, 4)) {
        report_once(addr, ctx);
        /* stop, like RPCS3 refuses the call */
    }

Je ne l'ai pas implemente dans ce round : il change le comportement en cas d'erreur, et le prompt exige de
mesurer d'abord.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |