# Guide CLI — SenseCAP Solar Node P1 recovery.7.4-radio-cli-time

Ce guide couvre les deux images expérimentales recovery.7 :

- `v1.17.0-p1-recovery.7.4-radio-cli-time` : profil terrain SYSTEMOFF/LPCOMP ;
- `v1.17.0-p1-recovery.7.4-test345-radio-cli-time` : profil de test avec réveil
  simulé à 3,45 V en SYSTEM ON basse consommation.

Les exemples supposent une console USB série. Les commandes ajoutées
`gps override ...` et `eventlog ...` sont volontairement limitées à cette
console locale. Les commandes `bat low ...`, `alert ...` et
`gps powerguard ...` fonctionnent aussi depuis une CLI MeshCore distante déjà
authentifiée comme administrateur. Envoyer une commande par ligne.

## Contrôle rapide recommandé

Après un flash ou un redémarrage :

```text
ver
board
get power.status
get pwrmgt.source
get pwrmgt.bootreason
get pwrmgt.bootmv
get gps.schedule
gps override status
gps powerguard status
bat low status
eventlog status
```

`ver` doit contenir `recovery.7`. Le profil test indique un seuil de réveil
ADC de 3450 mV ; le profil terrain indique `3/8 VDD x 3.004` et nécessite une
mesure réelle de son seuil LPCOMP.

## Profil réseau validé sur l'unité de test

Le bloc ci-dessous correspond au paramétrage effectivement appliqué, sauvegardé
et relu sur le P1 de test. Envoyer **une commande par ligne** et attendre sa
réponse avant de poursuivre. Ne pas redémarrer pendant une écriture de
préférences ou avant la réponse `OK` de `region save`.

```text
set advert.interval 60
set flood.advert.interval 24
set flood.max.advert 6
set flood.max.unscoped 5
set path.hash.mode 1
set multi.acks 1
set loop.detect minimal
set dutycycle 10
set agc.reset.interval 4
set repeat on
region def eu fr fr-pac fr-05 fr-73
region default fr
region home fr
region save
reboot
```

Après le retour du port série, vérifier la persistance avec :

```text
get advert.interval
get flood.advert.interval
get flood.max.advert
get flood.max.unscoped
get path.hash.mode
get multi.acks
get loop.detect
get dutycycle
get agc.reset.interval
get repeat
region
region default
region home
eventlog status
```

Les valeurs attendues sont respectivement `60`, `24`, `6`, `5`, `1`, `1`,
`minimal`, `10.0%`, `4`, `on`, puis la chaîne
`eu > fr > fr-pac > fr-05 > fr-73`, avec `fr` comme région par défaut et
région d'origine. `eventlog status` doit montrer un nouveau numéro de boot et
`write_errors 0`.

### Détail des réglages réseau du profil

| Commande | Valeurs et unité | Persistance | Effet dans ce profil |
|---|---|---:|---|
| `set advert.interval 60` | `0` pour désactiver, sinon 60 à 240 minutes ; valeur arrondie au multiple de 2 inférieur | Oui | Une annonce locale zéro saut toutes les 60 minutes. |
| `set flood.advert.interval 24` | `0` pour désactiver, sinon 3 à 168 heures | Oui | Une annonce d'identité propagée en flood toutes les 24 heures. |
| `set flood.max.advert 6` | 0 à 64 sauts | Oui | Limite les annonces flood à 6 sauts. `0` interdit leur propagation. |
| `set flood.max.unscoped 5` | 0 à 64 sauts | Oui | Limite à 5 sauts les floods sans région/scope ; les paquets correctement scopés suivent leur propre politique. |
| `set path.hash.mode 1` | `0`, `1` ou `2` | Oui | Utilise des identifiants de chemin sur 2 octets pour les annonces émises par ce répéteur. `0` = 1 octet, `2` = 3 octets. |
| `set multi.acks 1` | `0` ou `1` | Oui | Active la prise en charge des accusés de réception multiples. |
| `set loop.detect minimal` | `off`, `minimal`, `moderate` ou `strict` | Oui | Rejette seulement les floods présentant les répétitions de chemin caractéristiques d'une boucle, avec la politique la moins agressive. |
| `set dutycycle 10` | 1 à 100 % | Oui | Limite l'occupation radio de longue durée à environ 10 %. Ce réglage ne remplace pas le respect du plan de fréquences local. |
| `set agc.reset.interval 4` | Secondes, arrondies au multiple de 4 inférieur ; `0` désactive | Oui | Réinitialise périodiquement l'AGC toutes les 4 secondes. Utiliser une valeur représentable de 0 à 1020 secondes. |
| `set repeat on` | `on` ou `off` | Oui | Autorise le transfert des paquets reçus. |
| `region def eu fr fr-pac fr-05 fr-73` | Noms séparés par des espaces | En RAM jusqu'à `region save` | Crée une chaîne hiérarchique : chaque nom devient l'enfant du précédent. Les nouvelles régions autorisent le flood par défaut. |
| `region default fr` | Nom existant ou `<null>` | Sauvegardé immédiatement | Utilise `fr` comme scope par défaut pour les émissions du nœud. |
| `region home fr` | Nom existant | En RAM jusqu'à `region save` | Marque `fr` comme région d'origine. Le caractère `^` affiché par `region` désigne cette région. |
| `region save` | Aucune option | Oui | Écrit la table et les marqueurs régionaux. Toujours attendre `OK`. |
| `reboot` | Aucune option | Sans objet | Redémarre immédiatement, sans réponse finale ; sert ici à vérifier la persistance. |

`path.hash.mode 1` réduit le risque de collision d'identifiants par rapport au
mode 0, mais les anciens nœuds antérieurs à MeshCore 1.14 peuvent ne pas relayer
les chemins multi-octets. `loop.detect minimal` laisse davantage de tolérance
qu'un réglage `moderate` ou `strict` tout en limitant les tempêtes de paquets.

`region def` ne supprime pas une ancienne arborescence. Toujours lire `region`
avant `region save` si le nœud avait déjà été configuré. Une erreur au milieu
d'une commande peut laisser les premiers éléments créés uniquement en RAM ;
corriger ou redémarrer sans sauvegarder.

## Commandes batterie et réveil ajoutées

| Commande | Résultat / usage |
|---|---|
| `get power.status` | Résumé : tension, état, durée critique, délai et seuil d'arrêt. |
| `get power.state` | État `normal`, `economy` ou `critical`. |
| `get power.vbat` | Dernière mesure batterie valide en millivolts. |
| `get power.lowtime` | Durée continue passée dans la zone critique. |
| `get power.wakethreshold` | Méthode et seuil de réveil de l'image installée. |
| `get power.temp` | Indique que la NTC batterie n'est pas accessible au MCU et affiche séparément la température MCU. |
| `get power.chargeguard` | Confirme la protection thermique autonome CN3165/NTC. |

Politique recovery.7 :

- passage en économie sous 3,50 V ;
- retour normal à partir de 3,55 V ;
- état critique sous 3,30 V ;
- annulation du compteur critique à partir de 3,35 V ;
- arrêt après 600 secondes critiques continues ;
- alerte chiffrée d'arrêt préventive à 3,35 V, réarmée à 3,40 V ;
- seconde alerte prioritaire juste avant l'arrêt effectif, puis délai radio de
  12 secondes pour l'émission et une retransmission en absence d'ACK ;
- l'alimentation USB/externe empêche l'arrêt automatique de test.

La valeur donnée par `get power.temp` n'est **pas** la température de la
batterie. La coupure de charge chaude/froide est assurée matériellement par le
CN3165 et la thermistance du pack, y compris lorsque le MCU est éteint.

## Commandes nRF52 utiles

| Commande | Résultat / usage |
|---|---|
| `get pwrmgt.support` | Doit répondre `supported`. |
| `get pwrmgt.source` | `battery` ou `external`. |
| `get pwrmgt.bootreason` | Raisons du reset et du dernier arrêt enregistré. |
| `get pwrmgt.bootmv` | Tension mesurée très tôt au démarrage. |
| `get bootloader.ver` | Version du bootloader nRF52. |

Après un réveil solaire, les trois commandes les plus importantes sont :

```text
get pwrmgt.bootreason
get pwrmgt.bootmv
eventlog
```

## Destination des alertes de batterie et de redémarrage

Recovery.7 sait publier les alertes sous la forme standard
`PAYLOAD_TYPE_GRP_TXT`. Elles apparaissent alors comme de vrais messages dans
le canal MeshCore choisi, avec le préfixe du nom du nœud. La destination reste
`private` après une migration afin qu'une installation ne publie jamais sur le
canal général sans ordre explicite.

| Commande | Effet |
|---|---|
| `bat low status` | Affiche la destination active, le nombre de destinataires privés et les seuils. La PSK du canal n'est jamais affichée. |
| `bat low public` | Sélectionne volontairement le canal MeshCore standard `Public`. À éviter pour les essais répétés. |
| `bat low private` | Revient aux messages directs chiffrés vers la liste de clés publiques. |
| `bat low channel <NOM> <PSK>` | Enregistre puis sélectionne un canal de surveillance. Le nom peut contenir des espaces ; la PSK doit être la clé de canal MeshCore base64 (16/32 octets) ou son équivalent hexadécimal. |
| `bat low channel` | Resélectionne le canal personnalisé déjà enregistré. |
| `bat low <NOM>` | Raccourci de sélection si `<NOM>` correspond exactement au canal enregistré, par exemple `bat low FR SV`. |
| `bat low test` | Publie immédiatement une alerte de test vers la destination active. |

Configuration recommandée pour ne pas encombrer le canal public : créer le
même canal de surveillance sur le T1000E, exporter/copier sa PSK, puis saisir :

```text
bat low channel FR SV <PSK_BASE64_DU_CANAL>
bat low status
bat low test
```

Le nom seul ne suffit pas à identifier cryptographiquement un canal MeshCore :
le P1 doit recevoir la PSK une première fois. Elle est conservée dans sa
configuration locale, n'apparaît ni dans `status`, ni dans le journal, ni dans
les réponses ultérieures. Toute personne possédant cette PSK et ayant ajouté
le même canal peut lire les alertes. Le canal `Public`, lui, est lisible par
tous les utilisateurs qui l'ont activé.

Les messages de canal ne possèdent pas d'ACK individuel. Le firmware émet deux
fois le même paquet (même horodatage et même hash) à quatre secondes
d'intervalle : un client ayant reçu le premier élimine le doublon, tandis
qu'un client l'ayant manqué peut recevoir la seconde émission.

### Diagnostic CLI directement dans le canal

Quand la destination active est un canal personnalisé, le P1 peut déchiffrer
un petit jeu de commandes de diagnostic envoyées comme messages ordinaires
dans ce même canal. Cette passerelle est **strictement en lecture seule** :

| Message envoyé dans le canal | Réponse du répéteur |
|---|---|
| `!p1 status` | État batterie, tension et résumé GPS. |
| `!p1 battery` ou `!p1 power` | État électrique, seuils et durée critique. |
| `!p1 gps` | Mode GPS, fenêtre, override et état d'alimentation. |
| `!p1 version` | Version exacte et date de build. |
| `!p1 alerts` | Destination et seuils des alertes. |
| `!p1 help` | Liste compacte des commandes autorisées. |

Chaque réponse commence par le nom du répéteur puis `CLI OK` ou `CLI ERR`.
Tous les P1 recovery.7.4 présents sur le canal peuvent répondre à la même
requête. Ils utilisent des créneaux stables répartis entre environ 0,35 et
7,1 secondes selon leur identité, afin de réduire les collisions radio. Un
même répéteur ignore les requêtes supplémentaires reçues moins de trois
secondes après la précédente.

La réponse reprend le scope régional de la requête entrante, y compris son
absence de scope. Tant que le GPS n'a pas encore corrigé l'horloge du P1, elle
reprend aussi l'horodatage récent de la requête : le compagnon ne classe donc
plus une réponse valide plusieurs années en arrière dans l'historique. Ce
trajet a été validé matériellement avec `!p1 gps` entre le P1 et un T1000E.

Cette interface n'écoute jamais le canal `Public` et n'expose ni `set`, ni
`reboot`, ni `shutdown`, ni effacement, ni changement de GPS. La PSK prouve
seulement que l'émetteur connaît le canal ; elle n'authentifie pas une personne
précise. C'est pourquoi aucune mutation n'est acceptée par cette voie.

### Anciennes alertes privées

Les alertes sont des messages privés MeshCore chiffrés pour chaque clé publique
enregistrée. La première installation contient déjà la clé publique fournie
pour les essais. La liste accepte quatre destinataires. Une route directe connue
est réutilisée ; sinon le message privé est envoyé en flood chiffré. Chaque
émission attend un ACK et effectue une retransmission après quatre secondes.

| Commande | Effet |
|---|---|
| `alert status` ou `alert list` | Alias historique de `bat low status`, avec les clés privées abrégées. |
| `alert get <N>` | Affiche la clé publique complète du destinataire N. |
| `alert add <clé_publique_64_hex>` | Ajoute et enregistre un destinataire, sans doublon. |
| `alert remove <N>` | Retire le destinataire par son numéro. |
| `alert remove <clé_publique_64_hex>` | Retire le destinataire par sa clé complète. |
| `alert clear` | Efface uniquement la liste privée. Une destination `Public` ou canal personnalisé reste active. |
| `alert threshold` | Affiche le seuil pré-arrêt et son seuil de réarmement. |
| `alert threshold <mV> [réarmement_mV]` | Enregistre les seuils ; sans seconde valeur, ajoute 50 mV. |
| `alert test` | Alias historique de `bat low test` ; utilise la destination active. |

Valeurs sûres livrées :

```text
alert threshold 3350 3400
```

Le premier message d'arrêt part à 3,35 V alors que le nœud est encore actif.
Si la tension reste ensuite sous 3,30 V pendant dix minutes, une alerte finale
prioritaire annonce la coupure LoRa. Au redémarrage, l'alerte `P1 demarre` est
mise en file uniquement après l'initialisation de la radio, du stockage, des
capteurs et du maillage ; elle contient la tension réellement mesurée, la cause
de reset et la raison de l'arrêt précédent.

Le premier fix obtenu après chaque remise sous tension du GPS publie aussi
`P1 GPS recupere` dans la destination active, avec la durée d'acquisition et
le nombre de satellites connus. Cela couvre notamment une reprise après mise
en économie, une nouvelle fenêtre quotidienne et un redémarrage complet.

Exemple pour ajouter un second appareil :

```text
alert add <64_caracteres_hex_de_la_cle_publique>
alert list
alert test
```

## GPS saisonnier ajouté

| Commande | Persistance | Comportement |
|---|---:|---|
| `gps override status` | Lecture seule | Mode effectif, état GPS, priorité batterie et saison mémorisée. |
| `gps override on` | Oui | Mode été : GPS demandé en continu jusqu'à `off`, y compris après redémarrage. |
| `gps override off` | Oui | Mode hiver : une fenêtre GPS d'une heure toutes les 24 heures. |
| `gps override 1h` | Temporaire | Ouvre le GPS pendant une heure. |
| `gps override 24h` | Temporaire | Ouvre le GPS pendant 24 heures. |
| `gps override 96h` | Temporaire | Ouvre le GPS quatre jours pour un essai de décharge. |
| `gps override 168h` | Temporaire | Durée maximale : sept jours. |
| `gps powerguard economy` | Oui | Réglage sûr par défaut : coupe le GPS dès l'état économie (sous 3,50 V). |
| `gps powerguard critical` | Oui | Laisse le GPS fonctionner en économie et le coupe seulement sous 3,30 V. |
| `gps powerguard off` | Oui | N'arrête pas le GPS en économie/critique ; l'arrêt final SYSTEMOFF à basse tension reste obligatoire. |
| `gps powerguard status` | Lecture seule | Affiche la politique persistante active. |

Toutes les durées entières de `1h` à `168h` sont acceptées. Une durée
temporaire sélectionne le profil hiver mémorisé ; à son expiration, le cycle
quotidien reprend automatiquement. `gps override` choisit quand le GPS est
demandé ; `gps powerguard` décide si une tension basse peut suspendre cette
demande. Même en mode `off`, le GPS est coupé pendant le délai final puis le
nœud entre en SYSTEMOFF : cette commande ne désactive jamais la protection
3,30 V/600 s.

Pour forcer le GPS en continu pendant un essai de décharge malgré une batterie
faible :

```text
gps override on
gps powerguard off
gps override status
gps powerguard status
```

Pour revenir au profil hiver sûr :

```text
gps override off
gps powerguard economy
```

Autres diagnostics GPS :

| Commande | Usage |
|---|---|
| `get gps.schedule` | Fenêtre quotidienne, secondes restantes, prochaine fenêtre et override éventuel. |
| `gps` | État GPS MeshCore : actif/inactif, fix et satellites. |
| `gps sync` | Demande une synchronisation dès que le GPS fournit plusieurs secondes de date/heure valides ; répond immédiatement `ok`, même si le fix doit encore être attendu. |
| `gps setloc` | Copie la position GPS dans la position configurée du nœud. |
| `gps advert` | Lit la politique de position dans les annonces. |
| `gps advert none` | N'annonce aucune position. |
| `gps advert share` | Annonce la position GPS courante. |
| `gps advert prefs` | Annonce la latitude/longitude enregistrée. |

Préférer `gps override on/off` à `gps on/off` : les overrides sont conçus pour
la politique saisonnière recovery.7 et sont restaurés après redémarrage.

### Horloge : GPS, CLI distante et USB

Pour une horloge issue du GPS, utiliser :

```text
gps override on
gps sync
gps
clock
eventlog
```

`gps sync` ne copie pas l'heure de l'ordinateur. Il arme la synchronisation et
le fournisseur NMEA règle l'horloge après réception d'une date GPS valide. Le
journal crée alors `GPS_TIME_SYNC` avec le temps d'acquisition et le nombre de
satellites. Recovery.7 effectue également cette synchronisation automatiquement
au premier fix valide d'une fenêtre GPS.

`clock sync` a une fonction différente : il synchronise le répéteur avec
l'horodatage porté par une commande MeshCore **distante**. Depuis la console USB,
la commande ne possède aucun horodatage émetteur (`0`) et répond normalement
`ERR: clock cannot go backwards`. Elle ne consulte ni le GPS ni l'horloge du PC.
Ne pas utiliser `clock sync` pour un essai GPS.

`clock` est une lecture seule. `time <epoch>` est un réglage manuel et n'est pas
nécessaire lorsque le GPS fonctionne. L'horloge ne doit jamais être considérée
comme valide avant `GPS_TIME_SYNC` ou une synchronisation MeshCore distante
acceptée.

## Journal persistant ajouté

| Commande | Effet |
|---|---|
| `eventlog status` | État du journal, nombre d'entrées sur 64, identifiant de boot, erreurs d'écriture et confiance UTC. |
| `eventlog` | Imprime les événements dans l'ordre puis renvoie `EOF`. |
| `eventlog clear` | Efface uniquement le journal opérationnel et crée une nouvelle entrée `LOG_CLEARED`. |

Le journal est stocké sur la mémoire QSPI externe avec délais d'opération
bornés. Au premier boot recovery.7, l'ancien journal InternalFS est copié sans
être effacé. Le journal contient notamment : démarrage, état brut des deux
boutons, configuration radio réellement appliquée, fin d'initialisation, reset,
raison d'arrêt, tension au boot, transitions batterie, arrêt basse tension,
réveil du profil 3,45 V, détection GPS, début/fin de fenêtre, premier fix,
temps d'acquisition, satellites, synchronisation RTC, absence de fix et
début/fin d'override.

Avant la première heure GPS valide, les événements portent seulement l'uptime.
Après synchronisation, le journal peut estimer l'UTC des événements antérieurs
du même boot ; le symbole `~` signale une heure estimée.

## État général et statistiques

| Commande | Usage |
|---|---|
| `ver` | Version et date de compilation. |
| `board` | Nom matériel détecté. |
| `clock` | Heure UTC courante. |
| `stats-core` | Batterie, uptime, files et drapeaux d'erreur — USB local. |
| `stats-radio` | Bruit, RSSI/SNR, airtime et erreurs radio — USB local. |
| `stats-packets` | Compteurs de paquets reçus et envoyés — USB local. |
| `clear stats` | Remet les statistiques volatiles à zéro. |
| `neighbors` | Jusqu'aux huit annonces voisines les plus récentes. |
| `discover.neighbors` | Relance une découverte à zéro saut. |
| `get name` | Nom annoncé par le répéteur. |
| `get public.key` | Clé publique/identité partageable. |
| `get role` | Rôle du firmware. |

## Annonces et position

| Commande | Usage |
|---|---|
| `advert` | Envoie immédiatement une annonce flood. |
| `advert.zerohop` | Envoie une annonce limitée au voisinage direct. |
| `get flood.advert.interval` | Intervalle d'annonce flood en heures. |
| `set flood.advert.interval <3..168>` | Modifie cet intervalle. |
| `get advert.interval` | Intervalle d'annonce zéro saut en minutes. |
| `set advert.interval <60..240>` | Modifie cet intervalle. |
| `get lat` / `get lon` | Position enregistrée. |
| `set lat <degrés>` / `set lon <degrés>` | Modifie la position enregistrée. |

Le premier fix de chaque fenêtre recovery.7 programme une annonce flood et
rafraîchit l'horloge lorsque le fix fournit une date plausible.

## Vérification et réglages LoRa

Commandes de lecture recommandées :

```text
get radio
get tx
get repeat
get radio.rxgain
get dutycycle
get flood.max
get flood.max.advert
```

Commandes de modification correspondantes :

```text
set radio <freq_MHz>,<bw_kHz>,<sf>,<cr>
set tx <dBm>
set repeat on
set repeat off
set radio.rxgain on
set radio.rxgain off
set dutycycle <1..100>
set flood.max <0..64>
set flood.max.advert <0..64>
```

Sur le P1, `set tx` accepte uniquement **-9 à +22 dBm**. Une ancienne préférence
à 30 dBm est automatiquement ramenée et enregistrée à 22 dBm au premier boot.
Le SX1262 ne sait pas produire 30 dBm.

`set radio` nécessite un redémarrage. Une fréquence, un débit ou une puissance
incorrects peuvent isoler le répéteur ou enfreindre la réglementation locale.
Sauvegarder d'abord la réponse de `get radio` et `get tx`.

Pour un essai radio temporaire sans écraser la configuration :

```text
tempradio <freq>,<bw>,<sf>,<cr>,<durée_minutes>
```

## Maintenance

| Commande | Effet / prudence |
|---|---|
| `reboot` | Redémarrage immédiat, sans réponse CLI finale. |
| `shutdown` | Arrêt volontaire immédiat. Recovery.7 arme aussi le réveil solaire afin qu'un faux arrêt ne puisse plus immobiliser le nœud. |
| `poweroff` | Alias de `shutdown`. |
| `time <epoch>` | Avance l'horloge à un timestamp Unix ; elle refuse de reculer. |
| `password <nouveau>` | Change le mot de passe administrateur et l'affiche en réponse. Ne pas publier cette réponse. |
| `set name <nom>` | Change le nom annoncé. |
| `set owner.info <texte>` | Change les informations propriétaire. |

L'arrêt automatique par maintien du bouton physique est désactivé dans
recovery.7. L'entrée avait déclenché un faux arrêt `user` sur l'unité de test.
Les deux niveaux de bouton sont néanmoins enregistrés à chaque boot sous
`BUTTON_STATE` afin de qualifier le câblage avant de réactiver cette fonction.

## Commandes dangereuses ou déconseillées pendant les essais

| Commande | Risque |
|---|---|
| `erase` | **Destructif** : efface le système de fichiers et la configuration. USB local uniquement. |
| `start ota` | Lance une mise à jour OTA ; ne pas l'utiliser sans paquet testé et moyen de récupération. |
| `get prv.key` | Affiche l'identité privée sur USB. Ne jamais copier cette valeur dans un rapport ou une issue. |
| `set prv.key <hex>` | Remplace l'identité du nœud et nécessite un redémarrage. |
| `set adc.multiplier <valeur>` | Modifie la calibration batterie et donc l'interprétation des seuils ; réserver à une calibration au multimètre. |
| `clkreboot` | Réinitialise l'horloge puis redémarre. |

## Recettes d'essai

### Décharge accélérée en été

```text
gps override on
gps override status
get gps.schedule
get power.status
eventlog status
```

### Override de quatre jours, puis retour hiver automatique

```text
gps override 96h
gps override status
```

### Mise en service hiver

```text
gps override off
gps override status
get gps.schedule
```

### Diagnostic après un réveil autonome

```text
ver
get pwrmgt.bootreason
get pwrmgt.bootmv
get power.status
get gps.schedule
eventlog status
eventlog
```

Conserver la sortie complète avant d'effacer le journal.
