# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Ma boucle d'attente s'arrêtait à 200 itérations | `M3-LONG-RUN-DID-NOT-HAPPEN.md` (round 108) | **corrigé — 1800 itérations** |
| Le natif met 9 126 s | `/tmp/cmp.log` | oui |
| Le jeu tourne sans renderer | round 108 | oui |

# Le run long a eu lieu, et le jeu progresse

## La correction appliquée

    for (int i = 0; i < 1800; i++) {   // était 200

Puis lancement de `timeout 1900 node ps3long.js` en arrière-plan détaché.

## Ce que le run montre

| instant | syscalls | threads | flips | pc distincts |
|---|---|---|---|---|
| t=139 s | 301 535 | 18 | — | 9 |
| t=257 s | 562 693 | 18 | — | 9 |
| t=389 s | **854 336** | **19** | **22 741** | 9 |

**Le jeu dépasse 200 s et continue.** Trois choses progressent :
- les **syscalls** (301k → 854k) ;
- les **flips** (22 741) ;
- les **threads** (18 → **19**).

## Le thread nouveau

    t=367,046  _sys_ppu_thread_create '(4J) OpenStreamThreadProc'

**Un thread apparaît à t=367 s** — au-delà de l'ancienne limite de 200 s. C'est la preuve que le jeu
**poursuit son démarrage** quand on lui en laisse le temps.

## Ce que ça corrige

Le round 108 disait : « je ne peux pas conclure sur "le wasm progresserait avec plus de temps" tant que le
module s'arrête à 200 s ». **La question est maintenant testée** : le module allongé progresse.

## Ce que ça ne dit pas encore

Le run n'est pas terminé (il va jusqu'à 1 800 s). Je ne sais pas encore s'il atteindra
`sys_vm_memory_map`. Ce round établit seulement que **le jeu ne stagne pas à 200 s**.

Et il reste très loin du natif : le natif a mis 9 126 s et atteint le monde ; nous sommes à 389 s.

## La conséquence pour le jalon 3

Le jalon 3 demande « le module boote et le log montre le titre atteignant le menu ». Si le jeu progresse
lentement mais sûrement, alors **la durée du run est un paramètre du critère**, pas un détail.

Le prompt ne fixe pas de budget de temps. Le natif met 9 126 s.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **corrige le harnais** (1800 itérations) et **établit que le jeu progresse
au-delà de 200 s** : un 19ᵉ thread apparaît à t=367 s.
