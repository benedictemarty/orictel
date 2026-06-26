/**
 * @file test_atmodem.c
 * @brief Tests unitaires de la machine d'etats modem AT (at_modem.c)
 *
 * Compile et execute sur l'hote (pas sur 6502) avec un FAUX modem en
 * memoire (file RX pre-remplie, capture TX) :
 *   gcc -Wall -Wextra -Isrc -o build/test_atmodem \
 *       tests/test_atmodem.c src/at_modem.c -DTEST_HOST
 *
 * Couvre le chemin critique qui echouait sur materiel reel :
 *   - reponses OK / CONNECT / NO CARRIER / timeout,
 *   - matcher ANCRE sur les lignes (pas de faux positif sous-chaine),
 *   - drain d'une rafale continue sans rendu intercale (regression overrun),
 *   - detection d'IP WiFi via ATI ("TO WIFI").
 */

#include <stdio.h>
#include <string.h>
#include "at_modem.h"

/* ------------------------------------------------------------------ */
/*  Faux modem : file RX pre-remplie, capture TX                       */
/* ------------------------------------------------------------------ */
static unsigned char rx_q[2048];
static int rx_head;
static int rx_len;
static int rx_infinite;          /* 1 = flux ininterrompu (debit sans fin) */
static unsigned char tx_cap[512];
static int tx_len;

static void modem_reset(void) { rx_head = 0; rx_len = 0; tx_len = 0; rx_infinite = 0; }
static void rx_push(const char* s)
{
    while (*s && rx_len < (int)sizeof(rx_q)) rx_q[rx_len++] = (unsigned char)*s++;
}
static void rx_push_fill(char c, int n)
{
    while (n-- > 0 && rx_len < (int)sizeof(rx_q)) rx_q[rx_len++] = (unsigned char)c;
}

/* Primitives serie attendues par at_modem.c (stubs TEST_HOST).
 * En mode rx_infinite, serial_poll reste vrai et serial_recv debite un octet
 * neutre ('.') indefiniment: simule un serveur/parasite qui n'arrete jamais
 * d'emettre, pour valider que les drains AT restent bornes (anti-blocage). */
unsigned char serial_poll(void) { return rx_infinite || rx_head < rx_len; }
unsigned char serial_recv(void)
{
    if (rx_head < rx_len) return rx_q[rx_head++];
    return rx_infinite ? (unsigned char)'.' : 0xFF;
}
void serial_send(unsigned char b) { if (tx_len < (int)sizeof(tx_cap)) tx_cap[tx_len++] = b; }
void serial_tx_flush(void) {}

/* ------------------------------------------------------------------ */
/*  Trace : compte les octets vus et les appels "idle" (rendu)         */
/* ------------------------------------------------------------------ */
static int trace_bytes;
static int trace_idle;
static void on_byte(unsigned char b) { (void)b; ++trace_bytes; }
static void on_idle(void) { ++trace_idle; }

/* ------------------------------------------------------------------ */
/*  Harnais                                                            */
/* ------------------------------------------------------------------ */
static int tests_run, tests_passed;
#define CHECK(cond, name) do {                              \
    ++tests_run;                                            \
    if (cond) { ++tests_passed; printf("ok   : %s\n", name); }   \
    else      { printf("FAIL : %s\n", name); }              \
} while (0)

int main(void)
{
    printf("=== OricTel - Tests machine d'etats modem AT ===\n\n");

    /* 1. OK simple */
    modem_reset();
    rx_push("\r\nOK\r\n");
    CHECK(at_wait_response("OK", 1000) == 1, "OK detecte");

    /* 2. Timeout si rien ne vient */
    modem_reset();
    CHECK(at_wait_response("OK", 100) == 0, "timeout sans reponse");

    /* 3. CONNECT avec suffixe (prefixe de ligne, ex: 'CONNECT 9600') */
    modem_reset();
    rx_push("\r\nCONNECT 9600\r\n");
    CHECK(at_wait_response("CONNECT", 1000) == 1, "CONNECT 9600 (prefixe)");

    /* 4. NO CARRIER: en attendant CONNECT -> timeout (pas de faux match) */
    modem_reset();
    rx_push("\r\nNO CARRIER\r\n");
    CHECK(at_wait_response("CONNECT", 100) == 0, "NO CARRIER -> pas de CONNECT");

    /* 5. ANCRAGE: 'OK' en sous-chaine de ligne ne doit PAS matcher.
     *    (l'ancien matcher sous-chaine renvoyait un faux positif ici) */
    modem_reset();
    rx_push("blOK\r\n");
    CHECK(at_wait_response("OK", 100) == 0, "OK en sous-chaine ignore (ancrage)");

    /* 6. ANCRAGE: echo de commande contenant le mot-cle puis vraie reponse */
    modem_reset();
    rx_push("AT$SSID=MyHomeOK\r\n");   /* echo: ne doit pas matcher 'OK' */
    rx_push("\r\nOK\r\n");             /* vraie reponse */
    CHECK(at_wait_response("OK", 1000) == 1, "echo ignore, vrai OK detecte");

    /* 7. REGRESSION OVERRUN: longue rafale CONTINUE (serial_poll toujours
     *    vrai jusqu'au mot-cle) -> tout draine, mot-cle trouve, AUCUN appel
     *    'idle' (donc aucun rendu intercale en pleine reception). */
    modem_reset();
    trace_bytes = 0; trace_idle = 0;
    at_set_trace(on_byte, on_idle);
    rx_push_fill('.', 300);            /* bruit avant la reponse */
    rx_push("\r\nCONNECT\r\n");
    CHECK(at_wait_response("CONNECT", 2000) == 1, "rafale longue: CONNECT trouve");
    CHECK(trace_idle == 0, "rafale continue: aucun rendu intercale (anti-overrun)");
    CHECK(trace_bytes >= 300, "rafale continue: tous les octets draines");
    at_set_trace(0, 0);

    /* 8. at_wait_ip: 'CONNECTED TO WIFI' -> IP prete */
    modem_reset();
    rx_push("WiFi status: CONNECTED TO WIFI\r\nOK\r\n");
    CHECK(at_wait_ip(1000) == 1, "ATI: TO WIFI -> IP prete");
    CHECK(tx_len >= 3 && tx_cap[0] == 'A' && tx_cap[1] == 'T' && tx_cap[2] == 'I',
          "at_wait_ip a bien emis ATI");

    /* 9. at_wait_ip: 'NOT CONNECTED' -> pas d'IP (Pico associe en cours: le
     *    vocabulaire "CONNECT" est present, donc on continue d'attendre) */
    modem_reset();
    rx_push("Call status: NOT CONNECTED\r\nOK\r\n");
    CHECK(at_wait_ip(100) == 0, "ATI: NOT CONNECTED -> pas d'IP");

    /* 9b. at_wait_ip: modem SANS WiFi (ATI sans "WIFI"/"CONNECT", ex backend
     *     Phosphoric --serial modem) -> sortie immediate (retour 0) apres UNE
     *     seule sonde ATI, sans attendre les 15 s du timeout. */
    modem_reset();
    rx_push("PHOSPHORIC MODEM V1\r\nOK\r\n");
    CHECK(at_wait_ip(15000) == 0, "ATI sans WiFi -> sortie immediate (0)");
    CHECK(tx_len == 4 && tx_cap[0]=='A' && tx_cap[1]=='T' &&
          tx_cap[2]=='I' && tx_cap[3]==0x0D,
          "ATI sans WiFi -> une seule sonde (pas de boucle 15s)");

    /* 10. at_send: termine bien par CR */
    modem_reset();
    at_send("ATZ");
    CHECK(tx_len == 4 && tx_cap[0]=='A' && tx_cap[3]==0x0D, "at_send ajoute le CR");

    /* 11. at_send_kv: prefixe + valeur + CR */
    modem_reset();
    at_send_kv("AT$SSID=", "Home");
    CHECK(tx_len == 13 && tx_cap[12] == 0x0D, "at_send_kv: prefixe+valeur+CR");

    /* 12. ANTI-BLOCAGE (#1): flux continu SANS le mot-cle. Le drain do/while
     *     est plafonne (AT_DRAIN_BURST) et fait progresser le timeout -> la
     *     fonction DOIT retourner 0 (timeout) au lieu de boucler a l'infini.
     *     Sans le correctif, ce test ne se terminerait jamais. */
    modem_reset();
    rx_infinite = 1;
    CHECK(at_wait_response("OK", 100) == 0,
          "flux continu sans mot-cle -> timeout borne (anti-blocage)");
    rx_infinite = 0;

    /* 13. ANTI-BLOCAGE (#1) cote at_wait_ip: flux continu sans WiFi/OK ->
     *     sortie bornee (retour 0), pas de boucle infinie dans la sonde ATI. */
    modem_reset();
    rx_infinite = 1;
    CHECK(at_wait_ip(15000) == 0,
          "at_wait_ip: flux continu -> sortie bornee (anti-blocage)");
    rx_infinite = 0;

    printf("\n=== Resultats: %d/%d passes ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
