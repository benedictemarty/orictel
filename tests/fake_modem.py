#!/usr/bin/env python3
"""fake_modem.py - faux modem Hayes sur TCP pour tests/test_carrier.sh.

Phosphoric se connecte ici (--serial tcp:127.0.0.1:PORT). On repond comme un
PicoWiFiModemUSB (ATZ -> OK, ATI -> "CONNECTED TO WIFI", ATDT -> CONNECT), on
envoie une petite page Videotex, puis NC_AFTER secondes apres le CONNECT un
"\r\nNO CARRIER\r\n" suivi du SILENCE : c'est la perte de porteuse a
confirmer. Usage : fake_modem.py PORT NC_AFTER
"""
import socket
import sys
import time

port = int(sys.argv[1])
nc_after = float(sys.argv[2])

srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", port))
srv.listen(1)
srv.settimeout(60)
try:
    c, _ = srv.accept()
except socket.timeout:
    sys.exit("fake_modem: aucune connexion")
c.settimeout(0.05)


def send(s):
    c.sendall(s.encode("latin-1"))


buf = b""
t_conn = None
sent_nc = False
while True:
    try:
        d = c.recv(64)
        if not d:
            break
        buf += d
    except socket.timeout:
        pass
    while b"\r" in buf:
        line, buf = buf.split(b"\r", 1)
        line = line.strip().upper()
        if not line:
            continue
        if line.startswith(b"ATZ"):
            send("\r\nOK\r\n")
        elif line.startswith(b"ATI"):
            send("\r\nWiFi status: CONNECTED TO WIFI\r\nOK\r\n")
        elif line.startswith(b"ATDT"):
            send("\r\nCONNECT\r\n")
            t_conn = time.time()
            send("\x0c\x1f\x41\x41PAGE DE TEST\x1f\x43\x41Bonjour")
        elif line.startswith(b"ATH"):
            send("\r\nOK\r\n")
    if t_conn is not None and not sent_nc and time.time() - t_conn > nc_after:
        send("\r\nNO CARRIER\r\n")
        sent_nc = True
