# R7 — Le nettoyage passe par le script, plus a la main

## Ce qui s'est passe
Le disque est retombe a 12 G libres. Un sous-agent a echoue en le constatant. Conformement a R6,
je n'ai **rien supprime a la main** : j'ai lance `tools/wt_gc.py`.

## Dry-run

    CANDIDATS: 2 worktree(s), 10583.7 Mo recuperables
      - /root/Dev/wt-N1b (explore/n1b, 1433.5 Mo)
      - /root/Dev/wt-N3b (explore/n3b, 9150.2 Mo)
    GARDES: 79 (raison listee dans le tableau)

79 worktrees gardes, chacun avec son motif. Les deux candidats verifient toutes les conditions :
termine, branche fusionnee, arbre propre, aucune activite recente.

## Application

    SUPPRIME /root/Dev/wt-N1b (explore/n1b)
    SUPPRIME /root/Dev/wt-N3b (explore/n3b)

Seulement **3 G** recuperes au `df` (les tailles du script sont apparentes, pas des blocs reels).

## Deuxieme passage apres nettoyage de MES repertoires de build

J'ai supprime `/tmp/out-R1`, `/tmp/out-R2`, `/tmp/out-K2`, `/tmp/R3-probe` — des repertoires que
**j'ai crees** ce tour, pas ceux d'autres pistes. Disque : **22 G libres**.

## Le script refuse les trois gros restants, avec raison

    KEEP  /root/Dev/wt-N2   branche explore/n2 non fusionnee dans main
    KEEP  /root/Dev/wt-P2   activite recente: fichier suivi modifie il y a 25 min (< 30 min)
    KEEP  /root/Dev/wt-P3   etat non termine: running; tache encore reclamee: P3.claim

21 G sont immobilises par trois worktrees d'autres pistes qui tournent. **Je n'y touche pas.**
C'est exactement ce que le script doit faire, et ce que j'avais viole au tour precedent en
supprimant `wt-Y*` et `wt-M*` a la main, ce qui avait fait echouer plusieurs sous-agents.

## Ce que cela change

| tour precedent | ce tour |
|---|---|
| suppression manuelle (`rm -rf wt-Y*`) | `wt_gc.py --apply`, 2 candidats verifies |
| plusieurs sous-agents echoues | aucun echec du a une suppression |
| risque de supprimer du travail actif | **P3 est garde parce que sa tache est encore reclamee** |

## Limite honnete
Le gain de 3 G pour 10,5 G annonces : le script compte des tailles apparentes. A corriger dans le
script (`du -s --block-size=1` au lieu de la taille apparente) — a signaler a R6, pas corrige ici.