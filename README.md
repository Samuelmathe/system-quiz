# DMX Quiz Project (Arduino + Python)

Ce dossier contient :
- des **sketches Arduino** (`.ino`)
- 2 **logiciels Python** (interfaces GUI) pour piloter/configurer le système via USB (port série)

## Contenu

- **Arduino**
  - `mega30Equipe.ino` : sketch pour Arduino MEGA (DMX + radio NRF24L01)
  - `nano_equipe_corrige.ino` : sketch pour Arduino NANO (équipe)
  - `nano_son_final.ino` : sketch pour Arduino NANO (son)
  - `nano_animateur_final.ino` : sketch pour Arduino NANO (animateur)

- **Python**
  - `configuration/config_final_30.py` : outil de configuration (équipes / DMX / sync MEGA)
  - `interface/interface_30eq.py` : interface “quiz board” (scores/buzz + série)

## Prérequis

- Python 3.9+ recommandé
- Arduino IDE (ou PlatformIO) pour compiler/téléverser les `.ino`

## Installation Python

Depuis la racine du projet :

```bash
python3 -m pip install --user -r requirements.txt
```

## Lancer les logiciels Python

Depuis la racine du projet :

```bash
python3 configuration/config_final_30.py
python3 interface/interface_30eq.py
```

## Fichiers créés automatiquement (configs)

- `configuration/config_final_30.py` lit/écrit : `configuration/config_quiz_pro.json`
- `interface/interface_30eq.py` lit/écrit : `interface/quiz_board_config.json`

Ces fichiers JSON sont créés automatiquement **à côté de chaque script**, ce qui rend le projet plus simple à partager et lancer depuis n'importe quel dossier.

## Sons (interface)

`interface/interface_30eq.py` essaie de charger :
- `sounds/buzz.mp3`
- `sounds/victoire.mp3`
- `sounds/echec.mp3`

Le dossier `sounds/` doit être **à côté du script** (ou à côté de l’exécutable si tu as “packagé” l’app).
Exemple attendu :

```
interface/
  interface_30eq.py
  sounds/
    buzz.mp3
    victoire.mp3
    echec.mp3
```

Si `pygame` n’est pas dispo (ou si les fichiers sont absents), l’interface fonctionne, mais sans sons.

## Linux : accès au port série Arduino

Si tu as une erreur de permission sur `/dev/ttyACM0` ou `/dev/ttyUSB0` :

```bash
sudo usermod -aG dialout $USER
```

Puis **déconnexion/reconnexion** (ou redémarrage) pour que le groupe soit pris en compte.

