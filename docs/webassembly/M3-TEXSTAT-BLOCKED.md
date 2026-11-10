# Antécédents

| Constat | Source (chemin:ligne) | S'applique ici ? |
|---|---|---|
| `texmask=0x0` : le guest ne lie aucune texture | `M3-NOT-TEXTURE-REGS.md` (round 82) | oui |
| Le natif n'a jamais été tracé pour cette mesure | `/tmp/cmp.log` : 0 `rsxm` | **oui — la comparaison manque** |
| Le build natif échoue sur `hook_flags` | `runtime/src/hooks.cpp:960,1007` | oui — chantier parallèle |

# La comparaison tentée, et son blocage

J'ai instrumenté le chemin FIFO avec un compteur dédié :

    if (reg >= 0x1A00 && reg < 0x1C00 && ((reg - 0x1A00) % 0x20) == 0) {
        const uint32_t n = hits.fetch_add(1) + 1;
        if (n <= 5) LOG_INFO("texstat", "TEXTURE-UNIT write #%u reg 0x%04x arg 0x%08x", n, reg, arg);
    }

L'objectif : mesurer **le même compteur** côté natif, pour savoir si le guest natif écrit ces registres.

## Le blocage

Configuration d'un répertoire séparé, puis compilation. **Le lien échoue** :

    hooks.cpp:960: undefined reference to `ppu::hook_flags'
    hooks.cpp:1007: undefined reference to `ppu::hook_flag_image_count'

C'est le **même blocage qu'au round 59**. `out/ppu/table.cpp` (daté du 5 novembre, 730 Ko) **ne
définit pas** ces symboles, alors que `CMakeLists.txt:209` affirme que « l'image table définit
maintenant `ppu::hook_flags` ». Le codegen qui doit les émettre est **le chantier de l'autre agent**.

Le build principal (`build/ps3base`, 346 Mo) **fonctionne** — il date d'avant ce changement de
contrat, donc il lie contre l'ancienne définition.

## Ce que j'en tire

Je ne peux pas produire la mesure native tant que le codegen n'a pas atterri. Je note aussi que cette
situation va se résoudre d'elle-même : dès que l'autre agent termine, la table définira les symboles
et le build de mesure fonctionnera.

## Ce que ce round apporte quand même

L'instrumentation est **écrite et compile** côté wasm. Elle est prête à être utilisée. Le compteur
place la détection **au même endroit** que le traitement des registres
(`lv2/rsx.cpp`, juste avant `gpu::rsx::method`), donc il verra exactement ce que le guest écrit.

## Position

| critère du jalon 3 | état |
|---|---|
| module sous 80 Mo | **oui, 56 Mo** |
| plus grosse fonction sous 1 Mo | non, 1,4 Mo |
| ligne de log du menu | **non** |

Le jalon 3 reste ouvert. Ce round **n'a pas produit la mesure visée** : le build natif de mesure est
bloqué par le codegen parallèle, comme au round 59. L'instrumentation est prête et sera utilisable dès
que ce chantier atterrit.
