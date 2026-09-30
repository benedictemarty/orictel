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
    /* ESC est la touche de SORTIE locale (quitter la session / retour menu),
     * pas l'ANNULATION Minitel, qui reste sur CTRL+A. */
    { unsigned char s[] = {0x1B}; CHECK(scan1(s,1) == KEY_LOCAL_ESCAPE,               "ESC -> sortie locale (pas ANNULATION)"); }
    { unsigned char s[] = {0x01}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_ANNULATION), "CTRL+A -> ANNULATION"); }
    { unsigned char s[] = {0x7F}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_CORRECTION), "DELETE -> CORRECTION"); }
    { unsigned char s[] = {0x0B}; CHECK(scan1(s,1) == KEY_ARROW_UP,                   "Fleche HAUT ($0B) -> KEY_ARROW_UP"); }
    { unsigned char s[] = {0x0A}; CHECK(scan1(s,1) == KEY_ARROW_DOWN,                 "Fleche BAS ($0A) -> KEY_ARROW_DOWN"); }
    { unsigned char s[] = {0x08}; CHECK(scan1(s,1) == KEY_ARROW_LEFT,                 "BS -> fleche gauche"); }
    { unsigned char s[] = {0x09}; CHECK(scan1(s,1) == KEY_ARROW_RIGHT,               "0x09 (code ROM) -> fleche droite"); }
    { unsigned char s[] = {0x15}; CHECK(scan1(s,1) == KEY_ARROW_RIGHT,               "0x15 (alias) -> fleche droite"); }

    /* --- CTRL+lettre -> touche fonction --- */
    { unsigned char s[] = {0x01}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_ANNULATION), "CTRL+A -> ANNULATION"); }
    { unsigned char s[] = {0x13}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_SOMMAIRE),   "CTRL+S -> SOMMAIRE"); }
    { unsigned char s[] = {0x0E}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_SUITE),      "CTRL+N -> SUITE"); }
    { unsigned char s[] = {0x12}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_REPETITION), "CTRL+R -> REPETITION (usage Minitel)"); }
    { unsigned char s[] = {0x05}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_REPETITION), "CTRL+E -> REPETITION (alias)"); }

    /* --- CTRL locaux (non envoyes au serveur) --- */
    { unsigned char s[] = {0x04}; CHECK(scan1(s,1) == KEY_TOGGLE_RENDER, "CTRL+D -> toggle rendu"); }
    { unsigned char s[] = {0x0C}; CHECK(scan1(s,1) == KEY_LOCAL_CLEAR,   "CTRL+L -> clear local"); }
    { unsigned char s[] = {0x06}; CHECK(scan1(s,1) == KEY_LOCAL_RESET,   "CTRL+F -> reset ACIA"); }

    /* --- CTRL non mappe -> NONE (avale, pas d'emission parasite) --- */
    { unsigned char s[] = {0x02}; CHECK(scan1(s,1) == KEY_NONE, "CTRL+B (non mappe) -> NONE"); }

    /* --- ASCII normal passe tel quel --- */
    { unsigned char s[] = {'A'}; CHECK(scan1(s,1) == 'A', "ASCII 'A' passthrough"); }

    /* --- FUNCT (Atmos) : la ROM livre lettre | $80 (mesure Phosphoric) --- */
    { unsigned char s[] = {'R'|0x80}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_RETOUR),    "FUNCT+R -> RETOUR"); }
    { unsigned char s[] = {'s'|0x80}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_SOMMAIRE),  "FUNCT+s -> SOMMAIRE"); }
    { unsigned char s[] = {'C'|0x80}; CHECK(scan1(s,1) == (KEY_FUNC_FLAG|KEY_CONNEXION), "FUNCT+C -> CONNEXION"); }
    { unsigned char s[] = {'Z'|0x80}; CHECK(scan1(s,1) == KEY_NONE,                       "FUNCT+Z (non mappe) -> rien"); }

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
        CHECK(txn == 2 && tx[0] == SEP && tx[1] == KEY_RETOUR,
              "fleche gauche hors mode curseur -> SEP RETOUR");

        tx_reset();
        keyboard_process(&ctx, KEY_ARROW_RIGHT);    /* mode curseur OFF */
        CHECK(txn == 2 && tx[0] == SEP && tx[1] == KEY_SUITE,
              "fleche droite hors mode curseur -> SEP SUITE");

        ctx.kbd_cursor = 1;
        tx_reset();
        keyboard_process(&ctx, KEY_ARROW_LEFT);     /* mode curseur ON */
        CHECK(txn == 3 && tx[0] == 0x1B && tx[1] == 0x5B && tx[2] == 0x44,
              "fleche gauche (curseur) -> ESC [ D");

        tx_reset();
        keyboard_process(&ctx, KEY_ARROW_RIGHT);    /* mode curseur ON */
        CHECK(txn == 3 && tx[0] == 0x1B && tx[1] == 0x5B && tx[2] == 0x43,
              "fleche droite (curseur) -> ESC [ C");

        tx_reset();
        keyboard_process(&ctx, KEY_ARROW_UP);       /* mode curseur ON */
        CHECK(txn == 3 && tx[2] == 0x41, "fleche haut (curseur) -> ESC [ A");

        ctx.kbd_cursor = 0;
        tx_reset();
        keyboard_process(&ctx, KEY_ARROW_UP);
        CHECK(txn == 2 && tx[0] == SEP && tx[1] == KEY_RETOUR, "fleche haut -> SEP RETOUR");
        tx_reset();
        keyboard_process(&ctx, KEY_ARROW_DOWN);
        CHECK(txn == 2 && tx[0] == SEP && tx[1] == KEY_ENVOI, "fleche bas -> SEP ENVOI");
    }

    printf("\n=== Resultats: %d/%d passes ===\n", pass, run);
    return (pass == run) ? 0 : 1;
}
