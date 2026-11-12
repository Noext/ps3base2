# H1 — LES TROIS CRITERES PASSENT (verifie par moi sur le log de G1)

## C1 — 40 en-tetes FIFO identiques au natif

    $ diff <wasm 40 raw words> <native 40 raw words>
    wasm lines: 40   native lines: 40   diff lines: 0

Premier en-tete des deux cotes : `raw 00040060 56616661 00040064 00000030`.
L'appel vers l'offset 0 (`ea 0x40100000 raw 00040000 31337000 003c0180 66604200`) est present.

## C2 — _gcm_intr_thread existe

    5 lignes, entry 0x3300be54

## C3 — flips par 0x102

    flips102=1534

## Preuves annexes

    gcmstub lines: 0
    images chargees: libfs libgcm_sys liblv2 libspurs_jq libsre libsysmodule mc   <- 7/7
    module: 45 440 209 octets, 49 703 fonctions, plus grosse 737 751 octets

## Comment G1 y est arrive — trois corrections, dont un fait qui contredit ma consigne

### 1. Retirer PS3BASE_HLE_OVER_LLE (ma consigne etait juste)

Les 20 exports de `libgcm_sys` viennent maintenant du corps LLE. Zero ligne `gcmstub` dans le run.

### 2. Ma consigne « deplacer le contexte vers VIDEO » etait INCOMPLETE

J'avais diffuse : « le HLE alloue en MAIN, deplace-le vers VIDEO ». G1 a mesure la cause reelle :
retirer le drapeau faisait echouer le LLE avec `CELL_ENOMEM`. La region `RsxContext` heritait du plan
SPARSE (256 Mo / alignement 256 Mo), qui ne tient pas dans l'espace utilisateur `[0x10000000,0x50000000)`
deja rempli par les slots d'images et la map 256 Mo du titre.

    sys_rsx_context_allocate -> ENOMEM -> _cellGcmInitBody = 0x80010004
      -> call indirect nul a lr=0x005d2c08

C'etait la vraie chaine, pas un probleme de placement. Corrige dans `memory.cpp` : region compacte de
**64 Mo**, alignement sur sa propre taille.

### 3. Un second bouchon que personne n'avait vu

`stbi_load_from_memory` etait un stub inerte dans `ps3base_wasm_stubs.cpp` qui rendait null
("unavailable") : **les 101 `cellPngDecOpen` echouaient a t=1,3 s**. Maintenant le vrai en-tete
compile pour wasm : **101/101 decodent**.

## Ce qui reste ouvert (declare par G1, pas masque)

- le thread principal cyclait dans `f_003a18b0` apres t=14 s (le natif passe ce point a 4,8 s : plus
  lent, pas mort — `fs_read`/`lwmutex` continuent) ;
- `rsx.cpp:805` ecrit encore `byte[0xBE070C]=1` en dur a chaque vblank, ce que le natif n'ecrit jamais
  (c'est le handler du jeu qui l'ecrit). Reste a supprimer, c'est le perimetre de G3.

## Note de methode
J'ai verifie les trois criteres moi-meme sur les logs de G1, pas sur son annonce :
`diff` des 40 mots bruts, `grep -ac _gcm_intr_thread`, `grep -oE 'flips102=[0-9]+'`. Les trois tiennent.