/**
 * @file display.c
 * @brief Moteur d'affichage HIRES pour OricTel (v0.2)
 *
 * Rendu en mode HIRES (240x200 pixels).
 * Chaque cellule Minitel = 6x8 pixels, rendue depuis les tables de fontes.
 * Les mosaiques G1 sont generees algorithmiquement (2x3 blocs).
 *
 * Organisation HIRES Oric:
 *   $A000 + ligne_pixel * 40 + colonne = 1 octet (6 pixels)
 *   Bit 6 = 1 : les bits 5-0 sont des pixels (1=encre, 0=fond)
 *   Bit 6 = 0, bit 5 = 0 : attribut serial (encre $00-$07, fond $10-$17)
 *
 * Strategie couleur simplifiee:
 *   - Premiere colonne de chaque ligne pixel = attribut encre
 *   - Le reste = donnees pixel avec bit 6 = 1
 */

#include <string.h>
#include "display.h"
#include "fonts.h"

/* Pointeur HIRES */
#define HIRES ((unsigned char*)0xA000)

/* Calcul adresse pixel: ligne_pixel * 40 + colonne */
#define HIRES_ADDR(px_row, col) (HIRES + (unsigned int)(px_row) * 40 + (col))

/* ===================================================================
 *  Passage en mode HIRES via ROM Atmos
 * =================================================================== */

static void hires_on(void)
{
    /* Appel ROM Atmos HIRES a $EC33 */
    __asm__("jsr $EC33");
}

/* ===================================================================
 *  Initialisation
 * =================================================================== */

void display_init(void)
{
    hires_on();
    /* hires_on() efface deja le framebuffer via la ROM */
}

/* ===================================================================
 *  Effacement HIRES
 * =================================================================== */

void display_clear(void)
{
    unsigned int i;
    unsigned char* ptr = HIRES;

    /* Remplir le framebuffer: chaque octet = $40 (bit 6 set, pixels off) */
    for (i = 0; i < 8000; ++i) {
        ptr[i] = 0x40;
    }
}

/* ===================================================================
 *  Generation glyphe G1 mosaique (2x3 blocs dans 6x8 pixels)
 * =================================================================== */

static void generate_mosaic(unsigned char code, unsigned char* glyph)
{
    unsigned char pattern;
    unsigned char left, right, line_byte;
    unsigned char i;

    /* Encodage mosaique Minitel (reference: telenet emulateur.js)
     * bit 0 = haut-gauche
     * bit 1 = haut-droit
     * bit 2 = milieu-gauche
     * bit 3 = milieu-droit
     * bit 4 = bas-gauche
     * bit 6 = bas-droit (PAS bit 5 !) */
    pattern = code & 0x1F;  /* Bits 0-4 */
    if (code & 0x40) pattern |= 0x20;  /* Bit 6 -> bit 5 du pattern */

    /* Lignes 0-2: rangee haute (bits 0=haut-gauche, 1=haut-droit) */
    left  = (pattern & 0x01) ? 0x38 : 0x00;  /* Pixels 5,4,3 */
    right = (pattern & 0x02) ? 0x07 : 0x00;  /* Pixels 2,1,0 */
    line_byte = (left | right) | 0x40;        /* Bit 6 = pixel mode */
    for (i = 0; i < 3; ++i) glyph[i] = line_byte;

    /* Lignes 3-5: rangee milieu (bits 2=milieu-gauche, 3=milieu-droit) */
    left  = (pattern & 0x04) ? 0x38 : 0x00;
    right = (pattern & 0x08) ? 0x07 : 0x00;
    line_byte = (left | right) | 0x40;
    for (i = 3; i < 6; ++i) glyph[i] = line_byte;

    /* Lignes 6-7: rangee basse (bits 4=bas-gauche, 5=bas-droit) */
    left  = (pattern & 0x10) ? 0x38 : 0x00;
    right = (pattern & 0x20) ? 0x07 : 0x00;
    line_byte = (left | right) | 0x40;
    glyph[6] = line_byte;
    glyph[7] = line_byte;
}

/* ===================================================================
 *  Rendu d'une cellule en HIRES
 * =================================================================== */

static void render_cell_hires(const vtx_cell_t* cell,
                               unsigned char col, unsigned char char_row)
{
    unsigned char* ptr;
    const unsigned char* glyph;
    unsigned char mosaic_buf[8];
    unsigned char line, pixel_byte;
    unsigned char ch;
    unsigned char xor_mask;

    /* Adresse de base: premiere ligne pixel de cette cellule */
    ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);

    ch = cell->ch;
    xor_mask = (cell->flags & ATTR_INVERT) ? 0x3F : 0x00;

    if (cell->flags & ATTR_CONCEALED) ch = ' ';

    /* Selectionner le glyphe */
    if (cell->charset == CHARSET_G1) {
        generate_mosaic(ch, mosaic_buf);
        glyph = mosaic_buf;
    } else {
        glyph = font_get_g0(ch);
    }

    /* Ecrire 8 lignes avec pointeur incremente (+40 par ligne) */
    /* RAPIDE: pas de multiplication, juste ptr += 40 */
    for (line = 0; line < CHAR_H; ++line) {
        *ptr = (glyph[line] ^ xor_mask) | 0x40;
        ptr += 40;
    }
}

/* ===================================================================
 *  Rendu d'une ligne complete avec attributs couleur
 *
 *  Sur l'Oric HIRES, les serial attributes occupent des colonnes
 *  entieres (toutes les 8 lignes de pixels). Un attribut encre ($00-$07)
 *  en colonne N affecte toutes les colonnes >= N de cette ligne.
 *
 *  Strategie v0.2:
 *  - Col 0 de chaque ligne = attribut encre (couleur dominante)
 *  - Cols 1-39 = pixels des caracteres
 *  - Quand la couleur change, on insere un attribut a cette colonne
 * =================================================================== */

static void render_row_hires(vtx_context_t* ctx, unsigned char row)
{
    unsigned char col;
    unsigned char* ptr;

    if (row >= SCREEN_ROWS) return;

    /* Rendre TOUTES les 40 colonnes comme des caracteres.
     * Pas d'attribut serial = pas de colonne mangee.
     * Col 0: attribut encre blanche (obligatoire pour l'ULA Oric).
     * Cols 1-39: pixels des caracteres.
     *
     * Note: on perd col 0 du contenu (remplacee par l'attribut).
     * C'est le minimum requis par l'ULA. Le Minitel utilise souvent
     * col 0 pour des mosaiques decoratives, donc c'est acceptable.
     */
    ptr = HIRES_ADDR((unsigned int)row * CHAR_H, 0);
    {
        unsigned char line;
        for (line = 0; line < CHAR_H; ++line) {
            *ptr = 0x07;  /* Ink white - obligatoire pour l'ULA */
            ptr += 40;
        }
    }

    /* Cols 1-39: rendu direct des cellules sans attribut intermediaire */
    for (col = 1; col < SCREEN_COLS; ++col) {
        render_cell_hires(&ctx->screen[row][col], col, row);
    }
}

/* ===================================================================
 *  API publiques
 * =================================================================== */

void display_render(vtx_context_t* ctx)
{
    unsigned char row;
    for (row = 0; row < SCREEN_ROWS; ++row) {
        if (!ctx->dirty[row] && !ctx->full_refresh) continue;
        render_row_hires(ctx, row);
        ctx->dirty[row] = 0;
    }
    ctx->full_refresh = 0;
}

void display_render_cell_row(vtx_context_t* ctx, unsigned char row)
{
    render_row_hires(ctx, row);
}

void display_render_cell(const vtx_cell_t* cell, unsigned char col, unsigned char row)
{
    render_cell_hires(cell, col, row);
}

/* Barre de statut (lignes texte en mode HIRES: $BF68+) */
void display_status(const char* msg)
{
    unsigned char i;
    /* En HIRES, les 3 dernieres lignes texte sont a $BF68 */
    unsigned char* line = (unsigned char*)(0xBF68 + 40);  /* 2eme ligne texte */

    line[0] = 0x03;  /* Ink yellow */
    for (i = 1; i < 40; ++i) {
        if (*msg) {
            line[i] = *msg++;
        } else {
            line[i] = ' ';
        }
    }
}

void display_cursor(unsigned char visible, unsigned char col, unsigned char row)
{
    unsigned char* base;

    if (row >= SCREEN_ROWS || col >= SCREEN_COLS) return;

    base = HIRES_ADDR((unsigned int)row * CHAR_H + 7, col);

    if (visible) {
        *base ^= 0x3F;  /* Inverser la derniere ligne pixel */
    }
}
