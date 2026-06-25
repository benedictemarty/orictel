# OricTel - Manuel d'utilisation

**Version du logiciel :** 0.2.32 - **Licence :** EUPL 1.2

Ce manuel decrit l'utilisation d'OricTel, le terminal Minitel 1B pour
Oric 1/Atmos, du lancement jusqu'a la navigation sur les serveurs
Minitel. Pour l'architecture technique, voir `ARCHITECTURE.md`.

---

## 1. Demarrage rapide

```bash
make run
```

Cette commande compile si necessaire puis lance l'emulateur Phosphoric
avec OricTel en mode **modem AT**. Le programme se charge et se lance
automatiquement (fast-load + RUN). Ensuite :

1. **Ecran d'accueil** (jingle) - appuyez sur une touche (ou attendez 5 s).
2. **Mode de connexion** - tapez `1` (Modem AT, recommande), `2` (Direct)
   ou `3` (Config WiFi, voir section 2bis pour le materiel PicoWiFiModemUSB).
3. **Serveur** - tapez `1` (PAVI 3617), `2` (MiniPavi) ou `3` (saisie libre
   d'un `hote:port`, validee par RETURN).
4. La sequence ATZ/ATD s'execute (~2 s) et la page d'accueil du serveur
   s'affiche.

Astuce affichage : la fenetre Phosphoric peut etre agrandie avec **F3**
(cycle x1-x4) ou `--scale 3` en ligne de commande. A l'echelle 1, les
cartouches en video inversee (ENVOI, SOMMAIRE...) sont peu lisibles -
c'est une limite de taille de pixel, pas un defaut de rendu.

## 2. Modes de connexion

### Mode 1 - Modem AT (recommande)

OricTel pilote le modem emule par Phosphoric (`--serial modem`) avec des
commandes Hayes : `ATZ` (reset) puis `ATD hote:port` (numerotation =
connexion TCP). Le serveur est choisi dans le menu d'OricTel, on peut
donc changer de serveur sans relancer l'emulateur.

### Mode 2 - Direct (TCP/V23)

La liaison est deja etablie par l'emulateur (`make run-direct`, serveur
fixe defini dans le Makefile via `MINITEL_SERVER`). Choisir ce mode si
Phosphoric a ete lance avec `--serial tcp:...` ou `digitelec:...`.

### Mode WebSocket (via bridge)

`make run-ws` lance le bridge Python (`orictel_bridge.py`) qui relaie
TCP (port 3615) vers le serveur WebSocket `ws://3617.fr/ws`, puis
l'emulateur en mode TCP. Dans OricTel, choisir alors le mode Direct.

## 2bis. Config WiFi (materiel PicoWiFiModemUSB)

Sur un montage reel **LOCI + PicoWiFiModemUSB**, le modem doit etre
associe a un reseau WiFi (avec une adresse IP) avant de pouvoir composer.
Sinon `ATD` echoue immediatement par `NO CARRIER (00:00:00)` (statut
`no ip`). Le menu `3 - Config WiFi` realise toute la configuration depuis
l'Oric :

1. **Scan** : OricTel envoie `AT$SCAN` ; les reseaux 2,4 GHz a portee
   s'affichent, numerotes. Un `*` rouge signale un reseau securise.
2. **Selection** : tapez le **chiffre** du reseau. **REPETITION** relance
   le scan, **ANNULATION** revient au menu.
3. **Mot de passe** : pour un reseau securise, saisissez le mot de passe
   (masque par des `*`), **ENVOI** valide, **CORRECTION** efface.
4. **Connexion** : OricTel envoie `AT$SSID=` / `AT$PASS=` / `ATC1`, puis
   attend l'IP DHCP. En cas de succes, `AT&W` sauve la config en NVRAM du
   Pico (« Connecte! Config sauvee. ») - elle sera rechargee aux demarrages
   suivants. Sinon « Echec IP. Verifier mot de passe. ».

Une fois le WiFi configure, revenez au menu et choisissez `1 - Modem AT`
pour vous connecter normalement. Note : apres un `ATZ`, OricTel patiente
desormais jusqu'a l'obtention de l'IP avant de composer, ce qui evite le
`NO CARRIER` du a un DHCP encore en cours.

## 3. L'ecran

- **Lignes 1-24** : la page Videotex du serveur (40 colonnes).
- **Ligne 0** (statut) : en haut a droite, l'indicateur de liaison -
  `C` inverse = donnees recues recemment (connecte), `F` = pas de
  donnees depuis ~30 s (liaison probablement coupee).
- **Curseur** : barre clignotante sous la cellule courante, lorsque le
  serveur l'active (zones de saisie).

## 4. Le clavier

OricTel fonctionne sur Oric-1 et Atmos. Les touches de fonction
Minitel passent par **CTRL+lettre** (les deux machines) ou
**FUNCT+lettre** (Atmos uniquement, FUNCT puis la lettre).

| Touche Minitel | A quoi ca sert | Oric |
|---|---|---|
| **ENVOI** | valider une saisie | RETURN |
| **SOMMAIRE** | revenir au sommaire du service | CTRL+S |
| **RETOUR** | page precedente | CTRL+R ou fleche HAUT |
| **SUITE** | page suivante | CTRL+N |
| **REPETITION** | reafficher la page | CTRL+E |
| **GUIDE** | aide du service | CTRL+G |
| **ANNULATION** | effacer la saisie en cours | CTRL+A ou ESC |
| **CORRECTION** | effacer le dernier caractere | DELETE |
| **CONNEXION/FIN** | se deconnecter du service | CTRL+C |

Les caracteres tapes sont envoyes au serveur, qui les echoie a l'ecran
(fonctionnement Minitel standard : pas d'echo local par defaut). Le
Minitel 1B affiche en MAJUSCULES tant que le serveur n'active pas le
mode minuscules.

Les **fleches gauche/droite** envoient les sequences curseur
(ESC[D / ESC[C) uniquement si le serveur a active le mode curseur
(PRO3), comme sur un vrai Minitel 1B.

### Raccourcis locaux (n'envoient rien au serveur)

| Touche | Effet |
|---|---|
| **CTRL+D** | change le mode de rendu (voir section 5) |
| **CTRL+L** | efface l'ecran localement |
| **CTRL+F** | reinitialise la liaison serie (ACIA) |

## 5. Les modes de rendu (CTRL+D)

L'Oric ne peut pas colorer chaque cellule individuellement comme le
Minitel : OricTel propose trois strategies, commutables a chaud :

1. **AUTO** (defaut) - le meilleur compromis, decide ligne par ligne :
   couleurs par attributs serial quand la ligne s'y prete (texte
   colore, mosaiques en couleur solide), trame de luminance pour les
   zones multicolores denses, rendu blanc pour les lignes inadaptees
   (double hauteur, pas de delimiteur).
2. **DITHERING** - tout en trames de densite : la hierarchie de
   luminosite des couleurs est preservee en "niveaux de gris" textures.
   Utile si une page joue mal avec l'heuristique AUTO.
3. **BRUT** - tout blanc sur noir : lisibilite maximale des formes,
   aucune couleur.

## 6. Serveurs testes

| Serveur | Adresse | Remarques |
|---|---|---|
| PAVI 3617 | `pavi.3617.fr:3617` | recommande, page d'accueil Teletel |
| MiniPavi | `go.minipavi.fr:516` | passerelle multi-services |
| Autre | saisie libre `hote:port` | option 3 du menu serveur |

Sur la page d'accueil PAVI : tapez un code de service puis **ENVOI**
(RETURN). **SOMMAIRE** (CTRL+S) liste les services. **CONNEXION/FIN**
(CTRL+C) termine la session.

## 7. Depannage

| Symptome | Cause probable | Remede |
|---|---|---|
| « PAS DE MODEM » / retour direct apres ATZ | l'emulateur n'est pas en `--serial modem` | utiliser `make run`, ou choisir le mode Direct |
| `NO CARRIER (00:00:00)` (Pico reel) | WiFi non associe / pas d'IP (`no ip`, echec en 0 s) | menu `3 - Config WiFi` : scanner, choisir le reseau, saisir le mot de passe (config sauvee en NVRAM) |
| Indicateur `F` permanent | pas de donnees du serveur | verifier la connexion Internet ; CTRL+F puis CTRL+E (repetition) |
| Caracteres perdus a la frappe | n'arrive plus depuis 0.2.24 | verifier que le tap est a jour (`make`) |
| Cartouches inverses illisibles | echelle d'affichage 1x | F3 (echelle x2-x4) |
| Page figee en cours de chargement | liaison interrompue | CTRL+F (reset ACIA) puis CTRL+E |
| L'ecran reste sur le BASIC `Ready` | fast-load sans autorun | taper `RUN` puis RETURN |

## 8. Limites connues

- Les couleurs par cellule du Minitel sont approximees par les
  attributs serial de l'Oric : sur une ligne sans cellule vide
  utilisable, la couleur d'un texte peut etre perdue (rendu blanc).
- Le mode MIXED (tele-informatique 80 colonnes) n'est pas supporte.
- L'identification terminal (ENQ/ENQROM) est volontairement muette :
  les serveurs modernes (MiniPavi) echoient la reponse dans le champ
  de saisie au lieu de la consommer (meme comportement que miedit).
- La configuration V23 reelle (1200/75 bauds, 7E1) pour vrai materiel
  Oric + modem est en ROADMAP ; sous emulateur le transfert est
  instantane.
