# Guide de portage par recompilation statique — leçons tirées

Retour d'expérience de deux ports pilotés par agent IA :
- **PS2** — Tourist Trophy (EU) : bloqué des jours sur un runtime maison, débloqué en une commande avec `ps2-web-recomp`.
- **PS3** — Minecraft PS3 Edition : de « rien ne démarre » à « monde jouable, code PPU et SPU compilés », sur un fork de `GoWA-pc` (snuri00).

Ce document ne garde que ce qui a fait avancer. Pour chaque blocage : le symptôme, la vraie cause, la solution qui a marché, et la règle à en tirer.

---

## 1. Les règles d'or

1. **Réutiliser avant de construire.** Les deux plus gros sauts du projet sont venus d'un dépôt existant essayé tel quel, pas d'un débogage. Avant tout code : chercher un runtime générique ou un port d'un jeu proche, et **essayer le jeu dessus pendant une journée**.
2. **Le jeu marche sur la console.** « Le titre s'est enfermé lui-même », « c'est côté jeu » : conclusion interdite. Un comportement différent de la console vient toujours d'une entrée qu'on lui fournit (syscall, import HLE, callback, mémoire).
3. **Notre code HLE est le premier suspect.** Tous les bugs longs à trouver (texte en bruit, sauvegardes, crash du serveur) étaient dans des fonctions système qu'on avait réimplémentées à la hâte. Les vérifier contre un oracle **avant** de soupçonner le GPU, le shader ou le compilateur.
4. **Une structure invité se lit avec l'en-tête de référence, jamais au jugé.** Offsets, enums, tables, ordre des arguments : depuis RPCS3 / ps2sdk / psdevwiki. Sur PS3, les pointeurs invité font **4 octets**.
5. **Mesurer, pas supposer.** Chaque affirmation de l'agent doit être appuyée par un log, une trace ou une capture. Une conclusion tirée d'une absence de log est suspecte (le log était peut-être plafonné, désactivé, ou sur le mauvais binaire).
6. **Jamais de faux succès.** Un syscall ou import non implémenté renvoie une erreur explicite et logge. Un stub qui renvoie `OK` sans rien faire est la pire classe de bug (le jeu prend un chemin faux en silence).
7. **Le critère de progression est ce que le jeu atteint**, pas le nombre de commits ni la « version » d'une base. Comparer deux runtimes = comparer le point atteint par le même jeu.

---

## 2. Choisir la base

| Situation | Ce qui a marché |
|---|---|
| PS2 | `ps2-web-recomp` (snuri00) : runtime **générique**, kernel EE HLE, modules IOP du jeu sur interpréteur, `scripts/port.sh` avec l'ISO. Tourist Trophy a tourné du premier coup. |
| PS3 | `GoWA-pc` (snuri00) : lift PPU/SPU maison, **modules firmware liftés** depuis `dev_flash` (LLE), kernel LV2 HLE, rendu Vulkan, **Linux natif**. Minecraft : 661 flips sans une ligne spécifique, là où ps3recomp n'en avait jamais produit un. |
| Ports PS3 de référence | Soulbrandt (Demon's Souls, jouable, Linux), Ratchet ToD, Project Pyrrhus (Killzone 2), Tokyo Jungle, Twisted Metal, flOw : leurs `docs/` et `patches/` contiennent la solution de beaucoup de blocages communs. |

**Méthode d'évaluation qui a marché** : forker la base, désactiver la couche spécifique au jeu d'origine derrière un « profil de jeu » (hooks, jobs, patches, `main.cpp` avec l'ID du jeu en dur), faire passer le nouveau jeu, mesurer le point atteint. Une journée suffit à trancher.

**Piège** : un renderer D3D12 ou Metal ne s'affiche pas sous Linux. Vérifier dès le départ que la base a un chemin **Vulkan** pour la plateforme cible.

**Annuaires utiles** pour vérifier qu'un port n'existe pas déjà et trouver des ports voisins : recomp.fyi, recomplauncher.com, les PR « discovery » de recomp.party.

---

## 3. Mettre l'agent dans de bonnes conditions (jour 1)

### Fichiers à créer
- **`AGENTS.md`** : les règles de la section 1, plus :
  - tableau « antériorités » obligatoire en tête de chaque rapport (ce que `refs/` dit du sujet) ;
  - `grep -R` (majuscule) sur `refs/`, sinon les liens symboliques ne sont pas suivis ;
  - un critère mal formulé se réinterprète et se note dans `decisions.md`, il ne justifie pas de s'arrêter ;
  - aucune structure, enum ou table recopiée de mémoire quand un en-tête existe ;
  - le hook pre-commit ne se contourne jamais.
- **`refs/`** (ignoré par Git) : liens vers RPCS3, les ports voisins, la base. **`docs/REFERENCES.md`** : sujet → fichier précis dans `refs/`.
- **`docs/decisions.md`**, **`docs/progress.md`** (une ligne par jalon avec sa preuve), **`docs/RUN.md`** (commande de lancement exacte), **`docs/forcings.md`** (dettes, voir §5).
- **Hook pre-commit** qui refuse tout binaire ou code dérivé du jeu ou du firmware (par extension **et** par magie : `7f454c46` ELF, `53434500` SCE). Le C++ généré, les sauvegardes, les captures de mémoire restent hors du dépôt.

### Outils à construire tout de suite
- **`tools/run_world.sh`** (ou équivalent) qui fait tout l'environnement d'un coup : build `RelWithDebInfo` avec clang (jamais gcc, jamais Release « par hasard »), liens `bin/`/`fw/`, VFS dédié en argument, serveur X propre par run, lancement, captures périodiques, **verdict mécanique**. À tester dans un worktree vierge avant de lancer des sous-agents.
- **Verdict mécanique** : vivant = processus atteint le timeout (code 124) avec le compteur de flips qui augmente ; crash = terminaison anticipée par signal ou code d'erreur. **Jamais un `grep 'crash:'`** (un seul défaut de page géré imprimait 16 lignes de pile, comptées comme 16 crashs pendant deux jours).
- **Des yeux pour l'agent** : ligne de statut périodique (flips, draws, threads vivants) et **capture PNG automatique** du tampon présenté. L'agent ne voit pas l'écran ; sans ça il conclut « figé » ou « crash » à tort.
- **Pilotage d'entrées en boucle fermée** : un fichier de commandes lu à chaque trame (`cross`, `down`…), consommé une fois, avec front montant, maintien de quelques trames et relâchement ; puis un script qui **lit l'écran** pour décider l'appui suivant (`drive_world.py`). Ne jamais piloter par des timings fixes.
- **Mécanisme de forçage** (`PS3BASE_FORCE`) : remplacer `r3` au retour d'une fonction liftée, écrire un octet, poser un bit d'event flag à un instant donné. Inactif par défaut.
- **Captures d'écran de debug de shader** (`PS3BASE_FS_DEBUG`) : remplacer la sortie d'un fragment shader identifié par hash par une de ses entrées (UV, alpha de texture, varying). RenderDoc n'a pas fonctionné sur le pilote ; cet outil maison a tranché en un run.

---

## 4. Catalogue des blocages et des solutions qui ont marché

### 4.1 Environnement et méthode

| Symptôme | Vraie cause | Solution |
|---|---|---|
| Le jeu « plantait » 16/16, alors qu'il marchait la veille | `grep 'crash:'` comptait les lignes de pile d'un défaut de page **géré** | Verdict mécanique (code de sortie + flips) |
| Corrections sans effet, mesures incohérentes | Build en échec silencieux, binaire périmé ; arbre de build sale (90 Mo contre 263 Mo propre) | Rebuild propre et vérification du code retour avant chaque mesure ; taille de l'arbre comme alerte |
| « Ça marchait » puis plus rien après un commit | L'état fonctionnel dépendait d'un **cache de shaders chaud** et de fichiers générés non commités | Valider à cache froid ; documenter les fichiers générés nécessaires |
| L'agent ne retrouve plus la commande qui marchait | Contexte compacté | `docs/RUN.md` écrit **au moment** où ça marche |
| Le jeu meurt sans crash au bout d'un moment | Processus lancé depuis un appel d'outil, tué à son retour | `nohup setsid tools/run_world.sh … & disown` |
| Deux runs parallèles se volent les entrées | Même `DISPLAY`, PID mal capturé | Un serveur X par run, PID du vrai processus |
| Mesures de vitesse incohérentes | Machine chargée par les runs parallèles | Mesures de perf : un seul run, machine au repos |
| 22 fps, menus à 2-8 fps | Xvfb ⇒ **lavapipe** (rendu Vulkan sur CPU) | Mesurer sur le vrai GPU (`tools/play.sh`) |
| L'agent répète « point d'étape » et s'arrête | Il rend la main à chaque fin de tour | « Aucun rapport intermédiaire ; si ton tour s'achève, tu reprends au suivant ; seule raison de revenir : fin de phase ou blocage technique prouvé de deux heures » |
| L'agent ne consulte pas les ports voisins | Hors de son champ de vision et de son contexte | `refs/` dans le dépôt + tableau d'antériorités obligatoire |

### 4.2 Mémoire et chargement

| Symptôme | Vraie cause | Solution |
|---|---|---|
| (PS2) Pointeurs aberrants, crashs aléatoires | Pointeurs hôte 64 bits qui fuient dans les registres invité ; deux `ps2asm.h` divergents masqués par l'ordre des `-I` | `docs/memory-model.md`, **une seule** fonction de traduction qui fait faute sur toute adresse non classée, tests unitaires par frontière |
| (PS2) Une fenêtre mémoire « miroir du port » | Contournement qui acceptait des pointeurs hôte tronqués | Supprimée ; test qui exige une faute sur l'adresse de l'image hôte |
| (PS3) FIFO lu comme des zéros, tampon de commandes effacé | L'arène de l'allocateur hôte placée sur une zone du runtime (fenêtre d'injection HLE à `0x20000000`, puis FIFO du jeu à `0x50000000`) | Placement dynamique validé par `vm_commit` ; lire d'abord les adresses que le jeu revendique |
| (PS3) Assertions dans le malloc Dinkumware | Remplacement partiel du tas (pointeurs qui traversent deux allocateurs) | Rediriger les **wrappers publics** (malloc/memalign/free/realloc) comme Tokyo Jungle, **laisser tourner l'init du tas** ; bonne signature (handle en `r3`, taille en `r4`) ; alignement jamais aveugle |
| Disque introuvable | VFS non monté ; décodeur d'images ouvrant le chemin invité sur l'hôte | Montage `dev_bdvd` ; tout accès fichier passe par le VFS |
| `sys_process_param` ignoré | Le chargeur ne lisait que `PT_LOAD`/`PT_TLS` | Parser le segment `0x60000001` |

### 4.3 Syscalls, imports et ABI

| Symptôme | Vraie cause | Solution |
|---|---|---|
| Longueur absurde `0x0FEFE790` dans un log tty | Une **adresse de pile** lue comme longueur ; syscall 988 aliasé sur `tty_write` | Reconnaître la signature « adresse de pile ≈ `0x0FEFxxxx` » ; vérifier la table de dispatch contre RPCS3 |
| Crash dans le serveur, pointeur nul 4 ms plus tard | `sys_vm_get_statistics` écrivait 64 octets dans une structure de 48 (écrasait les registres sauvegardés de l'appelant) ; `sys_vm_memory_map` absent | Tailles de structures depuis l'en-tête de référence ; un crash loin de sa cause = suspecter un débordement HLE récent |
| SPURS rejette le taskset | `cellSpursInitializeWithAttribute2` non enregistré, l'appel était avalé | Lister les NIDs non résolus du jeu ; brancher la variante sur l'init commune (comme Twisted Metal pour la v2) |
| Callbacks de sauvegarde jamais appelés | Fonctions à **9 ou 10 arguments** : les derniers sont **sur la pile** ; slots PPU64 de 8 octets, moitié basse à `slot+4` (`userdata` à `sp+0x70`, pas `sp+0x60`) | Signature RPCS3 ; lecture des arguments de pile selon l'ABI |
| Événements système ignorés | `CellSysutilCallback` appelé avec les arguments dans le mauvais ordre | `(fn, event, param, arg)` |
| Constantes réseau fausses | `CELL_NET_CTL_STATE_IPObtained` = 3, pas 4 ; `ChatRestrictionFlag` renvoyait 1 | Constantes depuis RPCS3 |
| Liste des mondes vide | `UserListAutoLoad` ignorait le disque ; préfixe `u0_` inventé (RPCS3 stocke le `dirName` tel quel) ; `GetListItem` absent | Énumération réelle, nommage conforme |
| « Connection to the server was lost » | Une correction NP avait laissé le runtime en mode « connexion simulée » | Le hors ligne doit rester purement local ; preuve 10/10 créations de monde après chaque changement NP |
| « NID incohérents » avec le SHA-1 du nom | Un NID = SHA-1(nom + suffixe fixe de 16 octets) | Ne pas « corriger » une table qui fonctionne |
| `exitspawn` en no-op | Conséquence, pas cause (le jeu sortait parce que SPURS avait échoué) | Chercher la cause en amont avant d'implémenter |

### 4.4 Rendu

| Symptôme | Vraie cause | Solution |
|---|---|---|
| Logo affiché, texte du menu en bruit | **Notre `cellPngDec`** : `outputColorSpace` lu au mauvais offset ; un seul état d'image partagé entre sous-handles ; sous-handle déréférencé au lieu d'être lu par valeur ; `CellPngDecInfo` écrit aux mauvais offsets | Décodeur testé contre **Pillow** octet par octet ; état par sous-handle ; structures aux offsets RPCS3 |
| Dix tours sur GPU, shader, UV, swizzle, remap | Toute la chaîne aval était correcte ; le bug était dix étapes en amont | Remonter la chaîne **depuis la source** des données avec un oracle externe, pas depuis l'écran |
| Texture « locale » vide | Ambiguïté de décodage de la localisation | `location = (format & 3) − 1` : brut 1 = locale, brut 2 = principale (vérifier la valeur brute) |
| Vertex program reçu tronqué | Chargé par blocs à des slots différents | Reconstruire depuis la séquence `SET_TRANSFORM_PROGRAM_LOAD`, pas par scan borné |
| `draw 0 %` alors que des draws partent | Classifieur en pourcentage ; les draws sont rares par nature | Regarder une **image**, décrire chaque draw (primitive, texture liée, cible) |
| Le jeu présente mais ne dessine rien, 4 fps constants | Boucle d'attente avec timeout de 250 ms | Dump de l'état de chaque thread (syscall, objet, timeout) ; le thread qui attend 250 ms désigne le sous-système |

### 4.5 SPU, SPURS et synchronisation

| Symptôme | Vraie cause | Solution |
|---|---|---|
| Écritures PPU et SPU qui se marchent dessus | Les stores PPU contournaient le système de réservation | Tous les stores (liftés, HLE, `stwcx`) passent par `store_range` ; test unitaire d'invalidation croisée dans les deux sens |
| Événements SPU « vides », réveil apparemment perdu | Normal pour un event flag SPURS : l'info est **dans la structure en mémoire**, l'événement ne fait que réveiller | Dumper la structure (`CellSpursEventFlag` : `direction`, `ppuWaitMask`, `pendingRecvTaskEvents`) et compter entrées/sorties des `Wait` |
| Chasse au « réveil perdu » pendant deux tours | Les 18 attentes étaient toutes revenues | Comptabilité entrée/sortie **avant** toute hypothèse |
| « Le producteur est en aval du consommateur, dans la même fonction » | Confusion **endroit du code / thread** | Tracer à l'exécution qui (thread, `lr`) pose et efface chaque bit |
| Génération de monde en 8 minutes, compilé pas plus rapide | 65-78 % des instructions SPU **interprétées** : les programmes que le jeu téléverse à l'exécution n'avaient jamais été capturés | Boucle de découverte (§6) |

### 4.6 Logique de jeu bloquée

| Symptôme | Vraie cause | Solution |
|---|---|---|
| Serveur intégré qui « sort proprement » à 66 ms | Pas propre : `f_00189614`, la boucle encadrée par des paires init/deinit, sortait sur un booléen lu à 0 | Descente par valeurs de retour jusqu'à la feuille (§5) ; la feuille était un `ld8` d'un champ jamais posé faute d'init NP |
| Écran « Finalizing / Preparing to Save Level » figé | En réalité : séquence d'entrées désynchronisée (dialogues d'autosave en double qui avalent un appui, curseur pas sur le bon bouton) | Pilotage qui lit l'écran ; jamais de comptage d'appuis |
| Appuis clavier ignorés | `xdotool` envoie appui et relâchement dans la même milliseconde, `poll_pad` échantillonne à 60 Hz ; l'injection était écrasée par le poll hôte | Latch d'au moins quelques trames ; injection **après** le poll |
| Champ texte qui avale les « bas » | Focus pris par le champ Seed | Détecter le focus à l'écran et le quitter avant de naviguer |
| (PS2) Boucles sur `FindAddress`, `SetSyscall`, tables d'interruptions | Exécution fidèle des internes de `libkernel` (patch du BIOS) au lieu de les remplacer | Couper au niveau des bibliothèques Sony ; finalement remplacé par un runtime générique |
| (PS2) « 7 causes d'interruption » | C'étaient des identifiants de commandes SIF (`sifcmd`) ; la boucle attendait une réponse de l'IOP | Comparer instruction par instruction avec le source ps2sdk ; IOP HLE qui répond, livraison SIF0 asynchrone |

---

## 5. Les deux techniques de diagnostic qui ont débloqué le plus

### Descente par valeurs de retour (jusqu'à la « feuille »)
Quand une boucle sort trop tôt ou qu'un chemin n'est jamais pris :
1. Hook entrée/sortie sur chaque fonction appelée directement, avec `r3` au retour.
2. La dernière avant la sortie est la coupable ; recommencer **dans** elle.
3. S'arrêter sur une **feuille** : import HLE, syscall, callback jamais appelé, ou lecture mémoire. Pour un appel indirect, logger `ctx.ctr` et suivre la cible.
4. Pour une lecture mémoire : watch dynamique sur l'adresse (qui écrit, quand, quelle valeur), puis recherche statique de toutes les écritures au même décalage.

Un prompt = **un objectif de sortie unique** (« la feuille, ses arguments, sa valeur »). Les prompts à plusieurs objectifs ont été mal suivis.

### Forçage puis correction
Quand la chaîne est longue :
1. Forcer la valeur fautive (`PS3BASE_FORCE`), relancer, observer ce qui vient ensuite.
2. Enchaîner les blocages sans rapport intermédiaire.
3. Chaque forçage est une ligne dans `docs/forcings.md` (adresse, valeur d'origine, valeur forcée, ce que ça débloque) et un commit, même en état forcé.
4. Remplacer ensuite chaque forçage par la vraie correction. Exemple : le forçage #1 (serveur) est devenu un no-op prouvé une fois la chaîne NP implémentée.

Limite : un pointeur corrompu (valeur de code ou de pile dans une donnée) ne se force pas ; il se traque (ligne exacte, champ, dernier écrivain). Une valeur comme `0x4bff926d` est une **instruction PowerPC** (`bl`) : quelqu'un a copié depuis du code.

---

## 6. Compiler 100 % du code SPU (boucle de découverte)

Le code PPU est compilé à l'avance en entier. Le code SPU que le jeu **téléverse à l'exécution** (tâches SPURS, génération de terrain) n'est pas dans l'ELF : sans capture, il passe par l'interpréteur de secours.

1. `GOWA_SPU_RECORD` pendant une session : chaque bloc inconnu est sauvegardé une fois par hachage `{lsa, n, words}`.
2. Les captures vont dans `out/gen/blocks/run*.bin` (hors dépôt), `codegen.py` les compile, rebuild.
3. Mesurer **pendant la génération du monde et en jeu**, pas seulement au boot (le 0 % du boot cachait 65 % en génération).
4. Converger : plusieurs graines, Créatif et Survie, chargement, sessions de jeu variées, jusqu'à zéro nouveau bloc sur deux séries.
5. `GOWA_SPU_STRICT=1` fait échouer le run au premier bloc interprété ; le verdict de fin de run affiche toujours le nombre de blocs interprétés.
6. **Correction** : exécution en miroir (`GOWA_SPU_SHADOW=1`, bloc compilé contre interpréteur sur une copie de l'état) ou même graine ⇒ même monde. La comparaison doit **échouer** si les graines lues dans les sauvegardes diffèrent.

Résultat Minecraft : 74,55 % → 0 % au boot, puis 65 % → 0 % en génération (≈ 25 000 blocs compilés).

---

## 7. Travailler avec plusieurs sous-agents

Efficace pour explorer des pistes en parallèle, à condition d'isoler :
- un `git worktree` et une branche `explore/N` par sous-agent, jamais de commit sur `main` ;
- un dossier de build, une **copie du VFS** (avec les sauvegardes), un fichier d'entrées et un serveur X par sous-agent ;
- `-j 8` par build ;
- `tools/run_world.sh` validé **avant** de lancer qui que ce soit (sinon tout le budget part en erreurs d'environnement) ;
- l'orchestrateur relaie les découvertes entre pistes, fusionne la gagnante, puis **reproduit sur `main` propre** avant d'annoncer quoi que ce soit ;
- les mesures de performance se font seules sur la machine.

Exemple de répartition qui a marché : (1) boucle du serveur, (2) boucle du client / écran bloqué, (3) remplacer un forçage par un vrai comportement hors ligne, (4) inventaire des callbacks enregistrés et jamais appelés.

---

## 8. Ordre recommandé pour un nouveau jeu

1. **Recherche** : port existant ? décomp ? runtime générique pour la console ? ports voisins sur la même base ou le même moteur ? (recomp.fyi, recomp.party, GitHub).
2. **Essai direct sur la meilleure base** pendant une journée ; mesurer le point atteint.
3. **Jour 1 outillage** : `AGENTS.md`, `refs/`, hook pre-commit, `run_world.sh`, verdict mécanique, captures, pilotage en boucle fermée, `RUN.md`.
4. **Imports non résolus** : les lister, les implémenter d'après RPCS3 (codes de retour réels, callbacks livrés), jamais de faux `OK`.
5. **Jusqu'au premier pixel** : modules chargés, disque monté, décodeurs vérifiés contre un oracle, renderer Vulkan sur la plateforme cible.
6. **Menus** : texte, entrées, dialogues ; sauvegardes (signatures à arguments sur la pile).
7. **Entrer dans le jeu** : descente par valeurs de retour + forçages documentés.
8. **Stabilité** : crashs traités par `addr2line` → registre → champ → dernier écrivain.
9. **100 % compilé** : boucle de découverte SPU, mode strict, exécution en miroir.
10. **Vitesse** sur GPU réel, machine au repos.

---

## 9. Phrases de prompt qui ont fait la différence

- « Ne corrige rien, mesure. » / « Pas de nouvelle hypothèse sans preuve dans le log. »
- « Le jeu marche sur la console : un blocage comme celui-ci vient de nous. »
- « Un endroit du code n'est pas un thread. »
- « Ta mesure prouve que le chemin producteur n'est jamais exécuté : une entrée en amont diffère de la console. »
- « Objectif unique : la feuille, ses arguments, sa valeur de retour. Rien d'autre. »
- « Force la valeur fautive, ne la comprends pas d'abord ; note-la dans `forcings.md`. »
- « Colle-moi intégralement, sans résumé, le code de… » (pour vérifier soi-même les conclusions de l'agent)
- « Tu as répondu à une autre question que celle posée ; livre exactement les quatre tableaux. »
- « Commite maintenant, même dans un état forcé. »
- « Aucun rapport intermédiaire ; la seule raison de revenir est la fin de la phase ou un blocage technique prouvé. »
