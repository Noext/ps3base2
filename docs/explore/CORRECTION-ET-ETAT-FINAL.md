# Correction d'une erreur d'orchestrateur, et etat final

## L'erreur que je viens de commettre

J'ai lance un run de 400 s sur le build FIFO, puis :

    $ grep -aci 'MAIN MENU' /tmp/long.log
    1

et j'ai annonce « le log du menu est apparu ». **C'etait faux.** La ligne trouvee est :

    ps3base-wasm: main menu NOT reached -- menu 0, ui 0

Mon `grep -i` a matche la ligne **negative**. Un `grep -ac 'main menu NOT reached'` en donne 1, et les
trois threads qui corroborent le menu sont absents :

    GetFriends: 0    readSaveCache: 0    getSavesInfo: 0

C'est exactement l'erreur que la piste V m'a signalee sur les criteres : **compter une occurrence ne
prouve pas un etat**. Il faut lire la ligne.

## Ce que le run de 400 s montre vraiment

| marqueur | valeur |
|---|---|
| `main menu NOT reached` | 1 |
| flips soumis | 2 247 |
| `pc` | `0x002dc20c` (boucle de trame), stable jusqu'a t=346 s |
| `cellAudio` | actif, 0 blocs, 1-5 late wakeups |

Le jeu tient **346 secondes** dans sa boucle de trame sans crash, sans atteindre le menu.

## La piste D a corrige un de mes chiffres

J'ai repete « 25 740 usleep » toute la session. La mesure brute de la piste D donne **903 444 usleep
sur 150 s**, soit ~25 700 toutes les 4 secondes. Mon chiffre etait une valeur instantanee presentee
comme un total.

## Ce que la piste D a livre et mesure

| a t=60 s, meme base | avant | apres |
|---|---|---|
| `sys_timer_usleep` | 155 506 | **3 406** (45,7x) |
| syscalls totaux | 167 503 | 9 453 (17,7x) |
| avancement GCM | inchange | **identique**, `pc=0x002dc20c` |

Le mecanisme : `poll_target()` decode l'instruction invitee qui suit le syscall et calcule l'adresse
scrutee avec les registres vivants — aucun constant par titre. Puis `park()` via `Atomics.wait`,
reveille par les stores (`reservation.cpp`), plafond 16 ms.

Prouve hors-jeu : `tests/label_wait_park.cpp` 8 checks 0 echec, reveil en **98 µs** pour un plafond
de 2000 ms. `LABELWAIT-SELFTEST PASS` dans le module.

**Limite nommee par la piste elle-meme** : dans le jeu, `woken=0` — les 3 733 parcs expirent au
plafond parce qu'**aucun store n'ecrit `0x00be070c`**, l'evenement de frame qui depend du renderer
absent. Le compteur s'effondre par blocage de 16 ms, pas par reveil. C'est une acceleration reelle
mais pas la solution au blocage.

## Etat final honnete

| livrable | etat |
|---|---|
| ligne de log du menu en WebAssembly | **NON OBTENUE** |
| capture du menu dans Chromium via WebGPU | **NON OBTENUE** |
| resultat du test C | **OBTENU** : 13 PASS / 0 FAIL |
| regress.sh natif vert | **OBTENU** : 3 titres |

Le jeu n'est plus bloque : il est dans sa boucle de rendu, soumet 2 247 flips et 2 247 clears, tient
346 s, et attend un renderer qui ecrive `0x00be070c` et consomme ses commandes.
