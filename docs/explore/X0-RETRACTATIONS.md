# X0 — Retractations et corrections verifiees

## 1. MES CHIFFRES DE FLIPS ETAIENT FAUX (refute par X4 et V)

J'ai annonce 71 440 flips puis 107 885 flips. Aucun de ces nombres n'existe dans un log.

La cause : j'ai compte avec grep -aci flip, qui a matche la chaine flips=0 de chaque rapport
periodique. 107 885 occurrences de flips=0, pas 107 885 flips.

    $ grep -oE 'flips=[0-9]+' /tmp/symrun.log | tail -1
    flips=0

C'est la troisieme fois que je confonds une ligne qui contient le mot avec une valeur non nulle.
X4 et V l'ont vu avant moi. Les chiffres reels sur tous les builds wasm de cette session :
flips=0, clears=0.

Ce qui reste vrai : byte[0xBE070C]=1, les lignes rsx: sont passees de 0 a 40, et le processeur de
commandes travaille (voir tableau).

## 2. Mon correctif de namespace etait inoperant (refute par X1, X4, V)

J'avais ecrit corrige apres avoir sorti la declaration du namespace ANONYME. Elle restait dans
namespace lv2. nm sur les objets :

    U _ZN3lv212_GLOBAL__N_116gcm_flip_handlerEv   (attendu)
    T _ZN3hle16gcm_flip_handlerEv                 (defini)

Correction de X4 appliquee : namespace hle { uint32_t gcm_flip_handler(); }. Apres :
U _ZN3hle16gcm_flip_handlerEv = T _ZN3hle16gcm_flip_handlerEv, missing function: 0.

## 3. La base bloquait TOUTE fusion (trouve par V)

third_party etait un lien symbolique (120000) alors qu'origin/main le porte en arbre (040000).
Chaque diff de branche portait donc 3831 suppressions, et la porte ff-only refusait tout, y
compris les branches saines. Corrige : third_party est maintenant un arbre.

## 4. Le decodeur du FIFO s'arretait 48 fois par run (trouve par X4)

    W [loop] budget at get 0x00001454 (put 0x00001bec) call_active=1 call_return 0x00001618
    W [loop] budget at get 0x00001190 (put 0x000021f4) call_active=1 call_return 0x00001618

call_active=1 avec un call_return fige : le decodeur entrait dans une sous-routine et n'en sortait
jamais. La reference (runtime/src/lv2/rsx.cpp:566-570) traite un self-jump par idle_wait + continue,
pas par un return qui abandonne le reste du flux. Corrige et compte a part (g_idle_selfjumps).

## Etat mesure apres ces corrections

| mesure | avant | apres |
|---|---|---|
| methodes FIFO | 220 279 003 | 242 999 374 |
| paquets | 97 901 782 | 107 999 709 |
| draws | 2 447 544 | 2 699 991 |
| blocages du drain | 48 | 0 |
| flips | 0 | 0 |
| clears | 0 | 0 |
| lignes rsx: | 0 (au depart) | 40 |
| byte[0xBE070C] | 0 | 1 |

## Ce qui reste, nomme sans detour

Le jeu consomme 243 M de methodes et 2,7 M de draws, la trame s'ouvre, et il n'y a toujours aucun
flip ni clear dans le flux. Un flux de cette taille sans une seule commande de presentation n'est
pas un probleme de debit : soit le decodeur ne reconnait pas les methodes de flip (0xE920/0xE924),
soit le titre n'emet ces commandes qu'apres un evenement que nous ne lui donnons pas encore.