# G0 — Corrections du brief, mesurees

## 1. Les images : 7 en natif, 6 en wasm (pas 9)

J'avais ecrit dans le brief « les 9 images de Minecraft natif ». Mesure :

    $ grep -aoE 'loader: loaded [a-z_0-9]+' /root/Dev/z1-evidence/native-3000.log | sort -u
    libfs  libgcm_sys  liblv2  libspurs_jq  libsre  libsysmodule  mc         <- 7

    $ grep -aoE 'loader: loaded [a-z_0-9]+' /tmp/allrun.log | sort -u
    libfs  libgcm_sys  liblv2  libsre  libsysmodule  mc                     <- 6

`libvdec` et `libsmvd2` ne sont chargees **ni en natif ni en wasm**. La seule image qui manque
reellement cote wasm est **`libspurs_jq`**. Les 6 autres sont deja la, aux memes adresses
(0x30000000, 0x31000000, 0x32000000, 0x33000000, 0x34000000).

Correction trouvee par la piste G1.

## 2. Le bouchon n'est pas l'image, c'est le detournement HLE

`libgcm_sys` **est** charge en LLE (text 0x33000000). Ce qui remplace son code est
`PS3BASE_HLE_OVER_LLE` (`runtime/src/ppu_runtime.cpp:193`) : il fait passer les imports `cellGcmSys`
par notre HLE au lieu du corps LLE. C'est cela que G1 doit retirer.

## 3. La divergence des flux, cote a cote

Meme adresse effective des deux cotes, contenu different des le premier mot :

    NATIF  (ea 0x40101000) : raw 00040060 56616661 00040064 00000030  count 1  method 0x0060
    WASM   (get=0x001000)  : raw 00041d6c 00000ff0 00041d70 00010000  count 1  method 0x1d6c

Et a la base du ring (ea 0x40100000) :

    NATIF : raw 00040000 31337000 003c0180 66604200   <- buffer d'appel valide
    WASM  : ring[0..7] = 00000000 ...                <- ZEROS (piste Z1)

Le natif commence par `NV406E_SET_CONTEXT_DMA_SEMAPHORE` (0x0060) avec l'argument ASCII `0x56616661`
("Vafa"). Le wasm commence par `NV4097_SET_SEMAPHORE_OFFSET` (0x1D6C). **Le guest ne produit pas le
meme flux**, ce qui est coherent avec un libgcm_sys dont le code est remplace.

## 4. Memoire : deux trous mesures par la piste G5

### 4.1 `vm::ptr` rebouclait sans faulter

En wasm32 `uintptr_t` fait 32 bits. `g_base + addr` peut reboucler **sous** g_base et designer un
autre objet du tas -- sans aucune erreur, parce que l'acces reste dans la memoire lineaire.
Mesure sur le binaire wasm reel :

    read 0x00000090 at raw pointer (no trap)      <- vm::ptr(0xFFFFFFFF) via la semantique d'origine
    RuntimeError: memory access out of bounds     <- vm::ptr(0xC0000000), cas du depassement

Le rebouclage **lisait `0x00000090` dans le tas sans trap**. C'est pire qu'un fault : la valeur
ressemble a une donnee valide. Corrige par une assertion dans `ptr()` (somme en uint64, refus du
rebouclage et du depassement), avec `g_limit` pose par `vm::init()` a `base + besoin reel`.

### 4.2 `0xD0000000` ne tient pas dans MAXIMUM_MEMORY

    g_base=8566016 (0x82b500)  MAXIMUM_MEMORY=3221225472 (3.00 GiB)
    EA 0xC0000000 inside map_span=1, EA 0xD0000000 inside map_span=0
    fit brief 0xD0000000 (stack region base)   need_wasm=0xd082b500 (3.26 GiB) fits=0
    fit brief 0xC0000000 + 249MB video window  need_wasm=0xd012b500 (3.25 GiB) fits=0
    fit compact stack top (0x60000000 + 256MB) need_wasm=0x7082b500 (1.76 GiB) fits=1

**Les adresses du brief (piles a 0xD0000000, RSX a 0xC0000000) ne tiennent pas** avec g_base a
0x82b500 et 3 GiB de maximum. La disposition compacte (piles a 0x60000000, video a 0x50000000) tient,
et c'est celle que le runtime utilise deja.

## Etat des pistes G

| piste | etat |
|---|---|
| G1 | 55 lignes, baseline mesuree, retire les bouchons |
| G2 | demarre |
| G3 | 23 lignes, inventaire en cours |
| G4 | 25 lignes, sonde pthread en place |
| G5 | **123 lignes, deux trous memoire mesures et corriges** |