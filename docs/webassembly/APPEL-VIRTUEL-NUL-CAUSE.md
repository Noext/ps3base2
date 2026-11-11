# L'appel virtuel nul : la cause est identifiee

## La chaine, mesuree

L'erreur complete que le runtime imprime deja :

    F ppu: unknown indirect target 0x00000000 (lr=0x005d2c08) r3=0x0 r4=0x4 r9=0x0 r31=0x0 ctr=0x0

**`r31` et `r9` sont nuls**, donc la vtable et la methode le sont.

| etape | code | valeur |
|---|---|---|
| entree de `f_005d2b44` | `ctx.gpr[31] = ctx.gpr[3]` | `r31 = this` |
| site d'appel | `r9 = ld32(r31 + 12)` | `r9 = 0` |
| | `ctr = ld32(r9 + 0)` | `ctr = 0` |
| | `call_indirect(ctr)` | **appel a 0** |

## D'ou vient le pointeur nul

L'appelant (`mc_028.cpp`, `lr=0x2da54c`) charge l'objet ainsi :

    ctx.gpr[29] = 14090240;                                      // 0x00d70000
    ctx.gpr[3]  = ld32(r29 + 10240);                             // ld32(0x00d72800)

**`0x00d72800` n'est ecrit nulle part dans le code lifte** (verifie par `grep` sur les 100 fichiers).

La lecture retourne donc la valeur initiale de la region — zero.

## Ce que ca signifie

Ce n'est **ni un bug de lift, ni un probleme de liaison** : le code fait exactement ce que le binaire dit.

C'est un **champ de structure jamais initialise**, parce que le constructeur qui l'ecrit ne s'est pas execute.

## Ce que j'ai appris sur mon instrumentation

Mes trois gardes (`0x5d2bf0`, `0x5d2b44`, `0x5d2c08`) **n'ont jamais tire**, et j'ai compris pourquoi :

`hooks::add_guard` indexe `hook_flags` par `(addr - text_lo) / 4`, et le code lifté ne consulte ce drapeau
**qu'a l'entree d'une fonction** (`if (ppu::hook_flags[...]) { hook_call(...); return; }`).

Une garde posee **au milieu d'une fonction n'est jamais consultee**. Seules les entrees de fonction sont
instrumentables par ce mecanisme.

J'ai aussi decouvert que mes gardes etaient posees **avant `hooks::init()`**, donc ecrasees ; les deplacer
apres a corrige ca, sans rendre les adresses du milieu instrumentables pour autant.

## Ce qu'il faudrait

Trouver le constructeur qui devrait ecrire `0x00d72800`. C'est une fonction qui prend `0x00d70000` en base et
ecrit a `+10240`. Une recherche par cette constante dans les 100 fichiers liftés donne les candidats.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; module lie et demarrant, blocage sur un champ non initialise |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |

---

# Verification complementaire : aucune ecriture

J'ai cherche toutes les ecritures a `+10240` dans les 100 fichiers liftés :

    grep -rn 'st32(base, uint32_t(ctx.gpr[...] + 10240)' out/ppu/*.cpp

**Zero resultat.** Les six occurrences de `+10240` sont toutes des `ld32`, jamais des `st32`.

Le champ a l'offset 10240 de la region `0x00d70000` n'est donc **jamais ecrit par le code lifté**. Ce n'est
pas que le constructeur n'a pas tourne : c'est que **le lift n'a produit aucune ecriture pour ce champ**.

## Les deux lectures possibles

| lecture | consequence | comment trancher |
|---|---|---|
| le champ est ecrit par du code **non lifte** | il manque encore une fonction | la decouverte indirecte le revelerait |
| le champ est ecrit par un **bloc SPU** ou un DMA | le GPU/SPU le remplit | la region `0x00d70000` devrait etre une zone de travail DMA |

La seconde est plausible : `0x00d70000` est dans la plage des buffers RSX/DMA documentee nativement.

## Ce que ca change

Ce n'est plus « une initialisation a manquee » mais **« ce champ vient d'ailleurs que du PPU »**. La prochaine
mesure doit verifier si `0x00d72800` est dans une region que le jeu envoie au RSX ou au SPU.


---

# La cause reelle : la region 0x00d70000 n'est pas mappee

## Les regions mappees au moment du crash

    vm: region 1 placed at 0x10000000
    vm: region 2 placed at 0x40000000
    sys_memory_allocate(0x10000)  -> 0x10000000
    sys_memory_allocate(0x80000)  -> 0x10010000
    sys_memory_allocate(0x10000)  -> 0x10090000
    sys_memory_allocate(0x100000) -> 0x40000000
    sys_memory_allocate(0x300000) -> 0x40100000

**Deux regions seulement : `0x10000000` et `0x40000000`.**

## L'adresse fautive

    ctx.gpr[29] = 0x00d70000;
    ctx.gpr[3]  = ld32(r29 + 10240);   // 0x00d72800

`0x00d72800` n'est **dans aucune region mappee**. Le backend memoire du wasm rend **zero** pour un acces non",
mappe, au lieu de lever une faute.

## Pourquoi aucun code n'ecrit ce champ

Ce n'est pas un oubli du lift : **cette zone n'existe pas encore**. Elle serait creee par un `sys_memory_allocate`
ou un `sys_vm_memory_map` **qui n'a pas eu lieu**.

Le `sys_vm_memory_map` du monde survient a t=120 s chez le natif ; ici le jeu n'y est pas encore. Mais `0x00d70000`
est bien en dessous de `0x10000000`, donc il devrait etre couvert par une allocation **plus precoce**, faite par",
le chargeur ou par liblv2.

## Ce que ca etablit

| lecture | statut |
|---|---|
| bug de lift | **ecarte** — le code fait ce que le binaire dit |
| bug de liaison | **ecarte** — plus de symbole manquant sur ce chemin |
| champ non initialise par un constructeur PPU | **ecarte** — aucune ecriture existe, nulle part |
| **region non mappee** | **retenu** — la lecture rend zero car la page n'existe pas |

## Le probleme de fond

Un acces a une zone **non mappee rend zero silencieusement** au lieu de faulter. Le jeu continue avec un
pointeur nul et echoue **loin** de la vraie cause, 2 000 instructions plus tard.

C'est un defaut du backend memoire, pas du jeu.


---

# RETRACTATION : la lecture de 0x00d72800 n'a jamais lieu

## Ce que j'ai mesure

J'ai instrumente ppu::ld32 sous -DPS3BASE_LD_TRAP=1 pour rapporter toute lecture d'une adresse non mappee,
et specifiquement la lecture de 0x00d72800.

    unmapped: 0      -- aucune lecture non mappee dans tout le boot
    d72800 reads: 0  -- l'adresse n'est JAMAIS lue

## Ce que ca invalide

| affirmation du round 196 | statut |
|---|---|
| 0x00d72800 n'est pas mappee | non verifie -- et la lecture n'a pas lieu, donc sans objet |
| la lecture rend zero silencieusement | faux -- aucune lecture |
| c'est un defaut du backend memoire | faux -- le backend n'est pas sollicite |

J'avais deduit du log des regions (0x10000000, 0x40000000) que l'adresse n'etait pas couverte, sans jamais
instrumenter la lecture. C'est exactement l'erreur que j'ai commise plusieurs fois dans ce projet : deduire au
lieu de mesurer.

## Ce qui reste vrai

- r31 = r3 a l'entree de f_005d2b44, et r3 = 0 au moment de l'appel.
- Une garde posee au milieu d'une fonction n'est jamais consultee par hook_flags.
- Mes gardes etaient posees avant hooks::init().

## Ce que le code fait vraiment

La sequence ld32(r29+10240) lue dans mc_028.cpp correspond a un site d'appel, mais ce n'est pas celui qui a
produit le crash : l'adresse n'est jamais executee. Le vrai appelant est ailleurs.

## La prochaine mesure

Instrumenter ppu::call_indirect lui-meme, avec le lr exact et la pile des fonctions traversees, au lieu de
deviner le site d'appel par lecture de source.