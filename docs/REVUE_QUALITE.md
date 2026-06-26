# Revue qualité sénior — OricTel

**Date :** 2026-06-26 · **Référentiel :** état de l'art embarqué / OSS
**Périmètre :** firmware 6502 (cc65), bridge Python, outillage, process.
**Note globale initiale : 3,2 / 5** → après remédiation complète (P0–#8 corrigés,
CI build+test+cppcheck+fuzz+coverage, tests clavier) : **~4,2 / 5**.

Ce document archive la revue et le **suivi de remédiation**. Il est mis à jour
à mesure que les items de la feuille de route sont traités.

---

## Synthèse

Projet **remarquablement documenté et tracé**, à l'**architecture en couches
propre**, dont le **chemin réseau (décodeur Videotex) — la vraie surface
d'attaque — est effectivement durci** (validé par fuzzing, voir P2). Les
faiblesses initiales : écritures hors-borne dans les **saisies UI locales**,
attente AT non bornée sous flux continu, et **absence de chaîne qualité
automatisée** (CI, analyse statique, couverture, fuzzing).

---

## Findings & statut

### P0 — Sûreté (✅ corrigés en 0.2.48)

| # | Sév. | Lieu | Défaut | Statut |
|---|------|------|--------|--------|
| 1 | Majeur/élevé | `at_modem.c` `at_wait_response`/`at_wait_ip` | Drain `do{}while(serial_poll())` sans borne → timeout contournable sous flux continu | ✅ `AT_DRAIN_BURST`=1024 + progression `elapsed`/`rwait` ; 2 tests régression (flux infini) |
| 2 | Majeur | `main.c` `ui_print` | Pas de clip colonne → débordement sur la ligne voisine (UB) | ✅ Clip sur `VTX_COLS` |
| 3 | Majeur | `main.c` saisie serveur | Écriture jusqu'à la colonne 51 | ✅ `ui_text_input()` bornée |
| 4 | Majeur | `main.c` `wifi_input` | Mot de passe écrit jusqu'à la colonne 40 | ✅ `ui_text_input()` (masque `*`) |
| 5 | Majeur | `main.c` liste WiFi | SSID 32 car. + cadenas hors borne | ✅ Boucles clippées |
| 6 | Mineur | `videotex.c` params CSI | Débordement `unsigned char` (999→231) | ✅ 0.2.50 — accumulation bornée à 64 (calcul `unsigned int`) |
| 7 | Mineur | `videotex.c` REP/US | Soustraction non bornée sauvée par clamp | ✅ 0.2.50 — validation `>= $40` en amont |
| 8 | Mineur | `serial_asm.s` | Spin TDRE non borné | ✅ 0.2.51 — attente plafonnée (~65536 iter, X/Y scratch) → un modem figé ne gèle plus l'Oric (octet abandonné) |

### Bug supplémentaire trouvé par les tests (✅ 0.2.50)

- **Flèche droite inopérante** (`keyboard.c`) : le cas `$15 → KEY_ARROW_RIGHT`
  était placé *après* le handler CTRL+lettre, mais `$15 ∈ [$01,$1A]` → avalé en
  `KEY_NONE` (code mort). Révélé par `tests/test_keyboard.c`, corrigé (déplacé
  dans le switch des touches spéciales). Illustration directe de la valeur des
  tests ajoutés.

> Les débordements UI (#2–#5) étaient de l'UB C retombant dans le tableau
> `screen[25][40]` → **corruption d'affichage**, pas un crash/RCE. La cause
> racine commune (`ui_print` non clippé) est neutralisée ; les deux boucles de
> saisie dupliquées sont factorisées dans `ui_text_input()` (avec ANNULATION).

### P1 — Chaîne qualité (✅ en place en 0.2.48–0.2.49)

- ✅ **CI GitHub Actions** (`.github/workflows/ci.yml`) : build cc65 + `make
  test` (Videotex 219, ACIA/SMC 21, modem AT 18, bridge 23) sur push/PR.
- ✅ **Analyse statique cppcheck** (job CI) — **0 finding** après P0.
- ✅ **Couverture gcov** (`make coverage`, job CI) : videotex.c **83 %**,
  at_modem.c **94 %**.

### P2 — Durcissement (✅ partiel en 0.2.49)

- ✅ **Fuzzing du décodeur Videotex** (`make fuzz`, job CI, libFuzzer +
  ASAN/UBSAN) : > 1,2 M entrées/30 s **sans crash** → durcissement du chemin
  réseau prouvé.
- ✅ **Tests host pour `keyboard.c`** (`make test-keyboard`, 22 checks) — ont
  révélé et fait corriger le bug de la flèche droite.
- ✅ Findings mineurs #6–#7 (Videotex). Reste #8 (spin TDRE, faible risque).

### Process / hygiène (✅ partiel)

- ✅ **Makefile portable** : chemins ROM/émulateur surchargeables (`?=`,
  `ORIC_ROMS`).
- ⏳ CHANGELOG monolithique (49 Ko) ; binaires `.tap`/`.dsk` versionnés (choix
  assumé) ; cap de version 0.3/1.0 à acter.

---

## Points forts (à préserver)

- Décodeur réseau borné (`put_char`, `vtx_set_cursor`, `memmove` exacts) —
  confirmé par fuzzing.
- Séparation des couches nette, commentaires riches, traçabilité exemplaire
  (CHANGELOG/VERSION_TRACKING/ROADMAP/manuel).
- `serial_tx.c` avec borne anti-deadlock ; SMC `serial_asm.s` légitime (RAM).

## Décisions assumées (recommandations NON appliquées, volontairement)

- **CHANGELOG monolithique (49 Ko) conservé** : la convention du projet est
  « une entrée CHANGELOG par modification » ; le découper irait à l'encontre de
  cette discipline de traçabilité. Acté tel quel.
- **Binaires `.tap`/`.dsk` versionnés dans l'historique** : choix délibéré du
  projet (livrables versionnés, cf. CLAUDE.md). On ne les bascule pas en
  *releases* GitHub sans décision explicite.
- **Version cc65** : `apt` ne fige pas une version exacte ; on **enregistre**
  les versions de toolchain en CI (étape dédiée) pour la reproductibilité, ce
  qui est le compromis pragmatique retenu.

## Reste à faire (optionnel, faible priorité)

1. Tests host de `ui_text_input` pour verrouiller la non-régression des bornes
   de saisie (#2–#5) — la logique est désormais factorisée, donc facile à
   couvrir.
2. Cap de version produit (0.3 / 1.0) à acter quand le périmètre fonctionnel
   est jugé stable.

> Tous les findings de la revue (P0 #1–#5, mineurs #6–#8) sont **corrigés** ;
> la chaîne qualité (CI build+test+cppcheck+fuzz+coverage) est en place.
