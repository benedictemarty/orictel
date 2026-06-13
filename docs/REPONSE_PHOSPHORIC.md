# Réponse Phosphoric → OricTel — suite au rapport d'intégration

**À l'attention de l'équipe OricTel**
**Émetteur :** équipe Phosphoric (émulateur ORIC-1/Atmos)
**Date :** 2026-06-13
**En réponse à :** `docs/RAPPORT_PHOSPHORIC.md` (OricTel, 2026-06-13)
**Version Phosphoric livrée :** `1.19.1-alpha`
**Référence :** PR #18 / branche `fix/orictel-integration-feedback`

> Ce document est rédigé **côté OricTel** (réponse archivée dans votre dépôt) et
> ne décrit que des modifications faites **dans le dépôt Phosphoric**.

---

## Résumé

Merci pour ce rapport précis et bien instrumenté — les deux bugs étaient
parfaitement diagnostiqués. Tout a été traité côté Phosphoric et fusionné dans
la PR #18.

| # | Sujet | État | Localisation |
|---|-------|------|--------------|
| 1 | Verrou œuf-poule DCD (backend Digitelec) | ✅ **Corrigé** | `src/io/acia6551.c` |
| 2 | `ATD<hôte>` p/t tronqué (PicoWiFi) | ✅ **Corrigé côté Phosphoric** | `src/io/serial_picowifi.c` |
| 3 | Binaire distribué sans SDL | ✅ **Documenté** | `README.md` |
| 4 | `CMakeLists.txt` obsolète | ✅ **Corrigé** | `CMakeLists.txt` |

Suite complète : **603 tests PASS**, 0 échec.

---

## 1. Verrou œuf-poule DCD du backend Digitelec — Corrigé

Diagnostic confirmé à 100 %. `acia_tick()` gardait `backend->poll()` derrière
`acia->dcd`, alors que c'est `poll()` → `digitelec_check_dtr()` → `connect()`
qui lève `dcd`.

`poll()` est désormais appelé **inconditionnellement** ; le garde `dcd` ne
conditionne plus que la livraison des octets reçus à RDRF. Le backend établit
donc la porteuse de lui-même sur front DTR, sans attendre une première frappe
clavier (chemin TX). Nous avons vérifié que tous les autres `poll()` de backend
sont des lectures pures → l'appel hors-garde est sûr.

Régression ajoutée : `test_dcd_deadlock_autodial` (tests/unit/test_serial.c) —
un backend factice qui lève DCD depuis l'intérieur de `poll()` et dont l'octet
doit parvenir à RDRF alors que DCD démarre à faux.

## 2. `ATD<hôte>` commençant par p/t tronqué — Corrigé côté Phosphoric

Nous avons tranché en votre faveur : **vous n'avez rien à changer côté OricTel**.

Le modificateur de numérotation T/P n'est plus reconnu que **sensible à la
casse** (T/P majuscule uniquement, via un nouveau `pw_match_cs`). Résultat :

- `ATDpavi.3617.fr:3617` → composé tel quel, le `p` initial est **conservé** ;
- `ATDThost` / `ATDPhost` (modificateur explicite en majuscule) → continuent de
  fonctionner.

Vous pouvez donc émettre `ATD<hostname>` directement, sans le contournement
`ATDT`. Pas de double correction nécessaire.

Régressions : `test_dial_host_starting_with_p`,
`test_dial_uppercase_modifier_keeps_host` (tests/unit/test_picowifi.c).

## 3. Binaire distribué sans SDL — Documenté

Le défaut `SDL2 ?= 0` du `Makefile` est désormais explicité dans le README :
`make SDL2=1` est requis pour l'affichage graphique/audio/clavier. Le build
CMake, lui, active SDL2 inconditionnellement (`-DHAS_SDL2`).

## 4. `CMakeLists.txt` obsolète — Corrigé

Liste des sources réalignée sur le `Makefile` (les modules manquants, dont
`src/utils/symbols.c`, causaient les `undefined reference symbol_lookup/
symbol_resolve`), ajout de `-DHAS_SDL2`, version du projet portée à 1.19.0.
Build CMake vérifié OK de bout en bout.

---

L'ACIA 6551 `$031C` et le décodage Videotex restent inchangés. N'hésitez pas à
retester avec la branche `fix/orictel-integration-feedback`. Merci encore pour
ce retour de qualité.
