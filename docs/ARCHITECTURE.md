# Architecture technique - OricTel

## Vue d'ensemble

OricTel est compose de deux sous-systemes independants:

1. **Programme Oric** (6502 C/ASM) - le terminal Minitel
2. **Bridge Python** - passerelle WebSocket <-> TCP (chemin SECONDAIRE)

Le montage de reference est aujourd'hui **LOCI + PicoWiFiModemUSB** : le modem AT
ouvre lui-meme les sockets TCP vers le serveur Minitel, le bridge n'est plus dans
le chemin. Celui-ci reste utile pour les serveurs exposes uniquement en WebSocket.

## Flux de donnees

```
Serveur Minitel (ws://3617.fr/ws)
        |
        | WebSocket (frames binaires, octets Videotex bruts)
        v
+-------------------+
| orictel_bridge.py |  Python asyncio
| Port TCP 3615     |  Relais bidirectionnel transparent
+-------------------+
        |
        | TCP socket (octets bruts, pas de trame)
        v
+-------------------+
| Phosphoric        |  Emulateur Oric
| Backend TCP ACIA  |  --serial tcp:127.0.0.1:3615
| Mode V23          |  --serial-v23 (1200/75 baud)
+-------------------+
        |
        | Registres ACIA 6551 @ $0380-$0383 (base LOCI)
        | Timing cycle-accurate
        v
+-------------------+
| Programme OricTel |  6502 code (cc65)
|                   |
| serial_asm.s      |  <-- Driver ACIA bas niveau (polling, SMC de la base)
| serial.c/tx.c     |  <-- Couche fine + file d'emission non bloquante
| at_modem.c        |  <-- Commandes AT, raccrochage, veille de porteuse
| videotex.c        |  <-- Machine a etats protocole
| display.c         |  <-- Rendu HIRES (budget 1 ligne/passe)
| display_asm.s     |  <-- Blits assembleur (cellule, course, double hauteur)
| fonts.c           |  <-- Tables G0 / G1 / G2
| keyboard.c        |  <-- Scan clavier + mapping Minitel
| ui.c              |  <-- Helpers de menus (testables hote)
| main.c            |  <-- Boucle principale, menus, session
+-------------------+
```

`main.c` est un cycle *menus -> connexion -> session* : la session ne se quitte
que par ESC (confirme sur la ligne 0, sans effacer la page), qui raccroche
(`at_hangup`) et repart au menu Mode de connexion avec `vtx_init`. Les ecrans
d'echec de connexion et de perte de porteuse rendent 2 sur ESC pour le meme
retour. ESC sur le menu principal sort d'OricTel par `jmp ($FFFC)` (vecteur
de reset, independant de la ROM 1.0/1.1) : la zone programme BASIC ayant ete
ecrasee au chargement, seul un demarrage a froid rend un "Ready" propre.

### Montage de reference (materiel)

```
Oric --> ACIA 6551 $0380 (cartouche LOCI)
             |
             | firmware RP2040 : anneau RX de 32 OCTETS devant le registre
             |                   (src/mia/oric/acia.c, ACIA_RX_BUFFER_SIZE)
             v
        USB-CDC --> PicoWiFiModemUSB --> WiFi --> serveur Minitel (TCP)
```

L'anneau de 32 octets est un element de conception IMPORTANT : le 6551 nu ne
tamponne qu'un octet, et OricTel n'utilise aucune IRQ. C'est ce tampon qui donne
au logiciel une tolerance d'environ 250 000 cycles (30 octets a 1200 bauds) entre
deux lectures. Emuler ce montage SANS `--serial-buffer` est donc plus pessimiste
que le materiel reel.

## Carte memoire Oric

```
$0000-$00FB  Zero Page (cc65: $00E2-$00FB)
$0100-$01FF  Pile 6502
$0200-$02FF  Variables systeme
$0300-$030F  VIA 6522 (miroir $0300-$03FF)
$0380-$0383  ACIA 6551 (serie, base LOCI)
$0400-$0500  Zone systeme Oric
$0501-$97FF  CODE + DATA + BSS OricTel (~37 Ko ; BSS borne a $9800 par le cfg)
$9800-$9BFF  Jeu de caracteres standard des lignes texte (copie de font_g0)
$9C00-$9FFF  Pile cc65 (1 Ko ; releve < 32 octets en session, test_menus
             verifie qu'elle reste au-dessus de $9E00). C'est la place du jeu
             de caracteres ALTERNATIF, jamais selectionne.
$A000-$BF3F  Framebuffer HIRES (8000 octets)
$BF68-$BFDF  3 lignes texte sous le HIRES = barre de statut ($BFDF = octet
             de bascule HIRES pose par la ROM, jamais ecrit)
$C000-$FFFF  ROM (16 Ko)
```

## Registres ACIA 6551 (cote Oric)

| Adresse | Lecture          | Ecriture          |
|---------|------------------|-------------------|
| $0380   | Donnee recue     | Donnee a envoyer  |
| $0381   | Registre statut  | Reset programme   |
| $0382   | Registre commande| Registre commande |
| $0383   | Registre controle| Registre controle |

### Configuration effective (base LOCI $0380)
- Controle: **$18** = 1200 bauds, **8N1**, horloge interne
- Commande: **$0B** = DTR, IRQ RX desactivee, TIC=10 (RTS bas SANS IRQ TX)

> Piege datasheet 6551 : TIC=01 (valeurs $05/$07) ACTIVE l'IRQ TX. TDRE etant
> leve en permanence, l'ACIA reassertait /IRQ sans fin et la machine gelait
> (regressions v0.3.3/v0.3.4). $0B est la bonne valeur.

L'ancienne configuration V23 7E1 ($28/$69) ne correspond plus a aucun montage
supporte : le PicoWiFiModemUSB dialogue en 8N1.

### Bits du registre Status
- Bit 3 (RDRF=$08): Donnee recue disponible
- Bit 4 (TDRE=$10): Transmetteur pret
- Bit 5 (=$20): **/DCD, logique INVERSEE** — non nul = PAS de porteuse. Sur LOCI
  ce bit ne suit que le montage USB et DTR, jamais l'etat de l'appel : il est
  donc inutilisable pour detecter une perte de porteuse (voir `serial.h`). La
  detection passe par la reponse `NO CARRIER` du modem (`at_carrier_watch`),
  confirmee par 4 s de silence (`CARRIER_CONFIRM_MS`).
- Bit 6 (=$40): **/DSR**. Le firmware LOCI (`acia_task`) le tient haut tant
  qu'aucun peripherique USB-CDC modem n'est monte : `serial_modem_absent()`
  -> ecran « MODEM USB NON DETECTE » (non bloquant).

### Sonde de presence du 6551 (`serial_probe`, v0.3.20)

`$0380-$0383` n'est jamais vide sur un Oric : le VIA 6522 (`$0300`) est decode
sur toute la page `$03xx`, donc sans 6551 on lit/ecrit son **miroir** (ORB, ORA,
DDRB, DDRA). `serial_init` y ecrivait `$18` (DDRA) et `$0B` (DDRB) : clavier
mort, OricTel fige sur son menu (cas : Oric sans LOCI, LOCI hors contexte
disque, Phosphoric `--loci-emu` sans `--loci-cdc`). Avant de programmer,
`main.c` appelle `serial_probe(base)` : ecriture de `$55` puis `$AA` en `+1` et
relecture. Le STATUS d'un 6551 est en lecture seule (ecriture = reset programme,
valeur non retenue) ; l'ORA du VIA relit son latch. Les deux valeurs relues
identiques => pas de 6551 => ecran « PAS D'INTERFACE SERIE » (`1` resonde, ESC ->
BASIC). ORA est restaure, les DDR ne sont jamais ecrits, le tout sous `SEI`.
Exiger les deux relectures neutralise une lecture perdue (open-bus) sur LOCI.
Test : `make test-serial-probe` (faux bus hote) et `test_menus.sh` (`--loci`
sans `--serial` : ecran, puis ESC -> `Ready`, preuve que le clavier survit).

### Barre de statut (3 lignes texte)

En HIRES, l'Oric affiche encore 3 lignes texte sous les 200 lignes graphiques
(`$BF68-$BFDF`). Elles etaient cachees (encre noire) parce que leur jeu de
caracteres, que la ROM place en `$9800-$9FFF`, etait ecrase par la pile C.
La pile est ramenee a 1 Ko en `$9C00-$9FFF` (zone du jeu alternatif, jamais
selectionne) et `display_init` copie `font_g0` en `$9900-$9BFF` : la bascule
HIRES de la ROM (`$EC33` / `$F8E3`) ne regenere pas les glyphes, elle
remplit la zone de `$40`. Contenu (`main.c`, `status_bar_*`) :

- ligne 0 (cyan) : indicateur `C`/`F` en inverse, serveur, chrono `mm:ss`
  de session (tics de 10 ms du Timer 2), mode de rendu (`AUTO`/`TRAME`/
  `BRUT`, suit CTRL+D), rappel `ESC` ;
- ligne 1 (jaune) : message transitoire (`display_status` : question ESC,
  `ACIA reset`) ;
- ligne 2 (blanc) : aide des touches Minitel.

La colonne 0 de chaque ligne porte l'attribut d'encre ; 39 colonnes de texte,
38 pour la ligne 2. L'indicateur de liaison quitte ainsi la ligne 0 de la
page Videotex, qui appartient au serveur. Cout : 40 octets reecrits par
seconde, aucun impact sur le rendu HIRES (`make bench-render` inchange).

### Base de temps : Timer 2 du VIA 6522

La boucle de session n'a pas d'ISR et son iteration n'a pas de duree fixe
(~8,5 ms a vide, bien plus pendant un rendu). Les delais (confirmation de
perte de porteuse, retour de l'indicateur a `F`) sont donc comptes sur **T2**
(`$0308/$0309`), lu a chaque iteration : la ROM ne s'en sert que pour la
cassette, il decompte en continu a 1 MHz une fois arme, et on accumule les
cycles ecoules (soustraction modulo 65536) en tics de 10 ms. OricTel l'arme
lui-meme (`via_tick_reset`) car un 6522 emule le laisse fige tant que T2C-H
n'a pas ete ecrit, et interdit son IRQ (`IER = $20`) : le handler ROM
n'acquitte que T1, un drapeau T2 actif serait une tempete d'IRQ. T1 n'est
pas utilisable : lire son octet bas acquitterait l'IRQ 100 Hz a la place
de la ROM, et n'observer que l'octet haut rate les rechargements des qu'une
iteration depasse 10 ms (mesure : 6,1 s pour 4 s demandees). Verifie par
`make test-carrier` : 4,0 s +/- 0,1 sur ROM 1.0 et 1.1.

## Protocole Videotex - Machine a etats

```
NORMAL ──ESC($1B)──> ESC_SEQ
       ──US($1F)───> US_ROW
       ──SO($0E)───> (basculer G1)
       ──SI($0F)───> (basculer G0)
       ──$20-$7F──> (afficher caractere)
       ──$08-$0B──> (deplacement curseur)
       ──$0C─────> (effacer ecran)
       ──$0D─────> (retour chariot)

ESC_SEQ ──$40-$47──> (couleur encre) -> NORMAL
        ──$50-$57──> (couleur fond) -> NORMAL
        ──$48/$49──> (flash on/off) -> NORMAL
        ──$4C-$4F──> (taille) -> NORMAL
        ──$5B─────> CSI
        ──$39─────> PRO

US_ROW ──$40-$57──> US_COL (memorise ligne)

US_COL ──$41-$68──> NORMAL (positionne curseur)

CSI ──params+lettre──> NORMAL (commande ANSI-like)
```

## Affichage HIRES

- Resolution: 240x200 pixels = 40 colonnes x 25 lignes
- Chaque cellule: 6x8 pixels
- 3 jeux de caracteres:
  - G0: alphanumerique (ASCII Minitel, accents francais)
  - G1: mosaiques semi-graphiques (2x3 blocs = 64 motifs)
  - G2: caracteres supplementaires (diacritiques)

### Attributs par cellule
- Couleur encre (0-7)
- Couleur fond (0-7)
- Clignotement (flash)
- Inversion video
- Soulignement / separation mosaique
- Taille (normal, double hauteur, double largeur, double taille)
- Masquage (concealed)

## Rendu optimise

Chaque ligne modifiee est marquee "dirty" avec la plage de colonnes touchee.
Le rendu n'est PAS cadence sur la trame video : il est **budgete a UNE ligne par
passe** depuis la boucle principale, qui rappelle `display_render()` tant qu'il
reste des lignes sales *et* qu'aucun octet n'attend en reception.

Ce budget est un compromis mesure, pas arbitraire. Pendant une passe de rendu la
reception n'est pas relue ; si la passe depasse la tolerance du tampon RX, des
octets sont perdus et la page arrive mutilee.

### Couts mesures (`make bench-render`, cycles 6502 reels)

| region | cout | en temps-octet @1200 bauds |
|---|---|---|
| passe `display_render()` (1 ligne) | ~151 000 | 18,8 |
| ligne hybride realiste | ~79 000 | 10,1 |
| ligne double hauteur | ~134 000 | 16,7 |
| mise a jour d'une cellule | ~30 000 | 3,6 |
| `vtx_process()` (40 caracteres) | ~105 000 | 13,2 |

Budget d'un octet a 1200 bauds : **8 333 cycles**. Tolerance du montage reel :
~250 000 cycles (anneau de 32 octets du firmware LOCI).

Le banc dumpe aussi l'empreinte SHA-256 du framebuffer apres une page canonique :
toute optimisation qui se veut sans effet visuel doit la laisser INCHANGEE.

### Chemins rapides assembleur (`display_asm.s`)

| routine | role |
|---|---|
| `blit_cell8` | une cellule taille normale (~190 cy contre ~2 000 en C) |
| `blit_run` | course de cellules eligibles ; s'arrete sur toute taille non normale |
| `blit_cell4x2` | double hauteur : 4 lignes source etirees sur 8 lignes pixel |
| `display_clear` | remplissage du framebuffer par pages (~5 ms) |

### Piege cc65 a connaitre

`ctx->screen[row][col]` fait generer une multiplication 16 bits par 240 A CHAQUE
acces, et un pointeur *parametre* est recharge depuis la pile logicielle par un
`jsr ldptr1ysp` (~50 cycles) a chaque dereferencement. Les boucles chaudes
hissent donc un pointeur de ligne hors de la boucle, et `videotex.c` passe par un
pointeur de contexte de portee fichier plus des tables d'offsets. Ces deux
transformations ont donne 4,0x sur le rendu et 1,50x sur le decodeur.
