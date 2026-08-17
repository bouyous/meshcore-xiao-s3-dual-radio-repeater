# recovery.6-guarded — préversion expérimentale

Cette version reste une image de test. Elle corrige les deux défauts observés
sur le P1 réel après le premier cycle solaire.

## Correctifs de sûreté

- L'arrêt par maintien du bouton est désactivé par défaut : une unité avait
  enregistré `POWER_SHUTDOWN reason=user` sans action de l'utilisateur.
- Tout arrêt logiciel connu (`user`, basse tension ou protection au boot) arme
  désormais une récupération solaire, en plus du réveil USB.
- Un bouton bloqué à l'état bas ne peut plus immobiliser le chemin d'arrêt.
- Les deux lignes de bouton sont polarisées et leur niveau est enregistré à
  chaque boot sous `BUTTON_STATE`.

## Fiabilité radio et stockage

- La puissance du Wio-SX1262 est limitée à sa plage réelle, -9 à +22 dBm.
  Toute ancienne préférence à 30 dBm est réparée à 22 dBm.
- Le journal opérationnel est déplacé d'InternalFS vers la QSPI P25Q16H
  externe, dont les opérations possèdent des délais d'expiration bornés.
- L'ancien journal est migré en lecture seule lors du premier démarrage.
- Les sauvegardes ACL sont différées tant qu'un paquet radio est en attente.
- La dérive normale d'un fix GPS ne provoque plus des écritures répétées dans
  les préférences ; seule une relocalisation d'environ 100 m est persistée.
- `RADIO_READY` enregistre fréquence, puissance, SF et CR réellement appliqués.

## Fonctions conservées

- Profil de test 3,45 V en SYSTEM ON et profil terrain LPCOMP/SYSTEMOFF.
- GPS hiver une heure par jour et override été continu ou temporisé.
- Alertes privées de démarrage, seuil préventif 3,35 V et arrêt final.
- Liste persistante de quatre destinataires et journal CRC de 64 entrées.

## Validation avant flash

- 32 tests hôte ciblés réussis.
- Compilation propre du profil P1 terrain réussie.
- Compilation du profil P1 test 3,45 V réussie.
- Compilation de non-régression `Xiao_nrf52_repeater` réussie.
- Validation matérielle encore nécessaire : montage QSPI, radio RX/TX,
  livraison des alertes, GPS et cycle batterie/soleil.
