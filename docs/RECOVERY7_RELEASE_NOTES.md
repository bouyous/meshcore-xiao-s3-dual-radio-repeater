# recovery.7.4-radio-cli-time — préversion expérimentale

Cette version part de `recovery.6-guarded`, dont le profil 3,45 V a réussi un
cycle réel : arrêt automatique sur batterie faible, exposition solaire puis
redémarrage autonome et retour radio.

## Messages lisibles dans un canal MeshCore

- Les alertes de pré-arrêt, d'arrêt final et de redémarrage peuvent être
  publiées comme messages de canal MeshCore standard (`PAYLOAD_TYPE_GRP_TXT`).
- `bat low public` sélectionne explicitement le canal général standard.
- `bat low channel <NOM> <PSK>` configure un canal de surveillance partagé ;
  `bat low <NOM>` permet ensuite de le resélectionner.
- La PSK accepte le format base64 MeshCore 128/256 bits ou 32/64 caractères
  hexadécimaux. Elle est persistée localement mais n'est jamais affichée dans
  les statuts ou les journaux.
- La destination initiale reste `private` pour qu'un flash ou une migration ne
  puisse pas générer de messages publics sans décision de l'opérateur.
- Un paquet de canal identique est réémis après quatre secondes. Les clients
  ayant reçu le premier éliminent le doublon par son hash.
- Le premier fix qui suit chaque remise en route du GPS génère une alerte de
  canal indiquant la durée d'acquisition et le nombre de satellites connus.

## Diagnostic en lecture seule dans le canal personnalisé

- `!p1 status`, `!p1 battery`, `!p1 gps`, `!p1 version`, `!p1 alerts` et
  `!p1 help` peuvent être envoyées comme messages du canal de surveillance.
- Chaque P1 répond avec son nom et `CLI OK` ou `CLI ERR`; plusieurs répéteurs
  du même canal étalent leurs réponses dans des créneaux liés à leur identité.
- La réponse repart avec le même scope que la requête. Si l'horloge du P1
  attend encore son premier fix GPS, elle utilise l'horodatage récent de la
  requête afin de rester visible au bon endroit dans le compagnon.
- L'écoute est désactivée sur `Public` et en mode d'alertes privées.
- Aucune commande de modification, de redémarrage, d'arrêt ou d'effacement
  n'est accessible par cette passerelle : connaître la PSK d'un canal ne vaut
  pas authentification individuelle.

## GPS utilisable sous batterie faible

- `gps powerguard economy` conserve le comportement sûr recovery.6.
- `gps powerguard critical` autorise le GPS entre 3,30 et 3,50 V.
- `gps powerguard off` autorise le GPS jusque pendant l'état critique pour les
  essais de décharge.
- Aucun mode ne désactive l'arrêt final : le GPS est coupé pendant la grâce
  radio `systemoff`, puis le nœud s'éteint si la tension reste sous 3,30 V
  pendant 600 secondes.
- Le choix est persistant et indépendant de `gps override on|off|Nh`.

## Validation logicielle actuelle

- Compilation nRF52840 du profil test 3,45 V : réussie.
- Compilation nRF52840 du profil terrain LPCOMP : réussie.
- Utilisation Flash : environ 426 ko sur 811 ko ; RAM : environ 33,4 ko sur
  235,5 ko.
- Les tests hôte ajoutés décrivent les trois politiques GPS. Sur la machine de
  build Windows actuelle, la suite native PlatformIO reste non exécutable car
  aucun `g++` hôte n'est installé ; les deux compilations ARM réelles sont
  néanmoins propres.
- Le trajet réel `!p1 gps` a été validé entre le P1 et un T1000E. Les traces
  ont confirmé la réception, le déchiffrement et l'émission du message de
  réponse ; les correctifs de scope et d'horodatage le rendent lisible dans le
  canal.

Cette image reste une préversion : les alertes de premier fix GPS, les réponses
simultanées de plusieurs P1 et le profil terrain LPCOMP doivent encore être
qualifiés sur une durée plus longue.

## Livrables et SHA-256

```text
E216E2A1C1E0EFBE1AC4A4D6BECCF7D75BD64E53C6B7DCC09C363771D67396E7
  SenseCap_Solar_repeater-v1.17.0-p1-recovery.7.4-radio-cli-time-dfu.zip

282B82CED0CFF62619EEA5077F1EE06A285DEAE534F5C7029E9C484C4A270F41
  SenseCap_Solar_repeater-v1.17.0-p1-recovery.7.4-radio-cli-time.uf2

B4548C716F498C101BC2378B349C0E1F0ACED44AE64293DF7825B36209BBC0C3
  SenseCap_Solar_repeater-v1.17.0-p1-recovery.7.4-test345-radio-cli-time-dfu.zip

555ADA40CE5B30C556BAEE33259FB70C11AEC24E8DEC0E2EE26DE7111C118649
  SenseCap_Solar_repeater-v1.17.0-p1-recovery.7.4-test345-radio-cli-time.uf2
```
