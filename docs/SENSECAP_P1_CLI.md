# Guide CLI — SenseCAP Solar Node P1 recovery.4

Ce guide couvre les deux images recovery.4 :

- `v1.17.0-p1-recovery.4-gpsoverride` : profil terrain SYSTEMOFF/LPCOMP ;
- `v1.17.0-p1-recovery.4-test345-gpsoverride` : profil de test avec réveil
  simulé à 3,45 V en SYSTEM ON basse consommation.

Les exemples supposent une console USB série. Les commandes ajoutées
`gps override ...` et `eventlog ...` sont volontairement limitées à cette
console locale. Envoyer une commande par ligne.

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
eventlog status
```

`ver` doit contenir `recovery.4`. Le profil test indique un seuil de réveil
ADC de 3450 mV ; le profil terrain indique `3/8 VDD x 3.004` et nécessite une
mesure réelle de son seuil LPCOMP.

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

Politique recovery.4 :

- passage en économie sous 3,50 V ;
- retour normal à partir de 3,55 V ;
- état critique sous 3,30 V ;
- annulation du compteur critique à partir de 3,35 V ;
- arrêt après 600 secondes critiques continues ;
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

Toutes les durées entières de `1h` à `168h` sont acceptées. Une durée
temporaire sélectionne le profil hiver mémorisé ; à son expiration, le cycle
quotidien reprend automatiquement. Une protection batterie `economy` ou
`critical` coupe toujours le GPS, même avec `override on`.

Autres diagnostics GPS :

| Commande | Usage |
|---|---|
| `get gps.schedule` | Fenêtre quotidienne, secondes restantes, prochaine fenêtre et override éventuel. |
| `gps` | État GPS MeshCore : actif/inactif, fix et satellites. |
| `gps sync` | Synchronise l'horloge depuis un fix GPS valide. |
| `gps setloc` | Copie la position GPS dans la position configurée du nœud. |
| `gps advert` | Lit la politique de position dans les annonces. |
| `gps advert none` | N'annonce aucune position. |
| `gps advert share` | Annonce la position GPS courante. |
| `gps advert prefs` | Annonce la latitude/longitude enregistrée. |

Préférer `gps override on/off` à `gps on/off` : les overrides sont conçus pour
la politique saisonnière recovery.4 et sont restaurés après redémarrage.

## Journal persistant ajouté

| Commande | Effet |
|---|---|
| `eventlog status` | État du journal, nombre d'entrées sur 64, identifiant de boot, erreurs d'écriture et confiance UTC. |
| `eventlog` | Imprime les événements dans l'ordre puis renvoie `EOF`. |
| `eventlog clear` | Efface uniquement le journal opérationnel et crée une nouvelle entrée `LOG_CLEARED`. |

Le journal contient notamment : démarrage, fin d'initialisation, reset,
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

Le premier fix de chaque fenêtre recovery.4 programme une annonce flood et
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
| `shutdown` | Arrêt volontaire immédiat. Le mode de réveil volontaire diffère de l'arrêt automatique basse tension. |
| `poweroff` | Alias de `shutdown`. |
| `time <epoch>` | Avance l'horloge à un timestamp Unix ; elle refuse de reculer. |
| `password <nouveau>` | Change le mot de passe administrateur et l'affiche en réponse. Ne pas publier cette réponse. |
| `set name <nom>` | Change le nom annoncé. |
| `set owner.info <texte>` | Change les informations propriétaire. |

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
