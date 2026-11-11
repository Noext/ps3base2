# Le runtime nomme desormais l'acces non mappe au point fautif

## Ce qui est ajoute

Dans `ppu::call_indirect`, branche `!fn`, derriere `-DPS3BASE_CALL_TARGET_CHECK=1` :

    if (addr == 0u || !vm::range_mapped(addr, 4)) {
        /* une fois par adresse distincte */
        LOG_WARN("ppu", "call target 0x%08x is UNMAPPED (lr=0x%08x r3=0x%llx r31=0x%llx): "
                 "the guest read a pointer from memory it never allocated", ...);
    }

## Pourquoi opt-in

Le prompt du jalon 5 interdit de modifier le runtime sauf derriere une interface et exige de mesurer avant de
changer. Le controle est donc un drapeau de compilation, pas un comportement par defaut : il **signale** sans
refuser l'appel, comme le veut un portage en cours.

## La sortie

    W ppu: call target 0x00000000 is UNMAPPED (lr=0x005d2c08 r3=0x0 r31=0x0):
           the guest read a pointer from memory it never allocated

**Une ligne, au point fautif, avec le `lr`.** C'est exactement ce que RPCS3 obtient avec `check_addr`, mais en
mode observation plutot qu'en mode refus.

## Ce que ca change pour la suite

Avant : le jeu echouait 2 000 instructions plus loin, sur un `call_indirect(0)` sans rapport apparent avec sa
cause, et il fallait reconstruire la chaine a la main — ce qui m'a coute vingt rounds et quatre erreurs
d'instrumentation.

Maintenant : le runtime dit l'adresse, le `lr`, et la cause en une phrase.

## Ce que le check va reveler

`0x00000000` n'est que le premier. Toute cible non mappee sera nommee, une fois par adresse. C'est la liste qui
manquait pour savoir quelles allocations le jeu attend.

## Position

| jalon | etat |
|---|---|
| 1 | complet |
| 2 | 5 etapes sur 7 |
| 3 | 4 criteres sur 5 ; module 44,4 Mo, plus grosse fonction 725 073 o |
| 4 | ecrit, validation navigateur impossible |
| 5 | goulots nommes, fps natif mesure |