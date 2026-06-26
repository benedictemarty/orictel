/**
 * @file test_ui.c
 * @brief Tests host des helpers UI (ui.c) — verrouille les bornes de saisie.
 *
 * Cible en particulier la non-regression des findings revue #2-#5 (ecritures
 * hors borne dans ui_print et les saisies) : on verifie qu'AUCUNE ecriture ne
 * deborde au-dela de la colonne VTX_COLS-1 ni sur la ligne suivante.
 *
 *   gcc -Wall -Wextra -Isrc -o build/test_ui tests/test_ui.c src/ui.c
 */

#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "keyboard.h"   /* KEY_* */

/* --- clavier scripte (remplace keyboard_scan) --- */
static unsigned char keyq[80];
static int keyn, keyi;
static void key_feed(const unsigned char* k, int n) { memcpy(keyq, k, n); keyn = n; keyi = 0; }
unsigned char keyboard_scan(void) { return keyi < keyn ? keyq[keyi++] : KEY_NONE; }

/* --- rendu neutralise --- */
void display_render_all(vtx_context_t* c) { (void)c; }

/* --- harnais --- */
static int run, pass;
#define CHECK(c, name) do {                                   \
    ++run;                                                    \
    if (c) { ++pass; printf("ok   : %s\n", name); }           \
    else   { printf("FAIL : %s\n", name); }                   \
} while (0)

static vtx_context_t ctx;

int main(void)
{
    printf("=== OricTel - Tests helpers UI (bornes saisie) ===\n\n");

    /* --- ui_print: clip a la largeur ecran (#2) --- */
    memset(&ctx, 0, sizeof ctx);
    ui_print(&ctx, 5, 30, "ABCDEFGHIJKLMNOPQRST", VTX_YELLOW);  /* 20 car. a col 30 */
    CHECK(ctx.screen[5][39].ch == 'J', "ui_print: derniere cellule = col 39 ('J')");
    CHECK(ctx.screen[6][0].ch == 0,    "ui_print: pas de debordement ligne suivante");

    /* --- ui_text_input: longueur bornee par VTX_COLS-col (#3/#4) --- */
    {
        static char buf[40];
        unsigned char k[40];
        int i;
        for (i = 0; i < 30; ++i) k[i] = 'X';            /* 30 X (> 26 admissibles) */
        k[30] = KEY_FUNC_FLAG | KEY_ENVOI;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 31);
        i = ui_text_input(&ctx, 7, 14, buf, sizeof buf, 0);  /* col 14 -> maxlen 26 */
        CHECK(i == 26,                       "ui_text_input: longueur bornee a VTX_COLS-col (26)");
        CHECK(ctx.screen[7][39].ch == 'X',   "ui_text_input: derniere cellule = col 39");
        CHECK(ctx.screen[8][0].ch == 0,      "ui_text_input: pas de debordement ligne suivante");
        CHECK(buf[26] == 0 && strlen(buf) == 26, "ui_text_input: buf borne et termine");
    }

    /* --- ANNULATION -> 0xFF --- */
    {
        static char buf[40];
        unsigned char k[2];
        k[0] = 'A'; k[1] = KEY_FUNC_FLAG | KEY_ANNULATION;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 2);
        CHECK(ui_text_input(&ctx, 7, 3, buf, sizeof buf, 0) == 0xFF, "ANNULATION -> 0xFF");
    }

    /* --- CORRECTION efface le dernier caractere --- */
    {
        static char buf[40];
        unsigned char k[4];
        k[0] = 'A'; k[1] = 'B';
        k[2] = KEY_FUNC_FLAG | KEY_CORRECTION;
        k[3] = KEY_FUNC_FLAG | KEY_ENVOI;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 4);
        CHECK(ui_text_input(&ctx, 7, 3, buf, sizeof buf, 0) == 1 &&
              buf[0] == 'A' && buf[1] == 0, "CORRECTION efface le dernier caractere");
    }

    /* --- masque '*' a l'ecran, vrai texte dans buf --- */
    {
        static char buf[40];
        unsigned char k[3];
        k[0] = 'S'; k[1] = 'E'; k[2] = KEY_FUNC_FLAG | KEY_ENVOI;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 3);
        ui_text_input(&ctx, 7, 3, buf, sizeof buf, '*');
        CHECK(buf[0] == 'S' && buf[1] == 'E' &&
              ctx.screen[7][3].ch == '*' && ctx.screen[7][4].ch == '*',
              "masque '*' a l'ecran, vrai texte conserve dans buf");
    }

    printf("\n=== Resultats: %d/%d passes ===\n", pass, run);
    return (pass == run) ? 0 : 1;
}
