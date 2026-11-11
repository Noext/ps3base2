# Le crash du pointeur nul est resolu

## La correction

Trois changements, tous necessaires :

1. **Routing HLE** de `cellGcmSys` avant le LLE, pour que le corps firmware ne s'execute pas sur un runtime
   sans RSX ;
2. **le stub remplit** `begin`, `end`, `current` du contexte ;
3. **le stub installe un `callback`** a `+12`, dont le premier mot est une **adresse de code guest**
   (`0x00010300`, une fonction liftée dont le corps est un `return`).

## Le resultat

| mesure | avant | apres |
|---|---|---|
| `unknown indirect target 0x00000000` | **1** | **0** |
| imports video atteints | 2 | **10** |
| lignes de log | 202 | 221 |

**Le crash `call_indirect(0)` a `lr=0x005d2c08` n'existe plus.** C'est la premiere fois depuis le round 199.

## Les deux pieges qui ont coute le plus de temps

| piege | consequence |
|---|---|
| `cb = buf + cmd_size - 16` | `range_mapped` refuse : `buf + cmd_size` est **un octet apres** l'allocation |
| ecrire `0` dans `callback+0` | c'est **exactement** la cible nulle qui fait crasher |

Le second est instructif : j'ai reproduit le bug en croyant l'eviter. Un callback dont le premier mot vaut
zero est indistinguishable d'un callback absent, parce que le code fait `ld32(callback+0)` et saute dessus.

## Ce qui reste

Le boot va **plus loin** et s'arrete sur `RuntimeError: unreachable` — un autre mur, plus loin que le premier.

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | **4 criteres sur 5**, blocage deplace |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |