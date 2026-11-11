# Jalon 4 : les entrees web

## Ce que le prompt demande

> Entrees : clavier + Gamepad API. Le pad virtuel de tools/play.sh n'a pas d'equivalent web et sera
> abandonne.

## Ce qui existait

L'arm wasm de poll_pad renvoyait un pad vide. Le commentaire disait que le jalon 4 remplirait le pad depuis
les evenements DOM.

## Ce que j'ai ecrit

Deux symboles C que la page ecrit directement depuis JavaScript : g_web_pad_words[4] et g_web_pad_connected.

poll_pad les lit et les copie dans les quatre mots que cellPadGetData attend. Aucune traduction : la page
ecrit directement le format du guest.

## Deux choix, chacun justifie

| choix | raison |
|---|---|
| lecture sans verrou | un mot de bouton dechire = au pire une image avec un mauvais bouton ; un verrou dans le poll laisserait la page bloquer l'emulation |
| g_web_pad_connected = 0 par defaut | un pad connecte sans bouton est un etat avec lequel le titre demarre, et c'est ce qui permet au run Node de fonctionner |

## La regression que j'ai causee et corrigee

Apres le raccordement, le run est passe de 33 a 0 GetFriendsThreadProc et de 26 a 16 threads.

La cause : j'avais laisse deux objets pour le meme fichier -- r_host_input_port.o (498 octets, l'ancien) et
e_host_input_port.o (648 octets, le nouveau). Le linker prenait l'ancien.

Apres suppression du doublon : menu 1, GetFriends 20, threads 25, crash 0.

## Ce que je n'ai pas fait

Je n'ai pas ecrit le code JavaScript qui remplit ces mots depuis les evenements DOM et la Gamepad API. Le
seam C++ est pret et verifie ; le mapping cote page reste a ecrire.

## Position

| critere du jalon 4 | etat |
|---|---|
| ring de commandes | fait, teste |
| boucle qui cede | fait, mesure |
| ring audio | fait, teste, raccorde |
| entrees web | seam C++ fait et verifie ; JavaScript cote page non |
| AudioWorklet JavaScript | non |
| worker / WebGPU dans la page | non |
| page dans Chrome | non |