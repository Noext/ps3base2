# X0 — Ce qui empeche le jeu d'ouvrir une trame (orchestrateur, mesure directe)

## La fonction qui bloque, lue dans le code lifte

`f_002dc1e4_body` (out/ppu/mc_028.cpp:1065813) :

    L_2dc1e4:
        gpr[31] = 0 + 12451840;              // 0x00be0000
        gpr[3]  = ld8(gpr[31] + 1804);       // ld8(0x00be070c)
        cr_set(0, gpr[3], 0);
        if (bc_cond(4,2)) goto L_2dc21c;     // non nul -> ouvrir la trame
    L_2dc204:
        syscall(141, 300);                    // usleep(300)
        gpr[3] = ld8(gpr[31] + 1804);
        if (bc_cond(12,2)) goto L_2dc204;    // boucler tant que zero
    L_2dc21c:
        st8(gpr[31] + 1804, 0);               // le jeu remet a zero lui-meme
        return;

Le jeu attend un octet NON NUL a `0x00be070c`, ouvre sa trame, puis le remet a zero.

## Personne ne l'ecrit

    $ grep -rn '0xbe070c|0xBE070C' runtime/ --include=*.cpp --include=*.h
    runtime/src/wasm_entry.cpp:585    <- un probe qui LIT et affiche, rien d'autre

## Ce qui tourne deja mais ne fait pas ce travail

`vblank_thread()` (runtime/src/lv2/rsx.cpp:718, lance ligne 884) est actif en wasm. Il ecrit
`head_addr(1) + 0x30`, appelle `flip_done()` et `send_event(1u << 1)`. C'est le VBlank **du driver
RSX**, celui qui fait avancer les semaphores GCM. Il n'ecrit pas le semaphore de trame du titre.

**Deux mecanismes distincts.** Notre vblank satisfait le chemin label/semaphore ; il ne debloque
pas la synchronisation de trame du jeu.

## La correction

Le VBlank doit ecrire `1` (un OCTET) a `0x00be070c` a chaque trame, en plus de ce qu'il fait deja.
C'est le role du handler que le titre enregistre via `cellGcmSetVBlankHandler` — a verifier cote
natif par la piste X2.

## Ce que cette lecture elimine

| hypothese | statut |
|---|---|
| un import manquant empeche la trame | **non** — c'est une ecriture hote absente |
| le FIFO ne consomme pas | **non** — il consomme 4,5 M de methodes |
| le handler de flip n'est pas appele | **non** — corrige, 0x00bbe658 s'enregistre |
| le vblank ne tourne pas | **non** — il tourne, mais n'ecrit pas ce mot |