/**
 * @file at_modem.h
 * @brief Primitives modem AT (Hayes) decouplees de l'UI et du rendu
 *
 * Ce module isole la logique protocole AT (envoi de commandes, attente de
 * reponses) du reste de l'application :
 *
 *  - Il ne depend que de l'interface serie (serial.h). Aucun appel au
 *    moteur de rendu : la reception ne declenche AUCUN display_*, ce qui
 *    elimine l'overrun du 6551 reel (registre RX 1 octet, pas de FIFO)
 *    qui se produisait quand un rendu HIRES de plusieurs ms s'intercalait
 *    en pleine rafale (bug remonte par Dbug, forum Defence-Force).
 *
 *  - L'affichage de debug (trace des octets recus) passe par des hooks
 *    optionnels (at_set_trace) implementes par l'appelant. En production
 *    (DEBUG non defini) aucun hook n'est pose : la phase AT est muette et
 *    sans rendu.
 *
 *  - Le decouplage rend la machine d'etats testable sur l'hote (TEST_HOST)
 *    avec un faux modem en memoire, sans materiel ni emulateur.
 *
 * Le matcher de reponse est ANCRE SUR LES LIGNES (delimiteurs CR/LF) et non
 * en sous-chaine : un SSID ou un echo de commande contenant "OK"/"CONNECT"
 * ne provoque plus de faux positif. Conforme aux reponses Hayes, toujours
 * delimitees par CR/LF (ex: "\r\nOK\r\n", "\r\nCONNECT 9600\r\n").
 */

#ifndef AT_MODEM_H
#define AT_MODEM_H

#ifdef TEST_HOST
#define __fastcall__
#endif

/* Hooks de trace optionnels (NULL = pas de trace).
 *  - on_byte : appele pour chaque octet recu (affichage debug).
 *  - on_idle : appele dans un creux de reception (ligne au repos), seul
 *    moment ou un rendu est sans danger pour l'overrun. */
typedef void (*at_trace_byte_fn)(unsigned char b);
typedef void (*at_idle_fn)(void);
void at_set_trace(at_trace_byte_fn on_byte, at_idle_fn on_idle);

/* Envoie "str" puis CR. Flush bloquant (phase sans reception attendue). */
void at_send(const char* str);

/* Envoie "prefix" puis "value" puis CR (ex: AT$SSID=MonReseau). */
void at_send_kv(const char* prefix, const char* value);

/* Attend une ligne commencant par "keyword" (ancrage CR/LF), jusqu'a
 * timeout_ms. Retourne 1 si trouve, 0 sur timeout. Aucun rendu. */
unsigned char at_wait_response(const char* keyword, unsigned int timeout_ms);

/* Interroge le PicoWiFiModemUSB (ATI) jusqu'a voir "TO WIFI" (WiFi associe,
 * IP prete) ou epuiser timeout_ms. Retourne 1 si l'IP est prete, 0 sinon.
 *
 * Best-effort : l'appelant (modem_connect) IGNORE volontairement le retour
 * et compose quand meme - sur firmware deja connecte ATI peut ne pas
 * reporter le marqueur attendu, et le ATDT etablira la connexion de toute
 * facon. Le retour sert a la page Config WiFi pour confirmer l'association. */
unsigned char at_wait_ip(unsigned int timeout_ms);

#endif /* AT_MODEM_H */
