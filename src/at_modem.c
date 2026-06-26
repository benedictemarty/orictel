/**
 * @file at_modem.c
 * @brief Implementation des primitives modem AT (voir at_modem.h)
 */

#include "at_modem.h"
#include "serial.h"

/* Taille du tampon de ligne pour le matcher ancre. Une reponse Hayes ou
 * une ligne ATI tient tres largement dedans ; au-dela on tronque (le
 * matcher prefixe/sous-chaine reste correct sur le debut de ligne). */
#define AT_LINE_MAX 48

/* --- Hooks de trace (NULL par defaut: aucune trace, aucun rendu) --------- */
static at_trace_byte_fn s_on_byte;
static at_idle_fn       s_on_idle;

void at_set_trace(at_trace_byte_fn on_byte, at_idle_fn on_idle)
{
    s_on_byte = on_byte;
    s_on_idle = on_idle;
}

/* --- Delai ~N ms ---------------------------------------------------------
 * Sur cible Oric (1 MHz) : boucle calibree en assembleur (label unique pour
 * ne pas entrer en collision avec delay_ms de main.c a l'edition de liens).
 * Sur hote (TEST_HOST) : no-op, le faux modem repond immediatement. */
#ifdef TEST_HOST
static void at_delay_ms(unsigned int ms) { (void)ms; }
#else
static void at_delay_ms(unsigned int ms)
{
    unsigned int i;
    for (i = 0; i < ms; ++i) {
        __asm__("ldx #$C8");
        __asm__("_atd_lp: dex");
        __asm__("bne _atd_lp");
    }
}
#endif

/* --- Petits utilitaires de comparaison (pas de <string.h> sur cible) ----- */

/* 1 si "line" commence par "prefix". */
static unsigned char str_prefix(const char* line, const char* prefix)
{
    while (*prefix) {
        if (*line != *prefix) return 0;
        ++line;
        ++prefix;
    }
    return 1;
}

/* 1 si "needle" apparait quelque part dans "line". */
static unsigned char str_contains(const char* line, const char* needle)
{
    const char* l;
    const char* n;
    for (; *line; ++line) {
        l = line;
        n = needle;
        while (*n && *l == *n) { ++l; ++n; }
        if (!*n) return 1;
    }
    return 0;
}

/* --- Emission ------------------------------------------------------------ */

void at_send(const char* str)
{
    while (*str) {
        serial_send(*str);
        ++str;
    }
    serial_send(0x0D);  /* CR */
    serial_tx_flush();
}

void at_send_kv(const char* prefix, const char* value)
{
    while (*prefix) { serial_send(*prefix); ++prefix; }
    while (*value)  { serial_send(*value);  ++value; }
    serial_send(0x0D);  /* CR */
    serial_tx_flush();
}

/* --- Attente de reponse (matcher ancre sur les lignes) ------------------- */

unsigned char at_wait_response(const char* keyword, unsigned int timeout_ms)
{
    unsigned int  elapsed = 0;
    unsigned char pending = 0;          /* octets recus, rendu differe au creux */
    static char   line[AT_LINE_MAX];    /* ligne courante (BSS, pas la pile) */
    unsigned char lp = 0;

    while (elapsed < timeout_ms) {
        if (serial_poll()) {
            /* Drainer la rafale A PLEINE VITESSE: aucun rendu ici (le 6551
             * reel n'a qu'un registre RX 1 octet, un rendu perdrait des
             * octets par overrun). Le rendu eventuel (debug) est differe au
             * creux via le hook idle. */
            do {
                unsigned char b = serial_recv();
                if (s_on_byte) s_on_byte(b);
                if (b == 0x0D || b == 0x0A) {
                    line[lp] = 0;
                    if (lp && str_prefix(line, keyword)) {
                        return 1;
                    }
                    lp = 0;
                } else if (lp < AT_LINE_MAX - 1) {
                    line[lp++] = b;
                }
            } while (serial_poll());
            pending = 1;
        } else {
            if (pending) {              /* creux: ligne au repos, rendu sur */
                if (s_on_idle) s_on_idle();
                pending = 0;
            }
            at_delay_ms(10);
            elapsed += 10;
        }
    }
    return 0;
}

/* --- Attente d'IP WiFi (PicoWiFiModemUSB via ATI) ------------------------ */

unsigned char at_wait_ip(unsigned int timeout_ms)
{
    static const char S_UP[] = "TO WIFI";   /* "...CONNECTED TO WIFI" */
    unsigned int elapsed = 0;

    while (elapsed < timeout_ms) {
        unsigned char up = 0;           /* "TO WIFI" vu => WiFi associe (IP) */
        unsigned char done = 0;         /* "OK" final (fin de reponse ATI) */
        unsigned char vocab = 0;        /* reponse ATI mentionne le WiFi/connexion */
        unsigned char pending = 0;
        unsigned int  rwait = 0;        /* budget lecture d'une reponse ATI */
        static char   line[AT_LINE_MAX];
        unsigned char lp = 0;

        at_send("ATI");

        /* Lire la reponse ATI jusqu'au OK final (ou ~2s de silence).
         * Meme discipline que at_wait_response : drain sans rendu, rendu
         * differe aux creux. Matcher par ligne :
         *   - "TO WIFI" en SOUS-CHAINE (il est au milieu de la ligne
         *     "WiFi status: CONNECTED TO WIFI"),
         *   - "OK" en PREFIXE de ligne (terminateur de la reponse),
         *   - "WIFI"/"CONNECT" en SOUS-CHAINE => le modem a un sous-systeme
         *     WiFi (Pico, meme non encore associe : "...NOT CONNECTED").
         *     Son absence signale un modem SANS WiFi (backend Phosphoric
         *     --serial modem, modem Hayes generique) : inutile d'attendre. */
        for (;;) {
            if (serial_poll()) {
                do {
                    unsigned char b = serial_recv();
                    if (s_on_byte) s_on_byte(b);
                    if (b == 0x0D || b == 0x0A) {
                        line[lp] = 0;
                        if (str_contains(line, S_UP)) up = 1;
                        if (str_contains(line, "WIFI") ||
                            str_contains(line, "CONNECT")) vocab = 1;
                        if (str_prefix(line, "OK"))   done = 1;
                        lp = 0;
                    } else if (lp < AT_LINE_MAX - 1) {
                        line[lp++] = b;
                    }
                } while (serial_poll() && !done);
                pending = 1;
                if (done) break;
            } else {
                if (pending) {
                    if (s_on_idle) s_on_idle();
                    pending = 0;
                }
                if (rwait >= 2000) break;
                at_delay_ms(10);
                rwait += 10;
            }
        }

        if (up) return 1;               /* ATI signale "CONNECTED TO WIFI" */

        /* Modem sans sous-systeme WiFi: on n'attendra jamais d'IP, on sort
         * tout de suite (au lieu de re-sonder ATI jusqu'a timeout_ms). */
        if (!vocab) return 0;

        at_delay_ms(500);
        elapsed += rwait + 500;
    }
    return 0;
}
