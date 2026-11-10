# Antécédents

| Constat | Source | S'applique ? |
|---|---|---|
| Cinq fonctions importées jamais appelées | rounds 112-120 | oui |
| Le natif atteint le monde | `/tmp/cmp.log` | oui |
| **wasm : 9 imports non implémentés, tous communs. Natif : 17** | mesure | **oui** |

# Les imports manquants ne discriminent pas

## La mesure

| | natif | wasm |
|---|---|---|
| imports non implémentés | **17** | **9** |
| communs | 9 | 9 |
| **propres au wasm** | — | **0** |
| propres au natif | 8 | — |

Les 9 communs : `sceNpBasicGetFriendListEntryCount`, `cellSslInit`, `cellSslCertificateLoader`,
`cellRudpInit`, `cellRudpEnableInternalIOThread`, `cellHttpsInit`, `cellHttpInitCookie`, `cellHttpInit`,
`cellHttpCreateClient`.

Les 8 propres au natif : `cellFiberPpuSelf`, `cellPngEnc*` (7 fonctions).

## Ce que ça établit

**Aucun import manquant n'est propre au wasm.** Ils manquent aussi au natif, qui atteint le monde.

Donc **les HLE absents ne sont pas la cause du blocage.**

## Ce que ça élimine définitivement

| hypothèse | statut |
|---|---|
| il manque un HLE réseau (`sceNp`, `cellHttp`) | **éliminée** — manque aussi au natif |
| il manque `cellPngEnc` | **éliminée** — le natif ne l'a pas et progresse |
| il manque `cellFiber` | **éliminée** — idem |

## Ce que le natif fait de plus

Le natif **appelle** 8 fonctions qu'il n'implémente pas, et continue. Le wasm n'en appelle que 9.

Cela signifie que le natif **avance plus loin** dans le code, pas qu'il a plus de HLE.

## Ce que ça laisse

Le blocage est **dans le comportement du guest**, pas dans la couverture HLE.

## Position

| critère | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | n'existe pas |
| sys_vm_memory_map | non (0 contre 2) |