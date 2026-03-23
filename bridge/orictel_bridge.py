#!/usr/bin/env python3
"""
OricTel Bridge - Passerelle WebSocket <-> TCP pour Minitel

Relie l'emulateur Phosphoric (backend TCP ACIA) au serveur Minitel
accessible en WebSocket sur ws://3617.fr/ws.

Le bridge ecoute en TCP sur le port 3615 (configurable) et etablit
une connexion WebSocket vers le serveur Minitel. Les octets Videotex
sont relayes de maniere bidirectionnelle et transparente.

Usage:
    python3 orictel_bridge.py [--tcp-port PORT] [--ws-url URL]
"""

import asyncio
import argparse
import logging
import signal
import sys

try:
    import websockets
except ImportError:
    print("ERREUR: module 'websockets' requis.")
    print("Installer avec: pip3 install websockets")
    sys.exit(1)

# Configuration par defaut
DEFAULT_TCP_HOST = "127.0.0.1"
DEFAULT_TCP_PORT = 3615
DEFAULT_WS_URL = "ws://3617.fr/ws"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
log = logging.getLogger("orictel-bridge")


class Bridge:
    """Pont bidirectionnel entre une connexion TCP et un WebSocket."""

    def __init__(self, ws_url: str, pace_delay: float = 0.008):
        self.ws_url = ws_url
        self.pace_delay = pace_delay
        self.ws = None
        self.tcp_reader = None
        self.tcp_writer = None
        self.stats_rx = 0  # octets recus du WS (vers Oric)
        self.stats_tx = 0  # octets envoyes au WS (depuis Oric)

    async def handle_tcp_client(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ):
        """Gere une connexion TCP entrante depuis l'emulateur."""
        peer = writer.get_extra_info("peername")
        log.info("Emulateur connecte depuis %s", peer)

        self.tcp_reader = reader
        self.tcp_writer = writer

        try:
            # Connexion immediate au serveur Minitel via WebSocket
            # (pas de signal ready, compatible Digitelec et bridge)
            log.info("Connexion WebSocket vers %s ...", self.ws_url)
            async with websockets.connect(
                self.ws_url,
                subprotocols=["binary"],
                max_size=65536,
                ping_interval=30,
                ping_timeout=10,
            ) as ws:
                self.ws = ws
                log.info("WebSocket connecte a %s", self.ws_url)

                # Lancer les deux relais en parallele
                tcp_to_ws_task = asyncio.create_task(
                    self._relay_tcp_to_ws(reader, ws)
                )
                ws_to_tcp_task = asyncio.create_task(
                    self._relay_ws_to_tcp(ws, writer)
                )

                # Attendre que l'un des deux se termine
                done, pending = await asyncio.wait(
                    [tcp_to_ws_task, ws_to_tcp_task],
                    return_when=asyncio.FIRST_COMPLETED,
                )

                # Annuler l'autre
                for task in pending:
                    task.cancel()
                    try:
                        await task
                    except asyncio.CancelledError:
                        pass

                # Verifier les erreurs
                for task in done:
                    if task.exception():
                        log.error("Erreur relais: %s", task.exception())

        except websockets.exceptions.ConnectionClosed as e:
            log.warning("WebSocket ferme: %s", e)
        except ConnectionRefusedError:
            log.error("Impossible de se connecter a %s", self.ws_url)
        except Exception as e:
            log.error("Erreur bridge: %s", e)
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
            self.ws = None
            log.info(
                "Session terminee. RX: %d octets, TX: %d octets",
                self.stats_rx, self.stats_tx,
            )
            self.stats_rx = 0
            self.stats_tx = 0

    async def _relay_tcp_to_ws(
        self, reader: asyncio.StreamReader, ws
    ):
        """Relaie les octets TCP (Oric) vers le WebSocket (serveur Minitel)."""
        try:
            while True:
                data = await reader.read(1024)
                if not data:
                    log.info("TCP: connexion fermee par l'emulateur")
                    break
                self.stats_tx += len(data)
                # Envoyer comme message binaire au WebSocket
                await ws.send(data)
                if log.isEnabledFor(logging.DEBUG):
                    log.debug(
                        "TCP->WS: %s",
                        " ".join(f"{b:02X}" for b in data),
                    )
        except asyncio.CancelledError:
            raise
        except Exception as e:
            log.error("Erreur TCP->WS: %s", e)

    async def _relay_ws_to_tcp(self, ws, writer: asyncio.StreamWriter):
        """Relaie les octets WebSocket (serveur Minitel) vers TCP (Oric).

        Envoi a pleine vitesse TCP. Le FIFO emulateur (--serial-buffer 256)
        et l'ISR cote Oric absorbent le flux. Le main loop controle la
        vitesse d'affichage en lisant le buffer a rythme Minitel.
        """
        try:
            async for message in ws:
                if isinstance(message, str):
                    data = bytes(ord(c) & 0x7F for c in message)
                elif isinstance(message, bytes):
                    data = message
                else:
                    continue

                if not data:
                    continue

                self.stats_rx += len(data)
                writer.write(data)
                await writer.drain()

                if log.isEnabledFor(logging.DEBUG):
                    log.debug(
                        "WS->TCP: %d octets",
                        len(data), self.pace_delay * 1000,
                    )
        except asyncio.CancelledError:
            raise
        except websockets.exceptions.ConnectionClosed:
            log.info("WebSocket: connexion fermee par le serveur")
        except Exception as e:
            log.error("Erreur WS->TCP: %s", e)


async def main():
    parser = argparse.ArgumentParser(
        description="OricTel Bridge - Passerelle WebSocket <-> TCP pour Minitel"
    )
    parser.add_argument(
        "--tcp-host",
        default=DEFAULT_TCP_HOST,
        help=f"Adresse d'ecoute TCP (defaut: {DEFAULT_TCP_HOST})",
    )
    parser.add_argument(
        "--tcp-port",
        type=int,
        default=DEFAULT_TCP_PORT,
        help=f"Port d'ecoute TCP (defaut: {DEFAULT_TCP_PORT})",
    )
    parser.add_argument(
        "--ws-url",
        default=DEFAULT_WS_URL,
        help=f"URL du serveur WebSocket Minitel (defaut: {DEFAULT_WS_URL})",
    )
    parser.add_argument(
        "--pace",
        type=float,
        default=0.04,
        help="Delai entre octets WS->TCP en secondes (defaut: 0.04 = 40ms)",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Afficher les octets echanges (debug)",
    )
    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    bridge = Bridge(args.ws_url, pace_delay=args.pace)

    server = await asyncio.start_server(
        bridge.handle_tcp_client,
        args.tcp_host,
        args.tcp_port,
    )

    addr = server.sockets[0].getsockname()
    log.info("OricTel Bridge demarre sur %s:%d", addr[0], addr[1])
    log.info("En attente de connexion depuis l'emulateur Phosphoric...")
    log.info("Serveur Minitel cible: %s", args.ws_url)

    # Gestion propre de Ctrl+C
    loop = asyncio.get_running_loop()
    stop = loop.create_future()

    def signal_handler():
        if not stop.done():
            stop.set_result(None)

    for sig in (signal.SIGINT, signal.SIGTERM):
        loop.add_signal_handler(sig, signal_handler)

    async with server:
        try:
            await stop
        except asyncio.CancelledError:
            pass

    log.info("Bridge arrete.")


if __name__ == "__main__":
    asyncio.run(main())
