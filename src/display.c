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
 *   La ULA remet encre=blanc / fond=noir au debut de CHAQUE scanline:
 *   une ligne sans attribut rend donc toujours blanc sur noir.
 *
 * Strategie couleur (mode AUTO):
 *   - Attributs serial poses uniquement sur les cellules vides
 *     (un attribut sacrifie la cellule qu'il occupe)
 *   - Lignes sans cellule vide ou avec double hauteur: rendu brut
 *   - Mosaiques G1: dithering par densite de luminance
 */

#include "display.h"
#include "fonts.h"

/* Blink phase accessible depuis main.c */
extern unsigned char g_blink_phase;
extern unsigned char g_global_mask;

/* Mode rendu: 0=hybride (G0 serial + G1 dithering), 1=tout dithering */
unsigned char g_render_mode = 0;  /* 0=hybride (defaut), 1=dithering, 2=brut */

/* Blit cellule assembleur (display_asm.s):
 * dst[l*40] = (src[l] AND and_tbl[l]) OR or_mask, l = 0..7 */
extern const unsigned char* blit_src;
extern unsigned char* blit_dst;
extern const unsigned char* blit_and;
extern unsigned char blit_or;
void __fastcall__ blit_cell8(void);

/* Moteur de rendu de plage assembleur (display_asm.s): rend une suite
 * de cellules taille normale G0/G1 sans flash/concealed/souligne, et
 * retourne le nombre de cellules consommees (0 si la 1ere est hors
 * fast-path: l'appelant rend cette cellule en C et relance). */
extern unsigned char* run_cells;   /* 1ere cellule (vtx_cell_t brut) */
extern unsigned char* run_dst;     /* destination HIRES */
extern unsigned char run_count;    /* cellules max a rendre */
extern unsigned char run_mode;     /* dither: 0=aucun, 1=G1 seul, 2=tout */
unsigned char __fastcall__ blit_run(void);

/* Table AND neutre (pas de dithering) pour le blit.
 * Non-static: importee par display_asm.s. */
const unsigned char no_dither[8] = {
    0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F
};

/* Pointeur HIRES */
#define HIRES ((unsigned char*)0xA000)

/* Adresses de base des 25 lignes caracteres ($A000 + row * 320).
 * Precalcule en RODATA: cc65 emet une multiplication 16 bits a
 * l'execution pour row*320, payee a chaque cellule rendue sinon. */
static unsigned char* const hires_row_base[SCREEN_ROWS] = {
    (unsigned char*)0xA000, (unsigned char*)0xA140, (unsigned char*)0xA280,
    (unsigned char*)0xA3C0, (unsigned char*)0xA500, (unsigned char*)0xA640,
    (unsigned char*)0xA780, (unsigned char*)0xA8C0, (unsigned char*)0xAA00,
    (unsigned char*)0xAB40, (unsigned char*)0xAC80, (unsigned char*)0xADC0,
    (unsigned char*)0xAF00, (unsigned char*)0xB040, (unsigned char*)0xB180,
    (unsigned char*)0xB2C0, (unsigned char*)0xB400, (unsigned char*)0xB540,
    (unsigned char*)0xB680, (unsigned char*)0xB7C0, (unsigned char*)0xB900,
    (unsigned char*)0xBA40, (unsigned char*)0xBB80, (unsigned char*)0xBCC0,
    (unsigned char*)0xBE00,
};

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

static void g1_cache_init(void);

void display_init(void)
{
    unsigned char i;
    unsigned char* ptr;

    hires_on();
    g1_cache_init();

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
 *  Cache des glyphes mosaique G1
 *
 *  generate_mosaic coute ~400-800 cycles cc65 par cellule; les pages
 *  Minitel sont denses en mosaiques. Les 64 motifs (bits 0-4 + bit 6
 *  du code) x 2 modes (contigu/separe) sont precalcules une fois au
 *  demarrage: 1 Ko de BSS contre un lookup au rendu.
 * =================================================================== */

/* Non-static: imports de display_asm.s (blit_run) */
unsigned char g1_cache[2][64][8];

/* Cas special G1 $60 (rangee haute pleine, cf. generate_mosaic) */
const unsigned char g1_glyph_60[8] = {
    0x7F, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};

static void g1_cache_init(void)
{
    unsigned char sep, p;
    for (sep = 0; sep < 2; ++sep) {
        for (p = 0; p < 64; ++p) {
            /* Reconstruire un code dont le motif est p, sans tomber
             * sur le cas special $60 (bit5 du motif = bit6 du code) */
            unsigned char code = (p & 0x1F) | ((p & 0x20) << 1);
            generate_mosaic(code, &g1_cache[sep][p][0], sep);
        }
    }
}

/* Glyphe G1 depuis le cache */
static const unsigned char* g1_glyph(unsigned char ch, unsigned char separated)
{
    if (ch == 0x60) {
        return g1_glyph_60;
    }
    return &g1_cache[separated][(ch & 0x1F) | ((ch & 0x40) >> 1)][0];
}

/* ===================================================================
 *  Tables de dithering pour mosaiques G1
 *  Chaque couleur a 8 masques (1 par ligne pixel).
 *  Le masque AND avec les pixels du glyphe simule la couleur.
 * =================================================================== */
/* Les densites suivent l'ordre de luminance CCIR des couleurs
 * (noir 0% < bleu 11% < rouge 30% < magenta 41% < vert 59%
 *  < cyan 70% < jaune 89% < blanc 100%), pour que la hierarchie
 * visuelle d'une page colorisee soit preservee en monochrome.
 * Les textures different entre couleurs proches (lignes, damier,
 * diagonale) pour rester distinguables a densite egale. */
const unsigned char g1_dither[8][8] = {
    /* 0: Noir - 0% */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 1: Rouge - diagonale 33% */
    { 0x24, 0x09, 0x12, 0x24, 0x09, 0x12, 0x24, 0x09 },
    /* 2: Vert - damier 50% */
    { 0x2A, 0x15, 0x2A, 0x15, 0x2A, 0x15, 0x2A, 0x15 },
    /* 3: Jaune - dense 83% */
    { 0x3F, 0x1B, 0x3F, 0x36, 0x3F, 0x1B, 0x3F, 0x36 },
    /* 4: Bleu - diagonale eparse 25% */
    { 0x21, 0x12, 0x04, 0x08, 0x21, 0x12, 0x04, 0x08 },
    /* 5: Magenta - lignes irregulieres 42% */
    { 0x2A, 0x12, 0x2A, 0x12, 0x2A, 0x12, 0x2A, 0x12 },
    /* 6: Cyan - dense 75% */
    { 0x3F, 0x2A, 0x3F, 0x15, 0x3F, 0x2A, 0x3F, 0x15 },
    /* 7: Blanc - plein 100% */
    { 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F, 0x3F },
};

/* ===================================================================
 *  Rendu d'une cellule en HIRES
 *  G0/G2: pixels pleins (couleur via serial attributes)
 *  G1: dithering par couleur (pas de colonne perdue)
 * =================================================================== */

static void render_cell_hires(const vtx_cell_t* cell,
                               unsigned char col, unsigned char char_row)
{
    unsigned char* ptr;
    const unsigned char* glyph;
    unsigned char line;
    unsigned char ch;
    unsigned char inv_bit;
    unsigned char use_dither;
    unsigned char has_right;   /* 0 = double largeur en col 39: moitie
                                * droite clippee (ecrire ptr+1 deborderait
                                * sur la ligne pixel suivante, voire hors
                                * du framebuffer sur la derniere cellule) */

    ch = cell->ch;
    has_right = (col < SCREEN_COLS - 1) ? 1 : 0;
    if ((cell->flags & ATTR_CONCEALED) && g_global_mask) ch = ' ';

    inv_bit = (cell->flags & ATTR_INVERT) ? 0xC0 : 0x40;

    /* Clignotement: en phase eteinte, rendre la cellule "vide" en
     * conservant l'inversion (une cellule inversee qui flashe alterne
     * pave plein / pave vide dans le fond inverse, pas pave / noir). */
    if ((cell->flags & ATTR_FLASH) && g_blink_phase) {
        ptr = hires_row_base[char_row] + col;
        for (line = 0; line < CHAR_H; ++line) {
            *ptr = inv_bit;
            ptr += 40;
        }
        return;
    }

    /* G1 = dithering, G0/G2 = pixels pleins */
    /* Mode 0=hybride (G1 dither), 1=tout dither, 2=brut (tout blanc) */
    use_dither = (g_render_mode == 1 ||
                  (g_render_mode == 0 && cell->charset == CHARSET_G1)) ? 1 : 0;

    /* Selectionner le glyphe */
    if (cell->charset == CHARSET_G1) {
        glyph = g1_glyph(ch, (cell->flags & ATTR_SEPARATED) ? 1 : 0);
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
            ptr = hires_row_base[char_row] + col;
            for (line = 0; line < 4; ++line) {
                g = glyph[line + 4];
                /* Souligne: derniere ligne source (glyph[7]), avant le
                 * dithering pour qu'il suive la couleur du texte */
                if ((cell->flags & ATTR_UNDERLINE) && line == 3) g = 0x3F;
                if (use_dither) g &= g1_dither[cell->fg & 7][line + 4];
                /* Doubler les pixels: src bit5->dst bits 5,4; src bit4->dst bits 3,2; src bit3->dst bits 1,0 */
                left_byte  = ((g & 0x20) ? 0x30 : 0) |
                             ((g & 0x10) ? 0x0C : 0) |
                             ((g & 0x08) ? 0x03 : 0);
                right_byte = ((g & 0x04) ? 0x30 : 0) |
                             ((g & 0x02) ? 0x0C : 0) |
                             ((g & 0x01) ? 0x03 : 0);
                *ptr       = left_byte | inv_bit;
                if (has_right) *(ptr + 1) = right_byte | inv_bit;
                ptr += 40;
                *ptr       = left_byte | inv_bit;
                if (has_right) *(ptr + 1) = right_byte | inv_bit;
                ptr += 40;
            }

            /* Moitie haute (lignes 0-3) sur la ligne au-dessus */
            if (char_row > 0) {
                ptr = hires_row_base[char_row - 1] + col;
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
                    if (has_right) *(ptr + 1) = right_byte | inv_bit;
                    ptr += 40;
                    *ptr       = left_byte | inv_bit;
                    if (has_right) *(ptr + 1) = right_byte | inv_bit;
                    ptr += 40;
                }
            }
        } else {
            /* DOUBLE HAUTEUR seulement (pas double largeur) */
            /* Moitie basse (lignes 4-7 du glyphe) sur la ligne courante */
            ptr = hires_row_base[char_row] + col;
            for (line = 0; line < 4; ++line) {
                unsigned char g = glyph[line + 4];
                /* Souligne sur la derniere ligne source (glyph[7]) */
                if ((cell->flags & ATTR_UNDERLINE) && line == 3) g = 0x3F;
                if (use_dither) g &= g1_dither[cell->fg & 7][line + 4];
                *ptr = g | inv_bit; ptr += 40;
                *ptr = g | inv_bit; ptr += 40;
            }

            /* Moitie haute (lignes 0-3) sur la ligne au-dessus */
            if (char_row > 0) {
                ptr = hires_row_base[char_row - 1] + col;
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
        ptr = hires_row_base[char_row] + col;
        for (line = 0; line < CHAR_H; ++line) {
            unsigned char g = glyph[line];
            unsigned char left_byte, right_byte;

            /* Souligne sur la derniere ligne, avant le dithering
             * pour qu'il suive la couleur du texte */
            if ((cell->flags & ATTR_UNDERLINE) && line == 7) {
                g = 0x3F;
            }
            if (use_dither) g &= g1_dither[cell->fg & 7][line];

            left_byte  = ((g & 0x20) ? 0x30 : 0) |
                         ((g & 0x10) ? 0x0C : 0) |
                         ((g & 0x08) ? 0x03 : 0);
            right_byte = ((g & 0x04) ? 0x30 : 0) |
                         ((g & 0x02) ? 0x0C : 0) |
                         ((g & 0x01) ? 0x03 : 0);
            *ptr       = left_byte | inv_bit;
            if (has_right) *(ptr + 1) = right_byte | inv_bit;
            ptr += 40;
        }
    } else {
        /* TAILLE NORMALE: blit assembleur (~190 cycles contre ~2000
         * pour la boucle C). Le dithering passe par la table AND.
         * Seul le souligne (ligne 7 forcee a $3F avant dithering)
         * garde la boucle C. */
        if (!(cell->flags & ATTR_UNDERLINE)) {
            blit_src = glyph;
            blit_dst = hires_row_base[char_row] + col;
            blit_and = use_dither ? &g1_dither[cell->fg & 7][0] : no_dither;
            blit_or = inv_bit;
            blit_cell8();
            return;
        }

        ptr = hires_row_base[char_row] + col;
        for (line = 0; line < CHAR_H; ++line) {
            unsigned char g = glyph[line];

            /* Souligne sur la derniere ligne, avant le dithering
             * pour qu'il suive la couleur du texte */
            if (line == 7) {
                g = 0x3F;
            }
            if (use_dither) g &= g1_dither[cell->fg & 7][line];

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
    unsigned char* ptr = hires_row_base[char_row] + col;
    unsigned char line;
    for (line = 0; line < CHAR_H; ++line) {
        *ptr = ink & 0x07;  /* Attribut encre: $00-$07 */
        ptr += 40;
    }
}

/* Feature 1: Ecrit un attribut serial fond (PAPER) sur les 8 lignes pixel d'une colonne */
static void set_paper_attr(unsigned char col, unsigned char char_row, unsigned char paper)
{
    unsigned char* ptr = hires_row_base[char_row] + col;
    unsigned char line;
    for (line = 0; line < CHAR_H; ++line) {
        *ptr = 0x10 | (paper & 0x07);  /* Attribut fond: $10-$17 */
        ptr += 40;
    }
}

/* Vrai si la ligne contient au moins une cellule double hauteur ou
 * double taille (dont la moitie haute vit dans les scanlines de la
 * ligne du dessus). */
static unsigned char row_has_dblh(vtx_context_t* ctx, unsigned char row)
{
    unsigned char col;
    for (col = 0; col < SCREEN_COLS; ++col) {
        unsigned char s = ctx->screen[row][col].size;
        if (s == SIZE_DOUBLE_HEIGHT || s == SIZE_DOUBLE_SIZE) {
            return 1;
        }
    }
    return 0;
}

/* Lignes rendues en hybride au dernier passage: des octets d'attributs
 * serial subsistent sur ces lignes. Une transition vers un rendu brut
 * partiel (span) les laisserait hors du span -> ligne entiere forcee. */
static unsigned char row_had_attrs[SCREEN_ROWS];

/* Rendu d'une plage de cellules [col_from, col_to] sans attributs.
 * dither_mode: 0=aucun (brut force), 1=G1 seulement (AUTO), 2=tout
 * (dithering force). Le gros du travail part dans blit_run (asm);
 * les cellules hors fast-path (G2, doubles tailles, flash, souligne,
 * concealed) retombent sur render_cell_hires. */
static void render_span_raw(vtx_context_t* ctx, unsigned char row,
                            unsigned char col_from, unsigned char col_to,
                            unsigned char dither_mode)
{
    unsigned char col = col_from;
    unsigned char n;

    run_mode = dither_mode;
    while (col <= col_to) {
        run_cells = (unsigned char*)&ctx->screen[row][col];
        run_dst = hires_row_base[row] + col;
        run_count = (unsigned char)(col_to - col + 1);
        n = blit_run();
        col += n;
        if (col > col_to) break;
        /* Cellule hors fast-path: chemin C complet */
        render_cell_hires(&ctx->screen[row][col], col, row);
        if (ctx->screen[row][col].size == SIZE_DOUBLE_WIDTH ||
            ctx->screen[row][col].size == SIZE_DOUBLE_SIZE) {
            col += 2;
        } else {
            ++col;
        }
    }
}

static void render_row_hires(vtx_context_t* ctx, unsigned char row)
{
    unsigned char col;
    unsigned char prev_fg;
    unsigned char prev_bg;
    unsigned char cell_fg;
    unsigned char cell_bg;
    unsigned char is_empty;
    unsigned char use_attrs;  /* 1=hybride (couleurs), 0=brut (blanc/noir) */
    unsigned char has_colors;
    unsigned char has_empty;
    unsigned char col_from;
    unsigned char col_to;

    if (row >= SCREEN_ROWS) return;

    /* Plage de colonnes a rendre (invariant: span plein quand la ligne
     * n'a pas ete retrecie via vtx_touch) */
    col_from = ctx->dirty_min[row];
    col_to = ctx->dirty_max[row];
    if (col_to >= SCREEN_COLS) col_to = SCREEN_COLS - 1;
    if (col_from > col_to) col_from = col_to;

    /* Mode force par l'utilisateur (CTRL+D).
     * Pas d'attribut a poser: la ULA remet encre=blanc/fond=noir en
     * debut de scanline, le rendu brut/dithering est blanc par defaut. */
    if (g_render_mode == 2 || g_render_mode == 1) {
        if (row_had_attrs[row]) {
            row_had_attrs[row] = 0;
            col_from = 0;
            col_to = SCREEN_COLS - 1;
        }
        render_span_raw(ctx, row, col_from, col_to,
                        (g_render_mode == 1) ? 2 : 0);
        return;
    }

    /* Mode 0 = AUTO: decider par ligne si on utilise les attributs.
     * Regle:
     * - Couleurs SI changements de couleur ET cellules vides pour attributs
     * - Brut SI la ligne contient des cellules double-hauteur/taille
     *   (les attributs serial de row-1 ne correspondent pas au contenu)
     * - Brut aussi SI la ligne DU DESSOUS en contient: les moities
     *   hautes de ses glyphes sont dessinees dans NOS scanlines et
     *   seraient teintees par nos attributs alors que leurs moities
     *   basses sont rendues brut (blanc)
     * - Brut si pas de cellule vide pour poser les attributs */
    has_colors = 0;
    has_empty = 0;
    {
        unsigned char has_dblh = 0;
        for (col = 0; col < SCREEN_COLS; ++col) {
            vtx_cell_t* c = &ctx->screen[row][col];
            if (c->fg != VTX_WHITE || c->bg != VTX_BLACK) has_colors = 1;
            if (c->ch == ' ' || c->ch == 0) has_empty = 1;
            if (c->size == SIZE_DOUBLE_HEIGHT || c->size == SIZE_DOUBLE_SIZE)
                has_dblh = 1;
        }
        if (!has_dblh && row + 1 < SCREEN_ROWS) {
            has_dblh = row_has_dblh(ctx, row + 1);
        }
        use_attrs = (has_colors && has_empty && !has_dblh) ? 1 : 0;
    }

    if (!use_attrs) {
        /* Brut: rendre la plage modifiee en blanc, pas d'attributs.
         * Dither G1 seulement (mosaiques tramees, texte plein). */
        if (row_had_attrs[row]) {
            row_had_attrs[row] = 0;
            col_from = 0;
            col_to = SCREEN_COLS - 1;
        }
        render_span_raw(ctx, row, col_from, col_to, 1);
        return;
    }

    /* Hybride: attributs INK/PAPER sur cellules vides,
     * caractere TOUJOURS prioritaire. Ligne ENTIERE obligatoire: un
     * changement local peut deplacer la chaine d'attributs de toute
     * la suite de la ligne.
     * Etat initial = reset ULA en debut de scanline (blanc sur noir). */
    row_had_attrs[row] = 1;
    prev_bg = VTX_BLACK;
    prev_fg = VTX_WHITE;

    for (col = 0; col < SCREEN_COLS; ++col) {
        vtx_cell_t* cell = &ctx->screen[row][col];
        cell_fg = cell->fg;
        cell_bg = cell->bg;
        is_empty = (cell->ch == ' ' || cell->ch == 0);

        if (is_empty) {
            unsigned char bg_change = (cell_bg != prev_bg) ? 1 : 0;
            unsigned char fg_change = (cell_fg != prev_fg) ? 1 : 0;

            if (bg_change && fg_change) {
                /* Un seul octet d'attribut par cellule. Si le vide
                 * suivant existe, poser le fond ici: l'encre sera
                 * posee dessus a l'iteration suivante. Delimiteur
                 * isole (suivi de texte): la couleur du texte prime
                 * sur la zone de fond, poser l'encre. */
                unsigned char next_is_empty =
                    (col + 1 < SCREEN_COLS &&
                     (ctx->screen[row][col + 1].ch == ' ' ||
                      ctx->screen[row][col + 1].ch == 0)) ? 1 : 0;
                if (next_is_empty) {
                    set_paper_attr(col, row, cell_bg);
                    prev_bg = cell_bg;
                } else {
                    set_ink_attr(col, row, cell_fg);
                    prev_fg = cell_fg;
                }
            } else if (bg_change) {
                set_paper_attr(col, row, cell_bg);
                prev_bg = cell_bg;
            } else if (fg_change) {
                set_ink_attr(col, row, cell_fg);
                prev_fg = cell_fg;
            } else {
                render_cell_hires(cell, col, row);
            }
        } else {
            /* Course de cellules pleines jusqu'au prochain vide:
             * deleguee au moteur asm (fallback C pour les cas rares).
             * On doit s'arreter aux vides, eux seuls portent la
             * logique d'attributs ci-dessus. */
            unsigned char run_end = col;
            while (run_end < SCREEN_COLS - 1) {
                vtx_cell_t* nc = &ctx->screen[row][run_end + 1];
                if (nc->ch == ' ' || nc->ch == 0) break;
                ++run_end;
            }
            render_span_raw(ctx, row, col, run_end, 1);
            /* Si la course finit sur une double largeur, sauter la
             * moitie cachee (cellule run_end+1, contenu perime) */
            if (ctx->screen[row][run_end].size == SIZE_DOUBLE_WIDTH ||
                ctx->screen[row][run_end].size == SIZE_DOUBLE_SIZE) {
                col = run_end + 1;
            } else {
                col = run_end;
            }
        }
    }
}

/* ===================================================================
 *  API publiques
 * =================================================================== */

/* Derniere position ou la barre curseur a ete dessinee.
 * L'octet HIRES sous la barre est ecrase (pixels ou attribut serial):
 * le seul moyen fiable de le restaurer est de re-rendre la ligne. */
static unsigned char cur_drawn;      /* 1 = barre presente a l'ecran */
static unsigned char cur_drawn_x;
static unsigned char cur_drawn_y;

/* Rendu des lignes dirty, au plus max_rows par appel.
 * Le budget borne la latence de la boucle principale: le latch clavier
 * de la ROM Oric ne retient que la DERNIERE touche pressee, donc un
 * rendu long (page complete = 25 lignes, ~20-40 ms par ligne a 1 MHz)
 * sans re-scanner le clavier perd des frappes. Les lignes non rendues
 * restent dirty et partent aux appels suivants. */
static void render_dirty(vtx_context_t* ctx, unsigned char max_rows)
{
    unsigned char row;
    unsigned char rendered;
    unsigned char want_cursor;

    /* full_refresh = tout marquer dirty (span plein), le budget fait
     * le reste */
    if (ctx->full_refresh) {
        for (row = 0; row < SCREEN_ROWS; ++row) {
            ctx->dirty[row] = 1;
            ctx->dirty_min[row] = 0;
            ctx->dirty_max[row] = SCREEN_COLS - 1;
        }
        ctx->full_refresh = 0;
    }

    want_cursor = (ctx->cur_visible && g_blink_phase == 0 &&
                   ctx->cur_y < SCREEN_ROWS && ctx->cur_x < SCREEN_COLS)
                  ? 1 : 0;

    /* Effacer la barre precedente si le curseur a bouge ou si la phase
     * blink la cache: re-rendre sa cellule restaure pixels/attributs
     * (en hybride, render_row_hires elargit de lui-meme a la ligne). */
    if (cur_drawn && (!want_cursor ||
                      cur_drawn_x != ctx->cur_x ||
                      cur_drawn_y != ctx->cur_y)) {
        vtx_touch(ctx, cur_drawn_y, cur_drawn_x, cur_drawn_x);
        cur_drawn = 0;
    }

    rendered = 0;
    for (row = 0; row < SCREEN_ROWS && rendered < max_rows; ++row) {
        if (!ctx->dirty[row]) continue;
        render_row_hires(ctx, row);
        ctx->dirty[row] = 0;
        /* Retablir l'invariant: ligne propre = span plein */
        ctx->dirty_min[row] = 0;
        ctx->dirty_max[row] = SCREEN_COLS - 1;
        ++rendered;
        if (cur_drawn && row == cur_drawn_y) {
            /* La ligne sous la barre vient d'etre redessinee */
            cur_drawn = 0;
        }
        /* Le rendu de cette ligne a ecrase les scanlines ou la ligne
         * du dessous dessine les moities hautes de ses glyphes double
         * hauteur: la re-rendre entierement (cet appel si le budget le
         * permet, sinon le suivant). */
        if (row + 1 < SCREEN_ROWS && row_has_dblh(ctx, row + 1)) {
            vtx_touch(ctx, row + 1, 0, SCREEN_COLS - 1);
        }
    }

    /* Curseur visible: barre blanche sous le caractere. Pas de barre
     * tant que sa ligne attend un rendu (elle serait ecrasee). */
    if (want_cursor && !ctx->dirty[ctx->cur_y]) {
        unsigned char* base =
            hires_row_base[ctx->cur_y] + 7 * 40 + ctx->cur_x;
        *base = 0x7F;
        cur_drawn = 1;
        cur_drawn_x = ctx->cur_x;
        cur_drawn_y = ctx->cur_y;
    }
}

/* Boucle principale: rendu budgete pour garder le clavier reactif */
void display_render(vtx_context_t* ctx)
{
    render_dirty(ctx, 2);
}

/* Menus/splash: rendu complet en un appel (pas de boucle de rendu
 * derriere, l'appelant attend ensuite une touche) */
void display_render_all(vtx_context_t* ctx)
{
    render_dirty(ctx, SCREEN_ROWS);
}

/* Reste-t-il des lignes a rendre ? (pour le budget adaptatif de la
 * boucle principale) */
unsigned char display_dirty_pending(vtx_context_t* ctx)
{
    unsigned char row;
    if (ctx->full_refresh) return 1;
    for (row = 0; row < SCREEN_ROWS; ++row) {
        if (ctx->dirty[row]) return 1;
    }
    return 0;
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

    base = hires_row_base[row] + 7 * 40 + col;

    if (visible) {
        *base ^= 0x3F;  /* Inverser la derniere ligne pixel */
    }
}
