/**
 * @file bench_render.c
 * @brief Banc de mesure du cout CPU du rendu HIRES (bench.tap)
 *
 * Objectif : chiffrer, en CYCLES 6502 REELS, le cout des passes de rendu de
 * display.c, pour le comparer au budget d'un octet a 1200 bauds (8333 cycles
 * a 1 MHz). C'est ce budget qui decide si le 6551 (registre RX d'UN octet,
 * aucune IRQ) deborde pendant un rendu -> caracteres manquants a l'ecran.
 *
 * METHODE : on n'utilise PAS le Timer 1 du VIA (l'IRQ ROM 100 Hz le recharge
 * en permanence, les deltas seraient faux). A la place, chaque region mesuree
 * est encadree par l'ecriture d'un octet MARQUEUR dans le registre DATA de
 * l'ACIA. La trace serie de Phosphoric (--serial-trace) horodate chaque acces
 * en cycles emules : la difference des deux horodatages donne le cout exact,
 * IRQ ROM comprises (conditions identiques a la boucle de session).
 *
 * L'ACIA est programmee en Control=$00 (horloge EXTERNE) : Phosphoric fait
 * alors un transfert instantane, l'ecriture du marqueur ne coute que l'acces
 * memoire et ne fausse pas la mesure.
 *
 * Chaque scenario est repete BENCH_REPS fois : le depouillement calcule
 * min/moyenne/max (la dispersion vient des IRQ Timer-1 de la ROM).
 *
 * Construction / execution : `make bench-render`.
 */

#include "videotex.h"
#include "display.h"

/* --- Acces direct aux registres ACIA du LOCI ($0380-$0383) --------------- */
#define R_DATA (*(volatile unsigned char*)0x0380)
#define R_STAT (*(volatile unsigned char*)0x0381)
#define R_CMD  (*(volatile unsigned char*)0x0382)
#define R_CTRL (*(volatile unsigned char*)0x0383)

#define BENCH_REPS 8

/* Marqueurs : debut = $10+id ($10-$2F), fin = $60+id ($60-$7F), id 0..31.
 * Fin de campagne = $FF. Les plages ne doivent pas se chevaucher : un id > 31
 * casserait le depouillement (la region disparait silencieusement du tableau). */
#define MK_BEG(id) ((unsigned char)(0x10 + (id)))
#define MK_END(id) ((unsigned char)(0x60 + (id)))
#define MK_DONE    0xFF

static vtx_context_t vtx;

/* Interface du blit assembleur (display_asm.s), pour mesurer le chemin
 * rapide isole du wrapper C de render_cell_hires. */
extern unsigned char* run_cells;
extern unsigned char* run_dst;
extern unsigned char  run_count;
extern unsigned char  run_mode;
unsigned char __fastcall__ blit_run(void);

/* Puits anti-optimisation : empeche cc65 d'eliminer la boucle de pre-scan. */
unsigned char g_scan_sink;

/* Ligne cible en VARIABLE GLOBALE : avec un litteral (screen[5][col]) cc65
 * replie l'indice de ligne en offset constant et la mesure sous-estime
 * lourdement le vrai code, qui indexe avec une variable et paie une
 * multiplication 16 bits par 240 A CHAQUE TOUR. */
unsigned char g_bench_row = 5;

/* Globales normalement fournies par main.c */
unsigned char g_blink_phase;
unsigned char g_global_mask = 1;
extern unsigned char g_render_mode;

/* Ecriture marqueur : horodatee par --serial-trace. Pas d'attente TDRE
 * (horloge externe = transfert instantane cote Phosphoric), donc le cout
 * ajoute a la region mesuree est constant et negligeable. */
static void mark(unsigned char m)
{
    R_DATA = m;
}

static void acia_setup(void)
{
    R_STAT = 0x00;   /* programmed reset */
    R_CTRL = 0x00;   /* horloge externe -> transfert instantane */
    R_CMD  = 0x0B;   /* DTR, IRQ RX off, TIC=10 (pas d'IRQ TX) */
}

/* --- Construction des pages de test -------------------------------------- */

static void cell_set(vtx_cell_t* c, unsigned char ch, unsigned char charset,
                     unsigned char fg, unsigned char bg,
                     unsigned char flags, unsigned char size)
{
    c->ch = ch; c->charset = charset; c->fg = fg; c->bg = bg;
    c->flags = flags; c->size = size;
}

/* Ligne "hybride" : du texte colore avec des espaces pour poser les
 * attributs serial -> use_attrs = 1 (le chemin le plus lourd). */
static void fill_row_hybrid(unsigned char row)
{
    unsigned char col;
    for (col = 0; col < VTX_COLS; ++col) {
        vtx_cell_t* c = &vtx.screen[row][col];
        if ((col % 8) == 0)
            cell_set(c, ' ', CHARSET_G0, (unsigned char)(col % 7 + 1), VTX_BLACK, 0, SIZE_NORMAL);
        else
            cell_set(c, (unsigned char)('A' + (col % 26)), CHARSET_G0,
                     (unsigned char)(col % 7 + 1), VTX_BLACK, 0, SIZE_NORMAL);
    }
}

/* Ligne hybride REALISTE : mots de couleur CONSTANTE separes par des espaces,
 * comme une vraie page Minitel. Les courses de cellules pleines sont longues,
 * donc peu d'appels a render_span_raw. A opposer a fill_row_hybrid, qui change
 * de couleur A CHAQUE COLONNE et casse toutes les courses en longueur 1 :
 * c'est un pire cas pathologique, pas le cas courant. */
static void fill_row_hybrid_real(unsigned char row)
{
    unsigned char col, fg = 1;
    for (col = 0; col < VTX_COLS; ++col) {
        vtx_cell_t* c = &vtx.screen[row][col];
        if ((col % 8) == 7) {
            cell_set(c, ' ', CHARSET_G0, fg, VTX_BLACK, 0, SIZE_NORMAL);
            ++fg; if (fg > 7) fg = 1;          /* couleur du mot suivant */
        } else {
            cell_set(c, (unsigned char)('A' + (col % 26)), CHARSET_G0,
                     fg, VTX_BLACK, 0, SIZE_NORMAL);
        }
    }
}

/* Ligne monochrome pleine : pas de couleur -> use_attrs = 0 (span brut). */
static void fill_row_raw(unsigned char row)
{
    unsigned char col;
    for (col = 0; col < VTX_COLS; ++col)
        cell_set(&vtx.screen[row][col], (unsigned char)('A' + (col % 26)),
                 CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_NORMAL);
}

/* Ligne de mosaiques G1 : chemin dithering. */
static void fill_row_g1(unsigned char row)
{
    unsigned char col;
    for (col = 0; col < VTX_COLS; ++col)
        cell_set(&vtx.screen[row][col], (unsigned char)(0x30 + (col % 0x30)),
                 CHARSET_G1, (unsigned char)(col % 7 + 1), VTX_BLACK, 0, SIZE_NORMAL);
}

/* Ligne double hauteur. */
static void fill_row_dblh(unsigned char row)
{
    unsigned char col;
    for (col = 0; col < VTX_COLS; ++col)
        cell_set(&vtx.screen[row][col], (unsigned char)('A' + (col % 26)),
                 CHARSET_G0, VTX_WHITE, VTX_BLACK, 0, SIZE_DOUBLE_HEIGHT);
}

static void dirty_row(unsigned char row, unsigned char from, unsigned char to)
{
    vtx.dirty[row] = 1;
    vtx.dirty_min[row] = from;
    vtx.dirty_max[row] = to;
}

static void dirty_all(void)
{
    unsigned char row;
    for (row = 0; row < VTX_ROWS; ++row) dirty_row(row, 0, VTX_COLS - 1);
}

/* --- Scenarios ------------------------------------------------------------
 * id 1 : display_render() sur page pleine = LA passe budgetee de main.c
 *        (render_dirty(ctx, 2) : 2 lignes). C'est la fenetre pendant
 *        laquelle le 6551 n'est pas relu.
 * id 2 : une ligne hybride (couleurs + attributs serial)
 * id 3 : une ligne brute pleine largeur (40 colonnes)
 * id 4 : une ligne brute span d'UNE colonne (cas incremental en session)
 * id 5 : une ligne G1 mosaiques (dithering)
 * id 6 : une ligne double hauteur
 * id 7 : vtx_process() de 40 caracteres G0 (cout du drain, hors rendu)
 * id 9 / 13 / 15 : formes d'INDEXATION comparees, pas le code actuel.
 *         9 et 13 gardent la forme NON hissee (&ctx->screen[row][col] avec row
 *         variable) telle qu'elle etait AVANT optimisation ; 15 la forme hissee,
 *         qui est celle du code aujourd'hui. Elles servent de reference sur le
 *         cout de l'indexation cc65, pas de mesure du moteur en place.
 * id 10: render_cell_hires() d'UNE cellule G0 (blit pur, 8 octets)
 * id 11: render_cell_hires() x40 cellules G0 (blit pur d'une ligne)
 * id 12: render_cell_hires() x40 cellules G1 (blit + dithering)
 * id 17: vtx_process() x40 octets NEUTRES ($00 : code C0 sans effet). Isole le
 *         cout du DISPATCH (masquage, resync ESC, switch d'etat, test C0) de
 *         celui de put_char. id 7 - id 17 = cout reel de put_char.
 * id 16: ligne hybride REALISTE (mots de couleur constante) - le cas courant,
 *         a opposer a id 2 qui est un pire cas pathologique
 * id 13: scan "row_has_dblh" seul (2e pre-scan de 40 cellules)
 * id 14: blit_run() ASSEMBLEUR seul sur 40 cellules G0 eligibles
 *        -> tranche la question : le chemin rapide asm tient-il ses ~190
 *           cycles/cellule, ou le cout est-il ailleurs ?
 * id 8 : CALIBRATION - boucle assembleur au cout EXACTEMENT connu
 *        (`ldx #0 / dex / bne` = 256 tours * 5 cycles = 1280 cycles, +/- IRQ).
 *        Si la mesure ne rend pas ~1280, la chaine de mesure est fausse et
 *        aucun autre chiffre de ce banc n'est exploitable.
 * -------------------------------------------------------------------------*/

static void bench_all(void)
{
    unsigned char rep, row, col;

    /* --- id 1 : passe display_render() sur page pleine hybride --- */
    for (row = 0; row < VTX_ROWS; ++row) fill_row_hybrid(row);
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        dirty_all();
        mark(MK_BEG(1));
        display_render(&vtx);
        mark(MK_END(1));
    }

    /* --- id 2 : une ligne hybride --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        fill_row_hybrid(5);
        dirty_row(5, 0, VTX_COLS - 1);
        mark(MK_BEG(2));
        display_render_cell_row(&vtx, 5);
        mark(MK_END(2));
    }

    /* --- id 3 : une ligne brute pleine largeur --- */
    for (row = 0; row < VTX_ROWS; ++row) fill_row_raw(row);
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        dirty_row(5, 0, VTX_COLS - 1);
        mark(MK_BEG(3));
        display_render_cell_row(&vtx, 5);
        mark(MK_END(3));
    }

    /* --- id 4 : une ligne brute, span d'une seule colonne --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        dirty_row(5, 10, 10);
        mark(MK_BEG(4));
        display_render_cell_row(&vtx, 5);
        mark(MK_END(4));
    }

    /* --- id 5 : une ligne G1 mosaiques (dithering) --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        fill_row_g1(5);
        dirty_row(5, 0, VTX_COLS - 1);
        mark(MK_BEG(5));
        display_render_cell_row(&vtx, 5);
        mark(MK_END(5));
    }

    /* --- id 6 : une ligne double hauteur --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        fill_row_dblh(5);
        dirty_row(5, 0, VTX_COLS - 1);
        mark(MK_BEG(6));
        display_render_cell_row(&vtx, 5);
        mark(MK_END(6));
    }

    /* --- id 9 : pre-scan seul (meme forme que render_row_hires) --- */
    for (row = 0; row < VTX_ROWS; ++row) fill_row_raw(row);
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        unsigned char hc = 0, he = 0, hd = 0;
        mark(MK_BEG(9));
        for (col = 0; col < VTX_COLS; ++col) {
            vtx_cell_t* c = &vtx.screen[g_bench_row][col];
            if (c->fg != VTX_WHITE || c->bg != VTX_BLACK) hc = 1;
            if (c->ch == ' ' || c->ch == 0) he = 1;
            if (c->size == SIZE_DOUBLE_HEIGHT || c->size == SIZE_DOUBLE_SIZE) hd = 1;
        }
        mark(MK_END(9));
        g_scan_sink = (unsigned char)(hc + he + hd);
    }

    /* --- id 10 : blit pur d'UNE cellule G0 --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        mark(MK_BEG(10));
        display_render_cell(&vtx.screen[5][0], 0, 5);
        mark(MK_END(10));
    }

    /* --- id 11 : blit pur de 40 cellules G0 --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        mark(MK_BEG(11));
        for (col = 0; col < VTX_COLS; ++col)
            display_render_cell(&vtx.screen[5][col], col, 5);
        mark(MK_END(11));
    }

    /* --- id 12 : blit + dithering de 40 cellules G1 --- */
    fill_row_g1(5);
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        mark(MK_BEG(12));
        for (col = 0; col < VTX_COLS; ++col)
            display_render_cell(&vtx.screen[5][col], col, 5);
        mark(MK_END(12));
    }

    /* --- id 17 : dispatch seul (octet C0 sans effet) --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        mark(MK_BEG(17));
        for (col = 0; col < 40; ++col) vtx_process(&vtx, 0x00);
        mark(MK_END(17));
    }

    /* --- id 16 : ligne hybride realiste (mots de couleur constante) --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        fill_row_hybrid_real(5);
        dirty_row(5, 0, VTX_COLS - 1);
        mark(MK_BEG(16));
        display_render_cell_row(&vtx, 5);
        mark(MK_END(16));
    }

    /* --- id 13 : 2e pre-scan (row_has_dblh sur la ligne du dessous) --- */
    for (row = 0; row < VTX_ROWS; ++row) fill_row_raw(row);
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        unsigned char hd = 0;
        mark(MK_BEG(13));
        for (col = 0; col < VTX_COLS; ++col) {
            vtx_cell_t* c = &vtx.screen[g_bench_row][col];
            if (c->size == SIZE_DOUBLE_HEIGHT || c->size == SIZE_DOUBLE_SIZE) hd = 1;
        }
        mark(MK_END(13));
        g_scan_sink = hd;
    }

    /* --- id 14 : blit_run() asm seul, 40 cellules G0 eligibles --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        run_mode  = 1;
        run_cells = (unsigned char*)&vtx.screen[5][0];
        run_dst   = (unsigned char*)0xA000 + 5 * 320;
        run_count = VTX_COLS;
        mark(MK_BEG(14));
        g_scan_sink = blit_run();
        mark(MK_END(14));
    }

    /* --- id 15 : pre-scan avec POINTEUR DE LIGNE HISSE (optimisation
     * candidate : une seule multiplication par ligne au lieu de 40) --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        unsigned char hc = 0, he = 0, hd = 0;
        mark(MK_BEG(15));
        {
            vtx_cell_t* c = &vtx.screen[g_bench_row][0];
            for (col = 0; col < VTX_COLS; ++col, ++c) {
                if (c->fg != VTX_WHITE || c->bg != VTX_BLACK) hc = 1;
                if (c->ch == ' ' || c->ch == 0) he = 1;
                if (c->size == SIZE_DOUBLE_HEIGHT || c->size == SIZE_DOUBLE_SIZE) hd = 1;
            }
        }
        mark(MK_END(15));
        g_scan_sink = (unsigned char)(hc + he + hd);
    }

    /* --- id 8 : calibration (doit rendre ~1280 cycles) --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        mark(MK_BEG(8));
        __asm__("ldx #$00");
        __asm__("calloop: dex");
        __asm__("bne calloop");
        mark(MK_END(8));
    }

    /* --- id 7 : cout de vtx_process() pour 40 caracteres G0 --- */
    for (rep = 0; rep < BENCH_REPS; ++rep) {
        vtx_set_cursor(&vtx, 5, 0);
        mark(MK_BEG(7));
        for (col = 0; col < 40; ++col) vtx_process(&vtx, (unsigned char)('A' + (col % 26)));
        mark(MK_END(7));
    }
}

/* Page canonique : contenu mixte (hybride, brut, G1, double hauteur) rendu
 * INTEGRALEMENT via display_render_all(). L'empreinte du framebuffer prise
 * ensuite ne depend donc PAS du budget de display_render() (nombre de lignes
 * par passe) : elle ne bouge que si le RENDU LUI-MEME change. C'est le
 * garde-fou pixel-exact des optimisations. */
static void render_canonical_page(void)
{
    unsigned char row;
    for (row = 0; row < VTX_ROWS; ++row) {
        switch (row & 3) {
            case 0: fill_row_hybrid(row); break;
            case 1: fill_row_raw(row);    break;
            case 2: fill_row_g1(row);     break;
            default: fill_row_dblh(row);  break;
        }
    }
    dirty_all();
    display_render_all(&vtx);
}

int main(void)
{
    vtx_init(&vtx);
    display_init();
    acia_setup();

    g_render_mode = 0;          /* mode hybride = defaut de la session */
    vtx.cur_visible = 0;        /* pas de barre curseur : mesure du rendu seul */

    bench_all();

    /* Etat final deterministe et independant du budget de rendu. */
    render_canonical_page();

    mark(MK_DONE);
    for (;;) { }
    /* unreachable */
}
