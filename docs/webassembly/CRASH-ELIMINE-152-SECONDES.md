# Le crash est elimine : le jeu tourne 152 secondes

## Ce qui a change

La section de noms (`PS3BASE_NAMED=1` -> `-g` au lien) a permis de lire ce que `unreachable` cachait :

    at game.wasm.signature_mismatch:jsedge::spu_idle(unsigned int)
    at game.wasm.spu::SpuThread::read_channel(unsigned int)

**`jsedge::spu_idle` etait declare `void` dans mes stubs, alors que `jobsys::spu_idle_donate` rend
`bool`** (`runtime/include/runtime/jobsys.h:212`). Le lieur signale 29 avertissements de signature et remplace
l'appel par un abort.

Correction d'une ligne : `bool spu_idle(unsigned) { return false; }`.

## Le resultat

| mesure | avant | apres |
|---|---|---|
| `RuntimeError: unreachable` | **2** | **0** |
| code de sortie | 1 (crash) | **124 (timeout)** |
| lignes de log | 220 | **1 291** |
| duree de vie | ~0,9 s | **152 s** |

**Le jeu ne crashe plus.** Il tourne jusqu'au timeout de 250 secondes et produit un rapport de statut.

## Ou il bloque maintenant

    status: ppu 0x1000000 'main_thread' blocked in 141 (r3 0x1e) syscalls +925950
    status:    top syscalls 141x925950

Le syscall 141 est **`sys_timer_usleep`** (`syscall_table.h:122`). Le thread principal fait 925 950
d'endormissements courts en attendant un evenement que les threads SPURS ne signalent pas.

Les trois handlers SPURS (`MinecraftSpursHdlr0/1`) existent et sont bloques :

| thread | bloque dans |
|---|---|
| `main_thread` | 141 (`sys_timer_usleep`) |
| `MinecraftSpursHdlr1` | 130 |
| `MinecraftSpursHdlr0` | 178 |

## Ce que ce round a coute

**La section de noms etait la cle depuis le round 242** : sans elle, `unreachable` ne dit rien ; avec elle,
il nomme la fonction, le fichier et la ligne d'appel. Une seule variable d'environnement a transforme un
diagnostic aveugle en correction d'une ligne.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | **4 criteres sur 5**, plus aucun crash ; blocage = attente sans reveil |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |