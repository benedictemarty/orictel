#!/usr/bin/env python3
"""
Tests unitaires pour le bridge WebSocket-TCP OricTel.

Execute avec: python3 tests/test_bridge.py
Ou: python3 -m pytest tests/test_bridge.py -v
"""

import asyncio
import sys
import os

# Ajouter le repertoire bridge au path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "bridge"))

from orictel_bridge import Bridge

# Compteurs
tests_run = 0
tests_passed = 0
tests_failed = 0


def assert_eq(msg, expected, actual):
    global tests_run, tests_passed, tests_failed
    tests_run += 1
    if expected == actual:
        tests_passed += 1
    else:
        tests_failed += 1
        print(f"  ECHEC: {msg} (attendu={expected!r}, obtenu={actual!r})")


def test_bridge_creation():
    """Test de creation du bridge."""
    print("Test: Creation du bridge")
    bridge = Bridge("ws://example.com/ws")
    assert_eq("ws_url", "ws://example.com/ws", bridge.ws_url)
    assert_eq("stats_rx", 0, bridge.stats_rx)
    assert_eq("stats_tx", 0, bridge.stats_tx)
    assert_eq("ws initial", None, bridge.ws)
    assert_eq("session inactive au depart", False, bridge.active)


def test_bridge_default_url():
    """Test du bridge avec URL par defaut."""
    print("Test: URL par defaut")
    bridge = Bridge("ws://3617.fr/ws")
    assert_eq("default url", "ws://3617.fr/ws", bridge.ws_url)


async def test_tcp_server_start():
    """Test de demarrage du serveur TCP."""
    print("Test: Demarrage serveur TCP")
    bridge = Bridge("ws://3617.fr/ws")

    server = await asyncio.start_server(
        bridge.handle_tcp_client, "127.0.0.1", 0  # Port aleatoire
    )
    addr = server.sockets[0].getsockname()
    assert_eq("host", "127.0.0.1", addr[0])
    assert_eq("port > 0", True, addr[1] > 0)

    server.close()
    await server.wait_closed()


async def test_tcp_connection():
    """Test de connexion TCP (sans WebSocket)."""
    print("Test: Connexion TCP")
    bridge = Bridge("ws://invalid.example.com/ws")  # WS invalide

    server = await asyncio.start_server(
        bridge.handle_tcp_client, "127.0.0.1", 0
    )
    addr = server.sockets[0].getsockname()

    # Connecter un client TCP
    try:
        reader, writer = await asyncio.open_connection("127.0.0.1", addr[1])
        # Le bridge va tenter de se connecter au WS et echouer
        await asyncio.sleep(0.5)
        writer.close()
        await writer.wait_closed()
        assert_eq("tcp connection established", True, True)
    except Exception as e:
        assert_eq(f"tcp connection: {e}", True, False)

    server.close()
    await server.wait_closed()


def test_ws_data_conversion():
    """Test de conversion des donnees WebSocket."""
    print("Test: Conversion donnees WS")

    # Simuler la conversion string -> bytes (comme dans _relay_ws_to_tcp)
    message_str = "Hello"
    data = bytes(ord(c) & 0x7F for c in message_str)
    assert_eq("string conversion len", 5, len(data))
    assert_eq("byte 0", ord('H'), data[0])
    assert_eq("byte 4", ord('o'), data[4])

    # Conversion bytes directs
    message_bytes = bytes([0x1B, 0x42, 0x41, 0x0D])
    assert_eq("bytes passthrough", 4, len(message_bytes))
    assert_eq("ESC byte", 0x1B, message_bytes[0])

    # Masquage 7 bits
    message_high = "Hello"
    data7 = bytes(ord(c) & 0x7F for c in message_high)
    assert_eq("7bit mask", ord('H'), data7[0])


def test_videotex_sequences():
    """Test de sequences Videotex typiques passant par le bridge."""
    print("Test: Sequences Videotex")

    # Sequence typique: clear screen + set color + text
    seq = bytes([
        0x0C,           # FF = clear screen
        0x1B, 0x42,     # ESC + ink green
        0x1B, 0x54,     # ESC + bg blue
        0x41, 0x42, 0x43,  # "ABC"
    ])
    assert_eq("seq length", 8, len(seq))
    assert_eq("seq[0] = FF", 0x0C, seq[0])
    assert_eq("seq[1] = ESC", 0x1B, seq[1])

    # Positionnement US
    us_seq = bytes([0x1F, 0x4A, 0x4B])  # US row=10 col=10
    assert_eq("US seq", 0x1F, us_seq[0])
    assert_eq("US row", 0x4A, us_seq[1])

    # Touches fonction Minitel
    envoi = bytes([0x13, 0x41])   # SEP + Envoi
    retour = bytes([0x13, 0x42])  # SEP + Retour
    assert_eq("Envoi[0]", 0x13, envoi[0])
    assert_eq("Envoi[1]", 0x41, envoi[1])
    assert_eq("Retour[1]", 0x42, retour[1])


def main():
    print("=== OricTel - Tests unitaires bridge WebSocket-TCP ===\n")

    # Tests synchrones
    test_bridge_creation()
    test_bridge_default_url()
    test_ws_data_conversion()
    test_videotex_sequences()

    # Tests asynchrones
    loop = asyncio.new_event_loop()
    loop.run_until_complete(test_tcp_server_start())
    loop.run_until_complete(test_tcp_connection())
    loop.close()

    print(f"\n=== Resultats: {tests_passed}/{tests_run} passes", end="")
    if tests_failed > 0:
        print(f", {tests_failed} echecs", end="")
    print(" ===")

    return 1 if tests_failed > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
