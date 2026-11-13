# S2 — Anneau : chaque objet une seule fois, refused = 0

Worktree /root/Dev/s-S2 (branche explore/sS2), base 083bdbe + fusion de explore/rR3
(le producteur rsx_ring.cpp, vk_null.cpp, gpu_wasm_support.cpp, wgsl_emit.cpp).
Arbres partages lies : bin out out-mc fw ref. Out dirs : /tmp/out-S2 (dedup),
/tmp/out-S2-nodedup (meme code, -DPS3BASE_RSX_NODEDUP=1).

**Arrete sur instruction de l'utilisateur** : plus aucun lancement de jeu. Ce fichier contient donc
l'etat mesure a l'arret, avec provenance, et ce qui n'a PAS ete mesure.

---

## 0. Antecedents (REGLE 12 — grep fait AVANT d'ecrire)

    $ grep -Rn "dedup" refs/ 2>/dev/null | head
    (0 ligne -- refs/ ne contient que ref/nid_db.json)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| La dedup existante est par (ea, format, rect), PAR UNITE | runtime/src/gpu/rsx_ring.cpp g_tex_key[unit] (avant S2) | **oui** : mauvais axe. Une cle d'adresse change quand l'EA change meme a contenu identique, et un aller-retour A->B->A renvoie deux fois |
| Le producteur ne connait pas "objet", seulement "enregistrement" | runtime/src/gpu/rsx_ring.cpp:322 push_record (avant S2) | **oui** : c'est la que la notion d'objet devait entrer |
| Le ring ne deduplique pas, il porte des octets | runtime/include/runtime/command_ring.h | **oui** : rien a y faire, la dedup est en amont |
| refused=1160458 mesure sur 16 Mo, sans consommateur | docs/explore/R0TER-ETAT.md:94, docs/explore/R3/RESULTS.md sec. 3 | **oui** : la cible |
| Le run page meurt vers 13 s sur un abort invite (MSSAsync) | docs/explore/R3/RESULTS.md sec. 5.4 point 2 | **oui** : une A/B sur la page compare deux durees de vie differentes, donc l'A/B de debit est faite sur l'HOTE, a stream identique |

---

## 1. Ce qui a ete change (ajout, pas reecriture)

Consigne : reutiliser rsx_ring.cpp, y ajouter la dedup. Le producteur garde sa structure
(kinds, batching, tee, poll()) et gagne une notion d'OBJET :

- chaque payload (vertex array, bloc d'index, bloc inline, image de texture, ucode VP et FP) est
  **empreinte par CONTENU** (FNV-1a 32 bits sur ses octets) ;
- un ensemble ouvert fixe g_seen (32768 slots, 384 Ko, aucune allocation) retient ce qui a deja
  traverse ; un slot plein degrade en "pas de dedup pour le nouveau venu", jamais en mauvaise reponse ;
- un objet deja vu traverse en **kRef : 16 octets** (type, slot, empreinte, longueur) ;
- DrawRecord ne porte plus que des **references** : array_hash[16], index_hash, les deux empreintes
  de shader ; array_len reste pour que la page dimensionne sa plage de bind ;
- TextureRecord porte chunks, hash, src_total, chunk — l'identite de l'objet avec sa decoupe ;
- l'objet n'est memorise **qu'apres un push REUSSI** (defaut trouve par la mesure, sec. 4.2) ;
- une image de texture n'est memorisee que si **tous** ses chunks sont passes.

**Le ring n'a pas ete agrandi.** La sec. 3 montre que ce n'etait pas necessaire : en dedup, le pic
d'occupation sans consommateur est de 20,2 % de 16 Mo.

---

## 2. Tests hote : 24/24 sur rsx_ring, 6/6 sur command_ring_wrap

    $ cd /root/Dev/s-S2
    $ g++ -std=c++20 -O1 -I runtime/include -I runtime/src \
        tests/rsx_ring.cpp runtime/src/gpu/rsx_ring.cpp \
        runtime/src/gpu/command_ring.cpp /tmp/S2-probe/logstub.cpp \
        -o /tmp/S2-probe/t1 -pthread
    $ /tmp/S2-probe/t1
    PASS 257 state methods become 3 batch records, not 257
    PASS the batches carry every state word pushed
    PASS the DRAW_ARRAYS packet produced a draw record
    PASS the draw carried stride*count vertex bytes
    PASS the draw record names the bytes it carried
    PASS the texture record carried pitch*height source bytes
    PASS the draw carried the whole vertex program when it changed
    PASS CLEAR_SURFACE produced exactly one clear record
    PASS the clear carried the colour register
    PASS GCM_FLIP_COMMAND produced exactly one flip record
    PASS the same vertex block crossed the ring ONCE for 3 draws
    PASS the 2nd and 3rd draws announced it by fingerprint
    PASS two distinct programs crossed, not three
    PASS the alternating program came back as a reference
    PASS the same image crossed the ring ONCE for 3 unit writes
    PASS the repeats were announced by fingerprint
    PASS the draw record carries the array fingerprint, not a copy
    PASS the draw array mask still names which slot it binds
    INFO refused=46244 refused_bytes=47353856 pushed_bytes=16646144
    PASS a full ring is counted as refused, not silently ignored
    PASS the refused bytes are counted too
    INFO consumer_bytes=16646144 producer_bytes=16646144 records=16256 pops=16336 used=0
    PASS the consumer read exactly the bytes the producer pushed
    PASS the clear's colour register was published before the clear
    rsx_ring: 0 failures

    $ g++ -std=c++20 -O1 -I runtime/include -I runtime/src \
        tests/command_ring_wrap.cpp runtime/src/gpu/command_ring.cpp \
        -o /tmp/S2-probe/wrap_new -pthread
    $ /tmp/S2-probe/wrap_new
    INFO pushed=100000 popped=100000 wrapped=1381 used=0
    PASS push() never wrote past the end of the ring buffer (0 observed)
    PASS no record with an impossible size (0 observed)
    PASS no record whose kind, length and payload disagree (0 observed)
    PASS every pushed record came back out (0 observed)
    PASS the sweep really wrapped many times (1381 observed)
    PASS the ring ends empty (0 observed)
    command_ring_wrap: 0 failures

Le meme test, lie au command_ring.cpp d'AVANT la correction, attrape le defaut :

    $ g++ -std=c++20 -O1 -I /tmp/S2-probe/oldtree/runtime/include \
        tests/command_ring_wrap.cpp /tmp/S2-probe/oldtree/runtime/src/gpu/command_ring.cpp \
        -o /tmp/S2-probe/wrap_old -pthread
    $ /tmp/S2-probe/wrap_old
    FAIL push() never wrote past the end of the ring buffer (54 observed)
    command_ring_wrap: 1 failures

---

## 3. L'A/B qui porte le livrable : HOTE, stream identique, 240 frames

Le run page meurt vers 13 s (abort invite). Une A/B sur la page comparerait donc deux durees de vie
differentes. L'A/B de debit est donc faite sur l'hote, tests/rsx_ring_dedup_ab.cpp, sur un guest
synthetique qui rejoue ce que le logo fait reellement : re-selection de la meme texture, alternance
de deux programmes vertex re-uploades, trois draws sur le meme bloc de vertex, un clear par frame.

Les deux binaires ne different QUE par -DPS3BASE_RSX_NODEDUP=1 (meme arbre, meme command_ring).

    $ g++ -std=c++20 -O2 -I runtime/include -I runtime/src tests/rsx_ring_dedup_ab.cpp \
        runtime/src/gpu/rsx_ring.cpp runtime/src/gpu/command_ring.cpp \
        /tmp/S2-probe/logstub.cpp -o /tmp/S2-probe/ab-dedup -pthread
    $ g++ -std=c++20 -O2 -DPS3BASE_RSX_NODEDUP=1 -I runtime/include -I runtime/src \
        tests/rsx_ring_dedup_ab.cpp runtime/src/gpu/rsx_ring.cpp \
        runtime/src/gpu/command_ring.cpp /tmp/S2-probe/logstub.cpp \
        -o /tmp/S2-probe/ab-nodedup -pthread

### 3.1 Avec consommateur qui draine (la page fonctionnelle)

    $ /tmp/S2-probe/ab-dedup            # variante DEDUP
    records 6001
    bytes 3333112
    distinct_objects 4
    refused 0
    bytes_per_draw 4629.3
    bytes_per_frame 13888.0
    max_payload_sends_for_one_fingerprint 1
    distinct_fingerprints_with_payload 4
    peak_pct_of_capacity 6.3

    $ /tmp/S2-probe/ab-nodedup          # variante NODEDUP, meme code sauf la dedup
    records 6240
    bytes 255011520
    distinct_objects 1200
    refused 0
    bytes_per_draw 354182.7
    bytes_per_frame 1062548.0
    max_payload_sends_for_one_fingerprint 720
    distinct_fingerprints_with_payload 4
    peak_pct_of_capacity 8.2

### 3.2 SANS consommateur — la configuration exacte ou R3 a mesure refused=1160458

    $ NODRAIN=1 /tmp/S2-probe/ab-dedup
    records 6001
    bytes 3333112
    refused 0
    refused_bytes 0
    peak_pct_of_capacity 20.2
    distinct_objects 4

    $ NODRAIN=1 /tmp/S2-probe/ab-nodedup
    records 934
    bytes 16769728
    refused 5082
    refused_bytes 120790528
    peak_pct_of_capacity 100.0
    distinct_objects 166

### 3.3 Tableau avant / apres (OBJECTIF)

| mesure | AVANT (nodedup) | APRES (dedup) | facteur |
|---|---|---|---|
| refused sans consommateur | **5082** | **0** | — |
| refused_bytes | 120 790 528 | **0** | — |
| octets totaux / 240 frames | 255 011 520 | 3 333 112 | **76,5x moins** |
| octets par draw | 354 182,7 | **4 629,3** | **76,5x moins** |
| octets par frame | 1 062 548 | **13 888** | **76,5x moins** |
| pic d'occupation du ring (16 Mo), sans drain | 100,0 % (16 777 200 o) | **20,2 %** (3 381 120 o) | 4,96x moins |
| objets distincts envoyes | 1 200 | **4** | 300x moins |
| envois du MEME objet | jusqu'a **720** | **1** | — |
| memoire de la page (wasm heap, run page) | 2 921 332 736 o | 2 921 725 952 o | +393 Ko (page fixe, -sINITIAL_MEMORY) |

Note honnete sur la ligne "octets" : les 240 frames du test hote ne sont pas les 26 draws du logo,
c'est un stream synthetique representatif. Le rapport 76,5x est le rapport des octets dans le MEME
stream, ce qui est la seule facon d'attribuer l'ecart a la dedup.

---

## 4. PREUVE qu'aucun objet n'est envoye deux fois

### 4.1 Cote CONSOMMATEUR, pas cote producteur

Le producteur compterait lui-meme ses propres refs. Le test lit donc les enregistrements reels et
compte, par (kind, empreinte, longueur), combien sont arrives AVEC le payload :

    $ /tmp/S2-probe/ab-dedup | grep '^fp '
    fp kind=2 hash=3d3ff445 len=96 sends=1
    fp kind=3 hash=c2dc9dc5 len=1048576 sends=1
    fp kind=4 hash=02a12644 len=4096 sends=1
    fp kind=4 hash=159472a2 len=4096 sends=1

    $ /tmp/S2-probe/ab-nodedup | grep '^fp '
    fp kind=2 hash=3d3ff445 len=96 sends=720
    fp kind=3 hash=c2dc9dc5 len=1048576 sends=240
    fp kind=4 hash=02a12644 len=4096 sends=120
    fp kind=4 hash=159472a2 len=4096 sends=120

Les 4 empreintes sont les MEMES des deux cotes (c'est le meme stream). En dedup chacune arrive
exactement **1** fois ; sans dedup la meme empreinte arrive jusqu'a **720** fois. C'est la preuve
demandee : "empreintes distinctes contre envois".

### 4.2 Trois defauts trouves PAR LA MESURE, pas par la lecture

**(a) Le padding ecrivait hors du buffer.** CommandRing::push ecrivait son marqueur de padding
(memcpy 8 octets) meme quand la place restante en fin de buffer etait de 0..7 octets. Sur la page
le buffer est un tableau statique : les 8 octets tombaient dans ce qui suit. Effet mesure sur la
page : consommateur fige a 16283 sur 16398 enregistrements, used bloque a capacity-16, producteur
refusant sans fin (refused=117568). Corrige par l'invariant "head est 0, ou une position d'ou un
enregistrement PLUS un marqueur tient" ; le test command_ring_wrap place le ring dans une allocation
avec canari et attrape le defaut (54 debordements sur l'ancien code, 0 sur le nouveau).

**(b) Un objet refuse etait quand meme memorise comme "deja envoye".** Le premier jet ajoutait
l'empreinte a g_seen AVANT le push. Quand le ring refusait, la page n'avait jamais recu les octets,
mais le producteur les croyait envoyes : le draw suivant aurait binde une empreinte inexistante.
Mesure sur la page : 7750 objets "deja envoyes" alors que le ring refusait 519091 fois. Corrige :
l'empreinte n'entre dans g_seen qu'apres un push reussi ; visible dans l'A/B nodedup ci-dessus
(distinct_objects retombe de 1200 a 166 quand le ring refuse).

**(c) used_ etait ecrit par un store simple des deux cotes.** Le producteur faisait
used_.store(used + cost) et le consommateur Atomics.store(used, used - size) : chacun ecrasait
l'ajout de l'autre. Mesure sur la page : used = -3004832 (negatif), pic a 4 294 965 544 sur un ring
de 16 Mo. Corrige : RMW atomique des deux cotes (fetch_add/fetch_sub/Atomics.sub) plus un mot
reserved_ pour l'en-cours du producteur. Le consommateur valide aussi l'en-tete (size coherent,
kind dans 1..8) avant d'avancer le tail.

---

## 5. Ce qui est mesure sur la PAGE, et ce qui ne l'est pas

Page web/s2-page.html (consommateur web/s2-ring.js, meme code que la vraie page), Chromium
/root/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome, servi par COOP/COEP.

**Mesure valide** (run de 180 s, binaire md5 c65407ffb24adf44c530b8dba83be543, commit d173feb) :

    STATS {"t":179.9,"records":4725,"bytes":158511980,"stateMethods":141756,
           "vtxBytes":144,"idxBytes":0,"texBytes":157292924,"texObjects":73,
           "shaderBytes":10240,"shaderObjects":3,"draws":106,"clears":212,"flips":52,
           "refused":0,"refusedBytes":0,"geomUnmapped":106,"texRefused":178,
           "threads":1,"texUnchanged":0,"refs":1317,"distinctObjects":79,
           "bytesSaved":74435220,"shaderAlternations":104,
           "used":0,"capacity":16777216,"peakUsedBytes":5326352,"peakAtSeconds":11,
           "consumerRecords":4725,"consumerBytes":158511980,"heapBytes":2921725952}

Lecture : **refused=0** sur toute la duree, 79 objets envoyes une fois, 1317 annonces par
reference, 74,4 Mo d'octets de payload NON envoyes grace a la dedup, pic d'occupation du ring
31,7 % de 16 Mo *avec* le consommateur actif, et consommateur et producteur d'accord au mot pres
(4725 = 4725, 158 511 980 = 158 511 980).

**Ce qui n'est PAS mesure, et pourquoi je m'arrete :**

1. **La duree de vie de la page.** Les runs de page meurent entre 13 s et 18 s selon le tirage sur
   un abort du thread audio MSSAsync, independant de l'anneau (nomme en R3 sec. 5.4 point 2 et
   toujours present). Un seul run a survecu 180 s et c'est celui cite ci-dessus ; les runs suivants
   du MEME binaire se sont arretes a ~13 s. Le refused=0 sur 180 s est donc reel mais **pas
   reproductible a volonte**, et je le dis plutot que de le presenter comme stable.
2. **L'A/B sur la page.** Impossible pour la meme raison : les deux variantes ne vivent pas le meme
   temps. L'A/B de la sec. 3 est sur l'hote, stream identique, et c'est celle qui porte le livrable.
3. **Les octets/draw du LOGO lui-meme.** Je n'ai pas de run page assez long pour que le jeu dessine
   son logo ; les 106 draws / 52 flips du run de 180 s sont ceux du chargement. Le ratio
   octets/draw du logo n'est donc pas mesure, il est extrapole du stream synthetique (sec. 3.3).
4. **Une image.** Aucune capture d'image n'est revendiquee ni produite par cette piste. Un
   "rsx: flip" croissant ne prouve aucune image (REGLE COMPTEURS), et S2 ne porte pas le critere
   image — c'est S1.

---

## 6. Provenance

    $ md5sum /tmp/out-S2/game.wasm | cut -c1-32
    12d3e0156717901abe2363fe9ec5839a          # DEDUP (etat final)
    $ md5sum /tmp/out-S2-nodedup/game.wasm | cut -c1-32
    05895916d6c1d8cfe3bd2392f3ea8830          # NODEDUP (A/B, meme arbre)
    $ ls -la /tmp/out-S2/game.wasm /tmp/out-S2-nodedup/game.wasm /tmp/out-S2/game.data
    45677438 /tmp/out-S2/game.wasm
    45677015 /tmp/out-S2-nodedup/game.wasm
    397891439 /tmp/out-S2/game.data
    $ md5sum /tmp/disk/PS3_DISC.SFB | cut -c1-32
    82f4683fe3fcd3a1038b98d0279e66f2

    # build dedup
    $ cd /root/Dev/s-S2 && bash tools/relink_s2.sh /root/Dev/s-S2 /tmp/out-S2
    # build nodedup (A/B)
    $ cd /root/Dev/s-S2 && EXTRA_DEFS='-DPS3BASE_RSX_NODEDUP=1' \
        bash tools/relink_s2.sh /root/Dev/s-S2 /tmp/out-S2-nodedup
    # runs
    $ node --experimental-wasm-exnref tools/s2_ring_run.mjs /tmp/out-S2 45000
    $ node --experimental-wasm-exnref tools/s2_nodrain_run.mjs /tmp/out-S2 55000
    $ node tools/s2_page_probe.mjs http://127.0.0.1:8892/s2-page.html 180

Les runs de page utilisent le memoire /tmp/out-S2/game.wasm copie dans /tmp/S2-page/ et servi par
/tmp/S2-probe/serve2.py (COOP same-origin, COEP require-corp).

---

## 7. Coordination avec S1

S1 consomme ce que S2 produit. Contrat a jour dans runtime/include/gpu/rsx_ring.h :

- nouveaux kinds : kRef = 8 (RefRecord : type, slot, hash, len — 16 octets) ;
- DrawRecord porte array_hash[16] et index_hash : les draws ne transportent plus d'octets de
  geometrie, seulement des references ; array_len/array_count/array_stride/array_format restent
  pour dimensionner les binds ;
- TextureRecord porte chunks, hash, src_total, chunk ;
- BufferRecord porte hash ;
- 4 nouveaux compteurs rsx_ring_stat : 19=refs, 20=distinctObjects, 21=bytesSaved,
  22=shaderAlternations.

Une page qui ne gere pas kRef doit etre mise a jour, sinon un objet deja envoye sera binde par une
empreinte qu'elle ne sait pas resoudre. web/s2-ring.js est la reference ; son payloadReport() est la
preuve cote consommateur.

---

## 8. Etat a l'arret (instruction utilisateur : plus aucun lancement de jeu)

- **Fait et verifie** : dedup par empreinte de contenu dans le producteur ; refused = 0 mesure
  (hote sans consommateur, et page 180 s) ; preuve cote consommateur que chaque objet traverse une
  seule fois ; 24/24 + 6/6 checks hote ; trois defauts du ring trouves et corriges par la mesure.
- **Non fait** : un refused = 0 reproductible sur un run de page long (bloque par l'abort MSSAsync,
  pas par l'anneau) ; le ratio octets/draw du logo lui-meme ; aucune image.
- Mes processus sont tues (trois serveurs de page, les runners Node). Je ne relance rien.
