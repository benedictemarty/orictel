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

/* Blink phase accessible depuis main.c */
extern unsigned char g_blink_phase;

/* Mode rendu: 0=hybride (G0 serial + G1 dithering), 1=tout dithering */
unsigned char g_render_mode;

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

static void generate_mosaic(unsigned char code, unsigned char* glyph,
                            unsigned char separated)
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

    if (separated) {
        /* Mode mosaique separe: reduire chaque bloc d'1 pixel sur chaque bord.
         * Gauche: pixels 5,4 (pas 3) = 0x30
         * Droite: pixels 1,0 (pas 2) = 0x03
         * Rangee haute: lignes 0-1 (pas 2)
         * Rangee milieu: lignes 3-4 (pas 5)
         * Rangee basse: ligne 6 (pas 7) */

        /* Lignes 0-1: rangee haute */
        left  = (pattern & 0x01) ? 0x30 : 0x00;
        right = (pattern & 0x02) ? 0x03 : 0x00;
        line_byte = (left | right) | 0x40;
        glyph[0] = line_byte;
        glyph[1] = line_byte;
        glyph[2] = 0x40;  /* Ligne de separation */

        /* Lignes 3-4: rangee milieu */
        left  = (pattern & 0x04) ? 0x30 : 0x00;
        right = (pattern & 0x08) ? 0x03 : 0x00;
        line_byte = (left | right) | 0x40;
        glyph[3] = line_byte;
        glyph[4] = line_byte;
        glyph[5] = 0x40;  /* Ligne de separation */

        /* Ligne 6: rangee basse */
        left  = (pattern & 0x10) ? 0x30 : 0x00;
        right = (pattern & 0x20) ? 0x03 : 0x00;
        line_byte = (left | right) | 0x40;
        glyph[6] = line_byte;
        glyph[7] = 0x40;  /* Ligne de separation */
    } else {
        /* Mode mosaique contigu (original) */

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
}

/* ===================================================================
 *  Tables de dithering pour mosaiques G1
 *  Chaque couleur a 8 masques (1 par ligne pixel).
 *  Le masque AND avec les pixels du glyphe simule la couleur.
 * =================================================================== */
static const unsigned char g1_dither[8][8] = {
    /* 0: Noir */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 1: Rouge - lignes alternees */
    { 0x3F, 0x00, 0x3F, 0x00, 0x3F, 0x00, 0x3F, 0x00 },
    /* 2: Vert - damier */
    { 0x2A, 0x15, 0x2A, 0x15, 0x2A, 0x15, 0x2A, 0x15 },
    /* 3: Jaune - dense 3/4 */
    { 0x3F, 0x2A, 0x3F, 0x15, 0x3F, 0x2A, 0x3F, 0x15 },
    /* 4: Bleu - sparse */
    { 0x24, 0x09, 0x12, 0x24, 0x09, 0x12, 0x24, 0x09 },
    /* 5: Magenta - diagonale */
    { 0x21, 0x12, 0x04, 0x08, 0x21, 0x12, 0x04, 0x08 },
    /* 6: Cyan - dense 5/6 */
    { 0x3F, 0x1B, 0x3F, 0x36, 0x3F, 0x1B, 0x3F, 0x36 },
    /* 7: Blanc - plein */
    { 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F },
};

/* ===================================================================
 *  Rendu d'une cellule en HIRES
 *  G0/G2: pixels pleins (couleur via serial attributes)
 *  G1: dithering par couleur (pas de colonne perdue)
 * =================================================================== */

/* Variable globale blink_phase accessible depuis main.c */
extern unsigned char g_blink_phase;

static void render_cell_hires(const vtx_cell_t* cell,
                               unsigned char col, unsigned char char_row)
{
    unsigned char* ptr;
    const unsigned char* glyph;
    unsigned char mosaic_buf[8];
    unsigned char line;
    unsigned char ch;
    unsigned char inv_bit;
    unsigned char use_dither;

    ch = cell->ch;
    if (cell->flags & ATTR_CONCEALED) ch = ' ';

    /* Feature 4: Clignotement - si blink_phase=1 et ATTR_FLASH, rendre vide */
    if ((cell->flags & ATTR_FLASH) && g_blink_phase) {
        ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
        for (line = 0; line < CHAR_H; ++line) {
            *ptr = 0x40;
            ptr += 40;
        }
        return;
    }

    inv_bit = (cell->flags & ATTR_INVERT) ? 0xC0 : 0x40;

    /* G1 = dithering, G0/G2 = pixels pleins */
    /* Mode 0=hybride (G1 dither), 1=tout dither, 2=brut (tout blanc) */
    use_dither = (g_render_mode == 1 ||
                  (g_render_mode == 0 && cell->charset == CHARSET_G1)) ? 1 : 0;

    /* Selectionner le glyphe */
    if (cell->charset == CHARSET_G1) {
        generate_mosaic(ch, mosaic_buf,
                        (cell->flags & ATTR_SEPARATED) ? 1 : 0);
        glyph = mosaic_buf;
    } else if (cell->charset == CHARSET_G2) {
        glyph = font_get_g2(ch);
    } else {
        glyph = font_get_g0(ch);
    }

    if (cell->size == SIZE_DOUBLE_HEIGHT || cell->size == SIZE_DOUBLE_SIZE) {
        /* DOUBLE HAUTEUR / DOUBLE TAILLE: la moitie basse du glyphe (lignes 4-7)
         * est etiree sur 8 lignes pixel de la ligne courante.
         * La moitie haute (lignes 0-3) est etiree sur la ligne AU-DESSUS.
         * Chaque ligne source produit 2 lignes pixel. */

        if (cell->size == SIZE_DOUBLE_SIZE) {
            /* Feature 2: DOUBLE TAILLE = double hauteur + double largeur.
             * Chaque pixel colonne est doublee: 3 source cols -> 6 dest pixels.
             * On ecrit la moitie gauche (3 bits hauts) dans col,
             * et la moitie droite (3 bits bas) dans col+1. */
            unsigned char g, left_byte, right_byte;

            /* Moitie basse (lignes 4-7) sur la ligne courante */
            ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
            for (line = 0; line < 4; ++line) {
                g = glyph[line + 4];
                if (use_dither) g &= g1_dither[cell->fg & 7][line + 4];
                /* Doubler les pixels: src bit5->dst bits 5,4; src bit4->dst bits 3,2; src bit3->dst bits 1,0 */
                left_byte  = ((g & 0x20) ? 0x30 : 0) |
                             ((g & 0x10) ? 0x0C : 0) |
                             ((g & 0x08) ? 0x03 : 0);
                right_byte = ((g & 0x04) ? 0x30 : 0) |
                             ((g & 0x02) ? 0x0C : 0) |
                             ((g & 0x01) ? 0x03 : 0);
                *ptr       = left_byte | inv_bit;
                *(ptr + 1) = right_byte | inv_bit;
                ptr += 40;
                *ptr       = left_byte | inv_bit;
                *(ptr + 1) = right_byte | inv_bit;
                ptr += 40;
            }

            /* Moitie haute (lignes 0-3) sur la ligne au-dessus */
            if (char_row > 0) {
                ptr = HIRES_ADDR((unsigned int)(char_row - 1) * CHAR_H, col);
                for (line = 0; line < 4; ++line) {
                    g = glyph[line];
                    if (use_dither) g &= g1_dither[cell->fg & 7][line];
                    left_byte  = ((g & 0x20) ? 0x30 : 0) |
                                 ((g & 0x10) ? 0x0C : 0) |
                                 ((g & 0x08) ? 0x03 : 0);
                    right_byte = ((g & 0x04) ? 0x30 : 0) |
                                 ((g & 0x02) ? 0x0C : 0) |
                                 ((g & 0x01) ? 0x03 : 0);
                    *ptr       = left_byte | inv_bit;
                    *(ptr + 1) = right_byte | inv_bit;
                    ptr += 40;
                    *ptr       = left_byte | inv_bit;
                    *(ptr + 1) = right_byte | inv_bit;
                    ptr += 40;
                }
            }
        } else {
            /* DOUBLE HAUTEUR seulement (pas double largeur) */
            /* Moitie basse (lignes 4-7 du glyphe) sur la ligne courante */
            ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
            for (line = 0; line < 4; ++line) {
                unsigned char g = glyph[line + 4];
                if (use_dither) g &= g1_dither[cell->fg & 7][line + 4];
                *ptr = g | inv_bit; ptr += 40;
                *ptr = g | inv_bit; ptr += 40;
            }

            /* Moitie haute (lignes 0-3) sur la ligne au-dessus */
            if (char_row > 0) {
                ptr = HIRES_ADDR((unsigned int)(char_row - 1) * CHAR_H, col);
                for (line = 0; line < 4; ++line) {
                    unsigned char g = glyph[line];
                    if (use_dither) g &= g1_dither[cell->fg & 7][line];
                    *ptr = g | inv_bit; ptr += 40;
                    *ptr = g | inv_bit; ptr += 40;
                }
            }
        }
    } else if (cell->size == SIZE_DOUBLE_WIDTH) {
        /* Feature 2: DOUBLE LARGEUR seulement.
         * Chaque pixel colonne est doublee: 3 source cols -> 6 dest pixels.
         * On ecrit dans col (left half) et col+1 (right half). */
        ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
        for (line = 0; line < CHAR_H; ++line) {
            unsigned char g = glyph[line];
            unsigned char left_byte, right_byte;
            if (use_dither) g &= g1_dither[cell->fg & 7][line];

            /* Feature 3: Underline sur la derniere ligne */
            if ((cell->flags & ATTR_UNDERLINE) && line == 7) {
                g = 0x3F;
            }

            left_byte  = ((g & 0x20) ? 0x30 : 0) |
                         ((g & 0x10) ? 0x0C : 0) |
                         ((g & 0x08) ? 0x03 : 0);
            right_byte = ((g & 0x04) ? 0x30 : 0) |
                         ((g & 0x02) ? 0x0C : 0) |
                         ((g & 0x01) ? 0x03 : 0);
            *ptr       = left_byte | inv_bit;
            *(ptr + 1) = right_byte | inv_bit;
            ptr += 40;
        }
    } else {
        /* TAILLE NORMALE */
        ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
        for (line = 0; line < CHAR_H; ++line) {
            unsigned char g = glyph[line];
            if (use_dither) g &= g1_dither[cell->fg & 7][line];

            /* Feature 3: Underline - derniere ligne pixel = tous pixels on */
            if ((cell->flags & ATTR_UNDERLINE) && line == 7) {
                g = 0x3F;
            }

            *ptr = g | inv_bit;
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

/* Feature 1: Ecrit un attribut serial fond (PAPER) sur les 8 lignes pixel d'une colonne */
static void set_paper_attr(unsigned char col, unsigned char char_row, unsigned char paper)
{
    unsigned char* ptr = HIRES_ADDR((unsigned int)char_row * CHAR_H, col);
    unsigned char line;
    for (line = 0; line < CHAR_H; ++line) {
        *ptr = 0x10 | (paper & 0x07);  /* Attribut fond: $10-$17 */
        ptr += 40;
    }
}

static void render_row_hires(vtx_context_t* ctx, unsigned char row)
{
    unsigned char col;
    unsigned char prev_fg;
    unsigned char prev_bg;
    unsigned char cell_fg;
    unsigned char cell_bg;

    if (row >= SCREEN_ROWS) return;

    /* Mode 1 (tout-dithering) ou 2 (brut): pas d'attributs serial */
    if (g_render_mode >= 1) {
        set_ink_attr(0, row, VTX_WHITE);
        for (col = 0; col < SCREEN_COLS; ++col) {
            render_cell_hires(&ctx->screen[row][col], col, row);
            if (ctx->screen[row][col].size == SIZE_DOUBLE_WIDTH ||
                ctx->screen[row][col].size == SIZE_DOUBLE_SIZE) {
                ++col;
            }
        }
        return;
    }

    /* Mode hybride: PAPER/INK reset en cols 0-1 */
    {
        unsigned char ch0 = ctx->screen[row][0].ch;
        unsigned char ch1 = ctx->screen[row][1].ch;
        if (ch0 == ' ' || ch0 == 0x20 || ch0 == 0) {
            set_paper_attr(0, row, VTX_BLACK);
        } else {
            render_cell_hires(&ctx->screen[row][0], 0, row);
        }
        if (ch1 == ' ' || ch1 == 0x20 || ch1 == 0) {
            set_ink_attr(1, row, VTX_WHITE);
        } else {
            render_cell_hires(&ctx->screen[row][1], 1, row);
        }
    }
    prev_fg = VTX_WHITE;
    prev_bg = VTX_BLACK;

    /* Cols 2-39 (cols 0-1 deja traitees pour PAPER/INK reset) */
    for (col = 2; col < SCREEN_COLS; ++col) {
        vtx_cell_t* cell = &ctx->screen[row][col];
        cell_fg = cell->fg;
        cell_bg = cell->bg;

        /* INK prioritaire sur PAPER pour les cellules inversees
         * (ENVOI jaune inverse: on veut INK=jaune, pas PAPER) */
        if (cell_fg != prev_fg) {
            unsigned char ch = cell->ch;
            if (ch == ' ' || ch == 0x20 || ch == 0) {
                set_ink_attr(col, row, cell_fg);
                if (row > 0 && (cell->size == SIZE_DOUBLE_HEIGHT ||
                                cell->size == SIZE_DOUBLE_SIZE)) {
                    set_ink_attr(col, row - 1, cell_fg);
                }
                prev_fg = cell_fg;
                continue;
            }
        }

        /* PAPER: insertion apres INK (priorite moindre) */
        if (cell_bg != prev_bg) {
            unsigned char ch = cell->ch;
            if (ch == ' ' || ch == 0x20 || ch == 0) {
                set_paper_attr(col, row, cell_bg);
                prev_bg = cell_bg;
                continue;
            }
        }

        render_cell_hires(cell, col, row);

        /* Double largeur/taille: sauter la colonne suivante
         * (le rendu a deja ecrit dans col+1) */
        if (cell->size == SIZE_DOUBLE_WIDTH ||
            cell->size == SIZE_DOUBLE_SIZE) {
            ++col;  /* Skip next column */
        }
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

    /* Curseur visible: inverser la derniere ligne pixel
     * a la position du curseur (clignotement via blink_phase) */
    if (ctx->cur_visible && (g_blink_phase == 0)) {
        unsigned char* base;
        if (ctx->cur_y < SCREEN_ROWS && ctx->cur_x < SCREEN_COLS) {
            base = HIRES_ADDR((unsigned int)ctx->cur_y * CHAR_H + 7, ctx->cur_x);
            *base = 0x7F;  /* Barre blanche sous le caractere */
        }
    }
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

/* Beep via ROM Atmos - utilise la routine PING ($FA9F)
 * qui fait un bip court via le PSG AY-3-8912 */
void display_beep(void)
{
    __asm__("jsr $FA9F");
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
