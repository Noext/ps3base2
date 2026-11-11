# Recette : porter un jeu PS3 en natif, 100 % compilé, avec 50 sous-agents

Ce document s'adresse à **l'agent orchestrateur**. Il décrit, étape par étape, comment partir d'un jeu PS3 déchiffré et arriver le plus vite possible à une version jouable dont tout le code (PPU et SPU) est compilé à l'avance. Il est tiré d'un port réel (Minecraft PS3 Edition sur un fork de GoWA-pc) : chaque règle correspond à un blocage qui a coûté des jours.

**Définition de terminé**
1. Le jeu démarre, se navigue, entre en partie, se joue (déplacement et actions principales), sauvegarde et recharge.
2. Aucun forçage actif (`docs/forcings.md` vide ou entièrement remplacé par de vraies corrections).
3. `SPU_STRICT=1` passe sur toute la matrice de scénarios : zéro bloc SPU interprété.
4. Mode miroir SPU : zéro écart entre code compilé et interpréteur.
5. Une heure de jeu sans crash, reproduite sur `main` propre.
6. `tools/play.sh` lance le jeu en fenêtre sur le GPU réel avec manette.

---

## 0. Ce que l'humain fournit (ne commence pas sans)

- Le jeu déchiffré : `EBOOT.ELF` et le dossier du disque (`PS3_GAME/USRDIR`, `PARAM.SFO`).
- Le firmware extrait et déchiffré (`dev_flash/sys/external/*.sprx`).
- Le dépôt de base : un runtime de recompilation PS3 avec lift PPU, lift SPU, modules firmware liftés, kernel LV2 HLE et rendu **Vulkan** sur la plateforme cible (référence : GoWA-pc / ps3base2).
- La machine : nombre de cœurs, GPU, et le droit de lancer N instances du jeu.

Rien de dérivé du jeu ou du firmware n'entre jamais dans Git (code généré, sauvegardes, captures SPU, dumps mémoire).

---

## 1. Règles non négociables (à copier dans `AGENTS.md`)

1. **Le jeu marche sur la console.** Toute différence vient d'une entrée que nous lui fournissons. « C'est côté jeu » n'est jamais une conclusion.
2. **Notre code HLE est le premier suspect**, avant le GPU, le shader ou le compilateur.
3. **Aucune structure, enum, signature ou table recopiée de mémoire.** Toujours l'en-tête de référence (RPCS3, psdevwiki). Pointeurs invité = 4 octets. Arguments au-delà du 8ᵉ = sur la pile, slots de 8 octets, moitié basse à `slot+4`.
4. **Jamais de faux succès** : non implémenté ⇒ code d'erreur explicite + log. Jamais `OK` sans effet.
5. **Mesurer, puis conclure.** Une conclusion tirée d'une absence de log est invalide tant qu'on n'a pas prouvé que le log pouvait se produire (non plafonné, bon binaire, bon thread).
6. **Verdict mécanique** d'un run : vivant = atteint le timeout avec le compteur de flips croissant ; crash = terminaison anticipée. Jamais un `grep` sur un mot.
7. **Un endroit du code n'est pas un thread.** Toujours tracer à l'exécution qui fait quoi.
8. **Antériorités d'abord** : avant d'écrire du code sur un sujet, `grep -R` dans `refs/` et un tableau « qui a déjà résolu ça ».
9. **Un commit par preuve**, y compris en état forcé. La commande de lancement qui marche est écrite dans `docs/RUN.md` au moment où elle marche.
10. **Un rapport = un livrable.** Pas de point d'étape, pas d'hypothèse de plus sans mesure.

---

## 2. Organisation à 50 agents

### 2.1 Deux ressources rares : les builds et les runs
Cinquante agents ne peuvent pas tous compiler et lancer le jeu en même temps.

- **Build partagé.** Le code lifté du jeu (des centaines de Mo de C++) est compilé **une seule fois** en bibliothèque statique, en lecture seule, partagée par tous les worktrees. Un sous-agent ne recompile que le runtime et relie. Activer `ccache`. Jamais plus de `cœurs / agents_qui_compilent` en `-j`.
- **Créneaux de run.** Chaque instance du jeu consomme plusieurs cœurs. Fixer `RUN_SLOTS = cœurs / 6` (arrondi bas) et faire passer tout lancement par un sémaphore (`flock` sur `/tmp/run-slot-N`). Les mesures de performance prennent **tous** les créneaux (machine au repos).
- Conséquence : la plupart des agents font du travail **statique** (audit de code, comparaison avec les références, tests unitaires, analyse de code lifté) ; une minorité tient un créneau de run.

### 2.2 Isolation de chaque sous-agent
- `git worktree add ../wt-N -b explore/N`, jamais de commit sur `main`.
- Son dossier de build (relié à la bibliothèque partagée), **sa copie du VFS**, son fichier d'entrées, son serveur X, ses logs.
- Lancement uniquement via `tools/run_game.sh` avec `nohup setsid … & disown` (un processus lancé depuis un appel d'outil meurt à son retour).

### 2.3 Le tableau de bord
Un fichier `docs/board.md` tenu par l'orchestrateur : tickets (id, titre, propriétaire, état, preuve attendue), découvertes à diffuser, forçages actifs, point le plus loin atteint par le jeu. Chaque sous-agent lit le tableau au démarrage et y écrit son résultat.

### 2.4 Rôles
| Rôle | Nombre | Travail |
|---|---|---|
| Orchestrateur | 1 | Tableau, attribution, fusion, reproduction sur `main` propre, relais des découvertes |
| Intégrateur de build | 1 | Bibliothèque liftée partagée, ccache, `run_game.sh`, CI locale des tests |
| Auditeurs HLE | 15 à 25 | Un module système chacun, contre la référence, avec tests unitaires (§5) |
| Auditeurs syscalls | 4 à 6 | Un groupe LV2 chacun (threads, synchro, mémoire, fichiers, SPU, événements) |
| Pilote d'entrées | 1 | `drive_game.py` : lecture d'écran, navigation fiable |
| Chasseurs de blocage | 4 à 8 | Tiennent les créneaux de run ; descente et forçage sur le blocage courant |
| Rendu | 3 à 5 | Formats de texture, shaders, attributs, cibles de rendu |
| Compilation SPU | 4 à 6 | Scénarios de convergence, mode strict, mode miroir |
| Stabilité | 2 à 4 | Crashs, sanitizers, gardes de pile |
| Vérificateur | 2 | Rejoue indépendamment toute preuve annoncée avant fusion |

Règle de fusion : rien n'entre dans `main` sans (a) test ou capture comme preuve, (b) reproduction par un vérificateur, (c) la création de partie qui passe **10 fois sur 10** après fusion.

---

## 3. Phase 0 — Fondations (orchestrateur + intégrateur, séquentiel)

Ne lance aucun autre agent avant que tout ceci soit vert dans un worktree vierge.

1. **Dépôt** : fork de la base, profil de jeu vide (aucun hook, aucun patch, aucune adresse du jeu d'origine), `AGENTS.md`, `docs/{board,RUN,decisions,progress,forcings,REFERENCES}.md`, hook pre-commit (refus par extension et par magie `7f454c46` / `53434500`).
2. **`refs/`** (ignoré par Git) : RPCS3, la base d'origine, les ports voisins (Soulbrandt, Ratchet ToD, Pyrrhus, Tokyo Jungle, Twisted Metal, flOw, tout port du même moteur ou studio).
3. **Lift** : PPU du jeu, modules firmware relogés et liftés (au minimum `liblv2`, `libsysmodule`, `libsre`, `libgcm_sys`, `libfs`, plus ceux que le jeu charge via `cellSysmodule`), SPU extraits de l'ELF. Bibliothèque partagée construite.
4. **`tools/run_game.sh`** : build `RelWithDebInfo` avec clang, liens, VFS en argument, serveur X privé relancé s'il meurt, captures PNG périodiques **des surfaces GPU**, ligne de statut (flips, draws, threads), verdict mécanique, PID du vrai processus.
5. **Outils de diagnostic**, tous inactifs par défaut et pilotés par variables d'environnement :
   - trace d'imports par thread avec retours ; trace des syscalls ; dump de l'état des threads (syscall bloquant, objet, timeout) ;
   - hooks entrée/sortie sur fonctions liftées avec `r3` ; watch mémoire **réarmé** couvrant stores PPU, écritures HLE et DMA SPU ;
   - forçage (`FORCE=f<addr>=<val>;w<addr>:<n>=<val>;e<lr>:<bits>:<délai>`) ;
   - débogage de shader par hash (remplacer la sortie par UV, alpha de texture, varying) ;
   - `SPU_RECORD`, `SPU_STRICT`, `SPU_SHADOW`, `SPU_NO_AOT` ;
   - pages de garde sous chaque pile invitée, niveau bas de `r1` par thread.
6. **Entrées** : fichier de commandes lu à chaque trame (front montant, maintien de quelques trames, relâchement), injecté **après** le poll du périphérique hôte.

**Porte G0** : `run_game.sh` fonctionne du premier coup dans un worktree neuf ; le jeu se lie et exécute ses premières instructions ; tous les tests passent.

---

## 4. Phase 1 — Reconnaissance (15 agents en parallèle, statique, 1 à 2 h)

Chaque agent rend un fichier dans `docs/recon/`. Aucun run nécessaire sauf mention.

| Agent | Livrable |
|---|---|
| R1 | Table de tous les imports du jeu (module, NID, nom) × état dans le runtime : implémenté, stub, absent |
| R2 | Modules firmware chargés par `cellSysmodule` et leurs dépendances ; lesquels lifter (LLE) plutôt que réécrire |
| R3 | Syscalls LV2 atteignables (PPU du jeu et modules liftés) × état dans le runtime |
| R4 | Images SPU dans l'ELF et les fichiers ; usage de SPURS (tasksets, jobchains, job queues, event flags) |
| R5 | Formats de fichiers du jeu (archives, textures, polices, vidéo, audio) et bibliothèques de décodage importées |
| R6 | Threads du jeu (noms, fonctions d'entrée), singletons et tables de pointeurs globales repérables |
| R7 | Ports existants du même jeu, moteur ou studio ; correctifs réutilisables dans `refs/` |
| R8 | Page de compatibilité RPCS3 du jeu : réglages requis, modules LLE, problèmes connus |
| R9 à R15 | Lecture des `docs/` et `patches/` de chaque port voisin : liste des blocages qu'ils ont rencontrés et de leurs solutions, classée par sous-système |

**Porte G1** : `docs/recon/SUMMARY.md` avec la liste des imports à traiter, des modules à lifter et des risques. Les tickets de la phase 2 en découlent mécaniquement.

---

## 5. Phase 2 — Audit et implémentation HLE en masse (20 à 30 agents, statique + tests)

C'est ici que le parallélisme rapporte le plus. Dans le port de référence, **presque tous les blocages longs** étaient des défauts de fonctions système réimplémentées : offsets faux, arguments décalés, constantes fausses, callbacks jamais livrés, structures trop grandes. Ils se trouvent **sans lancer le jeu**, par comparaison systématique avec la référence.

### 5.1 Un agent par module importé par le jeu
`cellSaveData`, `cellPngDec`, `cellJpgDec`, `cellGifDec`, `cellFont`/`cellFontFT`, `cellL10n`, `cellSysutil`, `cellMsgDialog`, `cellOskDialog`, `cellGame`, `cellNetCtl`, `sys_net`, `sceNp`, `sceNp2`/`Matching2`, `sceNpTrophy`, `cellAudio`, `cellPad`, `cellRtc`, `cellVideoOut`/`cellAudioOut`, `cellSync`, `cellSpurs*` (si HLE), `cellVdec`/`cellAdec`/`cellSail`, `cellHttp`/`cellSsl`, `cellUserInfo`, etc.

### 5.2 Liste de contrôle de chaque auditeur (livrable : tableau fonction par fonction)
Pour chaque fonction du module que le jeu importe :
1. **Signature** identique à la référence ; nombre d'arguments ; ceux lus sur la pile.
2. **Structures** d'entrée et de sortie : taille totale et offset de chaque champ, tirés de l'en-tête. Un test `static_assert` par structure.
3. **Handles** : par valeur ou par pointeur ; un état **par handle** (jamais un état global partagé).
4. **Codes de retour** : ceux de la référence, y compris les erreurs.
5. **Callbacks** : chaque callback enregistré par le jeu est appelé, avec les bons arguments dans le bon ordre, sur le bon thread, au moment attendu (y compris en mode hors ligne).
6. **Protocoles asynchrones** : séquence complète (ouverture → événement système → résultat → fermeture → événement).
7. **Fichiers** : tout chemin passe par le VFS ; nommage identique à la référence.
8. **Oracle** quand il existe : décodeurs d'images contre Pillow, octet par octet ; conversions de texte contre une bibliothèque standard.
9. **Hors ligne** : réseau et PSN répondent « pas de connexion » de façon cohérente, sans jamais faire basculer le jeu sur un chemin de session en ligne.

Preuve exigée : tests unitaires par fonction + le tableau de conformité. Aucun run nécessaire.

### 5.3 Auditeurs syscalls (un groupe chacun)
Mêmes contrôles pour : threads et piles (taille demandée honorée, **pages de garde**), mutex/lwmutex/cond, event flags (`clear` = `pattern &= bitptn`, `WAIT_CLEAR` contre `CLEAR_ALL`), files et ports d'événements, sémaphores, timers, mémoire (`sys_memory`, `sys_mmapper`, `sys_vm` : tailles de structures exactes), fichiers, groupes de threads SPU, réservations (tout store PPU, HLE et SPU passe par le système de réservation, invalidation croisée testée dans les deux sens). Test de balayage : tout numéro non implémenté renvoie `ENOSYS`.

**Porte G2** : zéro import du jeu sans implémentation ou sans erreur explicite ; tous les tableaux de conformité verts ; tests fusionnés.

---

## 6. Phase 3 — Du démarrage au jeu (chasseurs de blocage sur les créneaux de run)

En parallèle de la phase 2. Boucle de l'orchestrateur :

1. Lancer le jeu ; noter le **point le plus loin atteint** (dernier écran, dernier import, état des threads).
2. Ouvrir un ticket par blocage, avec le livrable attendu, et l'attribuer.
3. Diffuser toute découverte aux auditeurs concernés (un callback jamais livré, une structure suspecte).
4. Fusionner, relancer, recommencer.

### 6.1 Méthode imposée aux chasseurs
**Descente par valeurs de retour** quand une boucle sort trop tôt ou qu'un chemin n'est jamais pris :
1. Hook entrée/sortie sur chaque fonction appelée directement, avec `r3`.
2. La dernière avant la sortie est la coupable ; recommencer dedans.
3. S'arrêter sur une **feuille** : import HLE, syscall, callback jamais appelé, ou lecture mémoire. Pour un appel indirect, logger la cible. Pour une lecture mémoire, watch sur l'adresse puis recherche des écritures au même décalage.

Livrable d'un ticket : **la feuille, ses arguments, sa valeur.** Rien d'autre.

**Forçage** pour ne pas attendre : forcer la feuille, continuer jusqu'au blocage suivant, noter chaque forçage dans `docs/forcings.md` (adresse, valeur d'origine, valeur forcée, ce que ça débloque), commiter. Un auditeur HLE reçoit le ticket de remplacer le forçage par la vraie correction.

**Thread qui attend** : dump de l'état de tous les threads ; une cadence fixe très basse (4 images par seconde) signale une attente à timeout ; compter les entrées **et les sorties** de chaque attente avant de parler de réveil perdu.

**Crash** : `addr2line` → ligne → registre → champ → dernier écrivain. Une valeur qui est une adresse au milieu d'une fonction dans une donnée = adresse de retour = pile qui déborde ou chevauche le tas. Un crash loin de sa cause = suspecter un débordement d'une structure HLE récemment modifiée.

### 6.2 Rendu (3 à 5 agents)
Dans l'ordre : le jeu configure la vidéo (`cellVideoOutGetDeviceInfo`), présente (flips), dessine (méthodes `BEGIN_END` dans le FIFO, pas des pourcentages), avec des textures correctes. Outils : capture des surfaces GPU, description de chaque draw (primitive, textures par unité, cible), débogage de shader par hash, comparaison mémoire source / staging / image GPU. Si une texture est fausse à la source, **remonter vers le producteur des données** (décodeur, fichier) avant de toucher au GPU.

### 6.3 Pilote d'entrées (1 agent, dès le premier menu)
`drive_game.py` lit l'écran après chaque appui et décide du suivant. Jamais de timings fixes ni de comptage d'appuis : dialogues en double, champs texte qui prennent le focus et transitions désynchronisent tout script aveugle. Livrable : une séquence fiable jusqu'en partie, prouvée 10 fois sur 10, et la table des commandes du jeu.

**Porte G3** : en partie, boucle de jeu de base prouvée par captures, sauvegarde écrite, rechargement d'une sauvegarde, zéro forçage restant.

---

## 7. Phase 4 — 100 % compilé (4 à 6 agents, puis tous les créneaux)

1. **Matrice de scénarios** couvrant le jeu : nouvelle partie avec plusieurs graines ou niveaux, chaque mode de jeu, chargement d'une sauvegarde, 15 minutes d'actions variées par scénario, retour au menu.
2. **`tools/spu_converge.sh`**, sans intervention : run `SPU_STRICT=1` → collecte des blocs manquants avec `SPU_RECORD` → régénération → rebuild → recommencer, jusqu'à un round complet sans rien collecter. Un scénario par agent et par VFS ; les captures sont fusionnées par l'orchestrateur à la fin de chaque round. Ne revenir qu'en fin de round.
3. **Pièges connus du compilateur SPU** :
   - la table doit accepter **plusieurs blocs à la même adresse**, distingués par leur contenu ;
   - le manifeste peut contenir du code PPU pris pour du SPU : écarter les entrées entièrement illégales, **sans** filtre du type « majorité de mots légaux » (il jette du vrai code) ;
   - mesurer pendant la partie, pas seulement au démarrage ;
   - si un scénario ne converge pas : comparer les nouveaux blocs aux anciens de même adresse et longueur ; s'ils ne diffèrent que de quelques mots, ce sont des adresses relogées ou des données dans le bloc.
4. **Preuve de correction** : `SPU_SHADOW=1` (bloc compilé contre interpréteur sur une copie de l'état : registres, mémoire locale, canaux, commandes MFC) sur toute la matrice, zéro écart. En complément : même sauvegarde chargée dans les deux modes ⇒ même résultat (la comparaison **échoue** si les entrées diffèrent).
5. Le verdict de fin de run affiche toujours le nombre de blocs interprétés.

**Porte G4** : matrice entière en `SPU_STRICT=1`, zéro écart en miroir.

---

## 8. Phase 5 — Stabilité, vitesse, livraison

- **Stabilité** (2 à 4 agents) : sessions d'une heure sur plusieurs scénarios, pages de garde de pile actives, ASan/UBSan/TSan en builds séparés, `rr` avec `watch -l` et `reverse-continue` pour toute corruption mémoire non attribuée.
- **Vitesse** : uniquement machine au repos, un seul run, sur le GPU réel (un profil sous Xvfb ne mesure que Xvfb). `perf record -g` : le code étant natif, le profil nomme directement les fonctions. Proposer les trois optimisations les plus rentables, les mesurer une par une.
- **Livraison** : `tools/play.sh` (fenêtre, GPU réel, clavier et manette, plein écran), `docs/PLAY.md`, `docs/RUN.md`, et un `setup` qui régénère tout le code dérivé du jeu à partir des fichiers de l'utilisateur.

**Porte G5** : la définition de terminé, en haut de ce document.

---

## 9. Gabarits

### Ticket (dans `docs/board.md`)
```
ID        : T-042
Titre     : Le serveur intégré sort après 66 ms
Symptôme  : écran figé sur « Finalizing », thread (4J) Server terminé
Livrable  : la feuille (import, syscall, callback ou lecture mémoire), ses arguments, sa valeur
Méthode   : descente par valeurs de retour depuis f_XXXXXXXX
Preuve    : log de la descente + capture après forçage de la feuille
Créneau   : run (oui/non)
```

### Rapport de sous-agent
```
Ticket, hypothèse testée
Antériorités (refs/) : ce que les autres ports disent du sujet
Mesures brutes (logs, tableaux), sans interprétation
Conclusion en une phrase, ou « non conclu » avec ce qui manque
Diff et tests ajoutés
Forçages utilisés
```

### Consigne type pour un auditeur HLE
```
Module : <nom>. Fonctions concernées : celles que le jeu importe (liste jointe).
Compare chaque fonction à la référence (signature, arguments sur la pile, structures et
offsets, handles, codes de retour, callbacks, protocole asynchrone, accès fichier par VFS).
Écris un test unitaire par fonction et un static_assert par structure.
Rends le tableau de conformité fonction par fonction. Ne lance pas le jeu.
```

### Consigne type pour un chasseur de blocage
```
Objectif unique : <le livrable du ticket>.
Méthode imposée : descente par valeurs de retour jusqu'à la feuille ; forçage de la feuille ;
ligne dans docs/forcings.md ; commit. Pas de rapport intermédiaire.
Reviens avec la feuille, ou après deux heures avec le dernier point atteint et ce que tu as vérifié.
```

---

## 10. Aide-mémoire des pièges (à relire avant chaque phase)

| Piège | Parade |
|---|---|
| Compter des « crashs » dans un log | Code de sortie + flips |
| Mesurer sur un binaire périmé ou un build en échec | Rebuild vérifié avant chaque mesure |
| État « fonctionnel » dépendant d'un cache chaud | Valider à cache froid |
| Stub qui renvoie `OK` | Erreur explicite + log |
| Offsets et constantes recopiés de mémoire | En-tête de référence, `static_assert` |
| Arguments au-delà du 8ᵉ lus dans un registre | Lecture sur la pile, moitié basse du slot |
| Un seul état pour tous les handles d'un module | État par handle |
| Callback enregistré jamais appelé | Inventaire des callbacks, livraison comme la référence |
| Débordement d'une structure HLE | Tailles exactes ; crash différé = suspect |
| Allocateur ou arène placé sur une zone du runtime ou du jeu | Placement validé par le gestionnaire mémoire |
| Remplacement partiel du malloc du jeu | Rediriger les wrappers publics, laisser l'init tourner |
| Chercher côté GPU une texture fausse à la source | Oracle sur le producteur des données |
| Import présent ≠ import appelé | Trace à l'exécution avant d'implémenter |
| Stores qui contournent les réservations | Tous les chemins d'écriture, test d'invalidation croisée |
| Piles trop petites ou qui chevauchent le tas | Taille demandée honorée, pages de garde |
| Script d'entrées aveugle | Lecture d'écran, preuve 10/10 |
| Processus tué au retour de l'appel d'outil | `nohup setsid … & disown` |
| Runs parallèles qui se volent écran, VFS ou entrées | Isolation complète par agent |
| Mesure de vitesse machine chargée ou sous Xvfb | Machine au repos, GPU réel |
| Compilation SPU mesurée au démarrage seulement | Mesure en partie, mode strict, convergence par rounds |
| Un seul bloc SPU par adresse | Variantes par contenu |
| Annoncer une preuve venant de la fenêtre d'un autre run | PID et écran vérifiés ; reproduction par un vérificateur |
