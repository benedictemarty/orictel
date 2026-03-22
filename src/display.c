/**
 * @file display.c
 * @brief Moteur d'affichage HIRES pour OricTel
 *
 * Rendu des cellules Videotex dans le framebuffer HIRES de l'Oric.
 * Mode HIRES: 240x200 pixels, organise en 40 colonnes de 6 pixels
 * sur 200 lignes. Chaque octet HIRES encode:
 *   - Bits 5-0: 6 pixels (bit 5 = gauche, bit 0 = droite)
 *   - Bit 6: attribut couleur/mode si bit 6+5 = 00 (serial attribute)
 *            sinon 1 = encre, 0 = fond
 *   - Bit 7: non utilise (toujours 0 pour video normale)
 *
 * Pour OricTel, on ecrit directement les pixels dans le framebuffer
 * en utilisant les attributs serie Oric pour la couleur.
 */

#include <string.h>
#include "display.h"
#include "fonts.h"

/* Pointeur vers le framebuffer HIRES */
#define HIRES   ((unsigned char*)0xA000)
#define TEXT    ((unsigned char*)0xBB80)

/* Table de correspondance couleur Oric -> attribut serial encre */
static const unsigned char ink_attr[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

/* Table de correspondance couleur Oric -> attribut serial fond */
static const unsigned char paper_attr[] = {
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17
};

/* ===================================================================
 *  Passage en mode HIRES
 * =================================================================== */

void display_init(void)
{
    /* Passage en HIRES via l'adresse de controle */
    /* Sur Oric: POKE $26A,#$1C pour HIRES via ROM, ou HIRES direct */
    /* En cc65, on utilise un inline asm ou acces direct */
    __asm__("lda #$1E");        /* HIRES + no text window */
    __asm__("sta $26A");        /* Variable systeme mode video */

    /* Activer HIRES: bit 3 du registre VIA pour le ULA */
    /* Methode standard: ecrire dans la zone HIRES pour forcer le mode */
    /* On utilise DOKE 618,$A000 equivalent */

    /* Effacer le framebuffer */
    display_clear();
}

void display_clear(void)
{
    /* Remplir le framebuffer HIRES avec fond noir */
    /* Chaque ligne de 40 octets: premier octet = attribut, suivants = pixels */
    unsigned char row;
    unsigned char* ptr;

    for (row = 0; row < 200; ++row) {
        ptr = HIRES + (unsigned int)row * 40;
        /* Premier octet: attribut serial encre blanche */
        *ptr = ink_attr[COLOR_WHITE];
        /* Remplir les 39 octets suivants avec des espaces (tous bits a 0 = fond) */
        memset(ptr + 1, 0x40, 39);  /* $40 = tous pixels off, bit 6 set */
    }
}

/* ===================================================================
 *  Rendu d'une cellule
 * =================================================================== */

void display_render_cell(const vtx_cell_t* cell, unsigned char col, unsigned char row)
{
    unsigned char* base;
    const unsigned char* glyph;
    unsigned char line;
    unsigned char fg, bg, ch;
    unsigned char pixel_byte;
    unsigned char invert;

    if (row >= SCREEN_ROWS || col >= SCREEN_COLS) {
        return;
    }

    /* Adresse de base dans le framebuffer HIRES */
    /* Chaque ligne ecran = 8 lignes pixels, chaque ligne pixel = 40 octets */
    base = HIRES + (unsigned int)(row * CHAR_H) * 40 + col;

    /* Recuperer le glyphe du caractere */
    ch = cell->ch;
    fg = cell->fg;
    bg = cell->bg;
    invert = (cell->flags & ATTR_INVERT) ? 1 : 0;

    /* Texte masque: afficher comme un espace */
    if (cell->flags & ATTR_CONCEALED) {
        ch = ' ';
    }

    /* Selectionner le bon jeu de caracteres */
    switch (cell->charset) {
        case CHARSET_G1:
            glyph = font_get_g1(ch);
            break;
        case CHARSET_G2:
            glyph = font_get_g2(ch);
            break;
        default:
            glyph = font_get_g0(ch);
            break;
    }

    /* Rendre les 8 lignes du caractere */
    for (line = 0; line < CHAR_H; ++line) {
        pixel_byte = glyph[line];

        /* Inverser si necessaire */
        if (invert) {
            pixel_byte ^= 0x3F;  /* Inverser les 6 bits de pixels */
        }

        /* Bit 6 = 1 pour que l'ULA interprete les bits comme pixels */
        pixel_byte |= 0x40;

        /* Ecrire dans le framebuffer */
        *(base + (unsigned int)line * 40) = pixel_byte;
    }
}

/* ===================================================================
 *  Rendu des attributs de couleur
 *
 *  L'Oric HIRES utilise des "serial attributes" dans le premier octet
 *  de certaines cellules pour changer encre/fond. C'est une contrainte
 *  forte: on ne peut pas avoir de couleur arbitraire par cellule.
 *
 *  Strategie simplifiee pour v0.1:
 *  - On utilise les 3 premieres colonnes de chaque ligne pixel
 *    pour placer les attributs encre et fond si necessaire.
 *  - Quand la couleur change, on insere un attribut serial.
 * =================================================================== */

static void render_row_colors(vtx_context_t* ctx, unsigned char row)
{
    unsigned char col;
    unsigned char prev_fg = COLOR_WHITE;
    unsigned char prev_bg = COLOR_BLACK;
    unsigned char* line_base;
    unsigned char pixel_line;

    for (pixel_line = 0; pixel_line < CHAR_H; ++pixel_line) {
        line_base = HIRES + (unsigned int)(row * CHAR_H + pixel_line) * 40;
        prev_fg = COLOR_WHITE;
        prev_bg = COLOR_BLACK;

        for (col = 0; col < SCREEN_COLS; ++col) {
            vtx_cell_t* cell = &ctx->screen[row][col];

            /* Si la couleur de fond change, inserer un attribut paper */
            if (cell->bg != prev_bg) {
                /* On insere l'attribut paper dans cette colonne */
                /* L'attribut consomme la cellule: pas de pixel affiche */
                *(line_base + col) = paper_attr[cell->bg];
                prev_bg = cell->bg;
                /* Le caractere sera perdu, mais c'est la contrainte HIRES Oric */
                continue;
            }

            /* Si la couleur d'encre change, inserer un attribut ink */
            if (cell->fg != prev_fg) {
                *(line_base + col) = ink_attr[cell->fg];
                prev_fg = cell->fg;
                continue;
            }
        }
    }
}

/* ===================================================================
 *  Rendu incremental (dirty rectangles par ligne)
 * =================================================================== */

void display_render(vtx_context_t* ctx)
{
    unsigned char row, col;

    for (row = 0; row < SCREEN_ROWS; ++row) {
        if (!ctx->dirty[row] && !ctx->full_refresh) {
            continue;
        }

        /* Rendre toutes les cellules de cette ligne */
        for (col = 0; col < SCREEN_COLS; ++col) {
            display_render_cell(&ctx->screen[row][col], col, row);
        }

        /* Gerer les attributs de couleur pour cette ligne */
        render_row_colors(ctx, row);

        ctx->dirty[row] = 0;
    }

    ctx->full_refresh = 0;
}

/* ===================================================================
 *  Barre de statut (lignes texte 25-27)
 * =================================================================== */

void display_status(const char* msg)
{
    unsigned char i;
    unsigned char* line = TEXT + (25 * 40);  /* Ligne 25 en mode texte */

    /* Attribut: encre blanche sur fond bleu */
    line[0] = 0x04;  /* Encre bleue -> non, attribut ink blue */

    /* Effacer et ecrire le message */
    for (i = 0; i < 40; ++i) {
        if (msg[i] && i < 39) {
            line[i + 1] = msg[i];
        } else {
            line[i + 1] = ' ';
            if (!msg[i]) {
                /* Remplir le reste avec des espaces */
                for (++i; i < 39; ++i) {
                    line[i + 1] = ' ';
                }
                break;
            }
        }
    }
}

/* ===================================================================
 *  Curseur visuel
 * =================================================================== */

void display_cursor(unsigned char visible, unsigned char col, unsigned char row)
{
    unsigned char* base;

    if (row >= SCREEN_ROWS || col >= SCREEN_COLS) {
        return;
    }

    base = HIRES + (unsigned int)(row * CHAR_H) * 40 + col;

    if (visible) {
        /* Curseur: inverser la derniere ligne du caractere */
        *(base + 7u * 40) ^= 0x3F;
    }
}
