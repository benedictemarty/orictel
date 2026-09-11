# Plan Agile - OricTel

## Methodologie
Scrum adapte avec sprints courts. Chaque sprint produit un increment fonctionnel.

## Product Backlog

### Epic 1: Infrastructure
- [x] US-001: Structure projet et depot git
- [x] US-002: Bridge WebSocket-TCP fonctionnel
- [x] US-003: Driver ACIA 6551 pour Oric
- [x] US-004: Configuration linker cc65

### Epic 2: Affichage
- [x] US-010: Moteur rendu HIRES 40x25
- [x] US-011: Jeu de caracteres G0 (alphanumerique)
- [x] US-012: Jeu de caracteres G1 (mosaiques)
- [x] US-013: Jeu de caracteres G2 (supplementaire) — `font_get_g2`, accents via
      SS2 (codes G2 internes >= $80). Observe sur page reelle PAVI ("TELETEL"
      accentue rendu correctement).
- [x] US-014: Double hauteur / double largeur — `SIZE_DOUBLE_*`, blit assembleur
      `blit_cell4x2`. Observe sur page reelle (logo "teletel 4" double hauteur).
- [x] US-015: Clignotement anime — `ATTR_FLASH` + `g_blink_phase` bascule dans la
      boucle de session.
- [x] US-016: Texte masque (concealed) — `ATTR_CONCEALED` + masque global
      (ESC # $20 $58/$5F), revele par commande serveur.

### Epic 3: Protocole Videotex
- [x] US-020: Machine a etats principale
- [x] US-021: Sequences ESC (attributs)
- [x] US-022: Positionnement curseur (US)
- [x] US-023: Sequences CSI (deplacement, effacement)
- [x] US-024: Sequences PRO (protocole) — `dispatch_pro` : ENQROM, aiguillages
      PRO3, modes clavier, minuscules.
- [ ] US-025: DRCS (caracteres redefinis) — non implemente.
- [x] US-026: Mode rouleau vs mode page — `rolling_mode` + `scroll_up`, mode page
      par defaut (Minitel 1B).

### Epic 4: Interaction
- [x] US-030: Scan clavier Oric
- [x] US-031: Mapping touches fonction Minitel
- [x] US-032: Envoi caracteres via ACIA
- [ ] US-033: Barre de statut interactive — les 3 lignes texte sont actuellement
      CACHEES (`display_status` neutralise, display.c).

### Epic 5: Qualite
- [x] US-040: Tests unitaires decodeur Videotex
- [x] US-041: Tests bridge
- [x] US-042: Tests d'integration end-to-end — `make test-menus` (parcours de
      menus jusqu'a l'ecran d'echec puis ESC, 11 checks) et `make test-servers` (connexion
      reelle a PAVI 3617 / MiniPavi, page decodee extraite de la RAM et verifiee
      par ancres stables). Le second exige le dongle : SKIP propre sinon.
- [x] US-043: Compatibilite Oric-1 (BASIC 1.0) — passage HIRES corrige (detection
      ROM + `jsr $F8E3`, v0.3.7) ET valide de bout en bout : `basic10.rom` mene
      jusqu'a une page Minitel reelle via le montage LOCI + PicoWiFi.
- [x] US-044: Banc de mesure du rendu (`make bench-render`) — cout en cycles 6502
      reels, calibration integree, empreinte SHA-256 du framebuffer comme
      garde-fou pixel-exact des optimisations.
- [x] US-045: Robustesse de la reception — overrun RX chiffre puis resorbe
      (rendu 4,0x, decodeur 1,50x), pas de sondage AT ramene sous le temps-octet.
- [x] US-046: Reprise sur modem reste en ligne (`at_hangup`) et perte de porteuse
      (`at_carrier_watch`), validees sur materiel.
- [x] US-047: ESC, touche de secours universelle (v0.3.17) — quitter la session
      (question ligne 0, ESC ESC raccroche et revient au menu, decodeur remis a
      neuf), retour sur echec / perte de porteuse / menus. Parcours verifie sur
      cible (`test_menus`, 11 checks). Au passage : ce test SKIPpait en silence
      depuis Phosphoric v2.0 (mineur de version seul compare) — repare.

## Sprint 1 - Fondations (v0.1.0) [TERMINE]
**Objectif:** Premiere connexion reussie a 3617.fr avec affichage basique
- US-001, US-002, US-003, US-004, US-010 — tous livres.

## Sprint courant - Fiabilite de la liaison et du rendu (v0.3.x) [TERMINE]
**Objectif:** qu'une page Minitel reelle s'affiche correctement du premier coup,
et que les regressions se voient sans intervention humaine.

Livre :
- Reprise sur modem reste en ligne (`at_hangup`) — cause de la "premiere page
  illisible" — puis retour de `modem_connect` traite avec ecran d'echec.
- Overrun RX : chiffre par un banc de mesure, puis resorbe (rendu 4,0x, decodeur
  1,50x, pas de sondage AT sous le temps-octet). Rendu prouve pixel-exact.
- Detection de perte de porteuse + reconnexion, validees sur materiel.
- Tests d'integration : menus, serveurs reels, fuzzing auto-verifiant.

Constat de retrospective : trois defauts avaient echappe a la relecture de code
et n'ont ete trouves que par la MESURE ou l'execution sur cible — un libelle
tronque a 40 colonnes, un pas de sondage AT plus lent que le temps-octet, et un
fuzzing mort en silence depuis des mois. D'ou l'ajout de garde-fous qui echouent
bruyamment plutot que de dependre d'un oeil humain.

## Prochaines pistes (non planifiees)
- US-025 DRCS, US-033 barre de statut interactive.
- Base de temps reelle pour `CARRIER_CONFIRM_IDLE`, aujourd'hui un nombre
  d'iterations que toute optimisation raccourcit mecaniquement.
- Fin de l'optimisation du rendu : pre-scans memorises dans le contexte
  (necessite un garde-fou que l'empreinte framebuffer ne couvre pas).

## Definition of Done
- Code compile sans erreur ni warning
- Tests unitaires passent (`make test`)
- Documentation mise a jour (README, ARCHITECTURE, MANUEL, CIRRUS_OS)
- CHANGELOG, ROADMAP et VERSION_TRACKING mis a jour
- Commit avec message descriptif, pousse sur les deux remotes
- Pour une optimisation : gain MESURE (`make bench-render`) et empreinte
  framebuffer inchangee
- Pour un comportement materiel : valide sur le montage reel, ou l'ecart
  explicitement note comme non valide
