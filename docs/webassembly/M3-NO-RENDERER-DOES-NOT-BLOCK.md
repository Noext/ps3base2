# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| Le guest configure complètement sa surface (1280×480) | `M3-CLIP-IS-CORRECT.md` (round 107) | oui |
| Conclusion du round 107 : « il manque le renderer pour consommer » | round 107 | **trop forte — corrigée ici** |
| Le wasm tourne 200 s sans aucune surface | `/tmp/def.log` | **oui — le fait décisif** |

# La conclusion du round 107 était trop forte

## Le fait

    ps3base-wasm: t=200s pc=0x002dc20c syscalls=401741 exited=0

Le jeu tourne **200 secondes** avec **zéro surface créée**, sans crash, et progresse (401 741 syscalls, en
croissance continue).

**L'absence de renderer ne bloque pas le jeu.**

## Ce que ça corrige

Le round 107 concluait : « c'est nous qui n'avons pas de renderer pour consommer la configuration du
guest ». Sous-entendu : c'est la cause du blocage.

**Le fait ne le soutient pas.** Le jeu continue sans renderer. Il ne s'arrête pas à cause de ça.

## Ce que le fait soutient

Le jeu **a besoin d'autre chose** pour avancer. Et cette autre chose n'est ni la surface, ni la texture, ni
la configuration RSX — tout cela est présent et correct.

## La comparaison des durées

| | natif | wasm |
|---|---|---|
| durée observée | **9 126 s** | **200 s** |
| résultat | monde atteint | boucle d'attente |
| pc | varie largement | **deux adresses** |

Le natif a mis **9 126 secondes**. Mon run était de **200**. Si le jeu a besoin de plusieurs milliers de
secondes pour franchir une étape, je ne l'ai jamais laissé assez longtemps.

## Ce que ça implique

Les rounds 69-107 ont cherché une **condition manquante**. Mais peut-être n'en manque-t-il aucune : peut-être
que le wasm **franchirait** l'étape avec assez de temps.

C'est testable : lancer le wasm pour 30 minutes au lieu de 200 secondes, et voir si le pc change de
catégorie.

## Ce que ça ne dit pas

Je ne sais pas si le jeu progresse **vers** le menu ou s'il tourne en rond. Le pc alterne entre deux
adresses, ce qui peut être une boucle stable ou un cycle lent.

## La mesure à faire

Un run long (20-30 minutes), en relevant le pc et le nombre de threads toutes les minutes. Si de nouveaux
threads apparaissent ou si le pc change de région, le jeu progresse. Sinon, il boucle.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **corrige** la conclusion du round 107 : l'absence de renderer ne bloque
pas le jeu (200 s sans surface, sans crash). Le natif a mis 9 126 s ; je n'ai jamais laissé le wasm aussi
longtemps.
