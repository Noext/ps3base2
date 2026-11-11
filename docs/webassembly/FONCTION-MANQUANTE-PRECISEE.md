# `0x000f656c` n'est pas un offset interne : c'est une fonction manquante

## La question

Le round 184 a montre que la sequence d'entree pousse le jeu a appeler `0x000f656c`, absent de la table. Etait-ce
une adresse au milieu d'une fonction existante, ou une fonction jamais liftée ?

## La mesure

| verification | resultat |
|---|---|
| entree de table la plus proche en dessous | **`0x000f6460`** |
| entree de table la plus proche au-dessus | `0x000f65cc` |
| corps de `f_000f6460` | 3 560 octets |
| labels de ce corps a proximite | `f6460`, `f64f8`, `f6540`, **`f6558`** |
| `f656c` apparait dans ce corps | **non** |

## Ce que ca etablit

La fonction `f_000f6460` **s'arrete a `0xf6558`**. L'adresse `0xf656c` est **apres la fin** de cette fonction
et **avant** la suivante a `0xf65cc`.

**C'est une fonction entiere qui n'a jamais ete liftée**, dans l'intervalle `0xf6558` - `0xf65cc`.

## Ce que ca corrige

Ma premiere lecture etait « adresse au milieu d'une fonction existante, donc offset interne ». **Faux** : les
labels s'arretent 20 octets avant.

## Ce qui est le plus probable

`0x000f656c` est une **stub de PLT** ou une petite fonction d'indirection : l'intervalle disponible fait
20 octets, assez pour un saut. Le lift a traite `f_000f6460` mais pas ce qui suit, parce que la decouverte
n'a pas suivi ce chemin.

## Ce qu'il faudrait

Regenerer `out/ppu` en decouvrant depuis `0xf656c`. C'est une operation de build (`tools/setup.py`), pas un
diagnostic — et elle est hors de mon controle direct puisqu'elle reconstruit tout le code lifté.

## Ce que ca change pour le jalon 3

Le blocage n'est plus « le wasm ne progresse pas ». C'est :

**le lift est incomplet sur un chemin que l'input atteint, et le wasm ne peut pas franchir ce chemin.**

Ce n'est ni un defaut du runtime, ni du stub RSX, ni de la configuration.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 ; **le chemin du monde exige du code non lifte** |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |