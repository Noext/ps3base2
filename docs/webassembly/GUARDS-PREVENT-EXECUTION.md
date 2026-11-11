# Mes gardes empechent l'execution qu'elles mesurent

## Le probleme

`f_0014f8e4` est appele et sa garde tire (`syncfn: 1`). Mais **aucun** des appels internes n'est atteint
(`inner: 0`), alors que le code est **strictement lineaire** : aucun retour, aucune branche, aucun `goto`
entre l'entree et le premier appel.

## La cause

Le code genere pour chaque fonction est :

    void f_0014f8e4(ppu::Context& ctx, uint8_t* base) {
        if (ppu::hook_flags[0x53e39]) [[unlikely]] {
            ppu::hook_call(ctx, base, 0x14f8e4, f_0014f8e4_body);
            return;
        }
        f_0014f8e4_body(ctx, base);
    }

Quand une garde est installee, `hook_flags` est arme : **`hook_call` remplace le corps**. Il notifie la garde
puis revient **sans executer le code de la fonction**.

## Ce que ca signifie

| instrument | effet |
|---|---|
| garde posee sur `A` | `A` **s'execute quand meme** (le corps tourne) |
| garde posee sur `A`, puis appel de `B` depuis `A` | `A` s'execute, **`B` aussi** |

Mais si `A` **est** la fonction gardee et que son corps est remplace, ses appels internes **disparaissent**.

## Ce que ca invalide

| mesure | validite |
|---|---|
| round 178 : « aucun des six appelants n'est execute » | **douteuse** — la garde sur `0x14f8e4` a pu les supprimer |
| round 179 : « `f_0014f8e4` est atteint » | **vrai**, mais il n'execute pas son corps |

Mes gardes de niveau 1 et 2 etaient posees sur `f_0014f8e4` **et** sur les fonctions internes. La garde sur la
fonction englobante a **supprime** les appels que je mesurais.

## La methode correcte

Une garde doit retourner `false` **et** le corps doit continuer a s'executer. Or `hook_call` ne le fait pas.

Il faudrait :

1. soit ne garder **qu'une seule adresse a la fois**, jamais une fonction et ses descendants ;
2. soit modifier `hook_call` pour executer le corps apres notification.

La seconde est un changement du runtime — le prompt du jalon 5 l'interdit sauf derriere une interface, et je
ne l'ai pas fait.

## Ce que je dois refaire

Toute mesure de reachabilite qui garde a la fois une fonction et ses callees est **suspecte**. Les rounds 124,
177, 178 et 179 sont concernes.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 5 criteres sur 6 |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |