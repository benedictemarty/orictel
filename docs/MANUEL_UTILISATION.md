# OricTel - Manuel d'utilisation

**Version du logiciel :** 0.3.20 - **Licence :** EUPL 1.2

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
2. **Interface serie** - ecran de rappel du seul montage materiel possible
   (`LOCI + PicoWiFiModemUSB`, ACIA 6551 a `$0380`). Aucun choix : une touche
   pour continuer. OricTel **sonde** alors la presence du 6551 avant de le
   programmer : sans ACIA en `$0380` (LOCI absent, ou emulateur sans backend
   serie), l'ecran **« PAS D'INTERFACE SERIE »** s'affiche (`1` resonde, ESC
   quitte vers le BASIC) au lieu de geler le clavier. Si l'ACIA repond mais
   qu'aucun modem USB n'est monte sur le LOCI (`/DSR` haut), l'avertissement
   **« MODEM USB NON DETECTE »** s'affiche ; une touche continue.
3. **Mode de connexion** - tapez `1` (Modem AT, recommande) ou `2` (Config
   WiFi, voir section 2bis pour le materiel PicoWiFiModemUSB). Le PicoWiFi
   etant un modem Hayes, la connexion passe toujours par AT : il n'y a plus
   de mode « Direct ».
4. **Serveur** - tapez `1` (PAVI 3617), `2` (MiniPavi) ou `3` (saisie libre
   d'un `hote:port`, validee par RETURN).
5. La sequence ATZ/ATD s'execute (~2 s) et la page d'accueil du serveur
   s'affiche.

Astuce affichage : la fenetre Phosphoric peut etre agrandie avec **F3**
(cycle x1-x4) ou `--scale 3` en ligne de commande. A l'echelle 1, les
cartouches en video inversee (ENVOI, SOMMAIRE...) sont peu lisibles -
c'est une limite de taille de pixel, pas un defaut de rendu.

## 2. Modes de connexion

### Mode 1 - Modem AT (seule methode de connexion)

OricTel pilote un modem Hayes via l'ACIA 6551 a `$0380` (PicoWiFiModemUSB,
ou son emulation Phosphoric `--serial modem`/`picowifi`) : `ATZ` (reset)
puis `ATDT hote:port` (numerotation = connexion TCP). Le serveur est choisi
dans le menu d'OricTel, on peut donc changer de serveur sans relancer
l'emulateur. C'est l'unique mode de connexion : l'ancien mode « Direct »
(ligne V23 brute sans AT) a ete retire, le montage cible ne sachant pas
ouvrir une ligne directe.

**Modem reste en ligne.** Si le modem est encore en communication d'une session
precedente (Oric resette, OricTel relance ou emulateur ferme sans raccrocher), il
transmet les donnees du serveur et n'interprete plus les commandes : le `ATZ` se
perd dans le flux. OricTel le detecte (pas de « OK »), affiche brievement
« Modem en ligne: ATH... », raccroche par la sequence d'echappement Hayes
(`+++` puis `ATH`) et rejoue `ATZ` avant de composer. Aucun geste manuel n'est
requis ; ce detour n'a lieu que si le premier `ATZ` echoue.

**Si la connexion echoue quand meme.** Sans « CONNECT », la ligne ne porte aucun
flux Videotex exploitable : soit rien du tout (modem absent ou muet), soit — si
le raccrochage n'a pas suffi — un flux commence EN COURS DE PAGE, qui s'affiche
en bouillie. OricTel n'entre donc plus en session en silence, il vous laisse
choisir :

```
        ECHEC DE CONNEXION
Pas de CONNECT: la ligne ne porte
aucun flux Videotex exploitable.

        1 Reessayer
        2 Choisir un autre serveur
        3 Entrer quand meme
```

L'option 3 reproduit l'ancien comportement, mais comme un choix explicite.

**Perte de porteuse en cours de session.** Quand le serveur raccroche (ou que la
liaison tombe), le modem repasse en mode commande et emet `NO CARRIER`. OricTel
le reconnait, attend **4 secondes** de silence pour confirmer — une page qui
citerait ces mots continuerait de defiler — puis propose :

```
        PERTE DE PORTEUSE
Le modem a signale NO CARRIER:
la communication est terminee.

        1 Reconnecter
        2 Rester en local
```

`1` recompose le meme serveur. Il n'y a pas de recomposition automatique
silencieuse : la reconnexion coute une communication, c'est a vous de la decider.

### Mode WebSocket (via bridge)

`make run-ws` lance le bridge Python (`orictel_bridge.py`) qui relaie
TCP (port 3615) vers le serveur WebSocket `ws://3617.fr/ws`, puis
l'emulateur. Le bridge est un relais binaire transparent : OricTel suit
son flux modem AT habituel (le handshake AT echoue silencieusement faute
de modem cote bridge, puis le flux Videotex circule).

## 2bis. Config WiFi (materiel PicoWiFiModemUSB)

Sur un montage reel **LOCI + PicoWiFiModemUSB**, le modem doit etre
associe a un reseau WiFi (avec une adresse IP) avant de pouvoir composer.
Sinon `ATD` echoue immediatement par `NO CARRIER (00:00:00)` (statut
`no ip`). Le menu `2 - Config WiFi` realise toute la configuration depuis
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

- **Lignes 0-24** : la page Videotex du serveur (40 colonnes), ligne 0
  comprise - elle lui appartient entierement, OricTel n'y ecrit plus rien.
- **Curseur** : barre clignotante sous la cellule courante, lorsque le
  serveur l'active (zones de saisie).
- **Barre de statut** : 3 lignes de texte sous la page.

```
 C  PAVI 3617         00:12  AUTO  ESC      <- etat (cyan)
ESC: quitter? ESC=menu autre=reprendre      <- messages (jaune)
^A Annul ^R Retour ^S Somm ^N Suite         <- aide (blanc)
```

  - `C` inverse = donnees recues recemment (connecte), `F` = pas de
    donnees depuis 30 s (liaison probablement coupee) ;
  - le serveur choisi, puis le **chrono** de la session (`mm:ss`) ;
  - le **mode de rendu** courant (`AUTO`, `TRAME`, `BRUT`, voir CTRL+D) ;
  - la ligne du milieu accueille les messages (question ESC, `ACIA reset`).

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
| **ANNULATION** | effacer la saisie en cours | CTRL+A |
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
| **ESC** | la touche de secours : quitter la session et revenir au menu |

### ESC : quitter, toujours de la meme facon

Quel que soit l'ecran, **ESC** ramene en arriere :

- **en session** : un premier ESC pose la question sur la ligne 0 de la
  page (`ESC: quitter? ESC=menu autre=reprendre`) sans effacer la page.
  Un second ESC raccroche (`+++`, `ATH`, environ 3 a 8 s) et revient au
  menu *Mode de connexion*, decodeur remis a neuf. Toute autre touche
  retire la question et reprend la session la ou elle en etait ;
- **sur l'ecran d'echec de connexion** ou **de perte de porteuse** : ESC
  abandonne et revient au menu ;
- **dans un menu ou une saisie** (serveur `host:port`, WiFi) : ESC annule
  et revient a l'ecran precedent.

- **sur le menu *Mode de connexion*** : ESC **quitte OricTel** et rend la
  main au BASIC (`Ready`), par un redemarrage a froid de la ROM : la zone
  programme BASIC ayant ete ecrasee au chargement, c'est la seule sortie
  propre. La chaine complete est donc ESC, ESC (session -> menu) puis ESC
  (menu -> BASIC). Verifie sur ROM 1.0 (Oric-1) et 1.1 (Atmos).

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
| « PAS D'INTERFACE SERIE » | aucun 6551 en `$0380` : c'est le miroir du VIA qui repond. Oric sans LOCI, LOCI hors contexte disque, ou Phosphoric lance sans backend serie (`--loci-emu <elf>` **sans** `--loci-cdc`, `--loci` sans `--serial`) | brancher/booter le LOCI (depuis le `.dsk`) ; sur emulateur : `make run` (`--loci --serial picowifi:…`) ou `--loci-emu … --loci-cdc /dev/ttyACMx` (ou un PTY faux modem) |
| Menu affiche mais **aucune touche ne repond** (versions < 0.3.20) | meme cause : `serial_init` reprogrammait DDRA/DDRB du VIA a travers le miroir `$0380`, clavier mort | mettre a jour (0.3.20 sonde avant de programmer) |
| « MODEM USB NON DETECTE » | l'ACIA LOCI repond mais `/DSR` est haut : aucun PicoWiFiModemUSB monte sur l'USB du LOCI | brancher le Pico ; une touche continue quand meme (la connexion echouera proprement) |
| « PAS DE MODEM » / retour apres ATZ | l'emulateur n'est pas en `--serial modem`/`picowifi` (aucun modem ne repond « OK ») | utiliser `make run` (ou `make run-loci`/`run-loci-emu`) |
| `NO CARRIER (00:00:00)` (Pico reel) | format de numerotation (`ATD<hote>` : le 1er car. de l'hote pris pour un modificateur Hayes) ou WiFi non associe | corrige en 0.2.42 (OricTel compose `ATDT<hote>`) ; si ca persiste : menu `2 - Config WiFi` pour (re)configurer le reseau |
| Premiere page illisible / ecran noir, les suivantes correctes | modem reste en communication d'une session precedente : `ATZ` perdu dans le flux, aucune numerotation, decodage demarre en cours de page | corrige : OricTel raccroche (`+++`/`ATH`) et rejoue `ATZ` automatiquement. Sur une version anterieure : raccrocher a la main avant de relancer |
| « PERTE DE PORTEUSE » en cours de consultation | le serveur a raccroche (inactivite) ou la liaison est tombee | `1 Reconnecter` recompose le meme serveur |
| Page corrompue SANS ecran d'erreur, sur emulateur | montage emule sans tampon RX : le 6551 nu ne garde qu'un octet, alors que le vrai firmware LOCI en tamponne 32 | ajouter `--serial-buffer 32` (c'est le defaut de `make run-loci-real` depuis 0.3.x) |
| « aucun modem ne repond » alors que le dongle est branche | le PicoWiFiModemUSB **se re-enumere** apres un debranchement : `ttyACM0` devient `ttyACM1`… | le Makefile detecte desormais le premier `/dev/ttyACM*` ; sinon forcer `PICO_DEV=/dev/ttyACMx` |
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
- La configuration V23 asymetrique (1200/75 bauds, 7E1) n'est plus une cible :
  le montage supporte est LOCI + PicoWiFiModemUSB, qui dialogue en **1200 8N1**
  (Controle `$18`, Commande `$0B`).
- Le bit /DCD du 6551 ne peut pas servir a detecter une perte de porteuse sur
  LOCI : le firmware ne le pilote que depuis le montage USB et DTR, jamais
  depuis l'etat de l'appel. D'ou la detection par `NO CARRIER`.
- Le delai de confirmation de perte de porteuse (4 s) et le retour de
  l'indicateur a `F` (30 s de silence) sont comptes sur le Timer 2 du VIA,
  une vraie base de temps : ils ne dependent plus du cout de la boucle.
