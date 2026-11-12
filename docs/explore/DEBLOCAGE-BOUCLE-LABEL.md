# Deblocage : la boucle de label est franchie, le jeu soumet des flips

## Ce qui a change (piste A, porte et verifie dans la base)

| mesure | avant (d290fda) | apres |
|---|---|---|
| pc echantillonne | `0x002da5b4` (usleep de la boucle de label) | **`0x002dc20c`** (boucle de trame) |
| `cellGcmGetLabelAddress(255)` | `0x1fe0`, **non mappe** | **`0x1000ff0`**, mappe |
| ecritures `label[255]` | **0** | **3 336** |
| `usleep` dans la boucle de label | 25 740 | **0** |
| flips/clears emis par le jeu | 0 | **2 247** |

Preuve brute (run de verification dans `/tmp/fifo2run.log`) :

    gcmfifo: reader armed: context=0x00e00000 control=0x00e00040 ring=0x40100000
    gcmfifo: first label release: back-end addr=0x01000ff0 0x00000000 -> 0x00000001
    gcmfifo: label[255] <- 0x00000001 (back-end)
    [videoimp] import=138 ret=0x1000ff0 lr=0x002da598
    ps3base-wasm: t=98s pc=0x002dc20c syscalls=245836 exited=0
    MAIN MENU: 0    flip: 2247    clear: 2247

## Les quatre correctifs que la piste A a du trouver

1. **L'indirection du contexte** : `_cellGcmInitBody` recoit un `vm::pptr<CellGcmContextData>` —
   un pointeur VERS un pointeur. Ecrire `begin`/`end`/`current` directement faisait passer la base du
   ring comme contexte, d'ou le `call_indirect(0)` a `lr=0x005d2c08` qui a occupe 40 rounds.
2. **Une zone RSX reellement mappee** : `vm::alloc(RsxContext)` echoue ; le repli `0xd00000` etait
   hors du pagemap, donc toutes les ecritures de label etaient **silencieusement jetees**.
3. **`get = put = 0x1000` a l'armement**, sinon le lecteur commence sur 4 Ko de zeros.
4. **Le semaphore de flip a 1**, re-arme sur release 0.

## Le blocage suivant, precis

Le drain s'arrete sur un auto-saut :

    W gcmfifo: drain budget exhausted at get 0x00001454 (put 0x00001bec)

Le lecteur decode un paquet dont la cible **est sa propre position** (`target == get`), traite comme
« le ring est fini ». C'est une **attente du jeu** : il tourne sur place en attendant que le GPU
consomme, et notre lecteur n'a rien a consommer. Le vrai GPU, lui, execute les commandes deja
soumises et ecrit les labels, ce qui fait avancer `put`.

## Ce qui reste pour le menu

| etape | etat |
|---|---|
| boucle de label | **franchie** |
| flips soumis par le jeu | **2 247** |
| clears soumis | **2 247** |
| flips presentes a l'ecran | **0** (le GPU reste un stub) |
| menu principal | **non atteint** |

Le jeu est maintenant dans sa **boucle de rendu normale** : il soumet, il attend le GPU. Ce qui
manque n'est plus un blocage mais une **consommation** : notre stub doit traiter les commandes et
ecrire les labels en retour, ce qui est exactement le travail des pistes G et H.
