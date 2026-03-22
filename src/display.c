/**
 * @file display.c
 * @brief Moteur d'affichage TEXTE pour OricTel (v0.1)
 *
 * Mode texte 40x28 via acces direct a la RAM ecran ($BB80-$BFDF).
 * Pas de conio pour le rendu (trop lent), on ecrit directement.
 *
 * Organisation RAM ecran:
 *   Chaque ligne = 40 octets
 *   Ligne 0 = $BB80, Ligne 1 = $BBA8, etc.
 *   Octets avec bits 6+5 = 00 -> serial attributes (encre/fond)
 *   Octets $20-$7F -> caracteres affichables
 *
 * Pour la couleur: on utilise les serial attributes de l'Oric.
 * Le premier octet de chaque ligne definit l'encre par defaut.
 */

#include <string.h>
#include <conio.h>
#include "display.h"
#include "fonts.h"

/* Pointeur direct vers la RAM ecran texte */
#define SCRN ((unsigned char*)0xBB80)

/* Calcul adresse d'une position ecran */
#define SCRN_ADDR(col, row) (SCRN + (unsigned int)(row) * 40 + (col))

/* Table de conversion caractere Minitel -> caractere Oric */
/* La plupart sont identiques (ASCII), sauf les accents Minitel */
/* $7B=e' $7C=e` $7D=e^ $7E=u" $7F=a`  (pas de correspondance Oric) */

/* Attributs serial Oric (premier octet valide de la ligne) */
/* Encre: $00-$07, Fond: $10-$17 */

/* ===================================================================
 *  Initialisation
 * =================================================================== */

void display_init(void)
{
    /* Effacer l'ecran via ROM (compatible Oric-1 et Atmos) */
    clrscr();

    /* Mettre l'encre blanche sur toutes les lignes */
    display_clear();
}

/* ===================================================================
 *  Effacement ecran
 * =================================================================== */

void display_clear(void)
{
    unsigned char row;
    unsigned char* ptr;

    for (row = 0; row < 28; ++row) {
        ptr = SCRN_ADDR(0, row);
        /* Octet 0: attribut encre blanche */
        ptr[0] = 0x07;  /* Ink white */
        /* Remplir le reste avec des espaces */
        memset(ptr + 1, ' ', 39);
    }
}

/* ===================================================================
 *  Rendu d'une cellule en mode texte
 *
 *  Limitation v0.1: pas de mosaiques G1 en mode texte.
 *  Les mosaiques sont affichees comme '#'.
 *  Les couleurs utilisent les serial attributes Oric.
 * =================================================================== */

static unsigned char vtx_to_oric_char(const vtx_cell_t* cell)
{
    unsigned char ch = cell->ch;

    /* Texte masque */
    if (cell->flags & ATTR_CONCEALED) {
        return ' ';
    }

    /* Mosaiques G1: afficher comme bloc ou '#' */
    if (cell->charset == CHARSET_G1) {
        /* Essayer de mapper les motifs G1 les plus courants */
        unsigned char pattern = ch & 0x3F;
        if (pattern == 0x00) return ' ';    /* Vide */
        if (pattern == 0x3F) return 0x7F;   /* Plein -> bloc plein Oric (DEL=bloc) */
        /* Blocs partiels: utiliser des caracteres semi-graphiques Oric */
        /* L'Oric n'a pas de vrais semi-graphiques, on approxime */
        if (pattern & 0x30) {               /* Bas rempli */
            if (pattern & 0x0F) return '#';
            return '_';
        }
        if (pattern & 0x0F) return '#';     /* Quelque chose de rempli */
        return ' ';
    }

    /* G2: fallback vers G0 */
    /* G0: la plupart des caracteres sont directement compatibles */

    /* Caracteres speciaux Minitel non disponibles en Oric standard */
    switch (ch) {
        case 0x7B: return '{';  /* e' -> { (placeholder) */
        case 0x7C: return '|';  /* e` -> | */
        case 0x7D: return '}';  /* e^ -> } */
        case 0x7E: return '~';  /* u" -> ~ */
        case 0x7F: return '`';  /* a` -> ` */
        default: break;
    }

    /* Verifier que c'est un caractere affichable */
    if (ch < 0x20 || ch > 0x7E) {
        return ' ';
    }

    return ch;
}

void display_render_cell(const vtx_cell_t* cell, unsigned char col, unsigned char row)
{
    unsigned char* ptr;
    unsigned char ch;

    if (row >= SCREEN_ROWS || col >= SCREEN_COLS) {
        return;
    }

    ptr = SCRN_ADDR(col, row);
    ch = vtx_to_oric_char(cell);

    /* Inversion video: bit 7 sur Oric inverse le caractere */
    if (cell->flags & ATTR_INVERT) {
        ch |= 0x80;
    }

    *ptr = ch;
}

/* ===================================================================
 *  Rendu incremental
 *
 *  Pour les couleurs: on place un attribut serial encre au debut
 *  de chaque ligne quand la couleur change. C'est une approximation
 *  car l'Oric ne supporte qu'un changement de couleur par colonne.
 * =================================================================== */

static void render_row(vtx_context_t* ctx, unsigned char row)
{
    unsigned char col;
    unsigned char* line;
    unsigned char prev_fg;

    if (row >= SCREEN_ROWS) return;

    line = SCRN_ADDR(0, row);
    prev_fg = VTX_WHITE;

    /* Premier octet: attribut encre de la premiere cellule */
    line[0] = ctx->screen[row][0].fg & 0x07;
    prev_fg = ctx->screen[row][0].fg;

    for (col = 1; col < SCREEN_COLS; ++col) {
        vtx_cell_t* cell = &ctx->screen[row][col];

        /* Si la couleur d'encre change, inserer un attribut */
        if (cell->fg != prev_fg && col < SCREEN_COLS - 1) {
            line[col] = cell->fg & 0x07;
            prev_fg = cell->fg;
            /* L'attribut consomme cette colonne */
            continue;
        }

        line[col] = vtx_to_oric_char(cell);

        /* Inversion */
        if (cell->flags & ATTR_INVERT) {
            line[col] |= 0x80;
        }
    }
}

void display_render(vtx_context_t* ctx)
{
    unsigned char row;

    for (row = 0; row < SCREEN_ROWS; ++row) {
        if (!ctx->dirty[row] && !ctx->full_refresh) {
            continue;
        }
        render_row(ctx, row);
        ctx->dirty[row] = 0;
    }

    ctx->full_refresh = 0;
}

/* ===================================================================
 *  Barre de statut (ligne 26)
 * =================================================================== */

void display_status(const char* msg)
{
    unsigned char i;
    unsigned char* line = SCRN_ADDR(0, STATUS_ROW);

    /* Attribut: encre jaune */
    line[0] = 0x03;  /* Ink yellow */

    for (i = 1; i < 40; ++i) {
        if (*msg) {
            line[i] = *msg++;
        } else {
            line[i] = ' ';
        }
    }
}

/* ===================================================================
 *  Curseur visuel
 * =================================================================== */

void display_cursor(unsigned char visible, unsigned char col, unsigned char row)
{
    unsigned char* ptr;

    if (row >= SCREEN_ROWS || col >= SCREEN_COLS) {
        return;
    }

    ptr = SCRN_ADDR(col, row);

    if (visible) {
        /* Inverser le caractere pour simuler un curseur */
        *ptr ^= 0x80;
    }
}
