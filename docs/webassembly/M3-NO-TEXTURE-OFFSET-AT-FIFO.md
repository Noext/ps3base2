# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `texmask=0x0` mesuré par le stub | round 85 | oui |
| Le wasm est 8× plus rapide, la lenteur n'est pas la cause | round 89 | oui |
| Les pixels décodés vont aux **mêmes adresses** des deux côtés | `/tmp/cmp.log`, `/tmp/def.log` | **oui — nouveau** |

# Trois instruments, un même résultat

## Ce que j'ai vérifié

Les 911 PNG décodés par le wasm vont aux **mêmes adresses** que chez le natif :

    dest 0x31284750  0x31297860  0x33b15fe0  0x33b161f0  0x33b16020
    (identiques dans les deux logs)

**Les pixels sont en mémoire invitée, au bon endroit.** Ce n'est pas un problème de décodage.

## La mesure au niveau du FIFO

J'ai instrumenté le point où le FIFO livre un registre, avant tout dispatch :

    if (reg >= 0x1A00 && reg < 0x1C00 && ((reg - 0x1A00) & 0x1F) == 0) {
        const uint32_t n = hits.fetch_add(1) + 1;
        if (n <= 6) LOG_INFO("texfifo", "TEXTURE_OFFSET write #%u reg 0x%04x off 0x%08x", n, reg, arg);
    }

Résultat : **aucune sortie**. Le FIFO ne transporte **aucun** `TEXTURE_OFFSET`.

## Les trois instruments concordent

| instrument | où | résultat |
|---|---|---|
| `texture_field_writes` (stub, plage élargie) | `rsx_no_display.cpp` | **0** |
| `texreg` (registres exacts) | `lv2/rsx.cpp` | 0 texture, des commandes |
| **`texfifo`** (au FIFO, avant dispatch) | `lv2/rsx.cpp` | **0** |

Trois mesures à trois endroits différents, même réponse.

## Ce que ça établit définitivement

**Le guest n'émet jamais de `TEXTURE_OFFSET`.** Ce n'est pas que le stub le perde, ni que la plage soit fausse, ni que le dispatch le consomme : le registre n'arrive pas au FIFO.

Donc le jeu, dans ce run, **ne demande aucune liaison de texture**. Les 40 `texloc` natifs se produisent chez un jeu qui, lui, en demande.

## Ce que ça implique pour la chaîne

Le maillon n'est pas « le stub ne lie pas » mais « **le guest ne demande pas** ». La question se déplace une dernière fois :

    pourquoi le guest émet-il des draws (418 572) et des clear, mais aucun TEXTURE_OFFSET ?

C'est une question sur **le chemin invité**, pas sur l'émulateur.

## Une hypothèse, non testée

Le jeu pourrait utiliser un **chemin de rendu alternative** : dessins sans texture (couleur unie,
police procédurale) qui n'ont pas besoin de `TEXTURE_OFFSET`. Cela collerait avec la primitive unique
constatée au round 86.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas dans le runtime |
| `sys_vm_memory_map ->` | non (0 contre 2) |

Le jalon 3 reste ouvert. Ce round **établit définitivement par trois instruments** que le guest
n'émet aucun `TEXTURE_OFFSET`, et que les pixels décodés sont pourtant aux bonnes adresses.
