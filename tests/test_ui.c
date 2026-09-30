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

    /* --- ESC annule aussi la saisie (touche de sortie universelle) --- */
    {
        static char buf[40];
        unsigned char k[2];
        k[0] = 'A'; k[1] = KEY_LOCAL_ESCAPE;
        memset(&ctx, 0, sizeof ctx);
        key_feed(k, 2);
        CHECK(ui_text_input(&ctx, 7, 3, buf, sizeof buf, 0) == 0xFF, "ESC -> 0xFF (saisie annulee)");
        CHECK(buf[0] == 0, "ESC -> tampon vide");
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

    /* ================= Charte des ecrans locaux (v0.3.23) ================= */

    /* --- bandeau : fond bleu rangees 1-2, titre double largeur en (2,2) --- */
    memset(&ctx, 0, sizeof ctx);
    ui_header(&ctx, "ORIC", "v1");
    CHECK(ctx.screen[1][0].bg == UI_BAND_BG && ctx.screen[2][39].bg == UI_BAND_BG,
          "ui_header : rangees 1-2 sur fond bleu, jusqu'aux bords");
    CHECK(ctx.screen[2][2].ch == 'O' && ctx.screen[2][4].ch == 'R' &&
          ctx.screen[2][2].size == SIZE_DOUBLE_WIDTH,
          "ui_header : titre en double largeur (une lettre toutes les 2 colonnes)");
    CHECK(ctx.screen[2][2].size != SIZE_DOUBLE_HEIGHT,
          "ui_header : pas de double hauteur (rangee rendue en brut sur Oric)");
    CHECK(ctx.screen[2][36].ch == 'v' && ctx.screen[2][37].ch == '1' &&
          ctx.screen[2][36].fg == VTX_YELLOW && ctx.screen[2][35].ch == ' ',
          "ui_header : texte de droite jaune, fin en col. 37, precede d'un espace");
    CHECK(ctx.screen[3][0].ch == ' ' && ctx.screen[3][1].ch == 0x60 &&
          ctx.screen[3][1].charset == CHARSET_G1 && ctx.screen[3][39].fg == VTX_CYAN,
          "ui_rule : filet G1 cyan des col. 1 a 39, col. 0 libre (attribut)");

    /* --- item non selectionne / selectionne --- */
    memset(&ctx, 0, sizeof ctx);
    ui_item(&ctx, 8, '3', "Rendu", "AUTO", 0);
    CHECK(ctx.screen[8][3].ch == '[' && ctx.screen[8][4].ch == '3' &&
          ctx.screen[8][5].ch == ']' && ctx.screen[8][4].fg == VTX_CYAN,
          "ui_item : [k] en cyan a la col. 3");
    CHECK(ctx.screen[8][7].ch == 'R' && ctx.screen[8][7].fg == VTX_YELLOW &&
          ctx.screen[8][6].ch == ' ', "ui_item : libelle jaune col. 7, precede d'un espace");
    CHECK(ctx.screen[8][12].ch == ' ' && ctx.screen[8][13].ch == '.' &&
          ctx.screen[8][20].ch == '.' && ctx.screen[8][21].ch == ' ',
          "ui_item : pointilles entre libelle et valeur, bordes d'espaces");
    CHECK(ctx.screen[8][22].ch == 'A' && ctx.screen[8][22].fg == VTX_WHITE,
          "ui_item : valeur blanche en col. 22");
    CHECK(ctx.screen[8][10].bg == VTX_BLACK, "ui_item non selectionne : fond noir");
    ui_item(&ctx, 8, '3', "Rendu", "AUTO", 1);
    CHECK(ctx.screen[8][0].bg == VTX_BLACK && ctx.screen[8][1].bg == UI_BAND_BG &&
          ctx.screen[8][38].bg == UI_BAND_BG && ctx.screen[8][39].bg == VTX_BLACK,
          "ui_item selectionne : fond bleu col. 1-38");
    CHECK(ctx.screen[8][1].ch == ' ' && ctx.screen[8][2].ch == ' ',
          "ui_item selectionne : 2 cases vides avant '[' (attributs fond + encre)");
    CHECK(ctx.screen[8][4].fg == VTX_WHITE && ctx.screen[8][7].fg == VTX_WHITE,
          "ui_item selectionne : encre blanche");
    ui_item(&ctx, 9, '1', "UnReseauAuNomTresLong", "cle", 0);
    CHECK(ctx.screen[9][27].ch == 'g' && ctx.screen[9][28].ch == ' ' &&
          ctx.screen[9][29].ch == 'c',
          "ui_item : libelle long non ecrase, valeur apres un espace");
    ui_item(&ctx, 8, '1', "Config WiFi", 0, 0);
    CHECK(ctx.screen[8][22].ch == ' ' && ctx.screen[8][1].bg == VTX_BLACK,
          "ui_item : rangee effacee avant redessin (valeur et fond de l'ancien)");

    /* --- pied de page --- */
    memset(&ctx, 0, sizeof ctx);
    ui_footer(&ctx, "gauche", "ESC");
    CHECK(ctx.screen[UI_ROW_FOOTRULE][1].ch == 0x60 &&
          ctx.screen[UI_ROW_FOOTER][2].ch == 'g' &&
          ctx.screen[UI_ROW_FOOTER][2].fg == VTX_GREEN &&
          ctx.screen[UI_ROW_FOOTER][37].ch == 'C',
          "ui_footer : filet, texte vert a gauche, texte a droite fin col. 37");

    /* --- navigation --- */
    {
        unsigned char sel = 0;
        CHECK(ui_nav(KEY_ARROW_DOWN, &sel, 5) == 1 && sel == 1, "ui_nav : bas -> item suivant");
        sel = 4;
        CHECK(ui_nav(KEY_ARROW_DOWN, &sel, 5) == 1 && sel == 0, "ui_nav : bas sur le dernier -> premier");
        CHECK(ui_nav(KEY_ARROW_UP, &sel, 5) == 1 && sel == 4, "ui_nav : haut sur le premier -> dernier");
        CHECK(ui_nav(KEY_FUNC_FLAG | KEY_ENVOI, &sel, 5) == 2 && sel == 4, "ui_nav : RETURN valide");
        CHECK(ui_nav(KEY_ARROW_RIGHT, &sel, 5) == 2, "ui_nav : fleche droite valide");
        CHECK(ui_nav(KEY_FUNC_FLAG | KEY_SUITE, &sel, 5) == 2, "ui_nav : SUITE valide");
        CHECK(ui_nav('x', &sel, 5) == 0 && sel == 4, "ui_nav : autre touche ignoree");
    }

    printf("\n=== Resultats: %d/%d passes ===\n", pass, run);
    return (pass == run) ? 0 : 1;
}
