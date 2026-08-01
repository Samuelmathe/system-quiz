Test radio Mega <-> Nano son (commandes 200 / 201 / 202)
=====================================================

0) nano_test_dfplayer_seul  (Nano : DFPlayer + radio, avec mega_test_son_tx)
   - Dossier : nano_test_dfplayer_seul / nano_test_dfplayer_seul.ino
   - Radio nRF24 CE=9 CSN=10 + DFPlayer D5/D4 (meme cablage que nano_son_final.ino).
   - Par defaut (RUN_STARTUP_MP3_SEQUENCE=1) : apres init, joue en local 0001 / 0002 / 0003
     pour verifier la SD, puis ecoute la Mega qui envoie 200 / 201 / 202 (meme pistes).
   - Mettre RUN_STARTUP_MP3_SEQUENCE a 0 dans le .ino pour sauter la sequence locale et
     tester tout de suite avec la Mega seulement.
   - TEST_DFPLAYER_SKIP_INIT=1 : radio + serie sans DFPlayer (debug).
   - Si le moniteur reste sur "Pret" sans RECU : verifier Mega TX allumee, moniteur **9600 baud**,
     cablage nRF24. Le sketch re-appelle startListening() toutes les ~2,5 s et affiche un rappel
     apres **30 s** sans paquet valide (200/201/202).
     Le sketch ne coupe la radio que le temps d'un play/stop DF (dfPlayTrack), pas pendant
     tout le buzz — sinon la Mega peut envoyer 201/202 pendant stopListening et les paquets
     se perdent (FIFO 3 niveaux). Un paquet par tour de loop. Ne pas vider la FIFO RX en fin
     de setup : sinon le premier 200 (BUZZ) est souvent jete avant loop().

1) mega_test_son_tx
   - Ouvrir dans Arduino IDE le dossier mega_test_son_tx / mega_test_son_tx.ino
   - Carte : Arduino Mega 2560
   - Moniteur serie 9600 baud. Cycle ~11,5 s : 200, pause ~2,8 s, 201, pause ~2,8 s, 202.
     La Mega test n'appelle plus startListening() apres chaque envoi (emission seule).
     Le Nano traite un paquet par tour de loop (pas de while sur toute la FIFO d'un coup).
   - L'emission utilise write(..., true) (paquet NO_ACK) car le Nano son est en setAutoAck(false)
     et ne renvoie pas d'ACK : sans cela le moniteur Mega affiche TX FAIL a tort (MAX_RT).

2) nano_test_son_rx
   - Ouvrir nano_test_son_rx / nano_test_son_rx.ino
   - Carte : Arduino Nano
   - Même câblage DFPlayer que nano_son_final.ino (RX=D5, TX=D4)
   - Carte SD : mp3/0001.mp3, 0002.mp3, 0003.mp3, et 0101.mp3 pour buzz équipe 1
   - Moniteur série 9600 baud : lignes des le demarrage (radio avant DFPlayer).
   - Si le moniteur reste vide : DFPlayer::begin() bloque souvent sans module — dans
     nano_test_son_rx.ino mettre TEST_RX_SKIP_DFPLAYER a 1 pour tester radio + serie sans MP3.

Conditions identiques au projet principal :
- Adresse pipe son : "00002" (5 octets + '\0' dans le tableau)
- Canal RF : 108
- Débit : 250 kbps, CRC 16 bits
- Paquet : SonPayload = uint16_t cmd + uint8_t team + uint8_t reserved (4 octets)
- Mega : CE=9, CSN=53  |  Nano : CE=9, CSN=10
- megaf : **double** envoi (~8 ms) pour **201 et 202** seulement ; le **200 (buzz)** part
  **une fois** (eviter que le Nano traite le 2e 200 comme doublon et supprime tout le buzz).
  Les Nanos dedoublonnent seulement 201/202 sur ~160 ms.

nano_test_son_rx inclut déjà le DFPlayer comme nano_son_final.ino (mêmes play 1/2/3 et 100+team).

nano_son_final.ino : radio alignee sur la Mega (setPayloadSize 4 octets, CRC 16, adresses 5).
  Ne pas vider la FIFO en fin de setup pour ne pas perdre le premier paquet son.

Alternative : téléverser nano_son_final.ino à la place du récepteur test — même protocole.
