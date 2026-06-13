# Rapport d'intégration OricTel → Phosphoric

**À l'attention de l'équipe Phosphoric**
**Émetteur :** équipe OricTel (émulateur Minitel 1B pour Oric)
**Date :** 2026-06-13
**Version Phosphoric testée :** `1.19.0-alpha` (binaire `oric1-emu`)
**Plateforme :** Linux, session Wayland, SDL2 2.32.4

> Ce document est rédigé **côté OricTel** et n'apporte **aucune modification** au
> dépôt Phosphoric. Il signale des points relevés lors de l'intégration des
> nouveaux backends série (DTL 2000 / PicoWiFi) avec OricTel, qui pilote
> l'**ACIA 6551 @ `$031C-$031F`** via son driver `src/serial_asm.s`.

---

## Résumé

| # | Sujet | Sévérité | Localisation | Côté correctif |
|---|-------|----------|--------------|----------------|
| 1 | Backend `digitelec` ne se connecte pas en réception pure | **Bug** | `src/io/acia6551.c:434` | Phosphoric |
| 2 | `ATD<host>` avec hôte commençant par `p`/`t` mal interprété (pulse/tone) | À arbitrer | `src/io/serial_picowifi.c:1071` | OricTel **ou** Phosphoric |
| 3 | Binaire distribué compilé sans SDL par défaut | Confort | `Makefile` (`SDL2 ?= 0`) | Build/doc |
| 4 | `CMakeLists.txt` obsolète (link cassé) | Confort | `CMakeLists.txt` | Phosphoric |

La bonne nouvelle d'abord : **`--serial tcp` (V23) et `--serial digitelec` fonctionnent**
de bout en bout — la page d'accueil Télétel de `pavi.3617.fr:3617` est reçue et
décodée correctement par OricTel (testé en SDL temps réel).

---

## 1. Bug — Le backend `digitelec` ne se connecte jamais en mode réception pure

### Symptôme
Lancé en mode direct V23 (OricTel attend la page serveur sans rien émettre), le
backend `--serial digitelec:H:P` n'établit **jamais** la porteuse : aucun
`CARRIER DETECT`, écran resté noir. Le serveur n'est pourtant jamais contacté.

### Cause racine
Le backend Digitelec établit la connexion TCP sur front montant de DTR, via
`digitelec_check_dtr()` (`src/io/serial_backend.c:1124`), lui-même appelé depuis
`digitelec_poll()` (1204) **et** `digitelec_send()` (1137).

Mais dans `acia_tick()`, le chemin RX qui appelle `backend->poll()` est
conditionné par `acia->dcd` :

```c
/* src/io/acia6551.c:434 */
if (acia->dcd && acia->backend->poll &&
    acia->backend->poll(acia->backend)) {
    ...
}
```

Or `dcd` n'est mis à vrai **que par** `digitelec_connect()` (via `acia_set_dcd`).
On a donc un **verrou œuf-poule** :

```
poll() déclenche check_dtr() → connect() → DCD=1
   mais poll() n'est appelé que si DCD=1 déjà
```

En réception pure, rien ne sort de ce verrou. Seul `digitelec_send()` (chemin TX,
non conditionné par DCD) peut amorcer : la connexion ne démarre donc qu'à la
**première frappe clavier** de l'utilisateur. C'est inattendu pour un modem
auto-appelant sur DTR.

### Reproduction
```bash
oric1-emu --rom roms/basic11b.rom --tape orictel.tap -f \
  --serial digitelec:pavi.3617.fr:3617 --serial-buffer 4096 \
  --serial-trace /tmp/dgt.log
# Mode Direct dans OricTel : aucun CARRIER DETECT, écran noir.
# Après une frappe quelconque : "Digitelec DTL 2000: CARRIER DETECT" → page OK.
```

### Pistes de correctif (côté Phosphoric)
- Appeler `backend->poll()` (ou au minimum `check_dtr`) **indépendamment de
  `dcd`**, de sorte que le backend puisse établir la porteuse de lui-même quand
  DTR est asserté. Le gardien `dcd` pourrait n'entourer que la partie
  `recv()`/RDRF, pas la détection de front DTR.
- Alternative : faire évaluer `check_dtr` dans `acia_tick()` dès que la commande
  ACIA (DTR) change, hors du chemin RX.

---

## 2. À arbitrer — `ATD<host>` : un hôte commençant par `p` ou `t` est tronqué

### Symptôme
En mode modem AT (`--serial picowifi`), OricTel compose `ATDpavi.3617.fr:3617`.
Résultat :
```
ERROR: PicoWiFi: getaddrinfo(avi.3617.fr:3617): Name or service not known
```
Le `p` initial de `pavi` a disparu.

### Cause racine
Le dispatch dial reconnaît les préfixes `DT`/`DP` de façon **insensible à la
casse** (`pw_match` → `strncasecmp`) :

```c
/* src/io/serial_picowifi.c:1071 */
if ((a = pw_match(p, "DT")) || (a = pw_match(p, "DP"))) {
    const char* end = a + strlen(a);
    pw_do_dial(pw, a);   /* a pointe après "DP" */
    return end;
}
```

Sur `ATDpavi…` → après `AT`, la chaîne est `Dpavi…`. `pw_match(p, "DP")`
compare `Dp` à `DP` sans tenir compte de la casse → **match** : le `p` est
consommé comme préfixe *pulse dial*, et l'hôte devient `avi.3617.fr`.

Tout hôte commençant par `p`/`P` (pulse) ou `t`/`T` (tone) est donc tronqué.
C'est un standard Hayes légitime pour des numéros de téléphone, mais piégeux
pour un modem qui compose des **noms d'hôtes**.

### Notes
- Côté **OricTel**, le contournement immédiat est d'émettre `ATDT` + hôte (le
  `T` consommé est alors le vrai modificateur), ou un séparateur. Nous pouvons
  l'appliquer dans `src/main.c` (`modem_connect`, ~ligne 547) si vous préférez
  que la responsabilité reste côté terminal.
- Côté **Phosphoric**, si vous souhaitez accepter `ATD<hostname>` tel quel, une
  option serait de ne traiter `DT`/`DP` comme modificateurs **que si le
  caractère suivant est un chiffre** (numéro), sinon traiter le reste comme
  hôte.

Nous attendons votre préférence avant d'agir, pour éviter une double
correction.

---

## 3. Confort — Binaire distribué compilé sans SDL (headless only)

Le binaire `oric1-emu` fourni n'est lié qu'à la libc (`ldd` ne montre ni SDL2
ni X11/Wayland) : il tourne uniquement en `--headless` et **n'ouvre aucune
fenêtre**. La cause est le défaut du `Makefile` racine :

```make
SDL2 ?= 0
```

Pour l'affichage graphique il faut recompiler :
```bash
make clean && make SDL2=1 -j
```
Côté OricTel nous utilisons une **copie locale** de ce binaire SDL
(`orictel/tools/oric1-emu-sdl`) pour ne pas modifier votre dépôt, mais il
serait pratique que la distribution propose un binaire SDL, ou que ce point
soit documenté dans le README de Phosphoric.

---

## 4. Confort — `CMakeLists.txt` obsolète

Une compilation via CMake échoue à l'édition de liens :
```
undefined reference to `symbol_lookup' / `symbol_resolve'
```
Le `CMakeLists.txt` ne référence pas tous les modules sources actuels (ex.
`src/utils/symbols.c`), contrairement au `Makefile` racine qui, lui, est à jour.
Le `Makefile` est donc le seul chemin de build fiable aujourd'hui.

---

## Méthode de test (pour reproduire de votre côté)

Tests réalisés en **SDL temps réel** (l'émulateur headless dépasse les E/S
réseau réelles et ne reçoit pas la page d'accueil ; SDL est synchronisé à
50 fps, ce qui laisse arriver le flux). Navigation des menus OricTel
automatisée via `--type-keys` (`\n`=Return, `\pN`=pause N s en cycles) et
capture via `--screenshot-at C:FILE`.

```bash
# Exemple digitelec, capture après réception
oric1-emu --rom roms/basic11b.rom --tape orictel.tap -f \
  --serial digitelec:pavi.3617.fr:3617 --serial-buffer 4096 --scale 3 \
  --type-keys 4000000:'RUN\n\p9\p92\p9\p9\p9\p91\p9\p9\n' \
  --serial-trace /tmp/dgt.log --screenshot-at 92000000:/tmp/dgt.bmp
```

Merci pour ces nouveaux backends — l'ACIA 6551 `$031C` reste parfaitement
compatible et le décodage Videotex fonctionne. Nous restons disponibles pour
préciser n'importe quel point.
