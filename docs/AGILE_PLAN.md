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
- [ ] US-013: Jeu de caracteres G2 (supplementaire)
- [ ] US-014: Double hauteur / double largeur
- [ ] US-015: Clignotement anime
- [ ] US-016: Texte masque (concealed)

### Epic 3: Protocole Videotex
- [x] US-020: Machine a etats principale
- [x] US-021: Sequences ESC (attributs)
- [x] US-022: Positionnement curseur (US)
- [x] US-023: Sequences CSI (deplacement, effacement)
- [ ] US-024: Sequences PRO (protocole)
- [ ] US-025: DRCS (caracteres redefinis)
- [ ] US-026: Mode rouleau vs mode page

### Epic 4: Interaction
- [x] US-030: Scan clavier Oric
- [x] US-031: Mapping touches fonction Minitel
- [x] US-032: Envoi caracteres via ACIA
- [ ] US-033: Barre de statut interactive

### Epic 5: Qualite
- [x] US-040: Tests unitaires decodeur Videotex
- [x] US-041: Tests bridge
- [ ] US-042: Tests d'integration end-to-end
- [ ] US-043: Compatibilite Oric-1 (BASIC 1.0)

## Sprint 1 - Fondations (v0.1.0) [EN COURS]
**Objectif:** Premiere connexion reussie a 3617.fr avec affichage basique
**Velocity estimee:** 8 story points
- US-001 (1 SP)
- US-002 (2 SP)
- US-003 (2 SP)
- US-004 (1 SP)
- US-010 (2 SP)

## Definition of Done
- Code compile sans erreur ni warning
- Tests unitaires passent
- Documentation mise a jour
- CHANGELOG mis a jour
- Commit avec message descriptif
