/**
 * @file test_keyboard.c
 * @brief Tests host du mapping clavier Oric -> Minitel (keyboard.c)
 *
 * kbhit()/cgetc() sont remplaces par un clavier SCRIPTE en memoire, et
 * serial_send()/vtx_process() sont captures/neutralises, pour valider sans
 * materiel :
 *   - keyboard_scan()    : touches speciales, CTRL+lettre, CTRL locaux,
 *                          FUNCT (Atmos), ASCII normal, combinaisons inconnues ;
 *   - keyboard_process() : emission SEP+code des touches fonction, passage des
 *                          ASCII, fleches curseur (actives seulement en mode
 *                          curseur PRO3).
 *
 *   gcc -Wall -Wextra -Isrc -DTEST_HOST -o build/test_keyboard \
 *       tests/test_keyboard.c src/keyboard.c
 */

#include <stdio.h>
#include "keyboard.h"

/* ------------------------------------------------------------------ */
/*  Clavier scripte (remplace la conio cc65)                           */
/* ------------------------------------------------------------------ */
static const unsigned char* kb_in;
static int kb_n, kb_i;
static void kb_feed(const unsigned char* s, int n) { kb_in = s; kb_n = n; kb_i = 0; }
int kbhit(void) { return kb_i < kb_n; }
int cgetc(void) { return kb_i < kb_n ? kb_in[kb_i++] : -1; }

/* ------------------------------------------------------------------ */
/*  Capture serie + stub vtx_process                                   */
/* ------------------------------------------------------------------ */
static unsigned char tx[64];
static int txn;
static void tx_reset(void) { txn = 0; }
void serial_send(unsigned char b) { if (txn < (int)sizeof tx) tx[txn++] = b; }
void vtx_process(vtx_context_t* c, unsigned char b) { (void)c; (void)b; }

/* ------------------------------------------------------------------ */
/*  Harnais                                                            */
/* ------------------------------------------------------------------ */
static int run, pass;
#define CHECK(c, name) do {                                   \
    ++run;                                                    \
    if (c) { ++pass; printf("ok   : %s\n", name); }           \
    else   { printf("FAIL : %s\n", name); }                   \
} while (0)

/* Scanne UNE touche logique a partir d'un buffer scripte. Re-scanne tant que
 * keyboard_scan rend KEY_NONE avec des octets restants (cas FUNCT: 0x09
 * consomme puis la lettre). */
static unsigned char scan1(const unsigned char* s, int n)
{
    unsigned char k;
    kb_feed(s, n);
    keyboard_init();
    do { k = keyboard_scan(); } while (k == KEY_NONE && kbhit());
    return k;
}

int main(void)
{
    printf("=== OricTel - Tests mapping clavier ===\n\n");

    /* --- keyboard_scan: touches speciales --- */
    { unsigned char s[] = {0x0D}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_ENVOI),      "RETURN -> ENVOI"); }
    { unsigned char s[] = {0x1B}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_ANNULATION), "ESC -> ANNULATION"); }
    { unsigned char s[] = {0x7F}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_CORRECTION), "DELETE -> CORRECTION"); }
    { unsigned char s[] = {0x0B}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_RETOUR),     "Fleche HAUT -> RETOUR"); }
    { unsigned char s[] = {0x08}; CHECK(scan1(s,1) == KEY_ARROW_LEFT,                 "BS -> fleche gauche"); }
    { unsigned char s[] = {0x15}; CHECK(scan1(s,1) == KEY_ARROW_RIGHT,               "0x15 -> fleche droite"); }

    /* --- CTRL+lettre -> touche fonction --- */
    { unsigned char s[] = {0x01}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_ANNULATION), "CTRL+A -> ANNULATION"); }
    { unsigned char s[] = {0x13}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_SOMMAIRE),   "CTRL+S -> SOMMAIRE"); }
    { unsigned char s[] = {0x0E}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_SUITE),      "CTRL+N -> SUITE"); }

    /* --- CTRL locaux (non envoyes au serveur) --- */
    { unsigned char s[] = {0x04}; CHECK(scan1(s,1) == KEY_TOGGLE_RENDER, "CTRL+D -> toggle rendu"); }
    { unsigned char s[] = {0x0C}; CHECK(scan1(s,1) == KEY_LOCAL_CLEAR,   "CTRL+L -> clear local"); }
    { unsigned char s[] = {0x06}; CHECK(scan1(s,1) == KEY_LOCAL_RESET,   "CTRL+F -> reset ACIA"); }

    /* --- CTRL non mappe -> NONE (avale, pas d'emission parasite) --- */
    { unsigned char s[] = {0x02}; CHECK(scan1(s,1) == KEY_NONE, "CTRL+B (non mappe) -> NONE"); }

    /* --- ASCII normal passe tel quel --- */
    { unsigned char s[] = {'A'}; CHECK(scan1(s,1) == 'A', "ASCII 'A' passthrough"); }

    /* --- FUNCT (Atmos): Tab 0x09 puis lettre --- */
    { unsigned char s[] = {0x09,'R'}; CHECK(scan1(s,2) == (KEY_FUNC_FLAG|KEY_RETOUR),    "FUNCT+R -> RETOUR"); }
    { unsigned char s[] = {0x09,'C'}; CHECK(scan1(s,2) == (KEY_FUNC_FLAG|KEY_CONNEXION), "FUNCT+C -> CONNEXION"); }
    { unsigned char s[] = {0x09,'Z'}; CHECK(scan1(s,2) == 'Z',                            "FUNCT+Z (non mappe) -> 'Z'"); }

    /* --- keyboard_process: emission vers le modem --- */
    {
        static vtx_context_t ctx;
        ctx.aiguillages = AIG_KBD_TO_MDM;   /* clavier -> modem (serie) */
        ctx.kbd_cursor = 0;

        tx_reset();
        keyboard_process(&ctx, KEY_FUNC_FLAG | KEY_ENVOI);
        CHECK(txn == 2 && tx[0] == SEP && tx[1] == KEY_ENVOI, "process ENVOI -> SEP + 0x41");

        tx_reset();
        keyboard_process(&ctx, 'a');
        CHECK(txn == 1 && tx[0] == 'a', "process 'a' -> 'a'");

        tx_reset();
        keyboard_process(&ctx, KEY_NONE);
        CHECK(txn == 0, "process KEY_NONE -> rien");

        tx_reset();
        keyboard_process(&ctx, KEY_ARROW_LEFT);     /* mode curseur OFF */
        CHECK(txn == 0, "fleche gauche hors mode curseur -> rien");

        ctx.kbd_cursor = 1;
        tx_reset();
        keyboard_process(&ctx, KEY_ARROW_LEFT);     /* mode curseur ON */
        CHECK(txn == 3 && tx[0] == 0x1B && tx[1] == 0x5B && tx[2] == 0x44,
              "fleche gauche (curseur) -> ESC [ D");
    }

    printf("\n=== Resultats: %d/%d passes ===\n", pass, run);
    return (pass == run) ? 0 : 1;
}
