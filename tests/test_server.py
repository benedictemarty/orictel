#!/usr/bin/env python3
"""
Serveur de test Videotex local pour OricTel.

Se connecte directement en TCP a l'emulateur (pas de bridge WebSocket).
Envoie des sequences Videotex progressives pour tester chaque fonctionnalite.

Usage:
    python3 tests/test_server.py [--test NOM]

Tests disponibles:
    hello     - Texte simple "HELLO WORLD"
    position  - Positionnement curseur US
    colors    - Changements de couleur ESC
    mosaic    - Mosaiques G1
    clear     - Effacement ecran
    page      - Page complete simulee
    all       - Tous les tests en sequence
"""

import asyncio
import argparse
import sys
import time

# Constantes Videotex
FF = 0x0C       # Clear screen
CR = 0x0D       # Carriage return
LF = 0x0A       # Line feed (cursor down)
SO = 0x0E       # Shift Out -> G1 (mosaics)
SI = 0x0F       # Shift In -> G0 (alpha)
ESC = 0x1B      # Escape
US = 0x1F       # Unit Separator (cursor positioning)
REP = 0x12      # Repeat
DC1 = 0x11      # Cursor ON
DC4 = 0x14      # Cursor OFF
RS = 0x1E       # Home (cursor to 1,0)
SEP = 0x13      # Separator (function key prefix)


def pos(row, col):
    """Sequence US pour positionner le curseur."""
    return bytes([US, 0x40 + row, 0x41 + col])


def ink(color):
    """Sequence ESC pour changer la couleur d'encre (0-7)."""
    return bytes([ESC, 0x40 + color])


def paper(color):
    """Sequence ESC pour changer la couleur de fond (0-7)."""
    return bytes([ESC, 0x50 + color])


def repeat(n):
    """Repeter le dernier caractere n fois."""
    return bytes([REP, 0x40 + n])


def text(s):
    """Convertir une chaine en octets Videotex."""
    return bytes(ord(c) & 0x7F for c in s)


def invert_on():
    return bytes([ESC, 0x5C])


def invert_off():
    return bytes([ESC, 0x5D])


# ============================================================
# Tests
# ============================================================

def test_hello():
    """Test 1: texte simple."""
    data = bytearray()
    data += bytes([FF])                         # Clear screen
    data += pos(1, 5)                           # Ligne 1, colonne 5
    data += text("HELLO WORLD!")
    data += pos(3, 2)
    data += text("OricTel fonctionne!")
    data += pos(5, 2)
    data += text("Test texte G0 OK.")
    return bytes(data)


def test_position():
    """Test 2: positionnement curseur."""
    data = bytearray()
    data += bytes([FF])
    # Ecrire aux 4 coins
    data += pos(1, 0)
    data += text("HG")                          # Haut-Gauche
    data += pos(1, 36)
    data += text("HD")                          # Haut-Droite
    data += pos(24, 0)
    data += text("BG")                          # Bas-Gauche
    data += pos(24, 36)
    data += text("BD")                          # Bas-Droite
    # Centre
    data += pos(12, 15)
    data += text("CENTRE")
    # Diagonale
    for i in range(10):
        data += pos(2 + i, 2 + i * 3)
        data += text("*")
    return bytes(data)


def test_colors():
    """Test 3: couleurs (via serial attributes Oric)."""
    data = bytearray()
    data += bytes([FF])
    data += pos(1, 1)
    data += text("Test couleurs:")
    # Note: les couleurs Videotex ne sont pas directement rendues
    # en mode texte Oric (tout est blanc), mais le decodeur doit
    # les traiter sans planter
    noms = ["NOIR", "ROUGE", "VERT", "JAUNE", "BLEU", "MAGENT", "CYAN", "BLANC"]
    for i in range(8):
        data += pos(3 + i, 1)
        data += ink(i)
        data += text(f" {noms[i]} ")
    return bytes(data)


def test_mosaic():
    """Test 4: mosaiques G1."""
    data = bytearray()
    data += bytes([FF])
    data += pos(1, 1)
    data += text("Test mosaiques G1:")
    # Ligne de mosaiques
    data += pos(3, 0)
    data += bytes([SO])                         # G1 mode
    # Quelques motifs: $21=top-left, $22=top-right, $2A=top-row, $3F=full
    for ch in [0x21, 0x22, 0x24, 0x28, 0x30, 0x3F, 0x2A, 0x15, 0x35]:
        data += bytes([ch])
    data += bytes([SI])                         # Retour G0
    data += text(" <-- mosaiques")
    # Ligne pleine de blocs
    data += pos(5, 0)
    data += bytes([SO])
    data += bytes([0x3F])                       # Bloc plein
    data += repeat(38)                          # Repeter 38 fois
    data += bytes([SI])
    data += pos(7, 1)
    data += text("Ligne 5 = blocs pleins")
    return bytes(data)


def test_clear():
    """Test 5: effacement ecran."""
    data = bytearray()
    data += bytes([FF])
    data += pos(12, 10)
    data += text("Ecran efface OK!")
    return bytes(data)


def test_repeat():
    """Test 6: repetition REP."""
    data = bytearray()
    data += bytes([FF])
    data += pos(1, 1)
    data += text("Test REP:")
    data += pos(3, 0)
    data += text("=")
    data += repeat(38)                          # 38 fois '='
    data += pos(5, 0)
    data += text("*")
    data += repeat(19)
    data += text(".")
    data += repeat(19)
    data += pos(7, 1)
    data += text("Ligne 3: 39x '='")
    data += pos(8, 1)
    data += text("Ligne 5: 20x '*' + 20x '.'")
    return bytes(data)


def test_invert():
    """Test 7: inversion video."""
    data = bytearray()
    data += bytes([FF])
    data += pos(1, 1)
    data += text("Test inversion:")
    data += pos(3, 5)
    data += text("Normal ")
    data += invert_on()
    data += text(" INVERSE ")
    data += invert_off()
    data += text(" Normal")
    data += pos(5, 5)
    data += invert_on()
    data += text(" MENU 1 ")
    data += invert_off()
    data += text("  ")
    data += invert_on()
    data += text(" MENU 2 ")
    data += invert_off()
    return bytes(data)


def test_page():
    """Test 8: page complete simulee type Minitel."""
    data = bytearray()
    data += bytes([FF, DC4])                    # Clear + cursor off

    # Barre de titre (ligne 1, mosaiques)
    data += pos(1, 0)
    data += bytes([SO])                         # G1
    data += bytes([0x3F])
    data += repeat(38)
    data += bytes([SI])                         # G0

    # Titre
    data += pos(2, 10)
    data += invert_on()
    data += text(" SERVEUR TEST ")
    data += invert_off()

    # Separateur
    data += pos(3, 0)
    data += text("-")
    data += repeat(38)

    # Contenu
    data += pos(5, 1)
    data += text("Bienvenue sur le serveur de test")
    data += pos(6, 1)
    data += text("OricTel v0.1 pour Oric 1/Atmos.")
    data += pos(8, 1)
    data += text("Tapez un code service:")
    data += pos(10, 3)
    data += text("1 - Test texte")
    data += pos(11, 3)
    data += text("2 - Test position")
    data += pos(12, 3)
    data += text("3 - Test mosaiques")
    data += pos(13, 3)
    data += text("4 - Test repetition")
    data += pos(14, 3)
    data += text("5 - Test inversion")

    # Barre de separateur
    data += pos(16, 0)
    data += text("-")
    data += repeat(38)

    # Pied de page
    data += pos(18, 1)
    data += text("Code + ENVOI (Return)")
    data += pos(20, 1)
    data += text("SOMMAIRE pour revenir ici")

    # Barre du bas (mosaiques)
    data += pos(24, 0)
    data += bytes([SO])
    data += bytes([0x3F])
    data += repeat(38)
    data += bytes([SI])

    # Curseur ON pour saisie
    data += pos(22, 5)
    data += text("Votre choix: ")
    data += bytes([DC1])                        # Cursor ON

    return bytes(data)


TESTS = {
    "hello": test_hello,
    "position": test_position,
    "colors": test_colors,
    "mosaic": test_mosaic,
    "clear": test_clear,
    "repeat": test_repeat,
    "invert": test_invert,
    "page": test_page,
}


PACE_DELAY = 0.008  # 8ms - vitesse Minitel 1200 baud


async def send_paced(writer, data):
    """Envoie les donnees octet par octet avec un delai pour eviter l'overrun ACIA."""
    for b in data:
        writer.write(bytes([b]))
        await writer.drain()
        await asyncio.sleep(PACE_DELAY)


async def handle_client(reader, writer):
    """Gere la connexion de l'emulateur."""
    peer = writer.get_extra_info("peername")
    print(f"[+] Emulateur connecte depuis {peer}")

    # Attendre le signal ready
    ready = await reader.read(1024)
    print(f"[+] Signal ready: {ready.hex()}")

    # Envoyer la page de menu
    print("[>] Envoi page de menu (pace)...")
    await send_paced(writer, test_page())

    # Boucle interactive
    try:
        while True:
            data = await reader.read(1024)
            if not data:
                break

            # Afficher ce que l'Oric envoie
            hex_str = " ".join(f"{b:02X}" for b in data)
            ascii_str = "".join(chr(b) if 32 <= b < 127 else "." for b in data)
            print(f"[<] Recu: {hex_str}  ({ascii_str})")

            # Interpreter les touches fonction Minitel
            if len(data) >= 2 and data[0] == SEP:
                func = data[1]
                if func == 0x46:  # Sommaire
                    print("[>] SOMMAIRE -> page de menu")
                    await send_paced(writer, test_page())
                elif func == 0x44:  # Guide
                    print("[>] GUIDE -> page aide")
                    await send_paced(writer, test_hello())
                elif func == 0x41:  # Envoi
                    # Regarder ce qui a ete tape avant
                    print("[>] ENVOI")
                    # Envoyer un echo
                    resp = bytearray()
                    resp += bytes([FF])
                    resp += pos(5, 5)
                    resp += text("Recu: ENVOI")
                    resp += pos(7, 5)
                    resp += text("Retour: SOMMAIRE")
                    await send_paced(writer, bytes(resp))
                continue

            # Caracteres normaux suivis de Envoi
            txt = ascii_str.strip(".")
            if txt:
                # Chercher si c'est un numero de test
                for b in data:
                    if b == ord('1'):
                        print("[>] Test 1: hello")
                        await send_paced(writer, test_hello())
                    elif b == ord('2'):
                        print("[>] Test 2: position")
                        await send_paced(writer, test_position())
                    elif b == ord('3'):
                        print("[>] Test 3: mosaic")
                        await send_paced(writer, test_mosaic())
                    elif b == ord('4'):
                        print("[>] Test 4: repeat")
                        await send_paced(writer, test_repeat())
                    elif b == ord('5'):
                        print("[>] Test 5: invert")
                        await send_paced(writer, test_invert())

    except Exception as e:
        print(f"[!] Erreur: {e}")
    finally:
        writer.close()
        print(f"[-] Deconnecte")


async def main():
    server = await asyncio.start_server(handle_client, "127.0.0.1", 3615)
    addr = server.sockets[0].getsockname()
    print(f"=== Serveur test OricTel sur {addr[0]}:{addr[1]} ===")
    print(f"=== En attente de l'emulateur... ===")

    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    asyncio.run(main())
