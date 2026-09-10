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
static int auto_reply;           /* 1 = faux modem REACTIF (repond aux commandes) */
static unsigned char tx_cap[512];
static int tx_len;

static void modem_reset(void) { rx_head = 0; rx_len = 0; tx_len = 0; rx_infinite = 0; auto_reply = 0; }
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
/* Faux modem REACTIF (auto_reply) : il repond a ce qu'on lui envoie, au lieu
 * d'avoir sa reponse pre-remplie. Indispensable pour at_hangup, qui DRAINE le
 * flux en cours avant d'emettre "+++" : une reponse pre-remplie serait avalee
 * par ce drain, ce que le vrai modem ne fait pas (il repond apres). */
void serial_send(unsigned char b)
{
    if (tx_len < (int)sizeof(tx_cap)) tx_cap[tx_len++] = b;
    if (!auto_reply) return;
    /* "+++" (sans CR) -> echo + passage en mode commande */
    if (b == '+' && tx_len >= 3 &&
        tx_cap[tx_len - 2] == '+' && tx_cap[tx_len - 3] == '+') {
        rx_push("+++\r\nOK\r\n");
    }
    /* "ATH" + CR -> raccrochage (forme PicoWiFiModemUSB) */
    if (b == 0x0D && tx_len >= 4 &&
        tx_cap[tx_len - 2] == 'H' && tx_cap[tx_len - 3] == 'T') {
        rx_push("\r\nNO CARRIER (00:05:46)\r\n");
    }
}
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

    /* 14. at_hangup: modem reste EN LIGNE (le flux Videotex arrive au lieu
     *     des reponses AT). L'echappement Hayes doit emettre "+++" SANS CR
     *     puis "ATH"+CR, et signaler le mode commande atteint sur "OK". */
    modem_reset();
    auto_reply = 1;                                   /* modem qui repond vraiment */
    rx_push("\x1b[Hdonnees videotex en cours\r\n");   /* flux residuel a drainer */
    CHECK(at_hangup() == 1, "at_hangup: mode commande atteint (OK apres +++)");
    CHECK(tx_len == 7 &&
          tx_cap[0] == '+' && tx_cap[1] == '+' && tx_cap[2] == '+' &&
          tx_cap[3] == 'A' && tx_cap[4] == 'T' && tx_cap[5] == 'H' &&
          tx_cap[6] == 0x0D,
          "at_hangup: '+++' SANS CR puis 'ATH'+CR");

    /* 15. at_hangup: aucun modem (rien ne repond) -> retour 0, mais le ATH
     *     est tout de meme emis (le modem peut etre passe en mode commande
     *     sans le confirmer de facon reconnaissable). */
    modem_reset();
    CHECK(at_hangup() == 0, "at_hangup: sans reponse -> 0");
    CHECK(tx_len == 7 && tx_cap[3] == 'A' && tx_cap[6] == 0x0D,
          "at_hangup: ATH emis meme sans OK");

    /* 16. ANTI-BLOCAGE: modem en ligne avec flux CONTINU (serveur qui
     *     n'arrete jamais d'emettre). Le drain initial est plafonne, la
     *     fonction DOIT rendre la main. Sans plafond, ce test bloquerait. */
    modem_reset();
    rx_infinite = 1;
    CHECK(at_hangup() == 0, "at_hangup: flux continu -> sortie bornee");
    rx_infinite = 0;
    /* ------------------------------------------------------------------ */
    /*  Surveillance de la porteuse (at_carrier_watch)                     */
    /* ------------------------------------------------------------------ */
    {
        const char* s1 = "\r\nNO CARRIER\r\n";
        int hit = 0, k;
        at_carrier_reset();
        for (k = 0; s1[k]; ++k) if (at_carrier_watch((unsigned char)s1[k])) hit = 1;
        CHECK(hit == 1, "carrier: ligne NO CARRIER reconnue");

        /* Ancrage sur les lignes : en MILIEU de ligne, aucune detection.
         * C'est ce qui evite qu'une page Videotex citant ces mots ne coupe
         * la session. */
        at_carrier_reset();
        hit = 0;
        s1 = "\r\nTAPEZ NO CARRIER POUR QUITTER\r\n";
        for (k = 0; s1[k]; ++k) if (at_carrier_watch((unsigned char)s1[k])) hit = 1;
        CHECK(hit == 0, "carrier: pas de faux positif en milieu de ligne");

        /* Une ligne qui COMMENCE par NO CARRIER est bien reconnue meme
         * suivie d'autre chose (forme "NO CARRIER (00:05:46)" du Pico). */
        at_carrier_reset();
        hit = 0;
        s1 = "\r\nNO CARRIER (00:05:46)\r\n";
        for (k = 0; s1[k]; ++k) if (at_carrier_watch((unsigned char)s1[k])) hit = 1;
        CHECK(hit == 1, "carrier: forme PicoWiFi avec duree reconnue");

        /* Sans delimiteur de fin, rien n'est conclu (ligne incomplete). */
        at_carrier_reset();
        hit = 0;
        s1 = "NO CARRIE";
        for (k = 0; s1[k]; ++k) if (at_carrier_watch((unsigned char)s1[k])) hit = 1;
        CHECK(hit == 0, "carrier: ligne incomplete ne declenche pas");

        /* at_carrier_reset purge l'etat en cours. */
        at_carrier_reset();
        at_carrier_watch('N'); at_carrier_watch('O');
        at_carrier_reset();
        hit = 0;
        s1 = " CARRIER\r";
        for (k = 0; s1[k]; ++k) if (at_carrier_watch((unsigned char)s1[k])) hit = 1;
        CHECK(hit == 0, "carrier: reset purge la ligne en cours");
    }


    printf("\n=== Resultats: %d/%d passes ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
