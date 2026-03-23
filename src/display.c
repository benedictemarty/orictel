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
    unsigned char i;
    unsigned char* ptr;

    hires_on();

    /* Cacher les 3 lignes texte en bas (remplir de noir) */
    /* En HIRES, les lignes texte 25-27 sont a $BF68-$BFDF */
    ptr = (unsigned char*)0xBF68;
    for (i = 0; i < 120; ++i) {  /* 3 lignes x 40 octets */
        ptr[i] = ' ';
    }
    /* Premiere colonne de chaque ligne texte: encre noire */
    *(unsigned char*)0xBF68 = 0x00;  /* Ink black ligne 25 */
    *(unsigned char*)0xBF90 = 0x00;  /* Ink black ligne 26 */
    *(unsigned char*)0xBFB8 = 0x00;  /* Ink black ligne 27 */
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
     * bit 6 = bas-droit (PAS bit 5 !)
     *
     * Bit 5 du code = flag "separated" (pas un bloc).
     * Les codes $60-$7F sont les memes motifs que $20-$3F
     * avec le bloc bas-droit EN PLUS (bit 6). */
    /* Cas special: G1 $60 = ligne horizontale bas pleine.
     * La ROM EF9345 du vrai Minitel a un glyphe specifique pour $60
     * qui remplit toute la rangee du bas (pas juste le bloc droit).
     * Ref: capture ecran Minitel reel. */
    if (code == 0x60) {
        glyph[0] = 0x3F | 0x40;  /* Haut: 6 pixels pleins */
        for (i = 1; i < 8; ++i) glyph[i] = 0x40;  /* Reste vide */
        return;
    }

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

    /* Lignes 6-7: rangee basse (bits 4=bas-gauche, 5=bas-droit)
     * Rendu fidele: chaque bloc independant, pas de fusion. */
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
    unsigned char line;
    unsigned char ch;
    unsigned char inv_bit;

    ch = cell->ch;
    if (cell->flags & ATTR_CONCEALED) ch = ' ';

    /* Bit 7 ULA = inversion encre/fond (pas de colonne perdue) */
    inv_bit = (cell->flags & ATTR_INVERT) ? 0xC0 : 0x40;

    /* Selectionner le glyphe */
    if (cell->charset == CHARSET_G1) {
        generate_mosaic(ch, mosaic_buf);
        glyph = mosaic_buf;
    } else if (cell->charset == CHARSET_G2) {
        glyph = font_get_g2(ch);
    } else {
        glyph = font_get_g0(ch);
    }

    if (cell->size == SIZE_DOUBLE_HEIGHT || cell->size == SIZE_DOUBLE_SIZE) {
        /* DOUBLE HAUTEUR: la moitie basse du glyphe (lignes 4-7)
         * est etiree sur 8 lignes pixel de la ligne courante.
         * La moitie haute (lignes 0-3) est etiree sur la ligne AU-DESSUS.
         * Chaque ligne source produit 2 lignes pixel. */

        /* Moitie basse (lignes 4-7 du glyphe) sur la ligne courante */
        ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
        for (line = 0; line < 4; ++line) {
            unsigned char px = glyph[line + 4] | inv_bit;
            *ptr = px; ptr += 40;
            *ptr = px; ptr += 40;  /* Doubler chaque ligne */
        }

        /* Moitie haute (lignes 0-3 du glyphe) sur la ligne au-dessus */
        if (char_row > 0) {
            ptr = HIRES_ADDR((unsigned int)(char_row - 1) * CHAR_H, col);
            for (line = 0; line < 4; ++line) {
                unsigned char px = glyph[line] | inv_bit;
                *ptr = px; ptr += 40;
                *ptr = px; ptr += 40;
            }
        }
    } else {
        /* TAILLE NORMALE: 8 lignes pixel directes */
        ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
        for (line = 0; line < CHAR_H; ++line) {
            *ptr = glyph[line] | inv_bit;
            ptr += 40;
        }
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

/* Ecrit un attribut serial encre sur les 8 lignes pixel d'une colonne */
static void set_ink_attr(unsigned char col, unsigned char char_row, unsigned char ink)
{
    unsigned char* ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
    unsigned char line;
    for (line = 0; line < CHAR_H; ++line) {
        *ptr = ink & 0x07;  /* Attribut encre: $00-$07 */
        ptr += 40;
    }
}

static void render_row_hires(vtx_context_t* ctx, unsigned char row)
{
    unsigned char col;
    unsigned char prev_fg;
    unsigned char cell_fg;

    if (row >= SCREEN_ROWS) return;

    /* Col 0: rendre le caractere si la couleur est blanche (defaut ULA). */
    prev_fg = ctx->screen[row][0].fg;
    if (prev_fg == VTX_WHITE) {
        render_cell_hires(&ctx->screen[row][0], 0, row);
    } else {
        set_ink_attr(0, row, prev_fg);
        /* Double hauteur: poser aussi l'attribut sur la ligne au-dessus */
        if (row > 0 && (ctx->screen[row][0].size == SIZE_DOUBLE_HEIGHT ||
                        ctx->screen[row][0].size == SIZE_DOUBLE_SIZE)) {
            set_ink_attr(0, row - 1, prev_fg);
        }
    }

    /* Cols 1-39 */
    for (col = 1; col < SCREEN_COLS; ++col) {
        cell_fg = ctx->screen[row][col].fg;

        if (cell_fg != prev_fg) {
            unsigned char ch = ctx->screen[row][col].ch;
            if (ch == ' ' || ch == 0x20 || ch == 0) {
                set_ink_attr(col, row, cell_fg);
                /* Double hauteur: attribut aussi sur la ligne au-dessus */
                if (row > 0 && (ctx->screen[row][col].size == SIZE_DOUBLE_HEIGHT ||
                                ctx->screen[row][col].size == SIZE_DOUBLE_SIZE)) {
                    set_ink_attr(col, row - 1, cell_fg);
                }
                prev_fg = cell_fg;
                continue;
            }
        }

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

/* Barre de statut desactivee (les 3 lignes texte sont cachees) */
void display_status(const char* msg)
{
    (void)msg;  /* Plus de barre de statut visible */
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
