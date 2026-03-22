# OricTel - Emulateur Minitel 1B pour Oric 1/Atmos

**Version:** 0.1.0-alpha
**Date:** 2026-03-22
**Auteur:** bmarty <bmarty@mailo.com>

## Description

OricTel transforme votre Oric 1 ou Oric Atmos en terminal Minitel 1B complet,
capable de se connecter aux serveurs Minitel encore en activite sur Internet.

Le logiciel fonctionne avec l'emulateur **Phosphoric** et utilise son interface
serie ACIA 6551 (backend TCP) pour communiquer via un bridge WebSocket avec
le serveur `ws://3617.fr/ws`.

## Architecture

```
[Programme Oric]  <--ACIA 6551 @ $031C-->  [Phosphoric TCP backend]
                  <--TCP socket-->          [orictel-bridge]
                  <--WebSocket-->           ws://3617.fr/ws
```

### Composants

1. **Programme Oric** (`orictel.tap`) - Terminal Minitel en 6502 (C + ASM via cc65)
   - Decodeur protocole Videotex (Teletel/Antiope)
   - Rendu HIRES 40x25 avec mosaiques G1
   - Jeux de caracteres G0 (alphanum), G1 (mosaiques), G2 (supplementaire)
   - Mapping clavier Oric -> touches fonction Minitel

2. **Bridge WebSocket-TCP** (`orictel_bridge.py`) - Proxy Python asyncio
   - Ecoute TCP sur port 3615 (emulateur)
   - Connexion WebSocket vers 3617.fr
   - Relais bidirectionnel transparent

## Pre-requis

- **cc65** (cross-compilateur 6502) >= 2.19
- **Python 3.8+** avec module `websockets`
- **Emulateur Phosphoric** (avec support ACIA + backend TCP)
- ROMs Oric (basic11b.rom pour Atmos, basic10.rom pour Oric-1)

## Compilation

```bash
# Compiler le programme Oric
make orictel.tap

# Installer les dependances du bridge
pip3 install -r bridge/requirements.txt
```

## Utilisation

```bash
# 1. Lancer le bridge WebSocket
python3 bridge/orictel_bridge.py &

# 2. Lancer l'emulateur avec OricTel
make run

# Ou manuellement:
/home/bmarty/Oric1/phosphoric --rom basic11b.rom \
    --tape orictel.tap --fastload \
    --serial tcp:127.0.0.1:3615 --serial-v23
```

## Touches Minitel

Compatible Oric-1 (pas de touche FUNCT) et Atmos.
Methode principale: **CTRL+lettre** (fonctionne sur les deux machines).

| Fonction Minitel  | Oric-1 & Atmos    | Atmos seul  | Codes envoyes  |
|-------------------|-------------------|-------------|----------------|
| Envoi             | RETURN            |             | SEP $41        |
| Retour            | CTRL+R ou Fl.HAUT | FUNCT+R     | SEP $42        |
| Repetition        | CTRL+E            | FUNCT+E     | SEP $43        |
| Guide             | CTRL+G            | FUNCT+G     | SEP $44        |
| Annulation        | CTRL+A ou ESC     | FUNCT+A     | SEP $45        |
| Sommaire          | CTRL+S            | FUNCT+S     | SEP $46        |
| Correction        | DELETE            |             | SEP $47        |
| Suite (page suiv) | CTRL+N ou Fl.BAS  | FUNCT+N     | SEP $48        |
| Connexion/Fin     | CTRL+C            | FUNCT+C     | SEP $49        |

## Specifications techniques

- **Affichage:** Mode HIRES 240x200, 40x25 caracteres (6x8 pixels/car)
- **Serie:** ACIA 6551 mode V23 - 1200 baud RX / 75 baud TX, 7 bits, parite paire
- **Memoire:** Code $0501-$9FFF (~39 Ko), HIRES $A000-$BF3F
- **Protocole:** Videotex Teletel/Antiope (norme francaise Minitel)
- **Reference:** Emulateur JS miedit/telenet pour validite du protocole

## Licence

GPL v3 - Voir fichier LICENSE
