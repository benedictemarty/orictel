# OricTel - Emulateur Minitel 1B pour Oric 1/Atmos

**Version:** 0.2.1
**Date:** 2026-03-24
**Auteur:** bmarty <bmarty@mailo.com>

## Description

OricTel transforme votre Oric 1 ou Oric Atmos en terminal Minitel 1B complet,
capable de se connecter aux serveurs Minitel encore en activite sur Internet.

Le logiciel supporte deux modes de connexion :

1. **Backend Digitelec** (recommande) : connexion TCP directe via l'emulateur
   Phosphoric, sans bridge intermediaire.
2. **Backend TCP + bridge WebSocket** : relais Python asyncio entre l'emulateur
   et un serveur Minitel WebSocket.

## Architecture

```
Serveur Minitel (ex: pavi.3617.fr:3617)
         |
         | TCP direct (Digitelec)     ou     WebSocket (ws://3617.fr/ws)
         |                                        |
         v                               [orictel_bridge.py]
[Phosphoric / Oricutron]                         |
  ACIA 6551 @ $031C                      TCP :3615
         |                                        |
         +----------------------------------------+
         |
         v
  [Programme OricTel sur Oric]
    +-- serial_asm.s   (driver ACIA 6551, polling)
    +-- videotex.c     (decodeur protocole Videotex)
    +-- display.c      (rendu HIRES 240x200)
    +-- keyboard.c     (clavier + mapping Minitel)
    +-- fonts.c        (G0, G1, G2)
    +-- main.c         (boucle principale, menus, modem AT)
```

### Composants

1. **Programme Oric** (`orictel.tap`) - Terminal Minitel en 6502 (C + ASM via cc65)
   - Decodeur protocole Videotex complet (Teletel/Antiope)
   - Machine a etats : ESC, CSI, US, SS2, REP, PRO
   - Rendu HIRES 40x25 avec optimisation dirty-rectangle
   - Jeux de caracteres G0 (alphanum + accents), G1 (mosaiques 2x3), G2 (supplementaire)
   - 3 modes de rendu commutables (CTRL+D) : hybride, dithering, brut
   - Attributs complets : couleurs (encre/fond), flash, inversion, soulignement, masque, separe
   - Double hauteur, double largeur, double taille
   - 12 combinaisons d'accents via SS2
   - Identification terminal ENQ (Minitel 1B)
   - Splash screen avec jingle AY-3-8912 style GameCube
   - Menu de selection de serveur (predefinis + saisie libre)
   - Support modem AT (compatibilite Oricutron)
   - Mode rouleau, curseur visible clignotant, beep PSG

2. **Bridge WebSocket-TCP** (`orictel_bridge.py`) - Proxy Python asyncio
   - Ecoute TCP sur port 3615 (emulateur)
   - Connexion WebSocket vers 3617.fr
   - Relais bidirectionnel transparent
   - Statistiques de transfert et mode verbose

## Pre-requis

- **cc65** (cross-compilateur 6502) >= 2.19
- **Python 3.8+** avec module `websockets` >= 12.0
- **Emulateur Phosphoric** (avec support ACIA + backend Digitelec ou TCP)
  - Ou **Oricutron** (avec support modem AT)
- ROMs Oric (basic11b.rom pour Atmos, basic10.rom pour Oric-1)

## Compilation

```bash
# Compiler le programme Oric
make

# Installer les dependances du bridge
pip3 install -r bridge/requirements.txt
```

## Utilisation

### Mode modem AT (recommande, serveur choisi dans le menu)

```bash
make run
# Equivalent a:
# oric1-emu --rom basic11b.rom --tape orictel.tap -f \
#     --serial modem --serial-buffer 4096
```

### Mode TCP direct V23 (serveur fixe, sans bridge)

```bash
make run-direct
# Equivalent a:
# oric1-emu --rom basic11b.rom --tape orictel.tap -f \
#     --serial tcp:pavi.3617.fr:3617 --serial-v23 --serial-buffer 4096
```

### Mode WebSocket (avec bridge)

```bash
# 1. Lancer le bridge
make bridge
# Ou: python3 bridge/orictel_bridge.py [--tcp-port 3615] [--ws-url ws://3617.fr/ws] [-v]

# 2. Lancer l'emulateur
make run-ws
```

### Serveurs Minitel disponibles

Le menu de selection integre propose plusieurs serveurs :
- `pavi.3617.fr:3617` - PAVI 3617 (recommande)
- `go.minipavi.fr:516` - MiniPavi
- Saisie libre pour tout autre serveur (hostname:port)

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
| Mode rendu        | CTRL+D            |             | (local)        |
| Effacer ecran     | CTRL+L            |             | (local)        |
| Reset ACIA        | CTRL+F            |             | (local)        |

## Specifications techniques

- **Affichage:** Mode HIRES 240x200, 40x25 caracteres (6x8 pixels/car)
- **Serie:** ACIA 6551 mode V23 - 1200 baud RX / 75 baud TX, 7 bits, parite paire
- **Memoire:** Code $0501-$97FF (~37 Ko), BSS $9800-$9FFF, HIRES $A000-$BF3F
- **Protocole:** Videotex Teletel/Antiope (norme francaise Minitel)
- **Rendu:** 3 modes (hybride G0+G1 dithering, tout dithering, brut)
- **Optimisation:** Dirty-rectangle (seules les lignes modifiees sont re-rendues)
- **Reference:** Emulateur JS miedit/telenet pour validite du protocole

## Documentation

- `docs/ARCHITECTURE.md` - Architecture technique detaillee, carte memoire, registres ACIA
- `docs/AGILE_PLAN.md` - Plan agile et suivi des sprints
- `ROADMAP` - Vision et planification des versions
- `CHANGELOG` - Historique des modifications
- `VERSION_TRACKING` - Suivi des versions par composant
- `CIRRUS_OS` - Statut de build et plateforme cible

## Licence

GPL v3 - Voir fichier LICENSE
